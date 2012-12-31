/* linux/drivers/media/video/samsung/fimc_capture.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com/
 *
 * V4L2 Capture device support file for Samsung Camera Interface (FIMC) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/slab.h>
#include <linux/bootmem.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/videodev2_samsung.h>
#include <linux/clk.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <plat/media.h>
#include <plat/clock.h>
#include <plat/fimc.h>
#include <linux/delay.h>

#include <asm/cacheflush.h>

#include "fimc.h"

static const struct v4l2_fmtdesc capture_fmts[] = {
	{
		.index		= 0,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PACKED,
		.description	= "RGB-5-6-5",
		.pixelformat	= V4L2_PIX_FMT_RGB565,
	}, {
		.index		= 1,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PACKED,
		.description	= "RGB-8-8-8, unpacked 24 bpp",
		.pixelformat	= V4L2_PIX_FMT_RGB32,
	}, {
		.index		= 2,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PACKED,
		.description	= "YUV 4:2:2 packed, YCbYCr",
		.pixelformat	= V4L2_PIX_FMT_YUYV,
	}, {
		.index		= 3,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PACKED,
		.description	= "YUV 4:2:2 packed, CbYCrY",
		.pixelformat	= V4L2_PIX_FMT_UYVY,
	}, {
		.index		= 4,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PACKED,
		.description	= "YUV 4:2:2 packed, CrYCbY",
		.pixelformat	= V4L2_PIX_FMT_VYUY,
	}, {
		.index		= 5,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PACKED,
		.description	= "YUV 4:2:2 packed, YCrYCb",
		.pixelformat	= V4L2_PIX_FMT_YVYU,
	}, {
		.index		= 6,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PLANAR,
		.description	= "YUV 4:2:2 planar, Y/Cb/Cr",
		.pixelformat	= V4L2_PIX_FMT_YUV422P,
	}, {
		.index		= 7,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PLANAR,
		.description	= "YUV 4:2:0 planar, Y/CbCr",
		.pixelformat	= V4L2_PIX_FMT_NV12,
	}, {
		.index		= 8,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PLANAR,
		.description	= "YUV 4:2:0 planar, Y/CbCr, Tiled",
		.pixelformat	= V4L2_PIX_FMT_NV12T,
	}, {
		.index		= 9,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PLANAR,
		.description	= "YUV 4:2:0 planar, Y/CrCb",
		.pixelformat	= V4L2_PIX_FMT_NV21,
	}, {
		.index		= 10,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PLANAR,
		.description	= "YUV 4:2:2 planar, Y/CbCr",
		.pixelformat	= V4L2_PIX_FMT_NV16,
	}, {
		.index		= 11,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PLANAR,
		.description	= "YUV 4:2:2 planar, Y/CrCb",
		.pixelformat	= V4L2_PIX_FMT_NV61,
	}, {
		.index		= 12,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PLANAR,
		.description	= "YUV 4:2:0 planar, Y/Cb/Cr",
		.pixelformat	= V4L2_PIX_FMT_YUV420,
	}, {
		.index		= 13,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PLANAR,
		.description	= "YUV 4:2:0 planar, Y/Cr/Cb",
		.pixelformat	= V4L2_PIX_FMT_YVU420,
	}, {
		.index		= 14,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.description	= "JPEG encoded data",
		.pixelformat	= V4L2_PIX_FMT_JPEG,
	},
};

static const struct v4l2_queryctrl fimc_controls[] = {
	{
		.id = V4L2_CID_ROTATION,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Roataion",
		.minimum = 0,
		.maximum = 270,
		.step = 90,
		.default_value = 0,
	}, {
		.id = V4L2_CID_HFLIP,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Horizontal Flip",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	}, {
		.id = V4L2_CID_VFLIP,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Vertical Flip",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	}, {
		.id = V4L2_CID_PADDR_Y,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Physical address Y",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
		.flags = V4L2_CTRL_FLAG_READ_ONLY,
	}, {
		.id = V4L2_CID_PADDR_CB,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Physical address Cb",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
		.flags = V4L2_CTRL_FLAG_READ_ONLY,
	}, {
		.id = V4L2_CID_PADDR_CR,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Physical address Cr",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
		.flags = V4L2_CTRL_FLAG_READ_ONLY,
	}, {
		.id = V4L2_CID_PADDR_CBCR,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Physical address CbCr",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
		.flags = V4L2_CTRL_FLAG_READ_ONLY,
	}, {
		.id = V4L2_CID_CACHEABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Cacheable",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
};

#ifndef CONFIG_VIDEO_FIMC_MIPI
void s3c_csis_start(int csis_id, int lanes, int settle, \
	int align, int width, int height, int pixel_format) {}
void s3c_csis_stop(int csis_id) {}
void s3c_csis_enable_pktdata(int csis_id, bool enable) {}
#endif

static int fimc_init_camera(struct fimc_control *ctrl)
{
	struct fimc_global *fimc = get_fimc_dev();
	struct s3c_platform_fimc *pdata;
	struct s3c_platform_camera *cam;
	int ret = 0, retry_cnt = 0;

#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	struct platform_device *pdev = to_platform_device(ctrl->dev);
#endif
	pdata = to_fimc_plat(ctrl->dev);

	cam = ctrl->cam;

	/* do nothing if already initialized */
	if (ctrl->cam->initialized)
		return 0;

#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	if (ctrl->power_status == FIMC_POWER_OFF)
		pm_runtime_get_sync(&pdev->dev);
#endif
	/*
	 * WriteBack mode doesn't need to set clock and power,
	 * but it needs to set source width, height depend on LCD resolution.
	*/
	if ((cam->id == CAMERA_WB) || (cam->id == CAMERA_WB_B)) {
		ret = s3cfb_direct_ioctl(0, S3CFB_GET_LCD_WIDTH,
					(unsigned long)&cam->width);
		if (ret) {
			fimc_err("fail to get LCD size\n");
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
			pm_runtime_put_sync(&pdev->dev);
#endif
			return ret;
		}

		ret = s3cfb_direct_ioctl(0, S3CFB_GET_LCD_HEIGHT,
					(unsigned long)&cam->height);
		if (ret) {
			fimc_err("fail to get LCD size\n");
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
			pm_runtime_put_sync(&pdev->dev);
#endif
			return ret;
		}

		cam->window.width = cam->width;
		cam->window.height = cam->height;
		cam->initialized = 1;

		return 0;
	}

retry:
	/* set rate for mclk */
	if ((clk_get_rate(cam->clk)) && (fimc->mclk_status == CAM_MCLK_OFF)) {
		clk_set_rate(cam->clk, cam->clk_rate);
		clk_enable(cam->clk);
		fimc->mclk_status = CAM_MCLK_ON;
		fimc_info1("clock for camera: %d\n", cam->clk_rate);
	}

	/* enable camera power if needed */
	if (cam->cam_power) {
		ret = cam->cam_power(1);
		if (unlikely(ret < 0))
			fimc_err("\nfail to power on\n");
	}

	/* "0" argument means preview init for s5k4ea */
	ret = v4l2_subdev_call(cam->sd, core, init, 0);

	/* Retry camera power-up if first i2c fails. */
	if (unlikely(ret < 0)) {
		if (cam->cam_power)
			cam->cam_power(0);

		if (fimc->mclk_status == CAM_MCLK_ON) {
			clk_disable(ctrl->cam->clk);
			fimc->mclk_status = CAM_MCLK_OFF;
		}
//		if (retry_cnt++ < 3) {
//			msleep(100);
//			fimc_err("Retry power on(%d/3)\n\n", retry_cnt);
//			goto retry;
//		}
		cam->initialized = 0;
	} else {
		cam->initialized = 1;
	}

	return ret;
}

static int fimc_camera_get_jpeg_memsize(struct fimc_control *ctrl)
{
	int ret = 0;
	struct v4l2_control cam_ctrl;
	cam_ctrl.id = V4L2_CID_CAM_JPEG_MEMSIZE;

	ret = v4l2_subdev_call(ctrl->cam->sd, core, g_ctrl, &cam_ctrl);
	if (ret < 0) {
		fimc_err("%s: Subdev doesn't support JEPG encoding.\n", \
				 __func__);
		return 0;
	}

	return cam_ctrl.value;
}


static int fimc_capture_scaler_info(struct fimc_control *ctrl)
{
	struct fimc_scaler *sc = &ctrl->sc;
	struct v4l2_rect *window = &ctrl->cam->window;
	int tx, ty, sx, sy;
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	int rot = 0;

	if (!ctrl->cam->use_isp) {
		sx = window->width;
		sy = window->height;
	} else {
		sx = ctrl->is.fmt.width;
		sy = ctrl->is.fmt.height;
	}

	sc->real_width = sx;
	sc->real_height = sy;

	rot = fimc_mapping_rot_flip(ctrl->cap->rotate, ctrl->cap->flip);

	if (rot & FIMC_ROT) {
		tx = ctrl->cap->fmt.height;
		ty = ctrl->cap->fmt.width;
	} else {
		tx = ctrl->cap->fmt.width;
		ty = ctrl->cap->fmt.height;
	}

	fimc_dbg("%s: CamOut (%d, %d), TargetOut (%d, %d)\n",
			__func__, sx, sy, tx, ty);

	if (sx <= 0 || sy <= 0) {
		fimc_err("%s: invalid source size\n", __func__);
		return -EINVAL;
	}

	if (tx <= 0 || ty <= 0) {
		fimc_err("%s: invalid target size\n", __func__);
		return -EINVAL;
	}

	fimc_get_scaler_factor(sx, tx, &sc->pre_hratio, &sc->hfactor);
	fimc_get_scaler_factor(sy, ty, &sc->pre_vratio, &sc->vfactor);

	if (sx == sy) {
		if (sx*10/tx >= 15 && sx*10/tx < 20) {
			sc->pre_hratio = 2;
			sc->hfactor = 1;
		}
		if (sy*10/ty >= 15 && sy*10/ty < 20) {
			sc->pre_vratio = 2;
			sc->vfactor = 1;
		}
	}


	sc->pre_dst_width = sx / sc->pre_hratio;
	sc->pre_dst_height = sy / sc->pre_vratio;

	if (pdata->hw_ver >= 0x50) {
		sc->main_hratio = (sx << 14) / (tx << sc->hfactor);
		sc->main_vratio = (sy << 14) / (ty << sc->vfactor);
	} else {
		sc->main_hratio = (sx << 8) / (tx << sc->hfactor);
		sc->main_vratio = (sy << 8) / (ty << sc->vfactor);
	}

	sc->scaleup_h = (tx >= sx) ? 1 : 0;
	sc->scaleup_v = (ty >= sy) ? 1 : 0;

	return 0;
}

static int fimc_capture_change_scaler_info(struct fimc_control *ctrl)
{
	struct fimc_scaler *sc = &ctrl->sc;
	struct v4l2_rect *window = &ctrl->cam->window;
	int tx, ty, sx, sy;
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	int rot = 0;

	if (!ctrl->cam->use_isp) {
		sx = window->width;
		sy = window->height;
	} else {
		sx = ctrl->is.zoom_in_width;
		sy = ctrl->is.zoom_in_height;
	}

	sc->real_width = sx;
	sc->real_height = sy;

	rot = fimc_mapping_rot_flip(ctrl->cap->rotate, ctrl->cap->flip);

	if (rot & FIMC_ROT) {
		tx = ctrl->cap->fmt.height;
		ty = ctrl->cap->fmt.width;
	} else {
		tx = ctrl->cap->fmt.width;
		ty = ctrl->cap->fmt.height;
	}

	fimc_dbg("%s: CamOut (%d, %d), TargetOut (%d, %d)\n",
			__func__, sx, sy, tx, ty);

	if (sx <= 0 || sy <= 0) {
		fimc_err("%s: invalid source size\n", __func__);
		return -EINVAL;
	}

	if (tx <= 0 || ty <= 0) {
		fimc_err("%s: invalid target size\n", __func__);
		return -EINVAL;
	}

	fimc_get_scaler_factor(sx, tx, &sc->pre_hratio, &sc->hfactor);
	fimc_get_scaler_factor(sy, ty, &sc->pre_vratio, &sc->vfactor);

	sc->pre_dst_width = sx / sc->pre_hratio;
	sc->pre_dst_height = sy / sc->pre_vratio;

	if (pdata->hw_ver >= 0x50) {
		sc->main_hratio = (sx << 14) / (tx << sc->hfactor);
		sc->main_vratio = (sy << 14) / (ty << sc->vfactor);
	} else {
		sc->main_hratio = (sx << 8) / (tx << sc->hfactor);
		sc->main_vratio = (sy << 8) / (ty << sc->vfactor);
	}

	sc->scaleup_h = (tx >= sx) ? 1 : 0;
	sc->scaleup_v = (ty >= sy) ? 1 : 0;

	return 0;
}

int fimc_start_zoom_capture(struct fimc_control *ctrl)
{
	fimc_dbg("%s\n", __func__);

	fimc_hwset_start_scaler(ctrl);

	fimc_hwset_enable_capture(ctrl, ctrl->sc.bypass);
	fimc_hwset_disable_frame_end_irq(ctrl);

	return 0;
}

int fimc_stop_zoom_capture(struct fimc_control *ctrl)
{
	fimc_dbg("%s\n", __func__);
	if (!ctrl->cam) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	if (!ctrl->cap) {
		fimc_err("%s: No cappure format.\n", __func__);
		return -ENODEV;
	}

	if (ctrl->cap->lastirq) {
		fimc_hwset_enable_lastirq(ctrl);
		fimc_hwset_disable_capture(ctrl);
		fimc_hwset_disable_lastirq(ctrl);
	} else {
		fimc_hwset_disable_capture(ctrl);
		fimc_hwset_enable_frame_end_irq(ctrl);
	}

	fimc_hwset_stop_scaler(ctrl);
	return 0;
}

static int fimc_add_inqueue(struct fimc_control *ctrl, int i)
{
	struct fimc_capinfo *cap = ctrl->cap;
	struct fimc_buf_set *tmp_buf;
	struct list_head *count;

	/* PINGPONG_2ADDR_MODE Only */
	list_for_each(count, &cap->inq) {
		tmp_buf = list_entry(count, struct fimc_buf_set, list);
		/* skip list_add_tail if already buffer is in cap->inq list*/
		if (tmp_buf->id == i)
			return 0;
	}
	list_add_tail(&cap->bufs[i].list, &cap->inq);

	return 0;
}

static int fimc_add_outqueue(struct fimc_control *ctrl, int i)
{
	struct fimc_capinfo *cap = ctrl->cap;
	struct fimc_buf_set *buf;
	unsigned int mask = 0x2;

	/* PINGPONG_2ADDR_MODE Only */
	/* pair_buf_index stands for pair index of i. (0<->2) (1<->3) */
	int pair_buf_index = (i^mask);

	/* FIMC have 4 h/w registers */
	if (i < 0 || i >= FIMC_PHYBUFS) {
		fimc_err("%s: invalid queue index : %d\n", __func__, i);
		return -ENOENT;
	}

	if (list_empty(&cap->inq))
		return -ENOENT;

	buf = list_first_entry(&cap->inq, struct fimc_buf_set, list);

	/* pair index buffer should be allocated first */
	cap->outq[pair_buf_index] = buf->id;
	fimc_hwset_output_address(ctrl, buf, pair_buf_index);

	cap->outq[i] = buf->id;
	fimc_hwset_output_address(ctrl, buf, i);

	list_del(&buf->list);

	return 0;
}

int fimc_g_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = 0;

	fimc_dbg("%s\n", __func__);

	/* WriteBack doesn't have subdev_call */

	if ((ctrl->cam->id == CAMERA_WB) || (ctrl->cam->id == CAMERA_WB_B))
		return 0;

	mutex_lock(&ctrl->v4l2_lock);
	ret = v4l2_subdev_call(ctrl->cam->sd, video, g_parm, a);
	mutex_unlock(&ctrl->v4l2_lock);

	return ret;
}

int fimc_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	int ret = 0;
	int new_fps = a->parm.capture.timeperframe.denominator /
					a->parm.capture.timeperframe.numerator;

	fimc_info2("%s fimc%d, %d\n", __func__, ctrl->id, new_fps);

	/* WriteBack doesn't have subdev_call */
	if ((ctrl->cam->id == CAMERA_WB) || (ctrl->cam->id == CAMERA_WB_B))
		return 0;

	mutex_lock(&ctrl->v4l2_lock);

	if (ctrl->cam->sd && fimc_cam_use)
		ret = v4l2_subdev_call(ctrl->cam->sd, video, s_parm, a);
	else if (ctrl->cam->use_isp)
		ret = v4l2_subdev_call(ctrl->is.sd, video, s_parm, a);

	mutex_unlock(&ctrl->v4l2_lock);

	return ret;
}

/* Enumerate controls */
int fimc_queryctrl(struct file *file, void *fh, struct v4l2_queryctrl *qc)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int i, ret;

	fimc_dbg("%s\n", __func__);

	for (i = 0; i < ARRAY_SIZE(fimc_controls); i++) {
		if (fimc_controls[i].id == qc->id) {
			memcpy(qc, &fimc_controls[i], sizeof(struct v4l2_queryctrl));
			return 0;
		}
	}

	mutex_lock(&ctrl->v4l2_lock);
	ret = v4l2_subdev_call(ctrl->cam->sd, core, queryctrl, qc);
	mutex_unlock(&ctrl->v4l2_lock);

	return ret;
}

/* Menu control items */
int fimc_querymenu(struct file *file, void *fh, struct v4l2_querymenu *qm)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = 0;

	fimc_dbg("%s\n", __func__);

	mutex_lock(&ctrl->v4l2_lock);
	ret = v4l2_subdev_call(ctrl->cam->sd, core, querymenu, qm);
	mutex_unlock(&ctrl->v4l2_lock);

	return ret;
}

int fimc_enum_input(struct file *file, void *fh, struct v4l2_input *inp)
{
	struct fimc_global *fimc = get_fimc_dev();
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;

	fimc_dbg("%s: index %d\n", __func__, inp->index);

	if (inp->index >= FIMC_MAXCAMS) {
		fimc_err("%s: invalid input index, received = %d\n",
				__func__, inp->index);
		return -EINVAL;
	}

	if (!fimc->camera_isvalid[inp->index])
		return -EINVAL;
	mutex_lock(&ctrl->v4l2_lock);

	if (fimc->camera[inp->index]->use_isp && !(fimc->camera[inp->index]->info))
		strcpy(inp->name, "ISP Camera");
	else
		strcpy(inp->name, fimc->camera[inp->index]->info->type);

	inp->type = V4L2_INPUT_TYPE_CAMERA;

	mutex_unlock(&ctrl->v4l2_lock);

	return 0;
}

int fimc_g_input(struct file *file, void *fh, unsigned int *i)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	struct fimc_global *fimc = get_fimc_dev();

	/* In case of isueing g_input before s_input */
	if (!ctrl->cam) {
		fimc_err("no camera device selected yet. do VIDIOC_S_INPUT first\n");
		return -ENODEV;
	}
	mutex_lock(&ctrl->v4l2_lock);

	*i = (unsigned int) fimc->active_camera;

	mutex_unlock(&ctrl->v4l2_lock);

	fimc_dbg("%s: index %d\n", __func__, *i);

	return 0;
}

int fimc_release_subdev(struct fimc_control *ctrl)
{
	struct fimc_global *fimc = get_fimc_dev();
	struct i2c_client *client;
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	int ret;

	if (ctrl->cam->sd && fimc_cam_use) {
		fimc_dbg("%s called\n", __func__);

		/* WriteBack doesn't need clock setting */
		if ((ctrl->cam->id == CAMERA_WB) ||
			(ctrl->cam->id == CAMERA_WB_B)) {
			ctrl->cam->initialized = 0;
			ctrl->cam = NULL;
			fimc->active_camera = -1;
			return 0;
		}

		client = v4l2_get_subdevdata(ctrl->cam->sd);
		i2c_unregister_device(client);
		ctrl->cam->sd = NULL;
		if (ctrl->cam->cam_power)
			ctrl->cam->cam_power(0);

		/* shutdown the MCLK */
		if (fimc->mclk_status == CAM_MCLK_ON) {
			clk_disable(ctrl->cam->clk);
			fimc->mclk_status = CAM_MCLK_OFF;
		}

		ctrl->cam->initialized = 0;
		ctrl->cam = NULL;
		fimc->active_camera = -1;
	}

	if (ctrl->flite_sd && fimc_cam_use) {
		ret = v4l2_subdev_call(ctrl->flite_sd, core, s_power, 0);
		if (ret)
			fimc_err("s_power failed: %d", ret);

		ctrl->flite_sd = NULL;
	}

	return 0;
}

static int fimc_configure_subdev(struct fimc_control *ctrl)
{
	struct i2c_adapter *i2c_adap;
	struct i2c_board_info *i2c_info;
	struct v4l2_subdev *sd;
	unsigned short addr;
	char *name;
	int ret = 0;

	i2c_adap = i2c_get_adapter(ctrl->cam->i2c_busnum);
	if (!i2c_adap) {
		fimc_err("subdev i2c_adapter missing-skip registration\n");
		return -ENODEV;
	}

	i2c_info = ctrl->cam->info;
	if (!i2c_info) {
		fimc_err("%s: subdev i2c board info missing\n", __func__);
		return -ENODEV;
	}

	name = i2c_info->type;
	if (!name) {
		fimc_err("subdev i2c driver name missing-skip registration\n");
		return -ENODEV;
	}

	addr = i2c_info->addr;
	if (!addr) {
		fimc_err("subdev i2c address missing-skip registration\n");
		return -ENODEV;
	}
	/*
	 * NOTE: first time subdev being registered,
	 * s_config is called and try to initialize subdev device
	 * but in this point, we are not giving MCLK and power to subdev
	 * so nothing happens but pass platform data through
	 */
	sd = v4l2_i2c_new_subdev_board(&ctrl->v4l2_dev, i2c_adap,
			i2c_info, &addr);
	if (!sd) {
		fimc_err("%s: v4l2 subdev board registering failed\n",
				__func__);
	}
	/* Assign subdev to proper camera device pointer */
	ctrl->cam->sd = sd;

	if (!ctrl->cam->initialized) {
		ret = fimc_init_camera(ctrl);
		if (ret < 0) {
			fimc_err("%s: fail to initialize subdev\n", __func__);
			return ret;
		}
	}

	return 0;
}

static int flite_register_callback(struct device *dev, void *p)
{
	struct v4l2_subdev **sd_list = p;
	struct v4l2_subdev *sd = NULL;

	sd = dev_get_drvdata(dev);
	if (sd) {
		struct platform_device *pdev = v4l2_get_subdev_hostdata(sd);
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

int fimc_subdev_attatch(struct fimc_control *ctrl)
{
	int ret = 0;
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);

	ctrl->flite_sd = exynos_flite_get_subdev(ctrl->cam->flite_id);
	if (IS_ERR_OR_NULL(ctrl->flite_sd)) {
			ctrl->flite_sd = NULL;
			return PTR_ERR(ctrl->flite_sd);
	} else {
		if (fimc_cam_use) {
			ret = v4l2_subdev_call(ctrl->flite_sd, core, s_power, 1);
			if (ret)
				fimc_err("s_power failed: %d", ret);
		}

	}

	return 0;
}

static int fimc_is_register_callback(struct device *dev, void *p)
{
	struct v4l2_subdev **sd = p;

	*sd = dev_get_drvdata(dev);

	if (!*sd)
		return -EINVAL;

	return 0; /* non-zero value stops iteration */
}

int fimc_is_release_subdev(struct fimc_control *ctrl)
{
	int ret;
	struct fimc_global *fimc = get_fimc_dev();
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);

	if (ctrl->is.sd && ctrl->cam && fimc_cam_use) {
		if (ctrl->cam->cam_power)
			ctrl->cam->cam_power(0);
		/* shutdown the MCLK */
		if (fimc->mclk_status == CAM_MCLK_ON) {
			clk_disable(ctrl->cam->clk);
			fimc->mclk_status = CAM_MCLK_OFF;
		}

		ret = v4l2_subdev_call(ctrl->is.sd, core, s_power, 0);
		if (ret < 0) {
			fimc_dbg("FIMC-IS init failed");
			return -ENODEV;
		}

		v4l2_device_unregister_subdev(ctrl->is.sd);
		ctrl->is.sd = NULL;
		ctrl->cam->initialized = 0;
		ctrl->cam = NULL;
		fimc->active_camera = -1;
	} else if (ctrl->is.sd && ctrl->cam) {
		v4l2_device_unregister_subdev(ctrl->is.sd);
		ctrl->is.sd = NULL;
		ctrl->cam->initialized = 0;
		ctrl->cam = NULL;
		fimc->active_camera = -1;
	}
	return 0;
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

static int fimc_is_init_cam(struct fimc_control *ctrl)
{
	struct fimc_global *fimc = get_fimc_dev();
	struct s3c_platform_camera *cam;
	int ret = 0;

#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	struct platform_device *pdev = to_platform_device(ctrl->dev);
#endif

	cam = ctrl->cam;
	/* Do noting if already initialized */
	if (ctrl->cam->initialized)
		return 0;

#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	if (ctrl->power_status == FIMC_POWER_OFF)
		pm_runtime_get_sync(&pdev->dev);
#endif
	/* set rate for mclk */
	if ((clk_get_rate(cam->clk)) && (fimc->mclk_status == CAM_MCLK_OFF)) {
		clk_set_rate(cam->clk, cam->clk_rate);
		clk_enable(cam->clk);
		fimc->mclk_status = CAM_MCLK_ON;
		fimc_info1("clock for camera (FIMC-IS): %d\n", cam->clk_rate);
	}

	/* enable camera power if needed */
	if (cam->cam_power) {
		ret = cam->cam_power(1);
		if (unlikely(ret < 0))
			fimc_err("\nfail to power on\n");
	}

	cam->initialized = 1;
	return ret;
}

int fimc_s_input(struct file *file, void *fh, unsigned int i)
{
	struct fimc_global *fimc = get_fimc_dev();
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	int ret = 0;

	fimc_dbg("%s: index %d\n", __func__, i);

	if (i >= FIMC_MAXCAMS) {
		fimc_err("%s: invalid input index\n", __func__);
		return -EINVAL;
	}

	if (!fimc->camera_isvalid[i])
		return -EINVAL;

	if (fimc->camera[i]->sd && fimc_cam_use) {
		fimc_err("%s: Camera already in use.\n", __func__);
		return -EBUSY;
	}
	mutex_lock(&ctrl->v4l2_lock);

	/* If ctrl->cam is not NULL, there is one subdev already registered.
	 * We need to unregister that subdev first. */
	if (i != fimc->active_camera) {
		fimc_info1("\n\nfimc_s_input activating subdev\n");
		if (ctrl->cam && (ctrl->cam->sd || ctrl->flite_sd))
			fimc_release_subdev(ctrl);
		else if (ctrl->is.sd)
			fimc_is_release_subdev(ctrl);
		ctrl->cam = fimc->camera[i];

		if ((ctrl->cam->id != CAMERA_WB) && (ctrl->cam->id !=
			CAMERA_WB_B) && (!ctrl->cam->use_isp) && fimc_cam_use) {
			ret = fimc_configure_subdev(ctrl);
			if (ret < 0) {
				mutex_unlock(&ctrl->v4l2_lock);
				fimc_err("%s: Could not register camera" \
					" sensor with V4L2.\n", __func__);
				return -ENODEV;
			}
		}
		fimc->active_camera = i;
		fimc_info2("fimc_s_input activated subdev = %d\n", i);
	}

	if (!fimc_cam_use) {
		if (i == fimc->active_camera) {
			ctrl->cam = fimc->camera[i];
			fimc_info2("fimc_s_input activating subdev FIMC%d\n",
							ctrl->id);
		} else {
			mutex_unlock(&ctrl->v4l2_lock);
			return -EINVAL;
		}
	}

	if (ctrl->cam->use_isp) {
	    /* fimc-lite attatch */
	    ret = fimc_subdev_attatch(ctrl);
	    if (ret) {
		    fimc_err("subdev_attatch failed\n");
		    mutex_unlock(&ctrl->v4l2_lock);
		    return -ENODEV;
	    }
	    /* fimc-is attatch */
	    ctrl->is.sd = fimc_is_get_subdev(i);
	    if (IS_ERR_OR_NULL(ctrl->is.sd)) {
		fimc_err("fimc-is subdev_attatch failed\n");
		mutex_unlock(&ctrl->v4l2_lock);
		return -ENODEV;
	    }

	    ctrl->is.fmt.width = ctrl->cam->width;
	    ctrl->is.fmt.height = ctrl->cam->height;
	    ctrl->is.frame_count = 0;
	    if (fimc_cam_use) {
		ret = fimc_is_init_cam(ctrl);
		if (ret < 0) {
			fimc_dbg("FIMC-IS init clock failed");
			mutex_unlock(&ctrl->v4l2_lock);
			return -ENODEV;
		}
		ret = v4l2_subdev_call(ctrl->is.sd, core, s_power, 1);
		if (ret < 0) {
			fimc_dbg("FIMC-IS init failed");
			mutex_unlock(&ctrl->v4l2_lock);
			return -ENODEV;
		}
		ret = v4l2_subdev_call(ctrl->is.sd, core, load_fw);
		if (ret < 0) {
			fimc_dbg("FIMC-IS init failed");
			mutex_unlock(&ctrl->v4l2_lock);
			return -ENODEV;
		}
		ret = v4l2_subdev_call(ctrl->is.sd, core, init, ctrl->cam->sensor_index);
		if (ret < 0) {
			fimc_dbg("FIMC-IS init failed");
			mutex_unlock(&ctrl->v4l2_lock);
			return -ENODEV;
		}
	    }
	}

	mutex_unlock(&ctrl->v4l2_lock);

	return 0;
}

int fimc_enum_fmt_vid_capture(struct file *file, void *fh,
					struct v4l2_fmtdesc *f)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int i = f->index;

	fimc_dbg("%s\n", __func__);

	mutex_lock(&ctrl->v4l2_lock);

	memset(f, 0, sizeof(*f));
	memcpy(f, &capture_fmts[i], sizeof(*f));

	mutex_unlock(&ctrl->v4l2_lock);

	return 0;
}

int fimc_g_fmt_vid_capture(struct file *file, void *fh, struct v4l2_format *f)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;

	fimc_dbg("%s\n", __func__);

	if (!ctrl->cap) {
		fimc_err("%s: no capture device info\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&ctrl->v4l2_lock);

	memset(&f->fmt.pix, 0, sizeof(f->fmt.pix));
	memcpy(&f->fmt.pix, &ctrl->cap->fmt, sizeof(f->fmt.pix));

	mutex_unlock(&ctrl->v4l2_lock);

	return 0;
}

/*
 * Check for whether the requested format
 * can be streamed out from FIMC
 * depends on FIMC node
 */
static int fimc_fmt_avail(struct fimc_control *ctrl,
		struct v4l2_pix_format *f)
{
	int i;

	/*
	 * TODO: check for which FIMC is used.
	 * Available fmt should be varied for each FIMC
	 */

	for (i = 0; i < ARRAY_SIZE(capture_fmts); i++) {
		if (capture_fmts[i].pixelformat == f->pixelformat)
			return 0;
	}

	fimc_info1("Not supported pixelformat requested\n");

	return -1;
}

/*
 * figures out the depth of requested format
 */
static int fimc_fmt_depth(struct fimc_control *ctrl, struct v4l2_pix_format *f)
{
	int err, depth = 0;

	/* First check for available format or not */
	err = fimc_fmt_avail(ctrl, f);
	if (err < 0)
		return -1;

	/* handles only supported pixelformats */
	switch (f->pixelformat) {
	case V4L2_PIX_FMT_RGB32:
		depth = 32;
		fimc_dbg("32bpp\n");
		break;
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_YUYV:
	case V4L2_PIX_FMT_UYVY:
	case V4L2_PIX_FMT_VYUY:
	case V4L2_PIX_FMT_YVYU:
	case V4L2_PIX_FMT_YUV422P:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV61:
		depth = 16;
		fimc_dbg("16bpp\n");
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV12T:
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		depth = 12;
		fimc_dbg("12bpp\n");
		break;
	case V4L2_PIX_FMT_JPEG:
		depth = -1;
		fimc_dbg("Compressed format.\n");
		break;
	default:
		fimc_dbg("why am I here?\n");
		break;
	}

	return depth;
}

int fimc_s_fmt_vid_private(struct file *file, void *fh, struct v4l2_format *f)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	struct v4l2_mbus_framefmt *mbus_fmt;
	int ret = 0;

	fimc_dbg("%s\n", __func__);
	if (ctrl->cam->sd) {
		struct v4l2_pix_format *pix = &f->fmt.pix;
		int depth;

		fimc_info1("%s %d:\n", __func__, __LINE__);

		mbus_fmt = &ctrl->cap->mbus_fmt;
		mbus_fmt->width = pix->width;
		mbus_fmt->height = pix->height;

		depth = fimc_fmt_depth(ctrl, pix);
		if (depth == 0) {
			fimc_err("%s: Invalid pixel format\n", __func__);
			return -EINVAL;
		} else if (depth < 0) {	/* JPEG  */
			mbus_fmt->code = V4L2_MBUS_FMT_JPEG_1X8;
			mbus_fmt->colorspace = V4L2_COLORSPACE_JPEG;
		} else {
			mbus_fmt->code = V4L2_MBUS_FMT_VYUY8_2X8;
		}

		if (fimc_cam_use) {
			ret = v4l2_subdev_call(ctrl->cam->sd, video,
					       s_mbus_fmt, mbus_fmt);
			if (ret) {
				fimc_err("%s: fail to s_mbus_fmt\n", __func__);
				return ret;
			}
		}

		return 0;
	} else {
		mbus_fmt = kzalloc(sizeof(*mbus_fmt), GFP_KERNEL);
		if (!mbus_fmt) {
			fimc_err("%s: no memory for "
				"mbus_fmt\n", __func__);
			return -ENOMEM;
		}
		ctrl->is.fmt.width = f->fmt.pix.width;
		ctrl->is.fmt.height = f->fmt.pix.height;
		ctrl->is.fmt.pixelformat = f->fmt.pix.pixelformat;

		mbus_fmt->width = f->fmt.pix.width;
		mbus_fmt->height = f->fmt.pix.height;
		mbus_fmt->code = V4L2_MBUS_FMT_YUYV8_2X8; /*dummy*/
		mbus_fmt->field = f->fmt.pix.field;
		mbus_fmt->colorspace = V4L2_COLORSPACE_SRGB;
		if (fimc_cam_use)
			ret = v4l2_subdev_call(ctrl->is.sd, video,
					s_mbus_fmt, mbus_fmt);
		kfree(mbus_fmt);
		return ret;
	}

	return -EINVAL;
}

int fimc_s_fmt_vid_capture(struct file *file, void *fh, struct v4l2_format *f)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	struct fimc_capinfo *cap = ctrl->cap;
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	struct platform_device *pdev = to_platform_device(ctrl->dev);
#endif
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);

	int ret = 0;
	int depth;
	struct v4l2_control is_ctrl;
	is_ctrl.id = 0;
	is_ctrl.value = 0;
	fimc_dbg("%s\n", __func__);

	/*
	 * The first time alloc for struct cap_info, and will be
	 * released at the file close.
	 * Anyone has better idea to do this?
	*/
	if (!cap) {
		cap = kzalloc(sizeof(*cap), GFP_KERNEL);
		if (!cap) {
			fimc_err("%s: no memory for "
				"capture device info\n", __func__);
			return -ENOMEM;
		}

		/* assign to ctrl */
		ctrl->cap = cap;
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
		if (ctrl->power_status == FIMC_POWER_OFF)
			pm_runtime_get_sync(&pdev->dev);
#endif
	} else {
		memset(cap, 0, sizeof(*cap));
	}

	mutex_lock(&ctrl->v4l2_lock);

	memset(&cap->fmt, 0, sizeof(cap->fmt));
	memcpy(&cap->fmt, &f->fmt.pix, sizeof(cap->fmt));

	/*
	 * Note that expecting format only can be with
	 * available output format from FIMC
	 * Following items should be handled in driver
	 * bytesperline = width * depth / 8
	 * sizeimage = bytesperline * height
	 */
	/* This function may return 0 or -1 in case of error,
	 * hence need to check here.
	 */

	depth = fimc_fmt_depth(ctrl, &cap->fmt);
	if (depth == 0) {
		mutex_unlock(&ctrl->v4l2_lock);
		fimc_err("%s: Invalid pixel format\n", __func__);
		return -EINVAL;
	} else if (depth < 0) {
		/*
		 * When the pixelformat is JPEG,
		 * the application is requesting for data
		 * in JPEG compressed format
		*/
		cap->fmt.colorspace = V4L2_COLORSPACE_JPEG;
		cap->fmt.priv = V4L2_PIX_FMT_MODE_CAPTURE;
	} else {
		cap->fmt.bytesperline = (cap->fmt.width * depth) >> 3;
		cap->fmt.sizeimage = (cap->fmt.bytesperline * cap->fmt.height);
		cap->fmt.priv = V4L2_PIX_FMT_MODE_PREVIEW;
	}

	if (cap->fmt.colorspace == V4L2_COLORSPACE_JPEG) {
		ctrl->sc.bypass = 1;
		cap->lastirq = 0;
		fimc_info1("fimc_s_fmt_vid_capture V4L2_COLORSPACE_JPEG\n");
	} else {
		ctrl->sc.bypass = 0;
		cap->lastirq = 0;
	}

	fimc_info1("s_fmt width = %d, height = %d\n", \
				cap->fmt.width, cap->fmt.height);

	/* WriteBack doesn't have subdev_call */
	if (ctrl->cam->id == CAMERA_WB || ctrl->cam->id == CAMERA_WB_B) {
		mutex_unlock(&ctrl->v4l2_lock);
		return 0;
	}

	if (ctrl->cam->use_isp) {
		ctrl->is.mbus_fmt.code = V4L2_MBUS_FMT_SGRBG10_1X10;
		is_ctrl.id = V4L2_CID_IS_GET_SENSOR_WIDTH;
		is_ctrl.value = 0;
		v4l2_subdev_call(ctrl->is.sd, core, g_ctrl, &is_ctrl);
		ctrl->is.fmt.width = ctrl->is.mbus_fmt.width = is_ctrl.value;

		is_ctrl.id = V4L2_CID_IS_GET_SENSOR_HEIGHT;
		is_ctrl.value = 0;
		v4l2_subdev_call(ctrl->is.sd, core, g_ctrl, &is_ctrl);
		ctrl->is.fmt.height = ctrl->is.mbus_fmt.height = is_ctrl.value;
		/* default offset values */
		ctrl->is.offset_x = 16;
		ctrl->is.offset_y = 12;
	}

	fimc_hwset_reset(ctrl);

	mutex_unlock(&ctrl->v4l2_lock);
	fimc_dbg("%s -- FIMC%d\n", __func__, ctrl->id);

	return ret;
}

int fimc_try_fmt_vid_capture(struct file *file, void *fh, struct v4l2_format *f)
{
	/* Not implement */
	return -ENOTTY;
}

static int fimc_alloc_buffers(struct fimc_control *ctrl,
			int plane, int size, int align, int bpp, int use_paddingbuf, int pad_size)
{
	struct fimc_capinfo *cap = ctrl->cap;
	int i, j;
	int plane_length[4] = {0, };

	switch (plane) {
	case 1:
		if (align) {
		plane_length[0] = PAGE_ALIGN((size*bpp) >> 3);
		plane_length[1] = 0;
		plane_length[2] = 0;
		} else {
			plane_length[0] = (size*bpp) >> 3;
			plane_length[1] = 0;
			plane_length[2] = 0;
		}
		break;
		/* In case of 2, only NV12 and NV12T is supported. */
	case 2:
		if (align) {
			plane_length[0] = PAGE_ALIGN((size*8) >> 3);
			plane_length[1] = PAGE_ALIGN((size*(bpp-8)) >> 3);
			plane_length[2] = 0;
			fimc_info2("plane_length[0] = %d, plane_length[1] = %d\n" \
					, plane_length[0], plane_length[1]);
		} else {
			plane_length[0] = ((size*8) >> 3);
			plane_length[1] = ((size*(bpp-8)) >> 3);
			plane_length[2] = 0;
			fimc_info2("plane_length[0] = %d, plane_length[1] = %d\n" \
					, plane_length[0], plane_length[1]);
		}

		break;
		/* In case of 3
		 * YUV422 : 8 / 4 / 4 (bits)
		 * YUV420 : 8 / 2 / 2 (bits)
	 * 3rd plane have to consider page align for mmap */
	case 3:
		if (align) {
			plane_length[0] = (size*8) >> 3;
			plane_length[1] = (size*((bpp-8)/2)) >> 3;
			plane_length[2] = PAGE_ALIGN((size*bpp)>>3) - plane_length[0]
				- plane_length[1];
		} else {
			plane_length[0] = (size*8) >> 3;
			plane_length[1] = (size*((bpp-8)/2)) >> 3;
			plane_length[2] = ((size*bpp)>>3) - plane_length[0]
				- plane_length[1];
		}
		break;
	default:
		fimc_err("impossible!\n");
		return -ENOMEM;
	}

	if (use_paddingbuf) {
		plane_length[plane] = pad_size;
		cap->pktdata_plane = plane;
	} else
		plane_length[plane] = 0;

	for (i = 0; i < cap->nr_bufs; i++) {
		for (j = 0; j < plane; j++) {
			cap->bufs[i].length[j] = plane_length[j];
			fimc_dma_alloc(ctrl, &cap->bufs[i], j, align);

			if (!cap->bufs[i].base[j])
				goto err_alloc;
		}
		if (use_paddingbuf) {
			cap->bufs[i].length[plane] = plane_length[plane];
			fimc_dma_alloc(ctrl, &cap->bufs[i], plane, align);

			cap->bufs[i].vaddr_pktdata = phys_to_virt(cap->bufs[i].base[plane]);
			/* printk(KERN_INFO "pktdata address = 0x%x, 0x%x\n"
				,cap->bufs[i].base[1], cap->bufs[i].vaddr_pktdata ); */

			if (!cap->bufs[i].base[plane])
				goto err_alloc;
		}
		cap->bufs[i].state = VIDEOBUF_PREPARED;
	}

	return 0;

err_alloc:
	for (i = 0; i < cap->nr_bufs; i++) {
		for (j = 0; j < plane; j++) {
			if (cap->bufs[i].base[j])
				fimc_dma_free(ctrl, &cap->bufs[i], j);
		}
		if (use_paddingbuf) {
			if (cap->bufs[i].base[plane])
				fimc_dma_free(ctrl, &cap->bufs[i], plane);
		}
		memset(&cap->bufs[i], 0, sizeof(cap->bufs[i]));
	}

	return -ENOMEM;
}

static void fimc_free_buffers(struct fimc_control *ctrl)
{
	struct fimc_capinfo *cap;
	int i;

	if (ctrl && ctrl->cap)
		cap = ctrl->cap;
	else
		return;

	for (i = 0; i < FIMC_PHYBUFS; i++) {
		memset(&cap->bufs[i], 0, sizeof(cap->bufs[i]));
		cap->bufs[i].state = VIDEOBUF_NEEDS_INIT;
	}

	ctrl->mem.curr = ctrl->mem.base;
}

int fimc_reqbufs_capture_mmap(void *fh, struct v4l2_requestbuffers *b)
{
	struct fimc_control *ctrl = fh;
	struct fimc_capinfo *cap = ctrl->cap;
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	struct platform_device *pdev = to_platform_device(ctrl->dev);
#endif
	int ret = 0, i;
	int bpp = 0;
	int size = 0;

	if (!cap) {
		fimc_err("%s: no capture device info\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&ctrl->v4l2_lock);

	/*  A count value of zero frees all buffers */
	if ((b->count == 0) || (b->count >= FIMC_CAPBUFS)) {
		/* aborting or finishing any DMA in progress */
		if (ctrl->status == FIMC_STREAMON)
			fimc_streamoff_capture(fh);
		for (i = 0; i < FIMC_CAPBUFS; i++) {
			fimc_dma_free(ctrl, &ctrl->cap->bufs[i], 0);
			fimc_dma_free(ctrl, &ctrl->cap->bufs[i], 1);
			fimc_dma_free(ctrl, &ctrl->cap->bufs[i], 2);
		}

		mutex_unlock(&ctrl->v4l2_lock);
		return 0;
	}
	/* free previous buffers */
	if ((cap->nr_bufs >= 0) && (cap->nr_bufs < FIMC_CAPBUFS)) {
		fimc_info1("%s : remained previous buffer count is %d\n", __func__,
				cap->nr_bufs);
		for (i = 0; i < cap->nr_bufs; i++) {
			fimc_dma_free(ctrl, &cap->bufs[i], 0);
			fimc_dma_free(ctrl, &cap->bufs[i], 1);
			fimc_dma_free(ctrl, &cap->bufs[i], 2);
		}
	}
	fimc_free_buffers(ctrl);

	cap->nr_bufs = b->count;
	if (pdata->hw_ver >= 0x51) {
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
		if (ctrl->power_status == FIMC_POWER_OFF) {
			pm_runtime_get_sync(&pdev->dev);
		}
#endif
		fimc_hw_reset_output_buf_sequence(ctrl);
		for (i = 0; i < cap->nr_bufs; i++) {
			fimc_hwset_output_buf_sequence(ctrl, i, 1);
			cap->bufs[i].id = i;
			cap->bufs[i].state = VIDEOBUF_NEEDS_INIT;

			/* initialize list */
			INIT_LIST_HEAD(&cap->bufs[i].list);
		}
		fimc_info1("%s: requested %d buffers\n", __func__, b->count);
		fimc_info1("%s: sequence[%d]\n", __func__,
				fimc_hwget_output_buf_sequence(ctrl));
		INIT_LIST_HEAD(&cap->outgoing_q);
	}
	if (pdata->hw_ver < 0x51) {
		INIT_LIST_HEAD(&cap->inq);
		for (i = 0; i < cap->nr_bufs; i++) {
			cap->bufs[i].id = i;
			cap->bufs[i].state = VIDEOBUF_NEEDS_INIT;

			/* initialize list */
			INIT_LIST_HEAD(&cap->bufs[i].list);
		}
	}

	if (cap->pktdata_enable)
		cap->pktdata_size = 0x1000;
		
	bpp = fimc_fmt_depth(ctrl, &cap->fmt);

	switch (cap->fmt.pixelformat) {
	case V4L2_PIX_FMT_RGB32:	/* fall through */
	case V4L2_PIX_FMT_RGB565:	/* fall through */
	case V4L2_PIX_FMT_YUYV:		/* fall through */
	case V4L2_PIX_FMT_UYVY:		/* fall through */
	case V4L2_PIX_FMT_VYUY:		/* fall through */
	case V4L2_PIX_FMT_YVYU:		/* fall through */
		fimc_info1("%s : 1plane\n", __func__);
		ret = fimc_alloc_buffers(ctrl, 1,
			cap->fmt.width * cap->fmt.height, SZ_4K, bpp, cap->pktdata_enable, cap->pktdata_size);
		break;

	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:		/* fall through */
	case V4L2_PIX_FMT_NV61:		/* fall through */
		fimc_info1("%s : 2plane for NV21 w %d h %d\n", __func__,
				cap->fmt.width, cap->fmt.height);
		ret = fimc_alloc_buffers(ctrl, 2,
			cap->fmt.width * cap->fmt.height, 0, bpp, cap->pktdata_enable, cap->pktdata_size);
		break;

	case V4L2_PIX_FMT_NV12:
		fimc_info1("%s : 2plane for NV12\n", __func__);
		ret = fimc_alloc_buffers(ctrl, 2,
			cap->fmt.width * cap->fmt.height, SZ_64K, bpp, cap->pktdata_enable, cap->pktdata_size);
		break;

	case V4L2_PIX_FMT_NV12T:
		fimc_info1("%s : 2plane for NV12T\n", __func__);
		ret = fimc_alloc_buffers(ctrl, 2,
			ALIGN(cap->fmt.width, 128) * ALIGN(cap->fmt.height, 32),
			SZ_64K, bpp, cap->pktdata_enable, cap->pktdata_size);
		break;

	case V4L2_PIX_FMT_YUV422P:	/* fall through */
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		fimc_info1("%s : 3plane\n", __func__);
		ret = fimc_alloc_buffers(ctrl, 3,
			cap->fmt.width * cap->fmt.height, 0, bpp, cap->pktdata_enable, cap->pktdata_size);
		break;

	case V4L2_PIX_FMT_JPEG:
		fimc_info1("%s : JPEG 1plane\n", __func__);
		size = fimc_camera_get_jpeg_memsize(ctrl);
		fimc_info2("%s : JPEG 1plane size = %x\n", __func__, size);
		ret = fimc_alloc_buffers(ctrl, 1, size, 0, 8, cap->pktdata_enable, cap->pktdata_size);
		break;
	default:
		break;
	}

	if (ret) {
		fimc_err("%s: no memory for capture buffer\n", __func__);
		mutex_unlock(&ctrl->v4l2_lock);
		return -ENOMEM;
	}

	mutex_unlock(&ctrl->v4l2_lock);

	return 0;
}

int fimc_reqbufs_capture_userptr(void *fh, struct v4l2_requestbuffers *b)
{
	struct fimc_control *ctrl = fh;
	struct fimc_capinfo *cap = ctrl->cap;
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	struct platform_device *pdev = to_platform_device(ctrl->dev);
#endif
	int i;

	if (!cap) {
		fimc_err("%s: no capture device info\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&ctrl->v4l2_lock);

	/*  A count value of zero frees all buffers */
	if ((b->count == 0) || (b->count >= FIMC_CAPBUFS)) {
		/* aborting or finishing any DMA in progress */
		if (ctrl->status == FIMC_STREAMON)
			fimc_streamoff_capture(fh);

		fimc_free_buffers(ctrl);

		mutex_unlock(&ctrl->v4l2_lock);
		return 0;
	}

	/* free previous buffers */
	if ((cap->nr_bufs >= 0) && (cap->nr_bufs < FIMC_CAPBUFS)) {
		fimc_info1("%s: prev buf cnt(%d)\n", __func__, cap->nr_bufs);
		fimc_free_buffers(ctrl);
	}

	cap->nr_bufs = b->count;

#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
	if (ctrl->power_status == FIMC_POWER_OFF) {
		pm_runtime_get_sync(&pdev->dev);
	}
#endif
	fimc_hw_reset_output_buf_sequence(ctrl);
	for (i = 0; i < cap->nr_bufs; i++) {
		fimc_hwset_output_buf_sequence(ctrl, i, 1);
		cap->bufs[i].id = i;
		cap->bufs[i].state = VIDEOBUF_IDLE;

		/* initialize list */
		INIT_LIST_HEAD(&cap->bufs[i].list);
	}
	fimc_info1("%s: requested %d buffers\n", __func__, b->count);
	fimc_info1("%s: sequence[%d]\n", __func__,
			fimc_hwget_output_buf_sequence(ctrl));
	INIT_LIST_HEAD(&cap->outgoing_q);

	mutex_unlock(&ctrl->v4l2_lock);

	return 0;
}

int fimc_reqbufs_capture(void *fh, struct v4l2_requestbuffers *b)
{
	int ret = 0;

	if (b->memory == V4L2_MEMORY_MMAP)
		ret = fimc_reqbufs_capture_mmap(fh, b);
	else
		ret = fimc_reqbufs_capture_userptr(fh, b);

	return ret;
}

int fimc_querybuf_capture(void *fh, struct v4l2_buffer *b)
{
	struct fimc_control *ctrl = fh;
	struct fimc_capinfo *cap = ctrl->cap;

	if (ctrl->status != FIMC_STREAMOFF) {
		fimc_err("fimc is running\n");
		return -EBUSY;
	}

	mutex_lock(&ctrl->v4l2_lock);

	switch (cap->fmt.pixelformat) {
	case V4L2_PIX_FMT_JPEG:		/* fall through */
	case V4L2_PIX_FMT_RGB32:	/* fall through */
	case V4L2_PIX_FMT_RGB565:	/* fall through */
	case V4L2_PIX_FMT_YUYV:		/* fall through */
	case V4L2_PIX_FMT_UYVY:		/* fall through */
	case V4L2_PIX_FMT_VYUY:		/* fall through */
	case V4L2_PIX_FMT_YVYU:		/* fall through */
		b->length = cap->bufs[b->index].length[0];
		break;

	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV16:		/* fall through */
	case V4L2_PIX_FMT_NV61:
		b->length = ctrl->cap->bufs[b->index].length[0]
			+ ctrl->cap->bufs[b->index].length[1];
		break;
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV12T:
		b->length = ALIGN(ctrl->cap->bufs[b->index].length[0], SZ_64K)
			+ ALIGN(ctrl->cap->bufs[b->index].length[1], SZ_64K);
		break;
	case V4L2_PIX_FMT_YUV422P:	/* fall through */
	case V4L2_PIX_FMT_YUV420:	/* fall through */
	case V4L2_PIX_FMT_YVU420:
		b->length = ctrl->cap->bufs[b->index].length[0]
			+ ctrl->cap->bufs[b->index].length[1]
			+ ctrl->cap->bufs[b->index].length[2];
		break;

	default:
		b->length = cap->bufs[b->index].length[0];
		break;
	}

	if (cap->pktdata_enable)
		b->length += ctrl->cap->bufs[b->index].length[cap->pktdata_plane];

	b->m.offset = b->index * PAGE_SIZE;
	/* memory field should filled V4L2_MEMORY_MMAP */
	b->memory = V4L2_MEMORY_MMAP;

	ctrl->cap->bufs[b->index].state = VIDEOBUF_IDLE;

	fimc_dbg("%s: %d bytes with offset: %d\n",
		__func__, b->length, b->m.offset);

	mutex_unlock(&ctrl->v4l2_lock);

	return 0;
}

int fimc_g_ctrl_capture(void *fh, struct v4l2_control *c)
{
	struct fimc_control *ctrl = fh;
	int ret = 0;

	fimc_dbg("%s\n", __func__);

	switch (c->id) {
	case V4L2_CID_ROTATION:
		c->value = ctrl->cap->rotate;
		break;

	case V4L2_CID_HFLIP:
		c->value = (ctrl->cap->flip & FIMC_XFLIP) ? 1 : 0;
		break;

	case V4L2_CID_VFLIP:
		c->value = (ctrl->cap->flip & FIMC_YFLIP) ? 1 : 0;
		break;

	case V4L2_CID_CACHEABLE:
		c->value = ctrl->cap->cacheable;
		break;

	default:
		/* get ctrl supported by subdev */
		/* WriteBack doesn't have subdev_call */
		if ((ctrl->cam->id == CAMERA_WB) || (ctrl->cam->id == CAMERA_WB_B))
			break;
		if (ctrl->cam->sd)
			ret = v4l2_subdev_call(ctrl->cam->sd, core, g_ctrl, c);
		if (ctrl->is.sd)
			ret = v4l2_subdev_call(ctrl->is.sd, core, g_ctrl, c);
		break;
	}

	return ret;
}

int fimc_s_ctrl_capture(void *fh, struct v4l2_control *c)
{
	struct fimc_control *ctrl = fh;
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	int ret = 0;

	fimc_dbg("%s\n", __func__);

	if (!ctrl->cam || !ctrl->cap || ((!ctrl->cam->sd) && (!ctrl->is.sd))) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	switch (c->id) {
	case V4L2_CID_ROTATION:
		ctrl->cap->rotate = c->value;
		break;

	case V4L2_CID_HFLIP:
		if (c->value)
			ctrl->cap->flip |= FIMC_XFLIP;
		else
			ctrl->cap->flip &= ~FIMC_XFLIP;
		break;

	case V4L2_CID_VFLIP:
		if (c->value)
			ctrl->cap->flip |= FIMC_YFLIP;
		else
			ctrl->cap->flip &= ~FIMC_YFLIP;
		break;

	case V4L2_CID_PADDR_Y:
		if (&ctrl->cap->bufs[c->value])
			c->value = ctrl->cap->bufs[c->value].base[FIMC_ADDR_Y];
		break;

	case V4L2_CID_PADDR_CB:		/* fall through */
	case V4L2_CID_PADDR_CBCR:
		if (&ctrl->cap->bufs[c->value])
			c->value = ctrl->cap->bufs[c->value].base[FIMC_ADDR_CB];
		break;

	case V4L2_CID_PADDR_CR:
		if (&ctrl->cap->bufs[c->value])
			c->value = ctrl->cap->bufs[c->value].base[FIMC_ADDR_CR];
		break;
	/* Implementation as per C100 FIMC driver */
	case V4L2_CID_STREAM_PAUSE:
		fimc_hwset_stop_processing(ctrl);
		break;

	case V4L2_CID_IMAGE_EFFECT_APPLY:
		ctrl->fe.ie_on = c->value ? 1 : 0;
		ctrl->fe.ie_after_sc = 0;
		ret = fimc_hwset_image_effect(ctrl);
		break;

	case V4L2_CID_IMAGE_EFFECT_FN:
		if (c->value < 0 || c->value > FIMC_EFFECT_FIN_SILHOUETTE)
			return -EINVAL;
		ctrl->fe.fin = c->value;
		ret = 0;
		break;

	case V4L2_CID_IMAGE_EFFECT_CB:
		ctrl->fe.pat_cb = c->value & 0xFF;
		ret = 0;
		break;

	case V4L2_CID_IMAGE_EFFECT_CR:
		ctrl->fe.pat_cr = c->value & 0xFF;
		ret = 0;
		break;

	case V4L2_CID_IS_LOAD_FW:
		if (ctrl->cam->use_isp)
			ret = v4l2_subdev_call(ctrl->is.sd, core, s_power, c->value);
		break;
	case V4L2_CID_IS_RESET:
		if (ctrl->cam->use_isp)
			ret = v4l2_subdev_call(ctrl->is.sd, core, reset, c->value);
		break;
	case V4L2_CID_IS_S_POWER:
		if (ctrl->cam->use_isp)
			ret = v4l2_subdev_call(ctrl->is.sd, core, s_power, c->value);
		break;
	case V4L2_CID_IS_S_STREAM:
		if (ctrl->cam->use_isp)
			ret = v4l2_subdev_call(ctrl->is.sd, video, s_stream, c->value);
		break;
	case V4L2_CID_CACHEABLE:
		ctrl->cap->cacheable = c->value;
		ret = 0;
		break;

	case V4L2_CID_EMBEDDEDDATA_ENABLE:
		ctrl->cap->pktdata_enable = c->value;
		ret = 0;
		break;

	case V4L2_CID_IS_ZOOM:
		fimc_is_set_zoom(ctrl, c);
		break;
#ifdef CONFIG_BUSFREQ_OPP
	case V4L2_CID_CAMERA_BUSFREQ_LOCK:
		/* lock bus frequency */
		dev_lock(ctrl->bus_dev, ctrl->dev, (unsigned long)c->value);
		break;
	case V4L2_CID_CAMERA_BUSFREQ_UNLOCK:
		/* unlock bus frequency */
		dev_unlock(ctrl->bus_dev, ctrl->dev);
		break;
#endif
	default:
		/* try on subdev */
		/* WriteBack doesn't have subdev_call */

		if ((ctrl->cam->id == CAMERA_WB) || \
			 (ctrl->cam->id == CAMERA_WB_B))
			break;
		if (fimc_cam_use)
			if (ctrl->cam->sd)
				ret = v4l2_subdev_call(ctrl->cam->sd,
							core, s_ctrl, c);
			if (ctrl->is.sd && ctrl->cam->use_isp)
				ret = v4l2_subdev_call(ctrl->is.sd,
							core, s_ctrl, c);
		else
			ret = 0;
		break;
	}

	return ret;
}

int fimc_g_ext_ctrls_capture(void *fh, struct v4l2_ext_controls *c)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = 0;
	mutex_lock(&ctrl->v4l2_lock);

	if (ctrl->is.sd)
		/* try on subdev */
		ret = v4l2_subdev_call(ctrl->is.sd, core, g_ext_ctrls, c);

	mutex_unlock(&ctrl->v4l2_lock);

	return ret;
}

int fimc_s_ext_ctrls_capture(void *fh, struct v4l2_ext_controls *c)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = 0;
	mutex_lock(&ctrl->v4l2_lock);

	if (ctrl->cam->sd)
		/* try on subdev */
		ret = v4l2_subdev_call(ctrl->cam->sd, core, s_ext_ctrls, c);
	else if (ctrl->is.sd)
		ret = v4l2_subdev_call(ctrl->is.sd, core, s_ext_ctrls, c);

	mutex_unlock(&ctrl->v4l2_lock);

	return ret;
}

int fimc_cropcap_capture(void *fh, struct v4l2_cropcap *a)
{
	struct fimc_control *ctrl = fh;
	struct fimc_capinfo *cap = ctrl->cap;
	struct fimc_global *fimc = get_fimc_dev();
	struct s3c_platform_fimc *pdata;

	fimc_dbg("%s\n", __func__);

	if (!ctrl->cam || !ctrl->cam->sd || !ctrl->cap) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}
	mutex_lock(&ctrl->v4l2_lock);

	pdata = to_fimc_plat(ctrl->dev);
	if (!ctrl->cam)
		ctrl->cam = fimc->camera[pdata->default_cam];

	if (!cap) {
		cap = kzalloc(sizeof(*cap), GFP_KERNEL);
		if (!cap) {
			fimc_err("%s: no memory for "
				"capture device info\n", __func__);
			return -ENOMEM;
		}

		/* assign to ctrl */
		ctrl->cap = cap;
	}

	/* crop limitations */
	cap->cropcap.bounds.left = 0;
	cap->cropcap.bounds.top = 0;
	cap->cropcap.bounds.width = ctrl->cam->width;
	cap->cropcap.bounds.height = ctrl->cam->height;

	/* crop default values */
	cap->cropcap.defrect.left = 0;
	cap->cropcap.defrect.top = 0;
	cap->cropcap.defrect.width = ctrl->cam->width;
	cap->cropcap.defrect.height = ctrl->cam->height;

	a->bounds = cap->cropcap.bounds;
	a->defrect = cap->cropcap.defrect;

	mutex_unlock(&ctrl->v4l2_lock);

	return 0;
}

int fimc_g_crop_capture(void *fh, struct v4l2_crop *a)
{
	struct fimc_control *ctrl = fh;

	fimc_dbg("%s\n", __func__);

	if (!ctrl->cap) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&ctrl->v4l2_lock);
	a->c = ctrl->cap->crop;
	mutex_unlock(&ctrl->v4l2_lock);

	return 0;
}

int fimc_s_crop_capture(void *fh, struct v4l2_crop *a)
{
	struct fimc_control *ctrl = fh;

	fimc_dbg("%s\n", __func__);

	mutex_lock(&ctrl->v4l2_lock);
	ctrl->cap->crop = a->c;
	mutex_unlock(&ctrl->v4l2_lock);

	return 0;
}

int fimc_start_capture(struct fimc_control *ctrl)
{
	fimc_dbg("%s\n", __func__);

	fimc_reset_status_reg(ctrl);

	if (!ctrl->sc.bypass)
		fimc_hwset_start_scaler(ctrl);

	fimc_hwset_enable_capture(ctrl, ctrl->sc.bypass);
	fimc_hwset_disable_frame_end_irq(ctrl);

	return 0;
}

int fimc_stop_capture(struct fimc_control *ctrl)
{
	fimc_dbg("%s\n", __func__);
	if (!ctrl->cam) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	if (!ctrl->cap) {
		fimc_err("%s: No cappure format.\n", __func__);
		return -ENODEV;
	}

	if (ctrl->cap->lastirq) {
		fimc_hwset_enable_lastirq(ctrl);
		fimc_hwset_disable_capture(ctrl);
		fimc_hwset_disable_lastirq(ctrl);
	} else {
		fimc_hwset_disable_capture(ctrl);
		fimc_hwset_enable_frame_end_irq(ctrl);
	}

	fimc_hwset_stop_scaler(ctrl);

	return 0;
}

static int fimc_check_capture_source(struct fimc_control *ctrl)
{
	if (!ctrl->cam)
		return -ENODEV;

	if (ctrl->cam->sd || ctrl->is.sd || !ctrl->flite_sd)
		return 0;

	if (ctrl->cam->id == CAMERA_WB || ctrl->cam->id == CAMERA_WB_B)
		return 0;

	return -ENODEV;
}

static int is_scale_up(struct fimc_control *ctrl)
{
	struct v4l2_mbus_framefmt *mbus_fmt = &ctrl->cap->mbus_fmt;
	struct v4l2_pix_format *pix = &ctrl->cap->fmt;

	if (!mbus_fmt->width) {
		fimc_err("%s: sensor resolution isn't selected.\n", __func__);
		return -EINVAL;
	}

	if (ctrl->cap->rotate == 90 || ctrl->cap->rotate == 270) {
		if (pix->width > mbus_fmt->height ||
			pix->height > mbus_fmt->width) {
			fimc_err("%s: ScaleUp isn't supported.\n", __func__);
			return -EINVAL;
		}
	} else {
		if (pix->width > mbus_fmt->width ||
			pix->height > mbus_fmt->height) {
			fimc_err("%s: ScaleUp isn't supported.\n", __func__);
			return -EINVAL;
		}
	}

	return 0;
}

int fimc_streamon_capture(void *fh)
{
	struct fimc_control *ctrl = fh;
	struct fimc_capinfo *cap = ctrl->cap;
	struct v4l2_frmsizeenum cam_frmsize;
	struct v4l2_control is_ctrl;

	int rot = 0, i;
	int ret = 0;
	struct s3c_platform_camera *cam = NULL;

	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);

	fimc_dbg("%s\n", __func__);
	cam_frmsize.discrete.width = 0;
	cam_frmsize.discrete.height = 0;
	is_ctrl.id = 0;
	is_ctrl.value = 0;

	if (!ctrl->cam) {
		fimc_err("%s: ctrl->cam is null\n", __func__);
		return -EINVAL;
	} else {
		cam = ctrl->cam;
	}

	if (fimc_check_capture_source(ctrl)) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	if (cam->sd) {
		if (is_scale_up(ctrl))
			1;  //			return -EINVAL; /* rapheal 2012.06.27*/
	}
	if (pdata->hw_ver < 0x51)
		fimc_hw_reset_camera(ctrl);
#if (!defined(CONFIG_EXYNOS_DEV_PD) && !defined(CONFIG_PM_RUNTIME))
	ctrl->status = FIMC_READY_ON;
#endif
	cap->irq = 0;

	fimc_hwset_enable_irq(ctrl, 0, 1);

	if ((cam->id != CAMERA_WB) && (cam->id != CAMERA_WB_B)) {
		if (fimc_cam_use && cam->sd) {
			ret = v4l2_subdev_call(cam->sd, video, enum_framesizes,
					&cam_frmsize);
			if (ret < 0) {
				dev_err(ctrl->dev, "%s: enum_framesizes failed\n",
						__func__);
				if (ret != -ENOIOCTLCMD)
					return ret;
			} else {
				if (cam_frmsize.discrete.width > 0
					&& cam_frmsize.discrete.height > 0) {
					cam->window.left = 0;
					cam->window.top = 0;
					cam->width = cam->window.width
						= cam_frmsize.discrete.width;
					cam->height
						= cam->window.height
						= cam_frmsize.discrete.height;
					fimc_info2("enum_framesizes width = %d,\
						height = %d\n", cam->width,
						cam->height);
				}
			}

			if (cap->fmt.priv == V4L2_PIX_FMT_MODE_CAPTURE) {
				ret = v4l2_subdev_call(cam->sd, video, s_stream, 1);
				if (ret < 0) {
					dev_err(ctrl->dev, "%s: s_stream failed\n",
							__func__);
					return ret;
				}
			}

			if (cam->type == CAM_TYPE_MIPI) {
				if(cam->id == CAMERA_CSI_C)	{
					s3c_csis_enable_pktdata(CSI_CH_0, cap->pktdata_enable);
					s3c_csis_start(CSI_CH_0, cam->mipi_lanes,
					cam->mipi_settle, cam->mipi_align,
					cam->width, cam->height,
					cap->fmt.pixelformat);
				}
				else {
					s3c_csis_enable_pktdata(CSI_CH_1, cap->pktdata_enable);
					s3c_csis_start(CSI_CH_1, cam->mipi_lanes,
					cam->mipi_settle, cam->mipi_align,
					cam->width, cam->height,
					cap->fmt.pixelformat);
				}
			}
			if (cap->fmt.priv != V4L2_PIX_FMT_MODE_CAPTURE) {
				ret = v4l2_subdev_call(cam->sd, video, s_stream, 1);
				if (ret < 0) {
					dev_err(ctrl->dev, "%s: s_stream failed\n",
							__func__);
					if (cam->id == CAMERA_CSI_C)
						s3c_csis_stop(CSI_CH_0);
					else
						s3c_csis_stop(CSI_CH_1);

					return ret;
				}
			}
		}
	}
	/* Set FIMD to write back */
	if ((cam->id == CAMERA_WB) || (cam->id == CAMERA_WB_B)) {
		if (cam->id == CAMERA_WB)
			fimc_hwset_sysreg_camblk_fimd0_wb(ctrl);
		else
			fimc_hwset_sysreg_camblk_fimd1_wb(ctrl);

		ret = s3cfb_direct_ioctl(0, S3CFB_SET_WRITEBACK, 1);
		if (ret) {
			fimc_err("failed set writeback\n");
			return ret;
		}

	}

	if (ctrl->cam->use_isp) {
		struct platform_device *pdev = to_platform_device(ctrl->dev);
		struct clk *pxl_async = NULL;
		is_ctrl.id = V4L2_CID_IS_GET_SENSOR_OFFSET_X;
		is_ctrl.value = 0;
		v4l2_subdev_call(ctrl->is.sd, core, g_ctrl, &is_ctrl);
		ctrl->is.offset_x = is_ctrl.value;
		is_ctrl.id = V4L2_CID_IS_GET_SENSOR_OFFSET_Y;
		is_ctrl.value = 0;
		v4l2_subdev_call(ctrl->is.sd, core, g_ctrl, &is_ctrl);
		ctrl->is.offset_y = is_ctrl.value;
		fimc_dbg("CSI setting width = %d, height = %d\n",
				ctrl->is.fmt.width + ctrl->is.offset_x,
				ctrl->is.fmt.height + ctrl->is.offset_y);

		if (ctrl->flite_sd && fimc_cam_use) {
			ctrl->is.mbus_fmt.width += ctrl->is.offset_x;
			ctrl->is.mbus_fmt.height += ctrl->is.offset_y;
			ret = v4l2_subdev_call(ctrl->flite_sd, video,
				s_mbus_fmt, &ctrl->is.mbus_fmt);
		}

		if (cam->id == CAMERA_CSI_C) {
			s3c_csis_start(CSI_CH_0, cam->mipi_lanes,
			cam->mipi_settle, cam->mipi_align,
			ctrl->is.fmt.width + ctrl->is.offset_x,
			ctrl->is.fmt.height + ctrl->is.offset_y,
			V4L2_PIX_FMT_SGRBG10);
		}
		else if (cam->id == CAMERA_CSI_D) {
			s3c_csis_start(CSI_CH_1, cam->mipi_lanes,
			cam->mipi_settle, cam->mipi_align,
			ctrl->is.fmt.width + ctrl->is.offset_x,
			ctrl->is.fmt.height + ctrl->is.offset_y,
			V4L2_PIX_FMT_SGRBG10);
		}

		pxl_async = clk_get(&pdev->dev, "pxl_async1");
		if (IS_ERR(pxl_async)) {
		    dev_err(&pdev->dev, "failed to get pxl_async\n");
		    return -ENODEV;
		}

		clk_enable(pxl_async);
		clk_put(pxl_async);
		fimc_hwset_sysreg_camblk_isp_wb(ctrl);
	}

	if (ctrl->flite_sd && fimc_cam_use)
		v4l2_subdev_call(ctrl->flite_sd, video, s_stream, 1);

	fimc_hwset_camera_type(ctrl);
	fimc_hwset_camera_polarity(ctrl);
	fimc_hwset_enable_lastend(ctrl);

	if (cap->fmt.pixelformat != V4L2_PIX_FMT_JPEG) {
		fimc_hwset_camera_source(ctrl);
		fimc_hwset_camera_offset(ctrl);

		fimc_capture_scaler_info(ctrl);
		fimc_hwset_prescaler(ctrl, &ctrl->sc);
		fimc_hwset_scaler(ctrl, &ctrl->sc);
		fimc_hwset_output_colorspace(ctrl, cap->fmt.pixelformat);
		fimc_hwset_output_addr_style(ctrl, cap->fmt.pixelformat);

		if (cap->fmt.pixelformat == V4L2_PIX_FMT_RGB32 ||
			cap->fmt.pixelformat == V4L2_PIX_FMT_RGB565)
			fimc_hwset_output_rgb(ctrl, cap->fmt.pixelformat);
		else
			fimc_hwset_output_yuv(ctrl, cap->fmt.pixelformat);

		fimc_hwset_output_area(ctrl, cap->fmt.width, cap->fmt.height);
		fimc_hwset_output_scan(ctrl, &cap->fmt);

		fimc_hwset_output_rot_flip(ctrl, cap->rotate, cap->flip);
		rot = fimc_mapping_rot_flip(cap->rotate, cap->flip);

		if (rot & FIMC_ROT) {
			fimc_hwset_org_output_size(ctrl, cap->fmt.width,
					cap->fmt.height);
			fimc_hwset_output_size(ctrl, cap->fmt.height,
					cap->fmt.width);
		} else {
			fimc_hwset_org_output_size(ctrl, cap->fmt.width,
					cap->fmt.height);
			fimc_hwset_output_size(ctrl, cap->fmt.width,
					cap->fmt.height);
		}

		fimc_hwset_jpeg_mode(ctrl, false);
	} else {
		fimc_hwset_output_size(ctrl,
				cap->fmt.width, cap->fmt.height);
		if (rot & FIMC_ROT)
			fimc_hwset_org_output_size(ctrl,
				cap->fmt.height, cap->fmt.width);
		else
			fimc_hwset_org_output_size(ctrl,
				cap->fmt.width, cap->fmt.height);

		fimc_hwset_output_area_size(ctrl,
				fimc_camera_get_jpeg_memsize(ctrl));
		fimc_hwset_jpeg_mode(ctrl, true);
	}

	if (pdata->hw_ver >= 0x51) {
		for (i = 0; i < cap->nr_bufs; i++)
			fimc_hwset_output_address(ctrl, &cap->bufs[i], i);
	} else {
		for (i = 0; i < FIMC_PINGPONG; i++)
			fimc_add_outqueue(ctrl, i);
	}

	if (ctrl->cap->fmt.colorspace == V4L2_COLORSPACE_JPEG)
		fimc_hwset_scaler_bypass(ctrl);

	fimc_start_capture(ctrl);
	ctrl->status = FIMC_STREAMON;

	if (ctrl->cam->use_isp)
		ret = v4l2_subdev_call(ctrl->is.sd, video, s_stream, 1);
	fimc_info1("%s-- fimc%d\n", __func__, ctrl->id);

	/* if available buffer did not remained */
	return 0;
}

int fimc_streamoff_capture(void *fh)
{
	struct fimc_control *ctrl = fh;
	struct fimc_capinfo *cap = ctrl->cap;

	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	int ret = 0;

	if (fimc_check_capture_source(ctrl)) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	ctrl->status = FIMC_READY_OFF;

	fimc_stop_capture(ctrl);

	/* wait for stop hardware */
	fimc_wait_disable_capture(ctrl);

	fimc_hwset_disable_irq(ctrl);
	if (pdata->hw_ver < 0x51)
		INIT_LIST_HEAD(&cap->inq);

	ctrl->status = FIMC_STREAMOFF;
	if (fimc_cam_use) {
		if (ctrl->cam->use_isp)
			v4l2_subdev_call(ctrl->is.sd, video, s_stream, 0);

		if (ctrl->flite_sd)
			v4l2_subdev_call(ctrl->flite_sd, video, s_stream, 0);

		if (ctrl->cam->sd)
			v4l2_subdev_call(ctrl->cam->sd, video, s_stream, 0);

		if (ctrl->cam->type == CAM_TYPE_MIPI) {
			if (ctrl->cam->id == CAMERA_CSI_C)
				s3c_csis_stop(CSI_CH_0);
			else
				s3c_csis_stop(CSI_CH_1);
		}
		fimc_hwset_reset(ctrl);

	} else {
		fimc_hwset_reset(ctrl);
	}

	/* Set FIMD to write back */
	if ((ctrl->cam->id == CAMERA_WB) || (ctrl->cam->id == CAMERA_WB_B)) {
		ret = s3cfb_direct_ioctl(0, S3CFB_SET_WRITEBACK, 0);
		if (ret) {
			fimc_err("failed set writeback\n");
			return ret;
		}
	}
	/* disable camera power */
	/* cam power off should call in the subdev release function */
	if (fimc_cam_use) {
		if (ctrl->cam->reset_camera) {
			if (ctrl->cam->cam_power)
				ctrl->cam->cam_power(0);
			if (ctrl->power_status != FIMC_POWER_SUSPEND)
				ctrl->cam->initialized = 0;
		}
	}
	fimc_info1("%s -- fimc%d\n", __func__, ctrl->id);
	return 0;
}

int fimc_is_set_zoom(struct fimc_control *ctrl, struct v4l2_control *c)
{
	struct v4l2_control is_ctrl;
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	struct s3c_platform_camera *cam = NULL;
	int ret = 0;

	is_ctrl.id = 0;
	is_ctrl.value = 0;

	if (ctrl->cam)
		cam = ctrl->cam;
	else
		return -ENODEV;

	/* 0. Check zoom width and height */
//	if (!c->value) {
//		ctrl->is.zoom_in_width = ctrl->is.fmt.width;
//		ctrl->is.zoom_in_height = ctrl->is.fmt.height;
//	} else {
//		ctrl->is.zoom_in_width = ctrl->is.fmt.width - (16 * c->value);
//		ctrl->is.zoom_in_height =
//			(ctrl->is.zoom_in_width * ctrl->is.fmt.height)
//			/ ctrl->is.fmt.width;
//		/* bayer crop contraint */
//		switch (ctrl->is.zoom_in_height%4) {
//		case 1:
//			ctrl->is.zoom_in_height--;
//			break;
//		case 2:
//			ctrl->is.zoom_in_height += 2;
//			break;
//		case 3:
//			ctrl->is.zoom_in_height++;
//			break;
//		}
//		if ((ctrl->is.zoom_in_width < (ctrl->is.fmt.width/4))
//		|| (ctrl->is.zoom_in_height < (ctrl->is.fmt.height/4))) {
//			ctrl->is.zoom_in_width = ctrl->is.fmt.width/4;
//			ctrl->is.zoom_in_height = ctrl->is.fmt.height/4;
//		}
//	}
	/* 1. fimc stop */
	fimc_stop_zoom_capture(ctrl);
	/* 2. Set zoom and calculate new width and height */
	if (ctrl->cam->use_isp) {
		ret = v4l2_subdev_call(ctrl->is.sd, core, s_ctrl, c);
		/* 2. Set zoom */
		is_ctrl.id = V4L2_CID_IS_ZOOM_STATE;
		is_ctrl.value = 0;
		while (!is_ctrl.value) {
			v4l2_subdev_call(ctrl->is.sd, core, g_ctrl, &is_ctrl);
			fimc_dbg("V4L2_CID_IS_ZOOM_STATE - %d", is_ctrl.value);
		}
	}
	/* 2. Change soruce size of FIMC */
	fimc_hwset_camera_change_source(ctrl);
	fimc_capture_change_scaler_info(ctrl);
	fimc_hwset_prescaler(ctrl, &ctrl->sc);
	fimc_hwset_scaler(ctrl, &ctrl->sc);
	/* 4. Start FIMC */
	fimc_start_zoom_capture(ctrl);
	/* 5. FIMC-IS stream on */
	if (ctrl->cam->use_isp)
		ret = v4l2_subdev_call(ctrl->is.sd, video, s_stream, 1);

	return 0;
}

static void fimc_buf2bs(struct fimc_buf_set *bs, struct fimc_buf *buf)
{
	bs->base[FIMC_ADDR_Y]	=  buf->base[FIMC_ADDR_Y];
	bs->length[FIMC_ADDR_Y]	=  buf->length[FIMC_ADDR_Y];

	bs->base[FIMC_ADDR_CB]	 =  buf->base[FIMC_ADDR_CB];
	bs->length[FIMC_ADDR_CB] =  buf->length[FIMC_ADDR_CB];

	bs->base[FIMC_ADDR_CR]	 =  buf->base[FIMC_ADDR_CR];
	bs->length[FIMC_ADDR_CR] =  buf->length[FIMC_ADDR_CR];
}

int fimc_qbuf_capture(void *fh, struct v4l2_buffer *b)
{
	struct fimc_control *ctrl = fh;
	struct fimc_buf *buf = (struct fimc_buf *)b->m.userptr;
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	struct fimc_capinfo *cap = ctrl->cap;
	int idx = b->index;
	int framecnt_seq;
	int available_bufnum;
	size_t length = 0;
	int i;

	if (!cap || !ctrl->cam) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}
	mutex_lock(&ctrl->v4l2_lock);
	if (pdata->hw_ver >= 0x51) {
		if (cap->bufs[idx].state != VIDEOBUF_IDLE) {
			fimc_err("%s: invalid state idx : %d\n", __func__, idx);
			mutex_unlock(&ctrl->v4l2_lock);
			return -EINVAL;
		} else {
			if (b->memory == V4L2_MEMORY_USERPTR) {
				fimc_buf2bs(&cap->bufs[idx], buf);
				fimc_hwset_output_address(ctrl,	&cap->bufs[idx], idx);
			}

			fimc_hwset_output_buf_sequence(ctrl, idx, FIMC_FRAMECNT_SEQ_ENABLE);
			cap->bufs[idx].state = VIDEOBUF_QUEUED;
			if (ctrl->status == FIMC_BUFFER_STOP) {
				framecnt_seq = fimc_hwget_output_buf_sequence(ctrl);
				available_bufnum =
					fimc_hwget_number_of_bits(framecnt_seq);
				if (available_bufnum >= 2) {
					fimc_start_capture(ctrl);
					ctrl->status = FIMC_STREAMON;
					ctrl->restart = true;
				}
			}
		}
	} else {
		fimc_add_inqueue(ctrl, b->index);
	}

	mutex_unlock(&ctrl->v4l2_lock);

	if (!cap->cacheable)
		return 0;

	for (i = 0; i < 3; i++) {
		if (cap->bufs[b->index].base[i])
			length += cap->bufs[b->index].length[i];
		else
			break;
	}

	if (length > (unsigned long) L2_FLUSH_ALL) {
		flush_cache_all();      /* L1 */
		smp_call_function((smp_call_func_t)__cpuc_flush_kern_all, NULL, 1);
		outer_flush_all();      /* L2 */
	} else if (length > (unsigned long) L1_FLUSH_ALL) {
		flush_cache_all();      /* L1 */
		smp_call_function((smp_call_func_t)__cpuc_flush_kern_all, NULL, 1);

		for (i = 0; i < 3; i++) {
			phys_addr_t start = cap->bufs[b->index].base[i];
			phys_addr_t end   = cap->bufs[b->index].base[i] +
					    cap->bufs[b->index].length[i] - 1;

			if (!start)
				break;

			outer_flush_range(start, end);  /* L2 */
		}
	} else {
		for (i = 0; i < 3; i++) {
			phys_addr_t start = cap->bufs[b->index].base[i];
			phys_addr_t end   = cap->bufs[b->index].base[i] +
					    cap->bufs[b->index].length[i] - 1;

			if (!start)
				break;

			dmac_flush_range(phys_to_virt(start), phys_to_virt(end));
			outer_flush_range(start, end);  /* L2 */
		}
	}

	return 0;
}

static void fimc_bs2buf(struct fimc_buf *buf, struct fimc_buf_set *bs)
{
	buf->base[FIMC_ADDR_Y]		=  bs->base[FIMC_ADDR_Y];
	buf->length[FIMC_ADDR_Y]	=  bs->length[FIMC_ADDR_Y];

	buf->base[FIMC_ADDR_CB]		=  bs->base[FIMC_ADDR_CB];
	buf->length[FIMC_ADDR_CB]	=  bs->length[FIMC_ADDR_CB];

	buf->base[FIMC_ADDR_CR]		=  bs->base[FIMC_ADDR_CR];
	buf->length[FIMC_ADDR_CR]	=  bs->length[FIMC_ADDR_CR];
}

int fimc_dqbuf_capture(void *fh, struct v4l2_buffer *b)
{
	unsigned long spin_flags;
	struct fimc_control *ctrl = fh;
	struct fimc_capinfo *cap = ctrl->cap;
	struct fimc_buf_set *bs;
	struct fimc_buf *buf = (struct fimc_buf *)b->m.userptr;
	int pp, ret = 0;

	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);

	if (!cap || !ctrl->cam) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	if (pdata->hw_ver >= 0x51) {
		spin_lock_irqsave(&ctrl->outq_lock, spin_flags);

		if (list_empty(&cap->outgoing_q)) {
			fimc_info2("%s: outgoing_q is empty\n", __func__);
			spin_unlock_irqrestore(&ctrl->outq_lock, spin_flags);
			return -EAGAIN;
		} else {
			bs = list_first_entry(&cap->outgoing_q, struct fimc_buf_set,
					list);
			fimc_info2("%s[%d]: bs->id : %d\n", __func__, ctrl->id, bs->id);
			b->index = bs->id;
			bs->state = VIDEOBUF_IDLE;

			if (b->memory == V4L2_MEMORY_USERPTR)
				fimc_bs2buf(buf, bs);

			list_del(&bs->list);
		}

		spin_unlock_irqrestore(&ctrl->outq_lock, spin_flags);
	} else {
		pp = ((fimc_hwget_frame_count(ctrl) + 2) % 4);
		if (cap->fmt.field == V4L2_FIELD_INTERLACED_TB)
			pp &= ~0x1;
		b->index = cap->outq[pp];
		fimc_info2("%s: buffer(%d) outq[%d]\n", __func__, b->index, pp);
		ret = fimc_add_outqueue(ctrl, pp);
		if (ret) {
			b->index = -1;
			fimc_err("%s: no inqueue buffer\n", __func__);
		}
	}

	return ret;
}

int fimc_enum_framesizes(struct file *filp, void *fh, struct v4l2_frmsizeenum *fsize)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int i;
	u32 index = 0;
	for (i = 0; i < ARRAY_SIZE(capture_fmts); i++) {
		if (fsize->pixel_format != capture_fmts[i].pixelformat)
			continue;
		if (fsize->index == index) {
			fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
			/* this is camera sensor's width, height.
			 * originally this should be filled each file format
			 */
			fsize->discrete.width = ctrl->cam->width;
			fsize->discrete.height = ctrl->cam->height;

			return 0;
		}
		index++;
	}

	return -EINVAL;
}
int fimc_enum_frameintervals(struct file *filp, void *fh,
		struct v4l2_frmivalenum *fival)
{
	if (fival->index > 0)
		return -EINVAL;
	/* temporary only support 30fps */
	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = 1000;
	fival->discrete.denominator = 30000;

	return 0;
}

/*
 * only used at mipi power func.
 */
struct device *fimc_get_active_device(void)
{
	struct fimc_global *fimc = get_fimc_dev();
	struct fimc_control *ctrl;

	if (!fimc || (fimc->active_camera < 0))
		return NULL;

	ctrl = get_fimc_ctrl(fimc->active_camera);

	return ctrl->dev;
}
