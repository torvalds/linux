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
#include <linux/videodev2_exynos_media.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/clk.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <plat/media.h>
#include <plat/clock.h>
#include <plat/fimc.h>
#include <linux/delay.h>
#include <mach/cpufreq.h>

#include <asm/cacheflush.h>

#include "fimc.h"

#define FRM_RATIO(w, h)		((w)*10/(h))

typedef enum {
	FRM_RATIO_QCIF = 12,
	FRM_RATIO_VGA = 13,
	FRM_RATIO_D1 = 15,
	FRM_RATIO_WVGA = 16,
	FRM_RATIO_HD = 17,
} frm_ratio_t;

/* subdev handling macro */
#define subdev_call(ctrl, o, f, args...) \
	v4l2_subdev_call(ctrl->cam->sd, o, f, ##args)

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
		.description	= "JPEG encoded data",
		.pixelformat	= V4L2_PIX_FMT_JPEG,
	}, {
		.index		= 14,
		.type		= V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.flags		= FORMAT_FLAGS_PLANAR,
		.description	= "YVU 4:2:0 planar, Y/Cr/Cb",
		.pixelformat	= V4L2_PIX_FMT_YVU420,
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
#endif

static int fimc_init_camera(struct fimc_control *ctrl)
{
	struct fimc_global *fimc = get_fimc_dev();
	struct s3c_platform_fimc *pdata;
	struct s3c_platform_camera *cam;
	int ret = 0, retry_cnt = 0;
	u32 pixelformat;

	pdata = to_fimc_plat(ctrl->dev);

	cam = ctrl->cam;

	/* do nothing if already initialized */
	if (ctrl->cam->initialized)
		return 0;

	/*
	 * WriteBack mode doesn't need to set clock and power,
	 * but it needs to set source width, height depend on LCD resolution.
	*/
	if ((cam->id == CAMERA_WB) || (cam->id == CAMERA_WB_B)) {
		s3cfb_direct_ioctl(0, S3CFB_GET_LCD_WIDTH, \
					(unsigned long)&cam->width);
		s3cfb_direct_ioctl(0, S3CFB_GET_LCD_HEIGHT, \
					(unsigned long)&cam->height);
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
			fimc_err("fail to power on\n\n");
	}

	/* subdev call for init */
	if (ctrl->cap->fmt.priv == V4L2_PIX_FMT_MODE_CAPTURE) {
		ret = v4l2_subdev_call(cam->sd, core, init, 1);
		pixelformat = V4L2_PIX_FMT_JPEG;
	} else {
		ret = v4l2_subdev_call(cam->sd, core, init, 0);
		pixelformat = cam->pixelformat;
	}

	/* Retry camera power-up if first i2c fails. */
	if (unlikely(ret < 0)) {
		if (cam->cam_power)
			cam->cam_power(0);

		if (fimc->mclk_status == CAM_MCLK_ON) {
			clk_disable(ctrl->cam->clk);
			fimc->mclk_status = CAM_MCLK_OFF;
		}

		if (retry_cnt++ < 3) {
			msleep(100);
			fimc_err("Retry power on(%d/3)\n\n", retry_cnt);
			goto retry;
		}
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

	ret = subdev_call(ctrl, core, g_ctrl, &cam_ctrl);
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

	sx = window->width;
	sy = window->height;

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

	fimc_warn("%s: CamOut (%d, %d), TargetOut (%d, %d)\n", __func__, sx, sy, tx, ty);

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
	ret = subdev_call(ctrl, video, g_parm, a);
	mutex_unlock(&ctrl->v4l2_lock);

	return ret;
}

int fimc_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = 0;
	int new_fps = a->parm.capture.timeperframe.denominator /
					a->parm.capture.timeperframe.numerator;

	fimc_info2("%s fimc%d, %d\n", __func__, ctrl->id, new_fps);

	/* WriteBack doesn't have subdev_call */
	if ((ctrl->cam->id == CAMERA_WB) || (ctrl->cam->id == CAMERA_WB_B))
		return 0;

	mutex_lock(&ctrl->v4l2_lock);
	if (ctrl->id != FIMC2)
		ret = subdev_call(ctrl, video, s_parm, a);
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
			memcpy(qc, &fimc_controls[i], \
				sizeof(struct v4l2_queryctrl));
			return 0;
		}
	}

	mutex_lock(&ctrl->v4l2_lock);
	ret = subdev_call(ctrl, core, queryctrl, qc);
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
	ret = subdev_call(ctrl, core, querymenu, qm);
	mutex_unlock(&ctrl->v4l2_lock);

	return ret;
}

int fimc_enum_input(struct file *file, void *fh, struct v4l2_input *inp)
{
	struct fimc_global *fimc = get_fimc_dev();
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;

	fimc_dbg("%s: index %d\n", __func__, inp->index);

	if (inp->index < 0 || inp->index >= FIMC_MAXCAMS) {
		fimc_err("%s: invalid input index, received = %d\n" \
				, __func__, inp->index);
		return -EINVAL;
	}

	if (!fimc->camera_isvalid[inp->index])
		return -EINVAL;
	mutex_lock(&ctrl->v4l2_lock);

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
		fimc_err("no camera device selected yet!" \
				"do VIDIOC_S_INPUT first\n");
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

	if (ctrl && ctrl->cam && ctrl->cam->sd) {
		fimc_dbg("%s called\n", __func__);

		/* WriteBack doesn't need clock setting */
		if ((ctrl->cam->id == CAMERA_WB) || (ctrl->cam->id == CAMERA_WB_B)) {
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
	return 0;
}

static int fimc_configure_subdev(struct fimc_control *ctrl)
{
	struct i2c_adapter *i2c_adap;
	struct i2c_board_info *i2c_info;
	struct v4l2_subdev *sd;
	unsigned short addr;
	char *name;

	i2c_adap = i2c_get_adapter(ctrl->cam->get_i2c_busnum());
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
		return -ENODEV;
	}

	/* Assign subdev to proper camera device pointer */
	ctrl->cam->sd = sd;

	return 0;
}

int fimc_s_input(struct file *file, void *fh, unsigned int i)
{
	struct fimc_global *fimc = get_fimc_dev();
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	struct fimc_capinfo *cap = ctrl->cap;
	struct platform_device *pdev = to_platform_device(ctrl->dev);
	int ret = 0;

	printk(KERN_INFO "%s: index %d FIMC%d\n", __func__, i, ctrl->id);

	if (i < 0 || i >= FIMC_MAXCAMS) {
		fimc_err("%s: invalid input index\n", __func__);
		return -EINVAL;
	}

	if (!fimc->camera_isvalid[i])
		return -EINVAL;

	if (fimc->camera[i]->sd && ctrl->id != FIMC2) {
		fimc_err("%s: Camera already in use.\n", __func__);
		return -EBUSY;
	}
	mutex_lock(&ctrl->v4l2_lock);

	/* If ctrl->cam is not NULL, there is one subdev already registered.
	 * We need to unregister that subdev first. */
	if (i != fimc->active_camera) {
		printk(KERN_INFO "\n\nfimc_s_input activating subdev\n");
		fimc_release_subdev(ctrl);
		ctrl->cam = fimc->camera[i];

		if ((ctrl->cam->id != CAMERA_WB) && (ctrl->cam->id != CAMERA_WB_B)) {
			ret = fimc_configure_subdev(ctrl);
			if (ret < 0) {
				mutex_unlock(&ctrl->v4l2_lock);
				fimc_err("%s: Could not register camera" \
						" sensor with V4L2.\n", __func__);
				return -ENODEV;
			}
		}

		fimc->active_camera = i;
		printk(KERN_INFO "fimc_s_input activated subdev = %d\n", i);
	}

	if (ctrl->id == FIMC2) {
		if (i == fimc->active_camera) {
			ctrl->cam = fimc->camera[i];
			fimc_info2("fimc_s_input activating subdev FIMC2 %d\n",
							ctrl->cam->initialized);
		} else {
			mutex_unlock(&ctrl->v4l2_lock);
			return -EINVAL;
		}
	}

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
			mutex_unlock(&ctrl->v4l2_lock);
			return -ENOMEM;
		}

		/* assign to ctrl */
		ctrl->cap = cap;
#if (defined(CONFIG_EXYNOS_DEV_PD) && defined(CONFIG_PM_RUNTIME))
		if (ctrl->power_status == FIMC_POWER_OFF) {
			pm_runtime_get_sync(&pdev->dev);
		}
#endif
	}

#if !defined(CONFIG_MACH_PX)
	if (fimc->active_camera == 0) {
		if (!ctrl->cam->initialized)
			ret = fimc_init_camera(ctrl);

		if (unlikely(ret < 0)) {
			if (ret == -ENOSYS) {
				/* return no error If firmware is bad.
				Because F/W update app should access the sensor through HAL instance */
				fimc_warn("%s: please update the F/W\n", __func__);
			} else {
				mutex_unlock(&ctrl->v4l2_lock);
				fimc_err("%s: fail to initialize subdev\n", __func__);
				return ret;
			}
		}
	}
#endif

	mutex_unlock(&ctrl->v4l2_lock);
	printk(KERN_INFO "%s--: index %d FIMC%d\n", __func__, i, ctrl->id);

	return 0;
}

int fimc_enum_fmt_vid_capture(struct file *file, void *fh,
					struct v4l2_fmtdesc *f)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int i = f->index;

	/* printk(KERN_INFO "%s++\n", __func__); */

	if (i >= ARRAY_SIZE(capture_fmts)) {
		fimc_err("%s: There is no support format index %d\n", __func__, i);
		return -EINVAL;
	}

	mutex_lock(&ctrl->v4l2_lock);

	memset(f, 0, sizeof(*f));
	memcpy(f, &capture_fmts[i], sizeof(*f));

	mutex_unlock(&ctrl->v4l2_lock);

	/* printk(KERN_INFO "%s--\n", __func__); */
	return 0;
}

int fimc_g_fmt_vid_capture(struct file *file, void *fh, struct v4l2_format *f)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;

	printk(KERN_INFO "%s++\n", __func__);

	if (!ctrl->cap) {
		fimc_err("%s: no capture device info\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&ctrl->v4l2_lock);

	memset(&f->fmt.pix, 0, sizeof(f->fmt.pix));
	memcpy(&f->fmt.pix, &ctrl->cap->fmt, sizeof(f->fmt.pix));

	mutex_unlock(&ctrl->v4l2_lock);
	printk(KERN_INFO "%s--\n", __func__);

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

static int fimc_calc_frame_ratio(struct fimc_control *ctrl,
				struct fimc_capinfo *cap)
{
	if (cap->fmt.priv != V4L2_PIX_FMT_MODE_PREVIEW)
		return 0;

	if ((cap->sensor_output_width != 0) &&
	    (cap->sensor_output_height != 0)) {
		cap->mbus_fmt.width = cap->sensor_output_width;
		cap->mbus_fmt.height = cap->sensor_output_height;
		cap->sensor_output_width = cap->sensor_output_height = 0;
		pr_info("fimc: forced sensor output size: (%d, %d) to (%d, %d)\n",
			cap->mbus_fmt.width, cap->mbus_fmt.height,
			cap->fmt.width, cap->fmt.height);
	} else if (cap->vt_mode) {
		cap->mbus_fmt.width = 640;
		cap->mbus_fmt.height = 480;
	}

	return 0;
}

#if defined(CONFIG_MACH_PX) && defined(CONFIG_VIDEO_HD_SUPPORT)
static int fimc_check_hd_mode(struct fimc_control *ctrl, struct v4l2_format *f)
{
	struct fimc_global *fimc = get_fimc_dev();
	struct fimc_capinfo *cap = ctrl->cap;
	u32 hd_mode = 0;
	int ret = -EINVAL;

	if (!cap->movie_mode || (fimc->active_camera != 0))
		return 0;

	if (f->fmt.pix.width == 1280 || cap->sensor_output_width == 1280)
		hd_mode = 1;

	printk(KERN_DEBUG "%s:movie_mode=%d, hd_mode=%d\n",
			__func__, cap->movie_mode, hd_mode);

	if (((cap->movie_mode == 2) && !hd_mode) ||
	    ((cap->movie_mode == 1) && hd_mode)) {
		fimc_warn("%s: mode change, power(%d) down\n",
			__func__, ctrl->cam->initialized);
		cap->movie_mode = hd_mode ? 2 : 1;

		if (ctrl->cam->initialized) {
			struct v4l2_control c;

			subdev_call(ctrl, core, reset, 0);
			c.id = V4L2_CID_CAMERA_SENSOR_MODE;
			c.value = cap->movie_mode;
			subdev_call(ctrl, core, s_ctrl, &c);

			if (ctrl->cam->cam_power) {
				ret = ctrl->cam->cam_power(0);
				if (unlikely(ret))
					return ret;
			}

			/* shutdown the MCLK */
			clk_disable(ctrl->cam->clk);
			fimc->mclk_status = CAM_MCLK_OFF;

			ctrl->cam->initialized = 0;
		}
	}

	return 0;
}
#endif

int fimc_s_fmt_vid_private(struct file *file, void *fh, struct v4l2_format *f)
{
	return -EINVAL;
}

int fimc_s_fmt_vid_capture(struct file *file, void *fh, struct v4l2_format *f)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	struct fimc_capinfo *cap = ctrl->cap;
	struct v4l2_mbus_framefmt *mbus_fmt;

	int ret = 0;
	int depth;
	printk(KERN_INFO "%s FIMC%d\n", __func__, ctrl->id);

	/* rotaton, flip, dtp_mode, movie_mode and vt_mode,
	 * sensor_output_width,height should be maintained.(by TN) */
	memset(cap, 0, sizeof(*cap) - sizeof(u32) * 7);

	mutex_lock(&ctrl->v4l2_lock);

	memset(&cap->fmt, 0, sizeof(cap->fmt));
	memcpy(&cap->fmt, &f->fmt.pix, sizeof(cap->fmt));

	mbus_fmt = &cap->mbus_fmt;
	if (ctrl->id != FIMC2) {
		if (cap->movie_mode || cap->vt_mode ||
		    cap->fmt.priv == V4L2_PIX_FMT_MODE_HDR) {
#if defined(CONFIG_MACH_PX) && defined(CONFIG_VIDEO_HD_SUPPORT)
			ret = fimc_check_hd_mode(ctrl, f);
			if (unlikely(ret)) {
				fimc_err("%s: error, check_hd_mode\n",
					__func__);
				return ret;
			}
#endif
			fimc_calc_frame_ratio(ctrl, cap);
		}
#if defined(CONFIG_MACH_U1_BD) || defined(CONFIG_MACH_Q1_BD)
		else {
			fimc_calc_frame_ratio(ctrl, cap);
		}
#endif

		if (!(mbus_fmt->width && mbus_fmt->height)) {
			mbus_fmt->width = cap->fmt.width;
			mbus_fmt->height = cap->fmt.height;
		}
		mbus_fmt->field = cap->fmt.priv;
	}

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
		mbus_fmt->code = V4L2_MBUS_FMT_JPEG_1X8;
	} else {
		cap->fmt.bytesperline = (cap->fmt.width * depth) >> 3;
		cap->fmt.sizeimage = (cap->fmt.bytesperline * cap->fmt.height);
		mbus_fmt->code = V4L2_MBUS_FMT_VYUY8_2X8;
	}
	mbus_fmt->colorspace = cap->fmt.colorspace;


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
	if ((ctrl->cam->id == CAMERA_WB) || (ctrl->cam->id == CAMERA_WB_B)) {
		mutex_unlock(&ctrl->v4l2_lock);
		return 0;
	}

	if (ctrl->id != FIMC2)
		ret = subdev_call(ctrl, video, s_mbus_fmt, mbus_fmt);

	mutex_unlock(&ctrl->v4l2_lock);
	printk(KERN_INFO "%s -- FIMC%d\n", __func__, ctrl->id);

	return ret;
}

int fimc_try_fmt_vid_capture(struct file *file, void *fh, struct v4l2_format *f)
{
	/* Not implement */
	return -ENOTTY;
}

static int fimc_alloc_buffers(struct fimc_control *ctrl,
				int plane, int size, int align, int bpp, int use_paddingbuf)
{
	struct fimc_capinfo *cap = ctrl->cap;
	int i, j;
	int plane_length[4] = {0, };
	if (plane < 1 || plane > 3)
		return -ENOMEM;

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
			plane_length[2] = PAGE_ALIGN((size*bpp)>>3) - plane_length[0] - plane_length[1];
		} else {
			plane_length[0] = (size*8) >> 3;
			plane_length[1] = (size*((bpp-8)/2)) >> 3;
			plane_length[2] = ((size*bpp)>>3) - plane_length[0] - plane_length[1];
		}
		break;
	default:
		fimc_err("impossible!\n");
		return -ENOMEM;
	}

	if (use_paddingbuf)
		plane_length[3] = 16;
	else
		plane_length[3] = 0;

	for (i = 0; i < cap->nr_bufs; i++) {
		for (j = 0; j < plane; j++) {
			cap->bufs[i].length[j] = plane_length[j];
			fimc_dma_alloc(ctrl, &cap->bufs[i], j, align);

			if (!cap->bufs[i].base[j])
				goto err_alloc;
		}
		if (use_paddingbuf) {
			cap->bufs[i].length[3] = plane_length[3];
			fimc_dma_alloc(ctrl, &cap->bufs[i], 3, align);

			if (!cap->bufs[i].base[3])
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
			if (cap->bufs[i].base[3])
				fimc_dma_free(ctrl, &cap->bufs[i], 3);
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

int fimc_reqbufs_capture(void *fh, struct v4l2_requestbuffers *b)
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
			/*vcm_set_pgtable_base(ctrl->vcm_id);*/
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
		fimc_info1("%s: sequence[%d]\n", __func__, fimc_hwget_output_buf_sequence(ctrl));
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

	bpp = fimc_fmt_depth(ctrl, &cap->fmt);

	switch (cap->fmt.pixelformat) {
	case V4L2_PIX_FMT_RGB32:	/* fall through */
	case V4L2_PIX_FMT_RGB565:	/* fall through */
	case V4L2_PIX_FMT_YUYV:		/* fall through */
	case V4L2_PIX_FMT_UYVY:		/* fall through */
	case V4L2_PIX_FMT_VYUY:		/* fall through */
	case V4L2_PIX_FMT_YVYU:		/* fall through */
	case V4L2_PIX_FMT_NV16:		/* fall through */
	case V4L2_PIX_FMT_NV61:		/* fall through */
		fimc_info1("%s : 1plane\n", __func__);
		ret = fimc_alloc_buffers(ctrl, 1,
			cap->fmt.width * cap->fmt.height, SZ_4K, bpp, 0);
		break;

	case V4L2_PIX_FMT_NV21:
		fimc_info1("%s : 2plane for NV21 w %d h %d\n", __func__, cap->fmt.width, cap->fmt.height);
		ret = fimc_alloc_buffers(ctrl, 2,
			cap->fmt.width * cap->fmt.height, 0, bpp, 0);
		break;

	case V4L2_PIX_FMT_NV12:		/* fall through */
		fimc_info1("%s : 2plane for NV12\n", __func__);
		ret = fimc_alloc_buffers(ctrl, 2,
			cap->fmt.width * cap->fmt.height, SZ_64K, bpp, 0);
		break;

	case V4L2_PIX_FMT_NV12T:	/* fall through */
		fimc_info1("%s : 2plane for NV12T\n", __func__);
		ret = fimc_alloc_buffers(ctrl, 2,
			ALIGN(cap->fmt.width, 128) * ALIGN(cap->fmt.height, 32), SZ_64K, bpp, 0);
		break;

	case V4L2_PIX_FMT_YUV422P:	/* fall through */
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		fimc_info1("%s : 3plane\n", __func__);
		ret = fimc_alloc_buffers(ctrl, 3,
			cap->fmt.width * cap->fmt.height, 0, bpp, 0);
		break;

	case V4L2_PIX_FMT_JPEG:
		fimc_info1("%s : JPEG 1plane\n", __func__);
		size = fimc_camera_get_jpeg_memsize(ctrl);
		fimc_info2("%s : JPEG 1plane size = %x\n",
			 __func__, size);
		ret = fimc_alloc_buffers(ctrl, 1,
			size, 0, 8, 0);
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
	case V4L2_PIX_FMT_NV16:		/* fall through */
	case V4L2_PIX_FMT_NV61:		/* fall through */
		b->length = cap->bufs[b->index].length[0];
		break;

	case V4L2_PIX_FMT_NV21:
		b->length = ctrl->cap->bufs[b->index].length[0]
			+ ctrl->cap->bufs[b->index].length[1];
		break;
	case V4L2_PIX_FMT_NV12:		/* fall through */
	case V4L2_PIX_FMT_NV12T:
		b->length = ALIGN(ctrl->cap->bufs[b->index].length[0], SZ_64K)
			+ ALIGN(ctrl->cap->bufs[b->index].length[1], SZ_64K);
		break;
	case V4L2_PIX_FMT_YUV422P:	/* fall through */
	case V4L2_PIX_FMT_YUV420:
	case V4L2_PIX_FMT_YVU420:
		b->length = ctrl->cap->bufs[b->index].length[0]
			+ ctrl->cap->bufs[b->index].length[1]
			+ ctrl->cap->bufs[b->index].length[2];
		break;

	default:
		b->length = cap->bufs[b->index].length[0];
		break;
	}
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
		ret = subdev_call(ctrl, core, g_ctrl, c);
		break;
	}

	return ret;
}

int fimc_g_ext_ctrls_capture(void *fh, struct v4l2_ext_controls *c)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = 0;
	printk(KERN_INFO "%s++\n", __func__);

	/* try on subdev */
	ret = subdev_call(ctrl, core, g_ext_ctrls, c);

	printk(KERN_INFO "%s--\n", __func__);

	return ret;
}

int fimc_s_ctrl_capture(void *fh, struct v4l2_control *c)
{
	struct fimc_control *ctrl = fh;
	struct fimc_global *fimc = get_fimc_dev();
	int ret = 0;

	fimc_dbg("%s\n", __func__);

	if (!ctrl->cam || !ctrl->cap ){
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	if ((ctrl->cam->id != CAMERA_WB) && (ctrl->cam->id != CAMERA_WB_B)) {
		if (!ctrl->cam->sd) {
			fimc_err("%s: No subdevice.\n", __func__);
			return -ENODEV;
		}
	}

	switch (c->id) {
	case V4L2_CID_CAM_UPDATE_FW:
		if (fimc->mclk_status == CAM_MCLK_ON) {
			if (ctrl->cam->cam_power)
				ctrl->cam->cam_power(0);

			/* shutdown the MCLK */
			clk_disable(ctrl->cam->clk);
			fimc->mclk_status = CAM_MCLK_OFF;

			mdelay(5);
		}

		if ((clk_get_rate(ctrl->cam->clk)) && (fimc->mclk_status == CAM_MCLK_OFF)) {
			clk_set_rate(ctrl->cam->clk, ctrl->cam->clk_rate);
			clk_enable(ctrl->cam->clk);
			fimc->mclk_status = CAM_MCLK_ON;
			fimc_info1("clock for camera: %d\n", ctrl->cam->clk_rate);

			if (ctrl->cam->cam_power)
				ctrl->cam->cam_power(1);
		}

		if (c->value == FW_MODE_UPDATE)
			ret = subdev_call(ctrl, core, load_fw);
		else
			ret = subdev_call(ctrl, core, s_ctrl, c);
		break;

	case V4L2_CID_CAMERA_RESET:
		fimc_warn("reset the camera sensor\n");
		if (ctrl->cam->initialized) {
			if (ctrl->cam->cam_power)
				ctrl->cam->cam_power(0);

			/* shutdown the MCLK */
			clk_disable(ctrl->cam->clk);
			fimc->mclk_status = CAM_MCLK_OFF;
			ctrl->cam->initialized = 0;
#if defined(CONFIG_MACH_PX)
			/* 5ms -> 100ms: increase delay.
			 * There are cases that sensor doesn't get revived
			 * inspite of doing power reset.*/
			msleep(100);
#else
			msleep(5);
#endif
		}
		ret = fimc_init_camera(ctrl);
		break;

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
		if (ctrl->cap->bufs)
			c->value = ctrl->cap->bufs[c->value].base[FIMC_ADDR_Y];
		break;

	case V4L2_CID_PADDR_CB:		/* fall through */
	case V4L2_CID_PADDR_CBCR:
		if (ctrl->cap->bufs)
			c->value = ctrl->cap->bufs[c->value].base[FIMC_ADDR_CB];
		break;

	case V4L2_CID_PADDR_CR:
		if (ctrl->cap->bufs)
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

	case V4L2_CID_CAMERA_SENSOR_MODE:
		ctrl->cap->movie_mode = c->value;
		ret = subdev_call(ctrl, core, s_ctrl, c);
#if defined(CONFIG_VIDEO_HD_SUPPORT)
		printk(KERN_INFO "%s: CAMERA_SENSOR_MODE=%d\n",
				__func__, c->value);
		if (!ctrl->cam->initialized)
			ret = fimc_init_camera(ctrl);
#endif /* CONFIG_VIDEO_HD_SUPPORT */
		break;

	case V4L2_CID_CAMERA_VT_MODE:
		ctrl->cap->vt_mode = c->value;
		ret = subdev_call(ctrl, core, s_ctrl, c);
		break;

	case V4L2_CID_CAMERA_CHECK_DATALINE:
#ifdef CONFIG_MACH_PX
		/* if camera type is MIPI,
		 * we does not do any subdev_calll */
		if ((ctrl->cam->type == CAM_TYPE_MIPI) ||
		    (ctrl->cap->dtp_mode == c->value)) {
#else
		if (ctrl->cap->dtp_mode == c->value) {
#endif
			ret = 0;
			break;
		} else {
			if (c->value == 0 && ctrl->cam->initialized) {
				/* need to reset after dtp test is finished */
				fimc_warn("DTP: reset the camera sensor\n");
				if (ctrl->cam->cam_power)
					ctrl->cam->cam_power(0);

				/* shutdown the MCLK */
				clk_disable(ctrl->cam->clk);
				fimc->mclk_status = CAM_MCLK_OFF;
				ctrl->cam->initialized = 0;

				msleep(100);
				ret = fimc_init_camera(ctrl);
			}
			ctrl->cap->dtp_mode = c->value;
		}
		ret = subdev_call(ctrl, core, s_ctrl, c);
		break;

	case V4L2_CID_CACHEABLE:
		ctrl->cap->cacheable = c->value;
		ret = 0;
		break;

	case V4L2_CID_CAMERA_SENSOR_OUTPUT_SIZE:
		ctrl->cap->sensor_output_width = (u32)c->value >> 16;
		ctrl->cap->sensor_output_height = (u32)c->value & 0x0FFFF;
		break;

	default:
		/* try on subdev */
		/* WriteBack doesn't have subdev_call */

		if ((ctrl->cam->id == CAMERA_WB) || \
			 (ctrl->cam->id == CAMERA_WB_B))
			break;
		if (FIMC2 != ctrl->id)
			ret = subdev_call(ctrl, core, s_ctrl, c);
		else
			ret = 0;
		break;
	}

	return ret;
}

int fimc_s_ext_ctrls_capture(void *fh, struct v4l2_ext_controls *c)
{
	struct fimc_control *ctrl = ((struct fimc_prv_data *)fh)->ctrl;
	int ret = 0;
	mutex_lock(&ctrl->v4l2_lock);

	/* try on subdev */
	ret = subdev_call(ctrl, core, s_ext_ctrls, c);

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

	if (!ctrl->sc.bypass)
		fimc_hwset_start_scaler(ctrl);

	fimc_hwset_enable_capture(ctrl, ctrl->sc.bypass);

	return 0;
}

int fimc_stop_capture(struct fimc_control *ctrl)
{
	fimc_dbg("%s\n", __func__);
	if (!ctrl->cam) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	if ((ctrl->cam->id != CAMERA_WB) && (ctrl->cam->id != CAMERA_WB_B)) {
		if (!ctrl->cam->sd) {
			fimc_err("%s: No subdevice.\n", __func__);
			return -ENODEV;
		}
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
	}

	fimc_hwset_stop_scaler(ctrl);

	return 0;
}


int fimc_streamon_capture(void *fh)
{
	struct fimc_control *ctrl = fh;
	struct fimc_capinfo *cap = ctrl->cap;
	struct fimc_global *fimc = get_fimc_dev();
	struct v4l2_frmsizeenum cam_frmsize;

	int rot = 0, i;
	int ret = 0;
	struct s3c_platform_camera *cam = NULL;

	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	unsigned int inner_elapsed_usec = 0;

	printk(KERN_INFO "%s fimc%d\n", __func__, ctrl->id);
	cam_frmsize.discrete.width = 0;
	cam_frmsize.discrete.height = 0;
	if (!ctrl->cam) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	if ((ctrl->cam->id != CAMERA_WB) && (ctrl->cam->id != CAMERA_WB_B)) {
		if (!ctrl->cam->sd) {
			fimc_err("%s: No subdevice.\n", __func__);
			return -ENODEV;
		}
	}

	if (pdata->hw_ver < 0x51)
		fimc_hw_reset_camera(ctrl);
#if (!defined(CONFIG_EXYNOS_DEV_PD) && !defined(CONFIG_PM_RUNTIME))
	ctrl->status = FIMC_READY_ON;
#endif
	cap->irq = 0;

	fimc_hwset_enable_irq(ctrl, 0, 1);

	if (!ctrl->cam->initialized) {
			ret = fimc_init_camera(ctrl);
		if (unlikely(ret < 0)) {
			fimc_err("%s: fail to initialize subdev\n", __func__);
			return ret;
		}
	}

	/* csi control position change because runtime pm */
	if (ctrl->cam)
		cam = ctrl->cam;

	if ((ctrl->cam->id != CAMERA_WB) && (ctrl->cam->id != CAMERA_WB_B)) {
		if (ctrl->id != FIMC2) {
			ret = subdev_call(ctrl, video, enum_framesizes, &cam_frmsize);
			if (ret < 0) {
				dev_err(ctrl->dev, "%s: enum_framesizes failed\n", __func__);
				if (ret != -ENOIOCTLCMD)
					return ret;
			} else {
#ifdef CONFIG_TARGET_LOCALE_KOR
				if ((ctrl->cap->vt_mode != 0) &&
#else
				if ((ctrl->cap->vt_mode == 1) &&
#endif
						(cap->rotate == 90 || cap->rotate == 270)) {
					ctrl->cam->window.left = 136;
					ctrl->cam->window.top = 0;
					ctrl->cam->window.width = 368;
					ctrl->cam->window.height = 480;
					ctrl->cam->width = cam_frmsize.discrete.width;
					ctrl->cam->height = cam_frmsize.discrete.height;
					dev_err(ctrl->dev, "vtmode = %d, rotate = %d,"
						" cam->width = %d,"
						" cam->height = %d\n", ctrl->cap->vt_mode, cap->rotate,
						 ctrl->cam->width, ctrl->cam->height);
				} else {
					if (cam_frmsize.discrete.width > 0 && cam_frmsize.discrete.height > 0) {
						ctrl->cam->window.left = 0;
						ctrl->cam->window.top = 0;
						ctrl->cam->width = ctrl->cam->window.width = cam_frmsize.discrete.width;
						ctrl->cam->height = ctrl->cam->window.height = cam_frmsize.discrete.height;
						fimc_info2("enum_framesizes width = %d, height = %d\n",
								ctrl->cam->width, ctrl->cam->height);
					}
				}
			}

			if (cam->type == CAM_TYPE_MIPI) {
				/*
				 * subdev call for sleep/wakeup:
				 * no error although no s_stream api support
				*/
#if defined(CONFIG_MACH_PX)
#ifdef CONFIG_VIDEO_IMPROVE_STREAMOFF
				v4l2_subdev_call(cam->sd, video, s_stream,
						STREAM_MODE_WAIT_OFF);
#endif /* CONFIG_VIDEO_IMPROVE_STREAMOFF */
#else /* CONFIG_MACH_PX */
				if (fimc->active_camera == 0) {
					if (cap->fmt.priv != V4L2_PIX_FMT_MODE_PREVIEW) {
						v4l2_subdev_call(cam->sd, video, s_stream,
								STREAM_MODE_CAM_ON);
					}
				} else {
					v4l2_subdev_call(cam->sd, video, s_stream,
						STREAM_MODE_WAIT_OFF);
				}
#endif
				if (cam->id == CAMERA_CSI_C) {
					s3c_csis_start(CSI_CH_0, cam->mipi_lanes, cam->mipi_settle, \
						cam->mipi_align, cam->width, cam->height, cap->fmt.pixelformat);
				} else {
					s3c_csis_start(CSI_CH_1, cam->mipi_lanes, cam->mipi_settle, \
						cam->mipi_align, cam->width, cam->height, cap->fmt.pixelformat);
				}
#if defined(CONFIG_MACH_PX)
				v4l2_subdev_call(cam->sd, video, s_stream,
					STREAM_MODE_CAM_ON);
#else /* CONFIG_MACH_PX */
				if (fimc->active_camera == 0) {
					if (cap->fmt.priv == V4L2_PIX_FMT_MODE_PREVIEW) {
						v4l2_subdev_call(cam->sd, video, s_stream,
								STREAM_MODE_CAM_ON);
					}
				} else {
					v4l2_subdev_call(cam->sd, video, s_stream,
							STREAM_MODE_CAM_ON);
				}
#endif
			} else {
				subdev_call(ctrl, video, s_stream, STREAM_MODE_CAM_ON);
			}
		} else {
			if (cap->fmt.priv != V4L2_PIX_FMT_MODE_HDR)
				v4l2_subdev_call(cam->sd, video, s_stream, STREAM_MODE_MOVIE_ON);
		}
	}

	/* Set FIMD to write back */
	if ((ctrl->cam->id == CAMERA_WB) || (ctrl->cam->id == CAMERA_WB_B)) {
		if (ctrl->cam->id == CAMERA_WB)
			fimc_hwset_sysreg_camblk_fimd0_wb(ctrl);
		else
			fimc_hwset_sysreg_camblk_fimd1_wb(ctrl);

		s3cfb_direct_ioctl(0, S3CFB_SET_WRITEBACK, 1);
	}

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
#ifndef CONFIG_VIDEO_CONFERENCE_CALL
			if (cap->fmt.width > cap->fmt.height)
				fimc_hwset_org_output_size(ctrl,
					cap->fmt.width, cap->fmt.width);
			else
				fimc_hwset_org_output_size(ctrl,
					cap->fmt.height, cap->fmt.height);

			fimc_hwset_output_size(ctrl, cap->fmt.height, cap->fmt.width);
#else
			/* Fix codes 110723 */
			fimc_hwset_org_output_size(ctrl,
				cap->fmt.width, cap->fmt.height);
			fimc_hwset_output_size(ctrl,
				cap->fmt.height, cap->fmt.width);
#endif
		} else {
			fimc_hwset_org_output_size(ctrl,
				cap->fmt.width, cap->fmt.height);
			fimc_hwset_output_size(ctrl, cap->fmt.width, cap->fmt.height);
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

		fimc_hwset_output_area_size(ctrl, fimc_camera_get_jpeg_memsize(ctrl));
		fimc_hwset_jpeg_mode(ctrl, true);
	}

	if (pdata->hw_ver >= 0x51) {
		for (i = 0; i < cap->nr_bufs; i++)
			fimc_hwset_output_address(ctrl, &cap->bufs[i], i);
	} else {
		for (i = 0; i < FIMC_PINGPONG; i++)
			fimc_add_outqueue(ctrl, i);
	}

	if (ctrl->cap->fmt.colorspace == V4L2_COLORSPACE_JPEG) {
		fimc_hwset_scaler_bypass(ctrl);
	}

	ctrl->cap->cnt = 0;
	fimc_start_capture(ctrl);
	ctrl->status = FIMC_STREAMON;
	printk(KERN_INFO "%s-- fimc%d\n", __func__, ctrl->id);

	/* if available buffer did not remained */
	return 0;
}

int fimc_streamoff_capture(void *fh)
{
	struct fimc_control *ctrl = fh;
	struct fimc_capinfo *cap = ctrl->cap;

	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);

	printk(KERN_INFO "%s fimc%d\n", __func__, ctrl->id);
	if (!ctrl->cam) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	if ((ctrl->cam->id != CAMERA_WB) && (ctrl->cam->id != CAMERA_WB_B)) {
		if (!ctrl->cam->sd) {
			fimc_err("%s: No subdevice.\n", __func__);
			return -ENODEV;
		}
	}

	ctrl->status = FIMC_READY_OFF;

	fimc_stop_capture(ctrl);

#if defined(CONFIG_MACH_PX)
#ifdef CONFIG_VIDEO_IMPROVE_STREAMOFF
	if ((ctrl->id != FIMC2) && (ctrl->cam->type == CAM_TYPE_MIPI))
		v4l2_subdev_call(ctrl->cam->sd, video, s_stream,
			STREAM_MODE_CAM_OFF);
#endif /* CONFIG_VIDEO_IMPROVE_STREAMOFF */
#else /* CONFIG_MACH_PX */
	if (get_fimc_dev()->active_camera == 1) {
		if ((ctrl->id != FIMC2) && (ctrl->cam->type == CAM_TYPE_MIPI))
			v4l2_subdev_call(ctrl->cam->sd, video, s_stream,
				STREAM_MODE_CAM_OFF);
	}
#endif

	/* wait for stop hardware */
	fimc_wait_disable_capture(ctrl);

	fimc_hwset_disable_irq(ctrl);
	if (pdata->hw_ver < 0x51)
		INIT_LIST_HEAD(&cap->inq);

	ctrl->status = FIMC_STREAMOFF;
	if (ctrl->id != FIMC2) {
		if (ctrl->cam->type == CAM_TYPE_MIPI) {
			if (ctrl->cam->id == CAMERA_CSI_C)
				s3c_csis_stop(CSI_CH_0);
			else
				s3c_csis_stop(CSI_CH_1);
		}

#if defined(CONFIG_MACH_PX)
#ifndef CONFIG_VIDEO_IMPROVE_STREAMOFF
		v4l2_subdev_call(ctrl->cam->sd, video, s_stream,
				STREAM_MODE_CAM_OFF);
#endif /* CONFIG_VIDEO_IMPROVE_STREAMOFF */
#else /* CONFIG_MACH_PX */
		if (get_fimc_dev()->active_camera == 0)
			v4l2_subdev_call(ctrl->cam->sd, video, s_stream, STREAM_MODE_CAM_OFF);
#endif
		fimc_hwset_reset(ctrl);
	} else {
		fimc_hwset_reset(ctrl);
		if (cap->fmt.priv != V4L2_PIX_FMT_MODE_HDR)
			v4l2_subdev_call(ctrl->cam->sd, video, s_stream, STREAM_MODE_MOVIE_OFF);
	}

	/* Set FIMD to write back */
	if ((ctrl->cam->id == CAMERA_WB) || (ctrl->cam->id == CAMERA_WB_B))
		s3cfb_direct_ioctl(0, S3CFB_SET_WRITEBACK, 0);

	/* disable camera power */
	/* cam power off should call in the subdev release function */
	if (ctrl->cam->reset_camera) {
		if (ctrl->cam->cam_power)
			ctrl->cam->cam_power(0);
		if (ctrl->power_status != FIMC_POWER_SUSPEND)
			ctrl->cam->initialized = 0;
	}

	printk(KERN_INFO "%s -- fimc%d\n", __func__, ctrl->id);
	return 0;
}

int fimc_qbuf_capture(void *fh, struct v4l2_buffer *b)
{
	struct fimc_control *ctrl = fh;
	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);
	struct fimc_capinfo *cap = ctrl->cap;

	if (!cap || !ctrl->cam) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	if (b->memory != V4L2_MEMORY_MMAP) {
		fimc_err("%s: invalid memory type\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&ctrl->v4l2_lock);
	if (pdata->hw_ver >= 0x51) {
		if (cap->bufs[b->index].state != VIDEOBUF_IDLE) {
			fimc_err("%s: invalid state b->index : %d\n", __func__,
					b->index);
			mutex_unlock(&ctrl->v4l2_lock);
			return -EINVAL;
		} else {
			fimc_info2("%s[%d] : b->index : %d\n", __func__, ctrl->id,
					b->index);
			fimc_hwset_output_buf_sequence(ctrl, b->index,
					FIMC_FRAMECNT_SEQ_ENABLE);
			cap->bufs[b->index].state = VIDEOBUF_QUEUED;
			if (ctrl->status == FIMC_BUFFER_STOP) {
				printk(KERN_INFO "fimc_qbuf_capture start fimc%d again\n",
					ctrl->id);
				fimc_start_capture(ctrl);
				ctrl->status = FIMC_STREAMON;
			}
		}
	} else {
		fimc_add_inqueue(ctrl, b->index);
	}

	mutex_unlock(&ctrl->v4l2_lock);

	return 0;
}

int fimc_dqbuf_capture(void *fh, struct v4l2_buffer *b)
{
	unsigned long spin_flags;
	struct fimc_control *ctrl = fh;
	struct fimc_capinfo *cap = ctrl->cap;
	struct fimc_buf_set *buf;
	size_t length = 0;
	int i, pp, ret = 0;
	phys_addr_t start, end;

	struct s3c_platform_fimc *pdata = to_fimc_plat(ctrl->dev);

	if (!cap || !ctrl->cam) {
		fimc_err("%s: No capture device.\n", __func__);
		return -ENODEV;
	}

	if (b->memory != V4L2_MEMORY_MMAP) {
		fimc_err("%s: invalid memory type\n", __func__);
		return -EINVAL;
	}

	if (pdata->hw_ver >= 0x51) {
		spin_lock_irqsave(&ctrl->outq_lock, spin_flags);

		if (list_empty(&cap->outgoing_q)) {
			fimc_info2("%s: outgoing_q is empty\n", __func__);
			spin_unlock_irqrestore(&ctrl->outq_lock, spin_flags);
			return -EAGAIN;
		} else {
			buf = list_first_entry(&cap->outgoing_q, struct fimc_buf_set,
					list);
			fimc_info2("%s[%d]: buf->id : %d\n", __func__, ctrl->id,
					buf->id);
			b->index = buf->id;
			buf->state = VIDEOBUF_IDLE;

			list_del(&buf->list);
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

	if (!cap->cacheable)
		return ret;

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
