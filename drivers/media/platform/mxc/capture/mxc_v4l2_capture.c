/*
 * Copyright 2004-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file drivers/media/video/mxc/capture/mxc_v4l2_capture.c
 *
 * @brief Mxc Video For Linux 2 driver
 *
 * @ingroup MXC_V4L2_CAPTURE
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/semaphore.h>
#include <linux/pagemap.h>
#include <linux/vmalloc.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/mxcfb.h>
#include <linux/of_device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include "v4l2-int-device.h"
#include <linux/fsl_devices.h>
#include "mxc_v4l2_capture.h"
#include "ipu_prp_sw.h"

#define init_MUTEX(sem)         sema_init(sem, 1)

static struct platform_device_id imx_v4l2_devtype[] = {
	{
		.name = "v4l2-capture-imx5",
		.driver_data = IMX5_V4L2,
	}, {
		.name = "v4l2-capture-imx6",
		.driver_data = IMX6_V4L2,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, imx_v4l2_devtype);

static const struct of_device_id mxc_v4l2_dt_ids[] = {
	{
		.compatible = "fsl,imx6q-v4l2-capture",
		.data = &imx_v4l2_devtype[IMX6_V4L2],
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, mxc_v4l2_dt_ids);

static int video_nr = -1;

/*! This data is used for the output to the display. */
#define MXC_V4L2_CAPTURE_NUM_OUTPUTS	6
#define MXC_V4L2_CAPTURE_NUM_INPUTS	2
static struct v4l2_output mxc_capture_outputs[MXC_V4L2_CAPTURE_NUM_OUTPUTS] = {
	{
	 .index = 0,
	 .name = "DISP3 BG",
	 .type = V4L2_OUTPUT_TYPE_ANALOG,
	 .audioset = 0,
	 .modulator = 0,
	 .std = V4L2_STD_UNKNOWN,
	 },
	{
	 .index = 1,
	 .name = "DISP3 BG - DI1",
	 .type = V4L2_OUTPUT_TYPE_ANALOG,
	 .audioset = 0,
	 .modulator = 0,
	 .std = V4L2_STD_UNKNOWN,
	 },
	{
	 .index = 2,
	 .name = "DISP3 FG",
	 .type = V4L2_OUTPUT_TYPE_ANALOG,
	 .audioset = 0,
	 .modulator = 0,
	 .std = V4L2_STD_UNKNOWN,
	 },
	{
	 .index = 3,
	 .name = "DISP4 BG",
	 .type = V4L2_OUTPUT_TYPE_ANALOG,
	 .audioset = 0,
	 .modulator = 0,
	 .std = V4L2_STD_UNKNOWN,
	 },
	{
	 .index = 4,
	 .name = "DISP4 BG - DI1",
	 .type = V4L2_OUTPUT_TYPE_ANALOG,
	 .audioset = 0,
	 .modulator = 0,
	 .std = V4L2_STD_UNKNOWN,
	 },
	{
	 .index = 5,
	 .name = "DISP4 FG",
	 .type = V4L2_OUTPUT_TYPE_ANALOG,
	 .audioset = 0,
	 .modulator = 0,
	 .std = V4L2_STD_UNKNOWN,
	 },
};

static struct v4l2_input mxc_capture_inputs[MXC_V4L2_CAPTURE_NUM_INPUTS] = {
	{
	 .index = 0,
	 .name = "CSI IC MEM",
	 .type = V4L2_INPUT_TYPE_CAMERA,
	 .audioset = 0,
	 .tuner = 0,
	 .std = V4L2_STD_UNKNOWN,
	 .status = 0,
	 },
	{
	 .index = 1,
	 .name = "CSI MEM",
	 .type = V4L2_INPUT_TYPE_CAMERA,
	 .audioset = 0,
	 .tuner = 0,
	 .std = V4L2_STD_UNKNOWN,
	 .status = V4L2_IN_ST_NO_POWER,
	 },
};

/*! List of TV input video formats supported. The video formats is corresponding
 * to the v4l2_id in video_fmt_t.
 * Currently, only PAL and NTSC is supported. Needs to be expanded in the
 * future.
 */
typedef enum {
	TV_NTSC = 0,		/*!< Locked on (M) NTSC video signal. */
	TV_PAL,			/*!< (B, G, H, I, N)PAL video signal. */
	TV_NOT_LOCKED,		/*!< Not locked on a signal. */
} video_fmt_idx;

/*! Number of video standards supported (including 'not locked' signal). */
#define TV_STD_MAX		(TV_NOT_LOCKED + 1)

/*! Video format structure. */
typedef struct {
	int v4l2_id;		/*!< Video for linux ID. */
	char name[16];		/*!< Name (e.g., "NTSC", "PAL", etc.) */
	u16 raw_width;		/*!< Raw width. */
	u16 raw_height;		/*!< Raw height. */
	u16 active_width;	/*!< Active width. */
	u16 active_height;	/*!< Active height. */
	u16 active_top;		/*!< Active top. */
	u16 active_left;	/*!< Active left. */
} video_fmt_t;

/*!
 * Description of video formats supported.
 *
 *  PAL: raw=720x625, active=720x576.
 *  NTSC: raw=720x525, active=720x480.
 */
static video_fmt_t video_fmts[] = {
	{			/*! NTSC */
	 .v4l2_id = V4L2_STD_NTSC,
	 .name = "NTSC",
	 .raw_width = 720,		/* SENS_FRM_WIDTH */
	 .raw_height = 525,		/* SENS_FRM_HEIGHT */
	 .active_width = 720,		/* ACT_FRM_WIDTH */
	 .active_height = 480,		/* ACT_FRM_HEIGHT */
	 .active_top = 13,
	 .active_left = 0,
	 },
	{			/*! (B, G, H, I, N) PAL */
	 .v4l2_id = V4L2_STD_PAL,
	 .name = "PAL",
	 .raw_width = 720,
	 .raw_height = 625,
	 .active_width = 720,
	 .active_height = 576,
	 .active_top = 0,
	 .active_left = 0,
	 },
	{			/*! Unlocked standard */
	 .v4l2_id = V4L2_STD_ALL,
	 .name = "Autodetect",
	 .raw_width = 720,
	 .raw_height = 625,
	 .active_width = 720,
	 .active_height = 576,
	 .active_top = 0,
	 .active_left = 0,
	 },
};

/*!* Standard index of TV. */
static video_fmt_idx video_index = TV_NOT_LOCKED;

static int mxc_v4l2_master_attach(struct v4l2_int_device *slave);
static void mxc_v4l2_master_detach(struct v4l2_int_device *slave);
static int start_preview(cam_data *cam);
static int stop_preview(cam_data *cam);

/*! Information about this driver. */
static struct v4l2_int_master mxc_v4l2_master = {
	.attach = mxc_v4l2_master_attach,
	.detach = mxc_v4l2_master_detach,
};

/***************************************************************************
 * Functions for handling Frame buffers.
 **************************************************************************/

/*!
 * Free frame buffers
 *
 * @param cam      Structure cam_data *
 *
 * @return status  0 success.
 */
static int mxc_free_frame_buf(cam_data *cam)
{
	int i;

	pr_debug("MVC: In mxc_free_frame_buf\n");

	for (i = 0; i < FRAME_NUM; i++) {
		if (cam->frame[i].vaddress != 0) {
			dma_free_coherent(0, cam->frame[i].buffer.length,
					  cam->frame[i].vaddress,
					  cam->frame[i].paddress);
			cam->frame[i].vaddress = 0;
		}
	}

	return 0;
}

/*!
 * Allocate frame buffers
 *
 * @param cam      Structure cam_data*
 * @param count    int number of buffer need to allocated
 *
 * @return status  -0 Successfully allocated a buffer, -ENOBUFS	failed.
 */
static int mxc_allocate_frame_buf(cam_data *cam, int count)
{
	int i;

	pr_debug("In MVC:mxc_allocate_frame_buf - size=%d\n",
		cam->v2f.fmt.pix.sizeimage);

	for (i = 0; i < count; i++) {
		cam->frame[i].vaddress =
		    dma_alloc_coherent(0,
				       PAGE_ALIGN(cam->v2f.fmt.pix.sizeimage),
				       &cam->frame[i].paddress,
				       GFP_DMA | GFP_KERNEL);
		if (cam->frame[i].vaddress == 0) {
			pr_err("ERROR: v4l2 capture: "
				"mxc_allocate_frame_buf failed.\n");
			mxc_free_frame_buf(cam);
			return -ENOBUFS;
		}
		cam->frame[i].buffer.index = i;
		cam->frame[i].buffer.flags = V4L2_BUF_FLAG_MAPPED;
		cam->frame[i].buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cam->frame[i].buffer.length =
		    PAGE_ALIGN(cam->v2f.fmt.pix.sizeimage);
		cam->frame[i].buffer.memory = V4L2_MEMORY_MMAP;
		cam->frame[i].buffer.m.offset = cam->frame[i].paddress;
		cam->frame[i].index = i;
	}

	return 0;
}

/*!
 * Free frame buffers status
 *
 * @param cam    Structure cam_data *
 *
 * @return none
 */
static void mxc_free_frames(cam_data *cam)
{
	int i;

	pr_debug("In MVC:mxc_free_frames\n");

	for (i = 0; i < FRAME_NUM; i++)
		cam->frame[i].buffer.flags = V4L2_BUF_FLAG_MAPPED;

	cam->enc_counter = 0;
	INIT_LIST_HEAD(&cam->ready_q);
	INIT_LIST_HEAD(&cam->working_q);
	INIT_LIST_HEAD(&cam->done_q);
}

/*!
 * Return the buffer status
 *
 * @param cam	   Structure cam_data *
 * @param buf	   Structure v4l2_buffer *
 *
 * @return status  0 success, EINVAL failed.
 */
static int mxc_v4l2_buffer_status(cam_data *cam, struct v4l2_buffer *buf)
{
	pr_debug("In MVC:mxc_v4l2_buffer_status\n");

	if (buf->index < 0 || buf->index >= FRAME_NUM) {
		pr_err("ERROR: v4l2 capture: mxc_v4l2_buffer_status buffers "
		       "not allocated\n");
		return -EINVAL;
	}

	memcpy(buf, &(cam->frame[buf->index].buffer), sizeof(*buf));
	return 0;
}

static int mxc_v4l2_release_bufs(cam_data *cam)
{
	pr_debug("In MVC:mxc_v4l2_release_bufs\n");
	return 0;
}

static int mxc_v4l2_prepare_bufs(cam_data *cam, struct v4l2_buffer *buf)
{
	pr_debug("In MVC:mxc_v4l2_prepare_bufs\n");

	if (buf->index < 0 || buf->index >= FRAME_NUM || buf->length <
			PAGE_ALIGN(cam->v2f.fmt.pix.sizeimage)) {
		pr_err("ERROR: v4l2 capture: mxc_v4l2_prepare_bufs buffers "
			"not allocated,index=%d, length=%d\n", buf->index,
			buf->length);
		return -EINVAL;
	}

	cam->frame[buf->index].buffer.index = buf->index;
	cam->frame[buf->index].buffer.flags = V4L2_BUF_FLAG_MAPPED;
	cam->frame[buf->index].buffer.length = buf->length;
	cam->frame[buf->index].buffer.m.offset = cam->frame[buf->index].paddress
		= buf->m.offset;
	cam->frame[buf->index].buffer.type = buf->type;
	cam->frame[buf->index].buffer.memory = V4L2_MEMORY_USERPTR;
	cam->frame[buf->index].index = buf->index;

	return 0;
}

/***************************************************************************
 * Functions for handling the video stream.
 **************************************************************************/

/*!
 * Indicates whether the palette is supported.
 *
 * @param palette V4L2_PIX_FMT_RGB565, V4L2_PIX_FMT_BGR24 or V4L2_PIX_FMT_BGR32
 *
 * @return 0 if failed
 */
static inline int valid_mode(u32 palette)
{
	return ((palette == V4L2_PIX_FMT_RGB565) ||
		(palette == V4L2_PIX_FMT_BGR24) ||
		(palette == V4L2_PIX_FMT_RGB24) ||
		(palette == V4L2_PIX_FMT_BGR32) ||
		(palette == V4L2_PIX_FMT_RGB32) ||
		(palette == V4L2_PIX_FMT_YUV422P) ||
		(palette == V4L2_PIX_FMT_UYVY) ||
		(palette == V4L2_PIX_FMT_YUYV) ||
		(palette == V4L2_PIX_FMT_YUV420) ||
		(palette == V4L2_PIX_FMT_YVU420) ||
		(palette == V4L2_PIX_FMT_NV12));
}

/*!
 * Start the encoder job
 *
 * @param cam      structure cam_data *
 *
 * @return status  0 Success
 */
static int mxc_streamon(cam_data *cam)
{
	struct mxc_v4l_frame *frame;
	unsigned long lock_flags;
	int err = 0;

	pr_debug("In MVC:mxc_streamon\n");

	if (NULL == cam) {
		pr_err("ERROR! cam parameter is NULL\n");
		return -1;
	}

	if (cam->capture_on) {
		pr_err("ERROR: v4l2 capture: Capture stream has been turned "
		       " on\n");
		return -1;
	}

	if (list_empty(&cam->ready_q)) {
		pr_err("ERROR: v4l2 capture: mxc_streamon buffer has not been "
			"queued yet\n");
		return -EINVAL;
	}
	if (cam->enc_update_eba &&
		cam->ready_q.prev == cam->ready_q.next) {
		pr_err("ERROR: v4l2 capture: mxc_streamon buffer need "
		       "ping pong at least two buffers\n");
		return -EINVAL;
	}

	cam->capture_pid = current->pid;

	if (cam->overlay_on == true)
		stop_preview(cam);

	if (cam->enc_enable) {
		err = cam->enc_enable(cam);
		if (err != 0)
			return err;
	}

	spin_lock_irqsave(&cam->queue_int_lock, lock_flags);
	cam->ping_pong_csi = 0;
	cam->local_buf_num = 0;
	if (cam->enc_update_eba) {
		frame =
		    list_entry(cam->ready_q.next, struct mxc_v4l_frame, queue);
		list_del(cam->ready_q.next);
		list_add_tail(&frame->queue, &cam->working_q);
		frame->ipu_buf_num = cam->ping_pong_csi;
		err = cam->enc_update_eba(cam, frame->buffer.m.offset);

		frame =
		    list_entry(cam->ready_q.next, struct mxc_v4l_frame, queue);
		list_del(cam->ready_q.next);
		list_add_tail(&frame->queue, &cam->working_q);
		frame->ipu_buf_num = cam->ping_pong_csi;
		err |= cam->enc_update_eba(cam, frame->buffer.m.offset);
		spin_unlock_irqrestore(&cam->queue_int_lock, lock_flags);
	} else {
		spin_unlock_irqrestore(&cam->queue_int_lock, lock_flags);
		return -EINVAL;
	}

	if (cam->overlay_on == true)
		start_preview(cam);

	if (cam->enc_enable_csi) {
		err = cam->enc_enable_csi(cam);
		if (err != 0)
			return err;
	}

	cam->capture_on = true;

	return err;
}

/*!
 * Shut down the encoder job
 *
 * @param cam      structure cam_data *
 *
 * @return status  0 Success
 */
static int mxc_streamoff(cam_data *cam)
{
	int err = 0;

	pr_debug("In MVC:mxc_streamoff\n");

	if (cam->capture_on == false)
		return 0;

	/* For both CSI--MEM and CSI--IC--MEM
	 * 1. wait for idmac eof
	 * 2. disable csi first
	 * 3. disable idmac
	 * 4. disable smfc (CSI--MEM channel)
	 */
	if (mxc_capture_inputs[cam->current_input].name != NULL) {
		if (cam->enc_disable_csi) {
			err = cam->enc_disable_csi(cam);
			if (err != 0)
				return err;
		}
		if (cam->enc_disable) {
			err = cam->enc_disable(cam);
			if (err != 0)
				return err;
		}
	}

	mxc_free_frames(cam);
	mxc_capture_inputs[cam->current_input].status |= V4L2_IN_ST_NO_POWER;
	cam->capture_on = false;
	return err;
}

/*!
 * Valid and adjust the overlay window size, position
 *
 * @param cam      structure cam_data *
 * @param win      struct v4l2_window  *
 *
 * @return 0
 */
static int verify_preview(cam_data *cam, struct v4l2_window *win)
{
	int i = 0, width_bound = 0, height_bound = 0;
	int *width, *height;
	unsigned int ipu_ch = CHAN_NONE;
	struct fb_info *bg_fbi = NULL, *fbi = NULL;
	bool foregound_fb = false;
	mm_segment_t old_fs;

	pr_debug("In MVC: verify_preview\n");

	do {
		fbi = (struct fb_info *)registered_fb[i];
		if (fbi == NULL) {
			pr_err("ERROR: verify_preview frame buffer NULL.\n");
			return -1;
		}

		/* Which DI supports 2 layers? */
		if (((strncmp(fbi->fix.id, "DISP3 BG", 8) == 0) &&
					(cam->output < 3)) ||
		    ((strncmp(fbi->fix.id, "DISP4 BG", 8) == 0) &&
					(cam->output >= 3))) {
			if (fbi->fbops->fb_ioctl) {
				old_fs = get_fs();
				set_fs(KERNEL_DS);
				fbi->fbops->fb_ioctl(fbi, MXCFB_GET_FB_IPU_CHAN,
						(unsigned long)&ipu_ch);
				set_fs(old_fs);
			}
			if (ipu_ch == MEM_BG_SYNC) {
				bg_fbi = fbi;
				pr_debug("Found background frame buffer.\n");
			}
		}

		/* Found the frame buffer to preview on. */
		if (strcmp(fbi->fix.id,
			    mxc_capture_outputs[cam->output].name) == 0) {
			if (((strcmp(fbi->fix.id, "DISP3 FG") == 0) &&
						(cam->output < 3)) ||
			    ((strcmp(fbi->fix.id, "DISP4 FG") == 0) &&
						(cam->output >= 3)))
				foregound_fb = true;

			cam->overlay_fb = fbi;
			break;
		}
	} while (++i < FB_MAX);

	if (foregound_fb) {
		width_bound = bg_fbi->var.xres;
		height_bound = bg_fbi->var.yres;

		if (win->w.width + win->w.left > bg_fbi->var.xres ||
		    win->w.height + win->w.top > bg_fbi->var.yres) {
			pr_err("ERROR: FG window position exceeds.\n");
			return -1;
		}
	} else {
		/* 4 bytes alignment for BG */
		width_bound = cam->overlay_fb->var.xres;
		height_bound = cam->overlay_fb->var.yres;

		if (cam->overlay_fb->var.bits_per_pixel == 24)
			win->w.left -= win->w.left % 4;
		else if (cam->overlay_fb->var.bits_per_pixel == 16)
			win->w.left -= win->w.left % 2;

		if (win->w.width + win->w.left > cam->overlay_fb->var.xres)
			win->w.width = cam->overlay_fb->var.xres - win->w.left;
		if (win->w.height + win->w.top > cam->overlay_fb->var.yres)
			win->w.height = cam->overlay_fb->var.yres - win->w.top;
	}

	/* stride line limitation */
	win->w.height -= win->w.height % 8;
	win->w.width -= win->w.width % 8;

	if (cam->rotation >= IPU_ROTATE_90_RIGHT) {
		height = &win->w.width;
		width = &win->w.height;
	} else {
		width = &win->w.width;
		height = &win->w.height;
	}

	if (*width == 0 || *height == 0) {
		pr_err("ERROR: v4l2 capture: width or height"
			" too small.\n");
		return -EINVAL;
	}

	if ((cam->crop_bounds.width / *width > 8) ||
	    ((cam->crop_bounds.width / *width == 8) &&
	     (cam->crop_bounds.width % *width))) {
		*width = cam->crop_bounds.width / 8;
		if (*width % 8)
			*width += 8 - *width % 8;
		if (*width + win->w.left > width_bound) {
			pr_err("ERROR: v4l2 capture: width exceeds "
				"resize limit.\n");
			return -1;
		}
		pr_err("ERROR: v4l2 capture: width exceeds limit. "
			"Resize to %d.\n",
			*width);
	}

	if ((cam->crop_bounds.height / *height > 8) ||
	    ((cam->crop_bounds.height / *height == 8) &&
	     (cam->crop_bounds.height % *height))) {
		*height = cam->crop_bounds.height / 8;
		if (*height % 8)
			*height += 8 - *height % 8;
		if (*height + win->w.top > height_bound) {
			pr_err("ERROR: v4l2 capture: height exceeds "
				"resize limit.\n");
			return -1;
		}
		pr_err("ERROR: v4l2 capture: height exceeds limit "
			"resize to %d.\n",
			*height);
	}

	return 0;
}

/*!
 * start the viewfinder job
 *
 * @param cam      structure cam_data *
 *
 * @return status  0 Success
 */
static int start_preview(cam_data *cam)
{
	int err = 0;

	pr_debug("MVC: start_preview\n");

	if (cam->v4l2_fb.flags == V4L2_FBUF_FLAG_OVERLAY)
	#ifdef CONFIG_MXC_IPU_PRP_VF_SDC
		err = prp_vf_sdc_select(cam);
	#else
		err = foreground_sdc_select(cam);
	#endif
	else if (cam->v4l2_fb.flags == V4L2_FBUF_FLAG_PRIMARY)
	#ifdef CONFIG_MXC_IPU_PRP_VF_SDC
		err = prp_vf_sdc_select_bg(cam);
	#else
		err = bg_overlay_sdc_select(cam);
	#endif
	if (err != 0)
		return err;

	if (cam->vf_start_sdc) {
		err = cam->vf_start_sdc(cam);
		if (err != 0)
			return err;
	}

	if (cam->vf_enable_csi)
		err = cam->vf_enable_csi(cam);

	pr_debug("End of %s: v2f pix widthxheight %d x %d\n",
		 __func__,
		 cam->v2f.fmt.pix.width, cam->v2f.fmt.pix.height);
	pr_debug("End of %s: crop_bounds widthxheight %d x %d\n",
		 __func__,
		 cam->crop_bounds.width, cam->crop_bounds.height);
	pr_debug("End of %s: crop_defrect widthxheight %d x %d\n",
		 __func__,
		 cam->crop_defrect.width, cam->crop_defrect.height);
	pr_debug("End of %s: crop_current widthxheight %d x %d\n",
		 __func__,
		 cam->crop_current.width, cam->crop_current.height);

	return err;
}

/*!
 * shut down the viewfinder job
 *
 * @param cam      structure cam_data *
 *
 * @return status  0 Success
 */
static int stop_preview(cam_data *cam)
{
	int err = 0;

	if (cam->vf_disable_csi) {
		err = cam->vf_disable_csi(cam);
		if (err != 0)
			return err;
	}

	if (cam->vf_stop_sdc) {
		err = cam->vf_stop_sdc(cam);
		if (err != 0)
			return err;
	}

	if (cam->v4l2_fb.flags == V4L2_FBUF_FLAG_OVERLAY)
	#ifdef CONFIG_MXC_IPU_PRP_VF_SDC
		err = prp_vf_sdc_deselect(cam);
	#else
		err = foreground_sdc_deselect(cam);
	#endif
	else if (cam->v4l2_fb.flags == V4L2_FBUF_FLAG_PRIMARY)
	#ifdef CONFIG_MXC_IPU_PRP_VF_SDC
		err = prp_vf_sdc_deselect_bg(cam);
	#else
		err = bg_overlay_sdc_deselect(cam);
	#endif

	return err;
}

/***************************************************************************
 * VIDIOC Functions.
 **************************************************************************/

/*!
 * V4L2 - mxc_v4l2_g_fmt function
 *
 * @param cam         structure cam_data *
 *
 * @param f           structure v4l2_format *
 *
 * @return  status    0 success, EINVAL failed
 */
static int mxc_v4l2_g_fmt(cam_data *cam, struct v4l2_format *f)
{
	int retval = 0;

	pr_debug("In MVC: mxc_v4l2_g_fmt type=%d\n", f->type);

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		pr_debug("   type is V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		f->fmt.pix = cam->v2f.fmt.pix;
		break;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		pr_debug("   type is V4L2_BUF_TYPE_VIDEO_OVERLAY\n");
		f->fmt.win = cam->win;
		break;
	default:
		pr_debug("   type is invalid\n");
		retval = -EINVAL;
	}

	pr_debug("End of %s: v2f pix widthxheight %d x %d\n",
		 __func__,
		 cam->v2f.fmt.pix.width, cam->v2f.fmt.pix.height);
	pr_debug("End of %s: crop_bounds widthxheight %d x %d\n",
		 __func__,
		 cam->crop_bounds.width, cam->crop_bounds.height);
	pr_debug("End of %s: crop_defrect widthxheight %d x %d\n",
		 __func__,
		 cam->crop_defrect.width, cam->crop_defrect.height);
	pr_debug("End of %s: crop_current widthxheight %d x %d\n",
		 __func__,
		 cam->crop_current.width, cam->crop_current.height);

	return retval;
}

/*!
 * V4L2 - mxc_v4l2_s_fmt function
 *
 * @param cam         structure cam_data *
 *
 * @param f           structure v4l2_format *
 *
 * @return  status    0 success, EINVAL failed
 */
static int mxc_v4l2_s_fmt(cam_data *cam, struct v4l2_format *f)
{
	int retval = 0;
	int size = 0;
	int bytesperline = 0;
	int *width, *height;

	pr_debug("In MVC: mxc_v4l2_s_fmt\n");

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		pr_debug("   type=V4L2_BUF_TYPE_VIDEO_CAPTURE\n");
		if (!valid_mode(f->fmt.pix.pixelformat)) {
			pr_err("ERROR: v4l2 capture: mxc_v4l2_s_fmt: format "
			       "not supported\n");
			return -EINVAL;
		}

		/*
		 * Force the capture window resolution to be crop bounds
		 * for CSI MEM input mode.
		 */
		if (strcmp(mxc_capture_inputs[cam->current_input].name,
			   "CSI MEM") == 0) {
			f->fmt.pix.width = cam->crop_current.width;
			f->fmt.pix.height = cam->crop_current.height;
		}

		if (cam->rotation >= IPU_ROTATE_90_RIGHT) {
			height = &f->fmt.pix.width;
			width = &f->fmt.pix.height;
		} else {
			width = &f->fmt.pix.width;
			height = &f->fmt.pix.height;
		}

		/* stride line limitation */
		*width -= *width % 8;
		*height -= *height % 8;

		if (*width == 0 || *height == 0) {
			pr_err("ERROR: v4l2 capture: width or height"
				" too small.\n");
			return -EINVAL;
		}

		if ((cam->crop_current.width / *width > 8) ||
		    ((cam->crop_current.width / *width == 8) &&
		     (cam->crop_current.width % *width))) {
			*width = cam->crop_current.width / 8;
			if (*width % 8)
				*width += 8 - *width % 8;
			pr_err("ERROR: v4l2 capture: width exceeds limit "
				"resize to %d.\n",
			       *width);
		}

		if ((cam->crop_current.height / *height > 8) ||
		    ((cam->crop_current.height / *height == 8) &&
		     (cam->crop_current.height % *height))) {
			*height = cam->crop_current.height / 8;
			if (*height % 8)
				*height += 8 - *height % 8;
			pr_err("ERROR: v4l2 capture: height exceeds limit "
			       "resize to %d.\n",
			       *height);
		}

		switch (f->fmt.pix.pixelformat) {
		case V4L2_PIX_FMT_RGB565:
			size = f->fmt.pix.width * f->fmt.pix.height * 2;
			bytesperline = f->fmt.pix.width * 2;
			break;
		case V4L2_PIX_FMT_BGR24:
			size = f->fmt.pix.width * f->fmt.pix.height * 3;
			bytesperline = f->fmt.pix.width * 3;
			break;
		case V4L2_PIX_FMT_RGB24:
			size = f->fmt.pix.width * f->fmt.pix.height * 3;
			bytesperline = f->fmt.pix.width * 3;
			break;
		case V4L2_PIX_FMT_BGR32:
			size = f->fmt.pix.width * f->fmt.pix.height * 4;
			bytesperline = f->fmt.pix.width * 4;
			break;
		case V4L2_PIX_FMT_RGB32:
			size = f->fmt.pix.width * f->fmt.pix.height * 4;
			bytesperline = f->fmt.pix.width * 4;
			break;
		case V4L2_PIX_FMT_YUV422P:
			size = f->fmt.pix.width * f->fmt.pix.height * 2;
			bytesperline = f->fmt.pix.width;
			break;
		case V4L2_PIX_FMT_UYVY:
		case V4L2_PIX_FMT_YUYV:
			size = f->fmt.pix.width * f->fmt.pix.height * 2;
			bytesperline = f->fmt.pix.width * 2;
			break;
		case V4L2_PIX_FMT_YUV420:
		case V4L2_PIX_FMT_YVU420:
			size = f->fmt.pix.width * f->fmt.pix.height * 3 / 2;
			bytesperline = f->fmt.pix.width;
			break;
		case V4L2_PIX_FMT_NV12:
			size = f->fmt.pix.width * f->fmt.pix.height * 3 / 2;
			bytesperline = f->fmt.pix.width;
			break;
		default:
			break;
		}

		if (f->fmt.pix.bytesperline < bytesperline)
			f->fmt.pix.bytesperline = bytesperline;
		else
			bytesperline = f->fmt.pix.bytesperline;

		if (f->fmt.pix.sizeimage < size)
			f->fmt.pix.sizeimage = size;
		else
			size = f->fmt.pix.sizeimage;

		cam->v2f.fmt.pix = f->fmt.pix;
		break;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		pr_debug("   type=V4L2_BUF_TYPE_VIDEO_OVERLAY\n");
		retval = verify_preview(cam, &f->fmt.win);
		cam->win = f->fmt.win;
		break;
	default:
		retval = -EINVAL;
	}

	pr_debug("End of %s: v2f pix widthxheight %d x %d\n",
		 __func__,
		 cam->v2f.fmt.pix.width, cam->v2f.fmt.pix.height);
	pr_debug("End of %s: crop_bounds widthxheight %d x %d\n",
		 __func__,
		 cam->crop_bounds.width, cam->crop_bounds.height);
	pr_debug("End of %s: crop_defrect widthxheight %d x %d\n",
		 __func__,
		 cam->crop_defrect.width, cam->crop_defrect.height);
	pr_debug("End of %s: crop_current widthxheight %d x %d\n",
		 __func__,
		 cam->crop_current.width, cam->crop_current.height);

	return retval;
}

/*!
 * get control param
 *
 * @param cam         structure cam_data *
 *
 * @param c           structure v4l2_control *
 *
 * @return  status    0 success, EINVAL failed
 */
static int mxc_v4l2_g_ctrl(cam_data *cam, struct v4l2_control *c)
{
	int status = 0;

	pr_debug("In MVC:mxc_v4l2_g_ctrl\n");

	/* probably don't need to store the values that can be retrieved,
	 * locally, but they are for now. */
	switch (c->id) {
	case V4L2_CID_HFLIP:
		/* This is handled in the ipu. */
		if (cam->rotation == IPU_ROTATE_HORIZ_FLIP)
			c->value = 1;
		break;
	case V4L2_CID_VFLIP:
		/* This is handled in the ipu. */
		if (cam->rotation == IPU_ROTATE_VERT_FLIP)
			c->value = 1;
		break;
	case V4L2_CID_MXC_ROT:
		/* This is handled in the ipu. */
		c->value = cam->rotation;
		break;
	case V4L2_CID_BRIGHTNESS:
		if (cam->sensor) {
			c->value = cam->bright;
			status = vidioc_int_g_ctrl(cam->sensor, c);
			cam->bright = c->value;
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			status = -ENODEV;
		}
		break;
	case V4L2_CID_HUE:
		if (cam->sensor) {
			c->value = cam->hue;
			status = vidioc_int_g_ctrl(cam->sensor, c);
			cam->hue = c->value;
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			status = -ENODEV;
		}
		break;
	case V4L2_CID_CONTRAST:
		if (cam->sensor) {
			c->value = cam->contrast;
			status = vidioc_int_g_ctrl(cam->sensor, c);
			cam->contrast = c->value;
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			status = -ENODEV;
		}
		break;
	case V4L2_CID_SATURATION:
		if (cam->sensor) {
			c->value = cam->saturation;
			status = vidioc_int_g_ctrl(cam->sensor, c);
			cam->saturation = c->value;
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			status = -ENODEV;
		}
		break;
	case V4L2_CID_RED_BALANCE:
		if (cam->sensor) {
			c->value = cam->red;
			status = vidioc_int_g_ctrl(cam->sensor, c);
			cam->red = c->value;
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			status = -ENODEV;
		}
		break;
	case V4L2_CID_BLUE_BALANCE:
		if (cam->sensor) {
			c->value = cam->blue;
			status = vidioc_int_g_ctrl(cam->sensor, c);
			cam->blue = c->value;
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			status = -ENODEV;
		}
		break;
	case V4L2_CID_BLACK_LEVEL:
		if (cam->sensor) {
			c->value = cam->ae_mode;
			status = vidioc_int_g_ctrl(cam->sensor, c);
			cam->ae_mode = c->value;
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			status = -ENODEV;
		}
		break;
	default:
		pr_err("ERROR: v4l2 capture: unsupported ioctrl!\n");
	}

	return status;
}

/*!
 * V4L2 - set_control function
 *          V4L2_CID_PRIVATE_BASE is the extention for IPU preprocessing.
 *          0 for normal operation
 *          1 for vertical flip
 *          2 for horizontal flip
 *          3 for horizontal and vertical flip
 *          4 for 90 degree rotation
 * @param cam         structure cam_data *
 *
 * @param c           structure v4l2_control *
 *
 * @return  status    0 success, EINVAL failed
 */
static int mxc_v4l2_s_ctrl(cam_data *cam, struct v4l2_control *c)
{
	int i, ret = 0;
	int tmp_rotation = IPU_ROTATE_NONE;
	struct sensor_data *sensor_data;

	pr_debug("In MVC:mxc_v4l2_s_ctrl\n");

	switch (c->id) {
	case V4L2_CID_HFLIP:
		/* This is done by the IPU */
		if (c->value == 1) {
			if ((cam->rotation != IPU_ROTATE_VERT_FLIP) &&
			    (cam->rotation != IPU_ROTATE_180))
				cam->rotation = IPU_ROTATE_HORIZ_FLIP;
			else
				cam->rotation = IPU_ROTATE_180;
		} else {
			if (cam->rotation == IPU_ROTATE_HORIZ_FLIP)
				cam->rotation = IPU_ROTATE_NONE;
			if (cam->rotation == IPU_ROTATE_180)
				cam->rotation = IPU_ROTATE_VERT_FLIP;
		}
		break;
	case V4L2_CID_VFLIP:
		/* This is done by the IPU */
		if (c->value == 1) {
			if ((cam->rotation != IPU_ROTATE_HORIZ_FLIP) &&
			    (cam->rotation != IPU_ROTATE_180))
				cam->rotation = IPU_ROTATE_VERT_FLIP;
			else
				cam->rotation = IPU_ROTATE_180;
		} else {
			if (cam->rotation == IPU_ROTATE_VERT_FLIP)
				cam->rotation = IPU_ROTATE_NONE;
			if (cam->rotation == IPU_ROTATE_180)
				cam->rotation = IPU_ROTATE_HORIZ_FLIP;
		}
		break;
	case V4L2_CID_MXC_ROT:
	case V4L2_CID_MXC_VF_ROT:
		/* This is done by the IPU */
		switch (c->value) {
		case V4L2_MXC_ROTATE_NONE:
			tmp_rotation = IPU_ROTATE_NONE;
			break;
		case V4L2_MXC_ROTATE_VERT_FLIP:
			tmp_rotation = IPU_ROTATE_VERT_FLIP;
			break;
		case V4L2_MXC_ROTATE_HORIZ_FLIP:
			tmp_rotation = IPU_ROTATE_HORIZ_FLIP;
			break;
		case V4L2_MXC_ROTATE_180:
			tmp_rotation = IPU_ROTATE_180;
			break;
		case V4L2_MXC_ROTATE_90_RIGHT:
			tmp_rotation = IPU_ROTATE_90_RIGHT;
			break;
		case V4L2_MXC_ROTATE_90_RIGHT_VFLIP:
			tmp_rotation = IPU_ROTATE_90_RIGHT_VFLIP;
			break;
		case V4L2_MXC_ROTATE_90_RIGHT_HFLIP:
			tmp_rotation = IPU_ROTATE_90_RIGHT_HFLIP;
			break;
		case V4L2_MXC_ROTATE_90_LEFT:
			tmp_rotation = IPU_ROTATE_90_LEFT;
			break;
		default:
			ret = -EINVAL;
		}
		#ifdef CONFIG_MXC_IPU_PRP_VF_SDC
		if (c->id == V4L2_CID_MXC_VF_ROT)
			cam->vf_rotation = tmp_rotation;
		else
			cam->rotation = tmp_rotation;
		#else
			cam->rotation = tmp_rotation;
		#endif

		break;
	case V4L2_CID_HUE:
		if (cam->sensor) {
			cam->hue = c->value;
			ret = vidioc_int_s_ctrl(cam->sensor, c);
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			ret = -ENODEV;
		}
		break;
	case V4L2_CID_CONTRAST:
		if (cam->sensor) {
			cam->contrast = c->value;
			ret = vidioc_int_s_ctrl(cam->sensor, c);
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			ret = -ENODEV;
		}
		break;
	case V4L2_CID_BRIGHTNESS:
		if (cam->sensor) {
			cam->bright = c->value;
			ret = vidioc_int_s_ctrl(cam->sensor, c);
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			ret = -ENODEV;
		}
		break;
	case V4L2_CID_SATURATION:
		if (cam->sensor) {
			cam->saturation = c->value;
			ret = vidioc_int_s_ctrl(cam->sensor, c);
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			ret = -ENODEV;
		}
		break;
	case V4L2_CID_RED_BALANCE:
		if (cam->sensor) {
			cam->red = c->value;
			ret = vidioc_int_s_ctrl(cam->sensor, c);
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			ret = -ENODEV;
		}
		break;
	case V4L2_CID_BLUE_BALANCE:
		if (cam->sensor) {
			cam->blue = c->value;
			ret = vidioc_int_s_ctrl(cam->sensor, c);
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			ret = -ENODEV;
		}
		break;
	case V4L2_CID_EXPOSURE:
		if (cam->sensor) {
			cam->ae_mode = c->value;
			ret = vidioc_int_s_ctrl(cam->sensor, c);
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			ret = -ENODEV;
		}
		break;
	case V4L2_CID_MXC_FLASH:
#ifdef CONFIG_MXC_IPU_V1
		ipu_csi_flash_strobe(true);
#endif
		break;
	case V4L2_CID_MXC_SWITCH_CAM:
		if (cam->sensor == cam->all_sensors[c->value])
			break;

		/* power down other cameraes before enable new one */
		for (i = 0; i < cam->sensor_index; i++) {
			if (i != c->value) {
				vidioc_int_dev_exit(cam->all_sensors[i]);
				vidioc_int_s_power(cam->all_sensors[i], 0);
				if (cam->mclk_on[cam->mclk_source]) {
					ipu_csi_enable_mclk_if(cam->ipu,
							CSI_MCLK_I2C,
							cam->mclk_source,
							false, false);
					cam->mclk_on[cam->mclk_source] =
								false;
				}
			}
		}
		sensor_data = cam->all_sensors[c->value]->priv;
		if (sensor_data->io_init)
			sensor_data->io_init();
		cam->sensor = cam->all_sensors[c->value];
		cam->mclk_source = sensor_data->mclk_source;
		ipu_csi_enable_mclk_if(cam->ipu, CSI_MCLK_I2C,
				       cam->mclk_source, true, true);
		cam->mclk_on[cam->mclk_source] = true;
		vidioc_int_s_power(cam->sensor, 1);
		vidioc_int_dev_init(cam->sensor);
		break;
	default:
		pr_debug("   default case\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

/*!
 * V4L2 - mxc_v4l2_s_param function
 * Allows setting of capturemode and frame rate.
 *
 * @param cam         structure cam_data *
 * @param parm        structure v4l2_streamparm *
 *
 * @return  status    0 success, EINVAL failed
 */
static int mxc_v4l2_s_param(cam_data *cam, struct v4l2_streamparm *parm)
{
	struct v4l2_ifparm ifparm;
	struct v4l2_format cam_fmt;
	struct v4l2_streamparm currentparm;
	ipu_csi_signal_cfg_t csi_param;
	u32 current_fps, parm_fps;
	int err = 0;

	pr_debug("In mxc_v4l2_s_param\n");

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		pr_err(KERN_ERR "mxc_v4l2_s_param invalid type\n");
		return -EINVAL;
	}

	/* Stop the viewfinder */
	if (cam->overlay_on == true)
		stop_preview(cam);

	currentparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	/* First check that this device can support the changes requested. */
	err = vidioc_int_g_parm(cam->sensor, &currentparm);
	if (err) {
		pr_err("%s: vidioc_int_g_parm returned an error %d\n",
			__func__, err);
		goto exit;
	}

	current_fps = currentparm.parm.capture.timeperframe.denominator
			/ currentparm.parm.capture.timeperframe.numerator;
	parm_fps = parm->parm.capture.timeperframe.denominator
			/ parm->parm.capture.timeperframe.numerator;

	pr_debug("   Current capabilities are %x\n",
			currentparm.parm.capture.capability);
	pr_debug("   Current capturemode is %d  change to %d\n",
			currentparm.parm.capture.capturemode,
			parm->parm.capture.capturemode);
	pr_debug("   Current framerate is %d  change to %d\n",
			current_fps, parm_fps);

	/* This will change any camera settings needed. */
	err = vidioc_int_s_parm(cam->sensor, parm);
	if (err) {
		pr_err("%s: vidioc_int_s_parm returned an error %d\n",
			__func__, err);
		goto exit;
	}

	/* If resolution changed, need to re-program the CSI */
	/* Get new values. */
	vidioc_int_g_ifparm(cam->sensor, &ifparm);

	csi_param.data_width = 0;
	csi_param.clk_mode = 0;
	csi_param.ext_vsync = 0;
	csi_param.Vsync_pol = 0;
	csi_param.Hsync_pol = 0;
	csi_param.pixclk_pol = 0;
	csi_param.data_pol = 0;
	csi_param.sens_clksrc = 0;
	csi_param.pack_tight = 0;
	csi_param.force_eof = 0;
	csi_param.data_en_pol = 0;
	csi_param.data_fmt = 0;
	csi_param.csi = cam->csi;
	csi_param.mclk = 0;

	pr_debug("   clock_curr=mclk=%d\n", ifparm.u.bt656.clock_curr);
	if (ifparm.u.bt656.clock_curr == 0)
		csi_param.clk_mode = IPU_CSI_CLK_MODE_CCIR656_INTERLACED;
	else
		csi_param.clk_mode = IPU_CSI_CLK_MODE_GATED_CLK;

	csi_param.pixclk_pol = ifparm.u.bt656.latch_clk_inv;

	if (ifparm.u.bt656.mode == V4L2_IF_TYPE_BT656_MODE_NOBT_8BIT) {
		csi_param.data_width = IPU_CSI_DATA_WIDTH_8;
	} else if (ifparm.u.bt656.mode
				== V4L2_IF_TYPE_BT656_MODE_NOBT_10BIT) {
		csi_param.data_width = IPU_CSI_DATA_WIDTH_10;
	} else {
		csi_param.data_width = IPU_CSI_DATA_WIDTH_8;
	}

	csi_param.Vsync_pol = ifparm.u.bt656.nobt_vs_inv;
	csi_param.Hsync_pol = ifparm.u.bt656.nobt_hs_inv;
	csi_param.ext_vsync = ifparm.u.bt656.bt_sync_correct;

	/* if the capturemode changed, the size bounds will have changed. */
	cam_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vidioc_int_g_fmt_cap(cam->sensor, &cam_fmt);
	pr_debug("   g_fmt_cap returns widthxheight of input as %d x %d\n",
			cam_fmt.fmt.pix.width, cam_fmt.fmt.pix.height);

	csi_param.data_fmt = cam_fmt.fmt.pix.pixelformat;

	cam->crop_bounds.top = cam->crop_bounds.left = 0;
	cam->crop_bounds.width = cam_fmt.fmt.pix.width;
	cam->crop_bounds.height = cam_fmt.fmt.pix.height;

	/*
	 * Set the default current cropped resolution to be the same with
	 * the cropping boundary(except for tvin module).
	 */
	if (cam->device_type != 1) {
		cam->crop_current.width = cam->crop_bounds.width;
		cam->crop_current.height = cam->crop_bounds.height;
	}

	/* This essentially loses the data at the left and bottom of the image
	 * giving a digital zoom image, if crop_current is less than the full
	 * size of the image. */
	ipu_csi_set_window_size(cam->ipu, cam->crop_current.width,
				cam->crop_current.height, cam->csi);
	ipu_csi_set_window_pos(cam->ipu, cam->crop_current.left,
			       cam->crop_current.top,
			       cam->csi);
	ipu_csi_init_interface(cam->ipu, cam->crop_bounds.width,
			       cam->crop_bounds.height,
			       cam_fmt.fmt.pix.pixelformat, csi_param);


exit:
	if (cam->overlay_on == true)
		start_preview(cam);

	return err;
}

/*!
 * V4L2 - mxc_v4l2_s_std function
 *
 * Sets the TV standard to be used.
 *
 * @param cam	      structure cam_data *
 * @param parm	      structure v4l2_streamparm *
 *
 * @return  status    0 success, EINVAL failed
 */
static int mxc_v4l2_s_std(cam_data *cam, v4l2_std_id e)
{
	pr_debug("In mxc_v4l2_s_std %Lx\n", e);

	if (e == V4L2_STD_PAL) {
		pr_debug("   Setting standard to PAL %Lx\n", V4L2_STD_PAL);
		cam->standard.id = V4L2_STD_PAL;
		video_index = TV_PAL;
	} else if (e == V4L2_STD_NTSC) {
		pr_debug("   Setting standard to NTSC %Lx\n",
				V4L2_STD_NTSC);
		/* Get rid of the white dot line in NTSC signal input */
		cam->standard.id = V4L2_STD_NTSC;
		video_index = TV_NTSC;
	} else {
		cam->standard.id = V4L2_STD_ALL;
		video_index = TV_NOT_LOCKED;
		pr_err("ERROR: unrecognized std! %Lx (PAL=%Lx, NTSC=%Lx\n",
			e, V4L2_STD_PAL, V4L2_STD_NTSC);
	}

	cam->standard.index = video_index;
	strcpy(cam->standard.name, video_fmts[video_index].name);
	cam->crop_bounds.width = video_fmts[video_index].raw_width;
	cam->crop_bounds.height = video_fmts[video_index].raw_height;
	cam->crop_current.width = video_fmts[video_index].active_width;
	cam->crop_current.height = video_fmts[video_index].active_height;
	cam->crop_current.top = video_fmts[video_index].active_top;
	cam->crop_current.left = video_fmts[video_index].active_left;

	return 0;
}

/*!
 * V4L2 - mxc_v4l2_g_std function
 *
 * Gets the TV standard from the TV input device.
 *
 * @param cam	      structure cam_data *
 *
 * @param e	      structure v4l2_streamparm *
 *
 * @return  status    0 success, EINVAL failed
 */
static int mxc_v4l2_g_std(cam_data *cam, v4l2_std_id *e)
{
	struct v4l2_format tv_fmt;

	pr_debug("In mxc_v4l2_g_std\n");

	if (cam->device_type == 1) {
		/* Use this function to get what the TV-In device detects the
		 * format to be. pixelformat is used to return the std value
		 * since the interface has no vidioc_g_std.*/
		tv_fmt.type = V4L2_BUF_TYPE_PRIVATE;
		vidioc_int_g_fmt_cap(cam->sensor, &tv_fmt);

		/* If the TV-in automatically detects the standard, then if it
		 * changes, the settings need to change. */
		if (cam->standard_autodetect) {
			if (cam->standard.id != tv_fmt.fmt.pix.pixelformat) {
				pr_debug("MVC: mxc_v4l2_g_std: "
					"Changing standard\n");
				mxc_v4l2_s_std(cam, tv_fmt.fmt.pix.pixelformat);
			}
		}

		*e = tv_fmt.fmt.pix.pixelformat;
	}

	return 0;
}

/*!
 * Dequeue one V4L capture buffer
 *
 * @param cam         structure cam_data *
 * @param buf         structure v4l2_buffer *
 *
 * @return  status    0 success, EINVAL invalid frame number,
 *                    ETIME timeout, ERESTARTSYS interrupted by user
 */
static int mxc_v4l_dqueue(cam_data *cam, struct v4l2_buffer *buf)
{
	int retval = 0;
	struct mxc_v4l_frame *frame;
	unsigned long lock_flags;

	pr_debug("In MVC:mxc_v4l_dqueue\n");

	if (!wait_event_interruptible_timeout(cam->enc_queue,
					      cam->enc_counter != 0,
					      10 * HZ)) {
		pr_err("ERROR: v4l2 capture: mxc_v4l_dqueue timeout "
			"enc_counter %x\n",
		       cam->enc_counter);
		return -ETIME;
	} else if (signal_pending(current)) {
		pr_err("ERROR: v4l2 capture: mxc_v4l_dqueue() "
			"interrupt received\n");
		return -ERESTARTSYS;
	}

	if (down_interruptible(&cam->busy_lock))
		return -EBUSY;

	spin_lock_irqsave(&cam->dqueue_int_lock, lock_flags);
	cam->enc_counter--;

	frame = list_entry(cam->done_q.next, struct mxc_v4l_frame, queue);
	list_del(cam->done_q.next);
	if (frame->buffer.flags & V4L2_BUF_FLAG_DONE) {
		frame->buffer.flags &= ~V4L2_BUF_FLAG_DONE;
	} else if (frame->buffer.flags & V4L2_BUF_FLAG_QUEUED) {
		pr_err("ERROR: v4l2 capture: VIDIOC_DQBUF: "
			"Buffer not filled.\n");
		frame->buffer.flags &= ~V4L2_BUF_FLAG_QUEUED;
		retval = -EINVAL;
	} else if ((frame->buffer.flags & 0x7) == V4L2_BUF_FLAG_MAPPED) {
		pr_err("ERROR: v4l2 capture: VIDIOC_DQBUF: "
			"Buffer not queued.\n");
		retval = -EINVAL;
	}

	cam->frame[frame->index].buffer.field = cam->device_type ?
				V4L2_FIELD_INTERLACED : V4L2_FIELD_NONE;

	buf->bytesused = cam->v2f.fmt.pix.sizeimage;
	buf->index = frame->index;
	buf->flags = frame->buffer.flags;
	buf->m = cam->frame[frame->index].buffer.m;
	buf->timestamp = cam->frame[frame->index].buffer.timestamp;
	buf->field = cam->frame[frame->index].buffer.field;
	spin_unlock_irqrestore(&cam->dqueue_int_lock, lock_flags);

	up(&cam->busy_lock);
	return retval;
}

/*!
 * V4L interface - open function
 *
 * @param file         structure file *
 *
 * @return  status    0 success, ENODEV invalid device instance,
 *                    ENODEV timeout, ERESTARTSYS interrupted by user
 */
static int mxc_v4l_open(struct file *file)
{
	struct v4l2_ifparm ifparm;
	struct v4l2_format cam_fmt;
	ipu_csi_signal_cfg_t csi_param;
	struct video_device *dev = video_devdata(file);
	cam_data *cam = video_get_drvdata(dev);
	int err = 0;
	struct sensor_data *sensor;

	pr_debug("\nIn MVC: mxc_v4l_open\n");
	pr_debug("   device name is %s\n", dev->name);

	if (!cam) {
		pr_err("ERROR: v4l2 capture: Internal error, "
			"cam_data not found!\n");
		return -EBADF;
	}

	if (cam->sensor == NULL ||
	    cam->sensor->type != v4l2_int_type_slave) {
		pr_err("ERROR: v4l2 capture: slave not found!\n");
		return -EAGAIN;
	}

	sensor = cam->sensor->priv;
	if (!sensor) {
		pr_err("%s: Internal error, sensor_data is not found!\n",
		       __func__);
		return -EBADF;
	}

	down(&cam->busy_lock);
	err = 0;
	if (signal_pending(current))
		goto oops;

	if (cam->open_count++ == 0) {
		wait_event_interruptible(cam->power_queue,
					 cam->low_power == false);

		if (strcmp(mxc_capture_inputs[cam->current_input].name,
			   "CSI MEM") == 0) {
#if defined(CONFIG_MXC_IPU_CSI_ENC) || defined(CONFIG_MXC_IPU_CSI_ENC_MODULE)
			err = csi_enc_select(cam);
#endif
		} else if (strcmp(mxc_capture_inputs[cam->current_input].name,
				  "CSI IC MEM") == 0) {
#if defined(CONFIG_MXC_IPU_PRP_ENC) || defined(CONFIG_MXC_IPU_PRP_ENC_MODULE)
			err = prp_enc_select(cam);
#endif
		}

		cam->enc_counter = 0;
		INIT_LIST_HEAD(&cam->ready_q);
		INIT_LIST_HEAD(&cam->working_q);
		INIT_LIST_HEAD(&cam->done_q);

		vidioc_int_g_ifparm(cam->sensor, &ifparm);

		csi_param.sens_clksrc = 0;

		csi_param.clk_mode = 0;
		csi_param.data_pol = 0;
		csi_param.ext_vsync = 0;

		csi_param.pack_tight = 0;
		csi_param.force_eof = 0;
		csi_param.data_en_pol = 0;

		csi_param.mclk = ifparm.u.bt656.clock_curr;

		csi_param.pixclk_pol = ifparm.u.bt656.latch_clk_inv;

		if (ifparm.u.bt656.mode
				== V4L2_IF_TYPE_BT656_MODE_NOBT_8BIT)
			csi_param.data_width = IPU_CSI_DATA_WIDTH_8;
		else if (ifparm.u.bt656.mode
				== V4L2_IF_TYPE_BT656_MODE_NOBT_10BIT)
			csi_param.data_width = IPU_CSI_DATA_WIDTH_10;
		else
			csi_param.data_width = IPU_CSI_DATA_WIDTH_8;


		csi_param.Vsync_pol = ifparm.u.bt656.nobt_vs_inv;
		csi_param.Hsync_pol = ifparm.u.bt656.nobt_hs_inv;

		csi_param.csi = cam->csi;

		cam_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		vidioc_int_g_fmt_cap(cam->sensor, &cam_fmt);

		/* Reset the sizes.  Needed to prevent carryover of last
		 * operation.*/
		cam->crop_bounds.top = cam->crop_bounds.left = 0;
		cam->crop_bounds.width = cam_fmt.fmt.pix.width;
		cam->crop_bounds.height = cam_fmt.fmt.pix.height;

		/* This also is the max crop size for this device. */
		cam->crop_defrect.top = cam->crop_defrect.left = 0;
		cam->crop_defrect.width = cam_fmt.fmt.pix.width;
		cam->crop_defrect.height = cam_fmt.fmt.pix.height;

		/* At this point, this is also the current image size. */
		cam->crop_current.top = cam->crop_current.left = 0;
		cam->crop_current.width = cam_fmt.fmt.pix.width;
		cam->crop_current.height = cam_fmt.fmt.pix.height;

		pr_debug("End of %s: v2f pix widthxheight %d x %d\n",
			__func__,
			cam->v2f.fmt.pix.width, cam->v2f.fmt.pix.height);
		pr_debug("End of %s: crop_bounds widthxheight %d x %d\n",
			__func__,
			cam->crop_bounds.width, cam->crop_bounds.height);
		pr_debug("End of %s: crop_defrect widthxheight %d x %d\n",
			__func__,
			cam->crop_defrect.width, cam->crop_defrect.height);
		pr_debug("End of %s: crop_current widthxheight %d x %d\n",
			__func__,
			cam->crop_current.width, cam->crop_current.height);

		csi_param.data_fmt = cam_fmt.fmt.pix.pixelformat;
		pr_debug("On Open: Input to ipu size is %d x %d\n",
				cam_fmt.fmt.pix.width, cam_fmt.fmt.pix.height);
		ipu_csi_set_window_size(cam->ipu, cam->crop_current.width,
					cam->crop_current.height,
					cam->csi);
		ipu_csi_set_window_pos(cam->ipu, cam->crop_current.left,
					cam->crop_current.top,
					cam->csi);
		ipu_csi_init_interface(cam->ipu, cam->crop_bounds.width,
					cam->crop_bounds.height,
					cam_fmt.fmt.pix.pixelformat,
					csi_param);
		clk_prepare_enable(sensor->sensor_clk);
		vidioc_int_s_power(cam->sensor, 1);
		vidioc_int_init(cam->sensor);
		vidioc_int_dev_init(cam->sensor);
	}

	file->private_data = dev;

oops:
	up(&cam->busy_lock);
	return err;
}

/*!
 * V4L interface - close function
 *
 * @param file     struct file *
 *
 * @return         0 success
 */
static int mxc_v4l_close(struct file *file)
{
	struct video_device *dev = video_devdata(file);
	int err = 0;
	cam_data *cam = video_get_drvdata(dev);
	struct sensor_data *sensor;
	pr_debug("In MVC:mxc_v4l_close\n");

	if (!cam) {
		pr_err("ERROR: v4l2 capture: Internal error, "
			"cam_data not found!\n");
		return -EBADF;
	}

	if (!cam->sensor) {
		pr_err("%s: Internal error, camera is not found!\n",
		       __func__);
		return -EBADF;
	}

	sensor = cam->sensor->priv;
	if (!sensor) {
		pr_err("%s: Internal error, sensor_data is not found!\n",
		       __func__);
		return -EBADF;
	}

	down(&cam->busy_lock);

	/* for the case somebody hit the ctrl C */
	if (cam->overlay_pid == current->pid && cam->overlay_on) {
		err = stop_preview(cam);
		cam->overlay_on = false;
	}
	if (cam->capture_pid == current->pid) {
		err |= mxc_streamoff(cam);
		wake_up_interruptible(&cam->enc_queue);
	}

	if (--cam->open_count == 0) {
		vidioc_int_s_power(cam->sensor, 0);
		clk_disable_unprepare(sensor->sensor_clk);
		wait_event_interruptible(cam->power_queue,
					 cam->low_power == false);
		pr_debug("mxc_v4l_close: release resource\n");

		if (strcmp(mxc_capture_inputs[cam->current_input].name,
			   "CSI MEM") == 0) {
#if defined(CONFIG_MXC_IPU_CSI_ENC) || defined(CONFIG_MXC_IPU_CSI_ENC_MODULE)
			err |= csi_enc_deselect(cam);
#endif
		} else if (strcmp(mxc_capture_inputs[cam->current_input].name,
				  "CSI IC MEM") == 0) {
#if defined(CONFIG_MXC_IPU_PRP_ENC) || defined(CONFIG_MXC_IPU_PRP_ENC_MODULE)
			err |= prp_enc_deselect(cam);
#endif
		}

		mxc_free_frame_buf(cam);
		file->private_data = NULL;

		/* capture off */
		wake_up_interruptible(&cam->enc_queue);
		mxc_free_frames(cam);
		cam->enc_counter++;
	}

	up(&cam->busy_lock);

	return err;
}

#if defined(CONFIG_MXC_IPU_PRP_ENC) || defined(CONFIG_MXC_IPU_CSI_ENC) || \
    defined(CONFIG_MXC_IPU_PRP_ENC_MODULE) || \
    defined(CONFIG_MXC_IPU_CSI_ENC_MODULE)
/*
 * V4L interface - read function
 *
 * @param file       struct file *
 * @param read buf   char *
 * @param count      size_t
 * @param ppos       structure loff_t *
 *
 * @return           bytes read
 */
static ssize_t mxc_v4l_read(struct file *file, char *buf, size_t count,
			    loff_t *ppos)
{
	int err = 0;
	u8 *v_address[2];
	struct video_device *dev = video_devdata(file);
	cam_data *cam = video_get_drvdata(dev);

	if (down_interruptible(&cam->busy_lock))
		return -EINTR;

	/* Stop the viewfinder */
	if (cam->overlay_on == true)
		stop_preview(cam);

	v_address[0] = dma_alloc_coherent(0,
				       PAGE_ALIGN(cam->v2f.fmt.pix.sizeimage),
				       &cam->still_buf[0],
				       GFP_DMA | GFP_KERNEL);

	v_address[1] = dma_alloc_coherent(0,
				       PAGE_ALIGN(cam->v2f.fmt.pix.sizeimage),
				       &cam->still_buf[1],
				       GFP_DMA | GFP_KERNEL);

	if (!v_address[0] || !v_address[1]) {
		err = -ENOBUFS;
		goto exit0;
	}

	err = prp_still_select(cam);
	if (err != 0) {
		err = -EIO;
		goto exit0;
	}

	cam->still_counter = 0;
	err = cam->csi_start(cam);
	if (err != 0) {
		err = -EIO;
		goto exit1;
	}

	if (!wait_event_interruptible_timeout(cam->still_queue,
					      cam->still_counter != 0,
					      10 * HZ)) {
		pr_err("ERROR: v4l2 capture: mxc_v4l_read timeout counter %x\n",
		       cam->still_counter);
		err = -ETIME;
		goto exit1;
	}
	err = copy_to_user(buf, v_address[1], cam->v2f.fmt.pix.sizeimage);

exit1:
	prp_still_deselect(cam);

exit0:
	if (v_address[0] != 0)
		dma_free_coherent(0, cam->v2f.fmt.pix.sizeimage, v_address[0],
				  cam->still_buf[0]);
	if (v_address[1] != 0)
		dma_free_coherent(0, cam->v2f.fmt.pix.sizeimage, v_address[1],
				  cam->still_buf[1]);

	cam->still_buf[0] = cam->still_buf[1] = 0;

	if (cam->overlay_on == true)
		start_preview(cam);

	up(&cam->busy_lock);
	if (err < 0)
		return err;

	return cam->v2f.fmt.pix.sizeimage - err;
}
#endif

/*!
 * V4L interface - ioctl function
 *
 * @param file       struct file*
 *
 * @param ioctlnr    unsigned int
 *
 * @param arg        void*
 *
 * @return           0 success, ENODEV for invalid device instance,
 *                   -1 for other errors.
 */
static long mxc_v4l_do_ioctl(struct file *file,
			    unsigned int ioctlnr, void *arg)
{
	struct video_device *dev = video_devdata(file);
	cam_data *cam = video_get_drvdata(dev);
	int retval = 0;
	unsigned long lock_flags;

	pr_debug("In MVC: mxc_v4l_do_ioctl %x\n", ioctlnr);
	wait_event_interruptible(cam->power_queue, cam->low_power == false);
	/* make this _really_ smp-safe */
	if (ioctlnr != VIDIOC_DQBUF)
		if (down_interruptible(&cam->busy_lock))
			return -EBUSY;

	switch (ioctlnr) {
	/*!
	 * V4l2 VIDIOC_QUERYCAP ioctl
	 */
	case VIDIOC_QUERYCAP: {
		struct v4l2_capability *cap = arg;
		pr_debug("   case VIDIOC_QUERYCAP\n");
		strcpy(cap->driver, "mxc_v4l2");
		cap->version = KERNEL_VERSION(0, 1, 11);
		cap->capabilities = V4L2_CAP_VIDEO_CAPTURE |
				    V4L2_CAP_VIDEO_OVERLAY |
				    V4L2_CAP_STREAMING |
				    V4L2_CAP_READWRITE;
		cap->card[0] = '\0';
		cap->bus_info[0] = '\0';
		break;
	}

	/*!
	 * V4l2 VIDIOC_G_FMT ioctl
	 */
	case VIDIOC_G_FMT: {
		struct v4l2_format *gf = arg;
		pr_debug("   case VIDIOC_G_FMT\n");
		retval = mxc_v4l2_g_fmt(cam, gf);
		break;
	}

	/*!
	 * V4l2 VIDIOC_S_DEST_CROP ioctl
	 */
	case VIDIOC_S_DEST_CROP: {
		struct v4l2_mxc_dest_crop  *of = arg;
		pr_debug("   case VIDIOC_S_DEST_CROP\n");
		cam->offset.u_offset = of->offset.u_offset;
		cam->offset.v_offset = of->offset.v_offset;
		break;
	}

	/*!
	 * V4l2 VIDIOC_S_FMT ioctl
	 */
	case VIDIOC_S_FMT: {
		struct v4l2_format *sf = arg;
		pr_debug("   case VIDIOC_S_FMT\n");
		retval = mxc_v4l2_s_fmt(cam, sf);
		break;
	}

	/*!
	 * V4l2 VIDIOC_REQBUFS ioctl
	 */
	case VIDIOC_REQBUFS: {
		struct v4l2_requestbuffers *req = arg;
		pr_debug("   case VIDIOC_REQBUFS\n");

		if (req->count > FRAME_NUM) {
			pr_err("ERROR: v4l2 capture: VIDIOC_REQBUFS: "
			       "not enough buffers\n");
			req->count = FRAME_NUM;
		}

		if ((req->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)) {
			pr_err("ERROR: v4l2 capture: VIDIOC_REQBUFS: "
			       "wrong buffer type\n");
			retval = -EINVAL;
			break;
		}

		mxc_streamoff(cam);
		if (req->memory & V4L2_MEMORY_MMAP) {
			mxc_free_frame_buf(cam);
			retval = mxc_allocate_frame_buf(cam, req->count);
		}
		break;
	}

	/*!
	 * V4l2 VIDIOC_QUERYBUF ioctl
	 */
	case VIDIOC_QUERYBUF: {
		struct v4l2_buffer *buf = arg;
		int index = buf->index;
		pr_debug("   case VIDIOC_QUERYBUF\n");

		if (buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
			pr_err("ERROR: v4l2 capture: "
			       "VIDIOC_QUERYBUFS: "
			       "wrong buffer type\n");
			retval = -EINVAL;
			break;
		}

		if (buf->memory & V4L2_MEMORY_MMAP) {
			memset(buf, 0, sizeof(buf));
			buf->index = index;
		}

		down(&cam->param_lock);
		if (buf->memory & V4L2_MEMORY_USERPTR) {
			mxc_v4l2_release_bufs(cam);
			retval = mxc_v4l2_prepare_bufs(cam, buf);
		}

		if (buf->memory & V4L2_MEMORY_MMAP)
			retval = mxc_v4l2_buffer_status(cam, buf);
		up(&cam->param_lock);
		break;
	}

	/*!
	 * V4l2 VIDIOC_QBUF ioctl
	 */
	case VIDIOC_QBUF: {
		struct v4l2_buffer *buf = arg;
		int index = buf->index;
		pr_debug("   case VIDIOC_QBUF\n");

		spin_lock_irqsave(&cam->queue_int_lock, lock_flags);
		if ((cam->frame[index].buffer.flags & 0x7) ==
		    V4L2_BUF_FLAG_MAPPED) {
			cam->frame[index].buffer.flags |=
			    V4L2_BUF_FLAG_QUEUED;
			list_add_tail(&cam->frame[index].queue,
				      &cam->ready_q);
		} else if (cam->frame[index].buffer.
			   flags & V4L2_BUF_FLAG_QUEUED) {
			pr_err("ERROR: v4l2 capture: VIDIOC_QBUF: "
			       "buffer already queued\n");
			retval = -EINVAL;
		} else if (cam->frame[index].buffer.
			   flags & V4L2_BUF_FLAG_DONE) {
			pr_err("ERROR: v4l2 capture: VIDIOC_QBUF: "
			       "overwrite done buffer.\n");
			cam->frame[index].buffer.flags &=
			    ~V4L2_BUF_FLAG_DONE;
			cam->frame[index].buffer.flags |=
			    V4L2_BUF_FLAG_QUEUED;
			retval = -EINVAL;
		}

		buf->flags = cam->frame[index].buffer.flags;
		spin_unlock_irqrestore(&cam->queue_int_lock, lock_flags);
		break;
	}

	/*!
	 * V4l2 VIDIOC_DQBUF ioctl
	 */
	case VIDIOC_DQBUF: {
		struct v4l2_buffer *buf = arg;
		pr_debug("   case VIDIOC_DQBUF\n");

		if ((cam->enc_counter == 0) &&
			(file->f_flags & O_NONBLOCK)) {
			retval = -EAGAIN;
			break;
		}

		retval = mxc_v4l_dqueue(cam, buf);
		break;
	}

	/*!
	 * V4l2 VIDIOC_STREAMON ioctl
	 */
	case VIDIOC_STREAMON: {
		pr_debug("   case VIDIOC_STREAMON\n");
		retval = mxc_streamon(cam);
		break;
	}

	/*!
	 * V4l2 VIDIOC_STREAMOFF ioctl
	 */
	case VIDIOC_STREAMOFF: {
		pr_debug("   case VIDIOC_STREAMOFF\n");
		retval = mxc_streamoff(cam);
		break;
	}

	/*!
	 * V4l2 VIDIOC_G_CTRL ioctl
	 */
	case VIDIOC_G_CTRL: {
		pr_debug("   case VIDIOC_G_CTRL\n");
		retval = mxc_v4l2_g_ctrl(cam, arg);
		break;
	}

	/*!
	 * V4l2 VIDIOC_S_CTRL ioctl
	 */
	case VIDIOC_S_CTRL: {
		pr_debug("   case VIDIOC_S_CTRL\n");
		retval = mxc_v4l2_s_ctrl(cam, arg);
		break;
	}

	/*!
	 * V4l2 VIDIOC_CROPCAP ioctl
	 */
	case VIDIOC_CROPCAP: {
		struct v4l2_cropcap *cap = arg;
		pr_debug("   case VIDIOC_CROPCAP\n");
		if (cap->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
		    cap->type != V4L2_BUF_TYPE_VIDEO_OVERLAY) {
			retval = -EINVAL;
			break;
		}
		cap->bounds = cam->crop_bounds;
		cap->defrect = cam->crop_defrect;
		break;
	}

	/*!
	 * V4l2 VIDIOC_G_CROP ioctl
	 */
	case VIDIOC_G_CROP: {
		struct v4l2_crop *crop = arg;
		pr_debug("   case VIDIOC_G_CROP\n");

		if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
		    crop->type != V4L2_BUF_TYPE_VIDEO_OVERLAY) {
			retval = -EINVAL;
			break;
		}
		crop->c = cam->crop_current;
		break;
	}

	/*!
	 * V4l2 VIDIOC_S_CROP ioctl
	 */
	case VIDIOC_S_CROP: {
		struct v4l2_crop *crop = arg;
		struct v4l2_rect *b = &cam->crop_bounds;
		pr_debug("   case VIDIOC_S_CROP\n");

		if (crop->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
		    crop->type != V4L2_BUF_TYPE_VIDEO_OVERLAY) {
			retval = -EINVAL;
			break;
		}

		crop->c.top = (crop->c.top < b->top) ? b->top
			      : crop->c.top;
		if (crop->c.top > b->top + b->height)
			crop->c.top = b->top + b->height - 1;
		if (crop->c.height > b->top + b->height - crop->c.top)
			crop->c.height =
				b->top + b->height - crop->c.top;

		crop->c.left = (crop->c.left < b->left) ? b->left
		    : crop->c.left;
		if (crop->c.left > b->left + b->width)
			crop->c.left = b->left + b->width - 1;
		if (crop->c.width > b->left - crop->c.left + b->width)
			crop->c.width =
				b->left - crop->c.left + b->width;

		crop->c.width -= crop->c.width % 8;
		crop->c.left -= crop->c.left % 4;
		cam->crop_current = crop->c;

		pr_debug("   Cropping Input to ipu size %d x %d\n",
				cam->crop_current.width,
				cam->crop_current.height);
		ipu_csi_set_window_size(cam->ipu, cam->crop_current.width,
					cam->crop_current.height,
					cam->csi);
		ipu_csi_set_window_pos(cam->ipu, cam->crop_current.left,
				       cam->crop_current.top,
				       cam->csi);
		break;
	}

	/*!
	 * V4l2 VIDIOC_OVERLAY ioctl
	 */
	case VIDIOC_OVERLAY: {
		int *on = arg;
		pr_debug("   VIDIOC_OVERLAY on=%d\n", *on);
		if (*on) {
			cam->overlay_on = true;
			cam->overlay_pid = current->pid;
			retval = start_preview(cam);
		}
		if (!*on) {
			retval = stop_preview(cam);
			cam->overlay_on = false;
		}
		break;
	}

	/*!
	 * V4l2 VIDIOC_G_FBUF ioctl
	 */
	case VIDIOC_G_FBUF: {
		struct v4l2_framebuffer *fb = arg;
		pr_debug("   case VIDIOC_G_FBUF\n");
		*fb = cam->v4l2_fb;
		fb->capability = V4L2_FBUF_CAP_EXTERNOVERLAY;
		break;
	}

	/*!
	 * V4l2 VIDIOC_S_FBUF ioctl
	 */
	case VIDIOC_S_FBUF: {
		struct v4l2_framebuffer *fb = arg;
		pr_debug("   case VIDIOC_S_FBUF\n");
		cam->v4l2_fb = *fb;
		break;
	}

	case VIDIOC_G_PARM: {
		struct v4l2_streamparm *parm = arg;
		pr_debug("   case VIDIOC_G_PARM\n");
		if (cam->sensor)
			retval = vidioc_int_g_parm(cam->sensor, parm);
		else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			retval = -ENODEV;
		}
		break;
	}

	case VIDIOC_S_PARM:  {
		struct v4l2_streamparm *parm = arg;
		pr_debug("   case VIDIOC_S_PARM\n");
		if (cam->sensor)
			retval = mxc_v4l2_s_param(cam, parm);
		else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			retval = -ENODEV;
		}
		break;
	}

	/* linux v4l2 bug, kernel c0485619 user c0405619 */
	case VIDIOC_ENUMSTD: {
		struct v4l2_standard *e = arg;
		pr_debug("   case VIDIOC_ENUMSTD\n");
		*e = cam->standard;
		break;
	}

	case VIDIOC_G_STD: {
		v4l2_std_id *e = arg;
		pr_debug("   case VIDIOC_G_STD\n");
		if (cam->sensor)
			retval = mxc_v4l2_g_std(cam, e);
		else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			retval = -ENODEV;
		}
		break;
	}

	case VIDIOC_S_STD: {
		v4l2_std_id *e = arg;
		pr_debug("   case VIDIOC_S_STD\n");
		retval = mxc_v4l2_s_std(cam, *e);

		break;
	}

	case VIDIOC_ENUMOUTPUT: {
		struct v4l2_output *output = arg;
		pr_debug("   case VIDIOC_ENUMOUTPUT\n");
		if (output->index >= MXC_V4L2_CAPTURE_NUM_OUTPUTS) {
			retval = -EINVAL;
			break;
		}
		*output = mxc_capture_outputs[output->index];

		break;
	}
	case VIDIOC_G_OUTPUT: {
		int *p_output_num = arg;
		pr_debug("   case VIDIOC_G_OUTPUT\n");
		*p_output_num = cam->output;
		break;
	}

	case VIDIOC_S_OUTPUT: {
		int *p_output_num = arg;
		pr_debug("   case VIDIOC_S_OUTPUT\n");
		if (*p_output_num >= MXC_V4L2_CAPTURE_NUM_OUTPUTS) {
			retval = -EINVAL;
			break;
		}
		cam->output = *p_output_num;
		break;
	}

	case VIDIOC_ENUMINPUT: {
		struct v4l2_input *input = arg;
		pr_debug("   case VIDIOC_ENUMINPUT\n");
		if (input->index >= MXC_V4L2_CAPTURE_NUM_INPUTS) {
			retval = -EINVAL;
			break;
		}
		*input = mxc_capture_inputs[input->index];
		break;
	}

	case VIDIOC_G_INPUT: {
		int *index = arg;
		pr_debug("   case VIDIOC_G_INPUT\n");
		*index = cam->current_input;
		break;
	}

	case VIDIOC_S_INPUT: {
		int *index = arg;
		pr_debug("   case VIDIOC_S_INPUT\n");
		if (*index >= MXC_V4L2_CAPTURE_NUM_INPUTS) {
			retval = -EINVAL;
			break;
		}

		if (*index == cam->current_input)
			break;

		if ((mxc_capture_inputs[cam->current_input].status &
		    V4L2_IN_ST_NO_POWER) == 0) {
			retval = mxc_streamoff(cam);
			if (retval)
				break;
			mxc_capture_inputs[cam->current_input].status |=
							V4L2_IN_ST_NO_POWER;
		}

		if (strcmp(mxc_capture_inputs[*index].name, "CSI MEM") == 0) {
#if defined(CONFIG_MXC_IPU_CSI_ENC) || defined(CONFIG_MXC_IPU_CSI_ENC_MODULE)
			retval = csi_enc_select(cam);
			if (retval)
				break;
#endif
		} else if (strcmp(mxc_capture_inputs[*index].name,
				  "CSI IC MEM") == 0) {
#if defined(CONFIG_MXC_IPU_PRP_ENC) || defined(CONFIG_MXC_IPU_PRP_ENC_MODULE)
			retval = prp_enc_select(cam);
			if (retval)
				break;
#endif
		}

		mxc_capture_inputs[*index].status &= ~V4L2_IN_ST_NO_POWER;
		cam->current_input = *index;
		break;
	}
	case VIDIOC_ENUM_FMT: {
		struct v4l2_fmtdesc *f = arg;
		if (cam->sensor)
			retval = vidioc_int_enum_fmt_cap(cam->sensor, f);
		else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			retval = -ENODEV;
		}
		break;
	}
	case VIDIOC_ENUM_FRAMESIZES: {
		struct v4l2_frmsizeenum *fsize = arg;
		if (cam->sensor)
			retval = vidioc_int_enum_framesizes(cam->sensor, fsize);
		else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			retval = -ENODEV;
		}
		break;
	}
	case VIDIOC_ENUM_FRAMEINTERVALS: {
		struct v4l2_frmivalenum *fival = arg;
		if (cam->sensor) {
			retval = vidioc_int_enum_frameintervals(cam->sensor,
								fival);
		} else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			retval = -ENODEV;
		}
		break;
	}
	case VIDIOC_DBG_G_CHIP_IDENT: {
		struct v4l2_dbg_chip_ident *p = arg;
		p->ident = V4L2_IDENT_NONE;
		p->revision = 0;
		if (cam->sensor)
			retval = vidioc_int_g_chip_ident(cam->sensor, (int *)p);
		else {
			pr_err("ERROR: v4l2 capture: slave not found!\n");
			retval = -ENODEV;
		}
		break;
	}
	case VIDIOC_TRY_FMT:
	case VIDIOC_QUERYCTRL:
	case VIDIOC_G_TUNER:
	case VIDIOC_S_TUNER:
	case VIDIOC_G_FREQUENCY:
	case VIDIOC_S_FREQUENCY:
	default:
		pr_debug("   case default or not supported\n");
		retval = -EINVAL;
		break;
	}

	if (ioctlnr != VIDIOC_DQBUF)
		up(&cam->busy_lock);
	return retval;
}

/*
 * V4L interface - ioctl function
 *
 * @return  None
 */
static long mxc_v4l_ioctl(struct file *file, unsigned int cmd,
			 unsigned long arg)
{
	pr_debug("In MVC:mxc_v4l_ioctl\n");
	return video_usercopy(file, cmd, arg, mxc_v4l_do_ioctl);
}

/*!
 * V4L interface - mmap function
 *
 * @param file        structure file *
 *
 * @param vma         structure vm_area_struct *
 *
 * @return status     0 Success, EINTR busy lock error, ENOBUFS remap_page error
 */
static int mxc_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *dev = video_devdata(file);
	unsigned long size;
	int res = 0;
	cam_data *cam = video_get_drvdata(dev);

	pr_debug("In MVC:mxc_mmap\n");
	pr_debug("   pgoff=0x%lx, start=0x%lx, end=0x%lx\n",
		 vma->vm_pgoff, vma->vm_start, vma->vm_end);

	/* make this _really_ smp-safe */
	if (down_interruptible(&cam->busy_lock))
		return -EINTR;

	size = vma->vm_end - vma->vm_start;
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	if (remap_pfn_range(vma, vma->vm_start,
			    vma->vm_pgoff, size, vma->vm_page_prot)) {
		pr_err("ERROR: v4l2 capture: mxc_mmap: "
			"remap_pfn_range failed\n");
		res = -ENOBUFS;
		goto mxc_mmap_exit;
	}

	vma->vm_flags &= ~VM_IO;	/* using shared anonymous pages */

mxc_mmap_exit:
	up(&cam->busy_lock);
	return res;
}

/*!
 * V4L interface - poll function
 *
 * @param file       structure file *
 *
 * @param wait       structure poll_table_struct *
 *
 * @return  status   POLLIN | POLLRDNORM
 */
static unsigned int mxc_poll(struct file *file, struct poll_table_struct *wait)
{
	struct video_device *dev = video_devdata(file);
	cam_data *cam = video_get_drvdata(dev);
	wait_queue_head_t *queue = NULL;
	int res = POLLIN | POLLRDNORM;

	pr_debug("In MVC:mxc_poll\n");

	if (down_interruptible(&cam->busy_lock))
		return -EINTR;

	queue = &cam->enc_queue;
	poll_wait(file, queue, wait);

	up(&cam->busy_lock);

	return res;
}

/*!
 * This structure defines the functions to be called in this driver.
 */
static struct v4l2_file_operations mxc_v4l_fops = {
	.owner = THIS_MODULE,
	.open = mxc_v4l_open,
	.release = mxc_v4l_close,
	.read = mxc_v4l_read,
	.unlocked_ioctl = mxc_v4l_ioctl,
	.mmap = mxc_mmap,
	.poll = mxc_poll,
};

static struct video_device mxc_v4l_template = {
	.name = "Mxc Camera",
	.fops = &mxc_v4l_fops,
	.release = video_device_release,
};

/*!
 * This function can be used to release any platform data on closing.
 */
static void camera_platform_release(struct device *device)
{
}

/*!
 * Camera V4l2 callback function.
 *
 * @param mask      u32
 *
 * @param dev       void device structure
 *
 * @return status
 */
static void camera_callback(u32 mask, void *dev)
{
	struct mxc_v4l_frame *done_frame;
	struct mxc_v4l_frame *ready_frame;
	struct timeval cur_time;

	cam_data *cam = (cam_data *) dev;
	if (cam == NULL)
		return;

	pr_debug("In MVC:camera_callback\n");

	spin_lock(&cam->queue_int_lock);
	spin_lock(&cam->dqueue_int_lock);
	if (!list_empty(&cam->working_q)) {
		do_gettimeofday(&cur_time);

		done_frame = list_entry(cam->working_q.next,
					struct mxc_v4l_frame,
					queue);

		if (done_frame->ipu_buf_num != cam->local_buf_num)
			goto next;

		/*
		 * Set the current time to done frame buffer's
		 * timestamp. Users can use this information to judge
		 * the frame's usage.
		 */
		done_frame->buffer.timestamp = cur_time;

		if (done_frame->buffer.flags & V4L2_BUF_FLAG_QUEUED) {
			done_frame->buffer.flags |= V4L2_BUF_FLAG_DONE;
			done_frame->buffer.flags &= ~V4L2_BUF_FLAG_QUEUED;

			/* Added to the done queue */
			list_del(cam->working_q.next);
			list_add_tail(&done_frame->queue, &cam->done_q);

			/* Wake up the queue */
			cam->enc_counter++;
			wake_up_interruptible(&cam->enc_queue);
		} else
			pr_err("ERROR: v4l2 capture: camera_callback: "
				"buffer not queued\n");
	}

next:
	if (!list_empty(&cam->ready_q)) {
		ready_frame = list_entry(cam->ready_q.next,
					 struct mxc_v4l_frame,
					 queue);
		if (cam->enc_update_eba)
			if (cam->enc_update_eba(
				cam,
				ready_frame->buffer.m.offset) == 0) {
				list_del(cam->ready_q.next);
				list_add_tail(&ready_frame->queue,
					      &cam->working_q);
				ready_frame->ipu_buf_num = cam->local_buf_num;
			}
	} else {
		if (cam->enc_update_eba)
			cam->enc_update_eba(
				cam, cam->dummy_frame.buffer.m.offset);
	}

	cam->local_buf_num = (cam->local_buf_num == 0) ? 1 : 0;
	spin_unlock(&cam->dqueue_int_lock);
	spin_unlock(&cam->queue_int_lock);

	return;
}

/*!
 * initialize cam_data structure
 *
 * @param cam      structure cam_data *
 *
 * @return status  0 Success
 */
static int init_camera_struct(cam_data *cam, struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(mxc_v4l2_dt_ids, &pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	int ipu_id, csi_id, mclk_source;
	int ret = 0;
	struct v4l2_device *v4l2_dev;

	pr_debug("In MVC: init_camera_struct\n");

	ret = of_property_read_u32(np, "ipu_id", &ipu_id);
	if (ret) {
		dev_err(&pdev->dev, "ipu_id missing or invalid\n");
		return ret;
	}

	ret = of_property_read_u32(np, "csi_id", &csi_id);
	if (ret) {
		dev_err(&pdev->dev, "csi_id missing or invalid\n");
		return ret;
	}

	ret = of_property_read_u32(np, "mclk_source", &mclk_source);
	if (ret) {
		dev_err(&pdev->dev, "sensor mclk missing or invalid\n");
		return ret;
	}

	/* Default everything to 0 */
	memset(cam, 0, sizeof(cam_data));

	/* get devtype to distinguish if the cpu is imx5 or imx6
	 * IMX5_V4L2 specify the cpu is imx5
	 * IMX6_V4L2 specify the cpu is imx6q or imx6sdl
	 */
	if (of_id)
		pdev->id_entry = of_id->data;
	cam->devtype = pdev->id_entry->driver_data;

	cam->ipu = ipu_get_soc(ipu_id);
	if (cam->ipu == NULL) {
		pr_err("ERROR: v4l2 capture: failed to get ipu\n");
		return -EINVAL;
	} else if (cam->ipu == ERR_PTR(-ENODEV)) {
		pr_err("ERROR: v4l2 capture: get invalid ipu\n");
		return -ENODEV;
	}

	init_MUTEX(&cam->param_lock);
	init_MUTEX(&cam->busy_lock);

	cam->video_dev = video_device_alloc();
	if (cam->video_dev == NULL)
		return -ENODEV;

	*(cam->video_dev) = mxc_v4l_template;

	video_set_drvdata(cam->video_dev, cam);
	dev_set_drvdata(&pdev->dev, (void *)cam);
	cam->video_dev->minor = -1;

	v4l2_dev = kzalloc(sizeof(*v4l2_dev), GFP_KERNEL);
	if (!v4l2_dev) {
		dev_err(&pdev->dev, "failed to allocate v4l2_dev structure\n");
		video_device_release(cam->video_dev);
		return -ENOMEM;
	}

	if (v4l2_device_register(&pdev->dev, v4l2_dev) < 0) {
		dev_err(&pdev->dev, "register v4l2 device failed\n");
		video_device_release(cam->video_dev);
		kfree(v4l2_dev);
		return -ENODEV;
	}
	cam->video_dev->v4l2_dev = v4l2_dev;

	init_waitqueue_head(&cam->enc_queue);
	init_waitqueue_head(&cam->still_queue);

	/* setup cropping */
	cam->crop_bounds.left = 0;
	cam->crop_bounds.width = 640;
	cam->crop_bounds.top = 0;
	cam->crop_bounds.height = 480;
	cam->crop_current = cam->crop_defrect = cam->crop_bounds;
	ipu_csi_set_window_size(cam->ipu, cam->crop_current.width,
				cam->crop_current.height, cam->csi);
	ipu_csi_set_window_pos(cam->ipu, cam->crop_current.left,
				cam->crop_current.top, cam->csi);
	cam->streamparm.parm.capture.capturemode = 0;

	cam->standard.index = 0;
	cam->standard.id = V4L2_STD_UNKNOWN;
	cam->standard.frameperiod.denominator = 30;
	cam->standard.frameperiod.numerator = 1;
	cam->standard.framelines = 480;
	cam->standard_autodetect = true;
	cam->streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	cam->streamparm.parm.capture.timeperframe = cam->standard.frameperiod;
	cam->streamparm.parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	cam->overlay_on = false;
	cam->capture_on = false;
	cam->v4l2_fb.flags = V4L2_FBUF_FLAG_OVERLAY;

	cam->v2f.fmt.pix.sizeimage = 352 * 288 * 3 / 2;
	cam->v2f.fmt.pix.bytesperline = 288 * 3 / 2;
	cam->v2f.fmt.pix.width = 288;
	cam->v2f.fmt.pix.height = 352;
	cam->v2f.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
	cam->win.w.width = 160;
	cam->win.w.height = 160;
	cam->win.w.left = 0;
	cam->win.w.top = 0;

	cam->ipu_id = ipu_id;
	cam->csi = csi_id;
	cam->mclk_source = mclk_source;
	cam->mclk_on[cam->mclk_source] = false;

	cam->enc_callback = camera_callback;
	init_waitqueue_head(&cam->power_queue);
	spin_lock_init(&cam->queue_int_lock);
	spin_lock_init(&cam->dqueue_int_lock);

	cam->self = kmalloc(sizeof(struct v4l2_int_device), GFP_KERNEL);
	cam->self->module = THIS_MODULE;
	sprintf(cam->self->name, "mxc_v4l2_cap%d", cam->csi);
	cam->self->type = v4l2_int_type_master;
	cam->self->u.master = &mxc_v4l2_master;

	return 0;
}

static ssize_t show_streaming(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct video_device *video_dev = container_of(dev,
						struct video_device, dev);
	cam_data *cam = video_get_drvdata(video_dev);

	if (cam->capture_on)
		return sprintf(buf, "stream on\n");
	else
		return sprintf(buf, "stream off\n");
}
static DEVICE_ATTR(fsl_v4l2_capture_property, S_IRUGO, show_streaming, NULL);

static ssize_t show_overlay(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct video_device *video_dev = container_of(dev,
						struct video_device, dev);
	cam_data *cam = video_get_drvdata(video_dev);

	if (cam->overlay_on)
		return sprintf(buf, "overlay on\n");
	else
		return sprintf(buf, "overlay off\n");
}
static DEVICE_ATTR(fsl_v4l2_overlay_property, S_IRUGO, show_overlay, NULL);

static ssize_t show_csi(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct video_device *video_dev = container_of(dev,
						struct video_device, dev);
	cam_data *cam = video_get_drvdata(video_dev);

	return sprintf(buf, "ipu%d_csi%d\n", cam->ipu_id, cam->csi);
}
static DEVICE_ATTR(fsl_csi_property, S_IRUGO, show_csi, NULL);

/*!
 * This function is called to probe the devices if registered.
 *
 * @param   pdev  the device structure used to give information on which device
 *                to probe
 *
 * @return  The function returns 0 on success and -1 on failure.
 */
static int mxc_v4l2_probe(struct platform_device *pdev)
{
	/* Create cam and initialize it. */
	cam_data *cam = kmalloc(sizeof(cam_data), GFP_KERNEL);
	if (cam == NULL) {
		pr_err("ERROR: v4l2 capture: failed to register camera\n");
		return -1;
	}

	init_camera_struct(cam, pdev);
	pdev->dev.release = camera_platform_release;

	/* Set up the v4l2 device and register it*/
	cam->self->priv = cam;
	v4l2_int_device_register(cam->self);

	/* register v4l video device */
	if (video_register_device(cam->video_dev, VFL_TYPE_GRABBER, video_nr)
		< 0) {
		kfree(cam);
		cam = NULL;
		pr_err("ERROR: v4l2 capture: video_register_device failed\n");
		return -1;
	}
	pr_debug("   Video device registered: %s #%d\n",
		 cam->video_dev->name, cam->video_dev->minor);

	if (device_create_file(&cam->video_dev->dev,
			&dev_attr_fsl_v4l2_capture_property))
		dev_err(&pdev->dev, "Error on creating sysfs file"
			" for capture\n");

	if (device_create_file(&cam->video_dev->dev,
			&dev_attr_fsl_v4l2_overlay_property))
		dev_err(&pdev->dev, "Error on creating sysfs file"
			" for overlay\n");

	if (device_create_file(&cam->video_dev->dev,
			&dev_attr_fsl_csi_property))
		dev_err(&pdev->dev, "Error on creating sysfs file"
			" for csi number\n");

	return 0;
}

/*!
 * This function is called to remove the devices when device unregistered.
 *
 * @param   pdev  the device structure used to give information on which device
 *                to remove
 *
 * @return  The function returns 0 on success and -1 on failure.
 */
static int mxc_v4l2_remove(struct platform_device *pdev)
{
	cam_data *cam = (cam_data *)platform_get_drvdata(pdev);
	if (cam->open_count) {
		pr_err("ERROR: v4l2 capture:camera open "
			"-- setting ops to NULL\n");
		return -EBUSY;
	} else {
		struct v4l2_device *v4l2_dev = cam->video_dev->v4l2_dev;
		device_remove_file(&cam->video_dev->dev,
			&dev_attr_fsl_v4l2_capture_property);
		device_remove_file(&cam->video_dev->dev,
			&dev_attr_fsl_v4l2_overlay_property);
		device_remove_file(&cam->video_dev->dev,
			&dev_attr_fsl_csi_property);

		pr_info("V4L2 freeing image input device\n");
		v4l2_int_device_unregister(cam->self);
		video_unregister_device(cam->video_dev);

		mxc_free_frame_buf(cam);
		kfree(cam);

		v4l2_device_unregister(v4l2_dev);
		kfree(v4l2_dev);
	}

	pr_info("V4L2 unregistering video\n");
	return 0;
}

/*!
 * This function is called to put the sensor in a low power state.
 * Refer to the document driver-model/driver.txt in the kernel source tree
 * for more information.
 *
 * @param   pdev  the device structure used to give information on which I2C
 *                to suspend
 * @param   state the power state the device is entering
 *
 * @return  The function returns 0 on success and -1 on failure.
 */
static int mxc_v4l2_suspend(struct platform_device *pdev, pm_message_t state)
{
	cam_data *cam = platform_get_drvdata(pdev);

	pr_debug("In MVC:mxc_v4l2_suspend\n");

	if (cam == NULL)
		return -1;

	down(&cam->busy_lock);

	cam->low_power = true;

	if (cam->overlay_on == true)
		stop_preview(cam);
	if ((cam->capture_on == true) && cam->enc_disable)
		cam->enc_disable(cam);

	if (cam->sensor && cam->open_count) {
		if (cam->mclk_on[cam->mclk_source]) {
			ipu_csi_enable_mclk_if(cam->ipu, CSI_MCLK_I2C,
					       cam->mclk_source,
					       false, false);
			cam->mclk_on[cam->mclk_source] = false;
		}
		vidioc_int_s_power(cam->sensor, 0);
	}

	up(&cam->busy_lock);

	return 0;
}

/*!
 * This function is called to bring the sensor back from a low power state.
 * Refer to the document driver-model/driver.txt in the kernel source tree
 * for more information.
 *
 * @param   pdev   the device structure
 *
 * @return  The function returns 0 on success and -1 on failure
 */
static int mxc_v4l2_resume(struct platform_device *pdev)
{
	cam_data *cam = platform_get_drvdata(pdev);

	pr_debug("In MVC:mxc_v4l2_resume\n");

	if (cam == NULL)
		return -1;

	down(&cam->busy_lock);

	cam->low_power = false;
	wake_up_interruptible(&cam->power_queue);

	if (cam->sensor && cam->open_count) {
		vidioc_int_s_power(cam->sensor, 1);

		if (!cam->mclk_on[cam->mclk_source]) {
			ipu_csi_enable_mclk_if(cam->ipu, CSI_MCLK_I2C,
					       cam->mclk_source,
					       true, true);
			cam->mclk_on[cam->mclk_source] = true;
		}
	}

	if (cam->overlay_on == true)
		start_preview(cam);
	if (cam->capture_on == true)
		mxc_streamon(cam);

	up(&cam->busy_lock);

	return 0;
}

/*!
 * This structure contains pointers to the power management callback functions.
 */
static struct platform_driver mxc_v4l2_driver = {
	.driver = {
		   .name = "mxc_v4l2_capture",
		   .owner = THIS_MODULE,
		   .of_match_table = mxc_v4l2_dt_ids,
		   },
	.id_table = imx_v4l2_devtype,
	.probe = mxc_v4l2_probe,
	.remove = mxc_v4l2_remove,
	.suspend = mxc_v4l2_suspend,
	.resume = mxc_v4l2_resume,
	.shutdown = NULL,
};

/*!
 * Initializes the camera driver.
 */
static int mxc_v4l2_master_attach(struct v4l2_int_device *slave)
{
	cam_data *cam = slave->u.slave->master->priv;
	struct v4l2_format cam_fmt;
	int i;
	struct sensor_data *sdata = slave->priv;

	pr_debug("In MVC: mxc_v4l2_master_attach\n");
	pr_debug("   slave.name = %s\n", slave->name);
	pr_debug("   master.name = %s\n", slave->u.slave->master->name);

	if (slave == NULL) {
		pr_err("ERROR: v4l2 capture: slave parameter not valid.\n");
		return -1;
	}

	if (sdata->csi != cam->csi) {
		pr_debug("%s: csi doesn't match\n", __func__);
		return -1;
	}

	cam->sensor = slave;

	if (cam->sensor_index < MXC_SENSOR_NUM) {
		cam->all_sensors[cam->sensor_index] = slave;
		cam->sensor_index++;
	} else {
		pr_err("ERROR: v4l2 capture: slave number exceeds "
		       "the maximum.\n");
		return -1;
	}

	for (i = 0; i < cam->sensor_index; i++) {
		vidioc_int_dev_exit(cam->all_sensors[i]);
		vidioc_int_s_power(cam->all_sensors[i], 0);
	}

	cam_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vidioc_int_g_fmt_cap(cam->sensor, &cam_fmt);

	/* Used to detect TV in (type 1) vs. camera (type 0)*/
	cam->device_type = cam_fmt.fmt.pix.priv;

	/* Set the input size to the ipu for this device */
	cam->crop_bounds.top = cam->crop_bounds.left = 0;
	cam->crop_bounds.width = cam_fmt.fmt.pix.width;
	cam->crop_bounds.height = cam_fmt.fmt.pix.height;

	/* This also is the max crop size for this device. */
	cam->crop_defrect.top = cam->crop_defrect.left = 0;
	cam->crop_defrect.width = cam_fmt.fmt.pix.width;
	cam->crop_defrect.height = cam_fmt.fmt.pix.height;

	/* At this point, this is also the current image size. */
	cam->crop_current.top = cam->crop_current.left = 0;
	cam->crop_current.width = cam_fmt.fmt.pix.width;
	cam->crop_current.height = cam_fmt.fmt.pix.height;

	pr_debug("End of %s: v2f pix widthxheight %d x %d\n",
		 __func__,
		 cam->v2f.fmt.pix.width, cam->v2f.fmt.pix.height);
	pr_debug("End of %s: crop_bounds widthxheight %d x %d\n",
		 __func__,
		 cam->crop_bounds.width, cam->crop_bounds.height);
	pr_debug("End of %s: crop_defrect widthxheight %d x %d\n",
		 __func__,
		 cam->crop_defrect.width, cam->crop_defrect.height);
	pr_debug("End of %s: crop_current widthxheight %d x %d\n",
		 __func__,
		 cam->crop_current.width, cam->crop_current.height);

	return 0;
}

/*!
 * Disconnects the camera driver.
 */
static void mxc_v4l2_master_detach(struct v4l2_int_device *slave)
{
	unsigned int i;
	cam_data *cam = slave->u.slave->master->priv;

	pr_debug("In MVC:mxc_v4l2_master_detach\n");

	if (cam->sensor_index > 1) {
		for (i = 0; i < cam->sensor_index; i++) {
			if (cam->all_sensors[i] != slave)
				continue;
			/* Move all the sensors behind this
			 * sensor one step forward
			 */
			for (; i <= MXC_SENSOR_NUM - 2; i++)
				cam->all_sensors[i] = cam->all_sensors[i+1];
			break;
		}
		/* Point current sensor to the last one */
		cam->sensor = cam->all_sensors[cam->sensor_index - 2];
	} else
		cam->sensor = NULL;

	cam->sensor_index--;
	vidioc_int_dev_exit(slave);
}

/*!
 * Entry point for the V4L2
 *
 * @return  Error code indicating success or failure
 */
static __init int camera_init(void)
{
	u8 err = 0;

	pr_debug("In MVC:camera_init\n");

	/* Register the device driver structure. */
	err = platform_driver_register(&mxc_v4l2_driver);
	if (err != 0) {
		pr_err("ERROR: v4l2 capture:camera_init: "
			"platform_driver_register failed.\n");
		return err;
	}

	return err;
}

/*!
 * Exit and cleanup for the V4L2
 */
static void __exit camera_exit(void)
{
	pr_debug("In MVC: camera_exit\n");

	platform_driver_unregister(&mxc_v4l2_driver);
}

module_init(camera_init);
module_exit(camera_exit);

module_param(video_nr, int, 0444);
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("V4L2 capture driver for Mxc based cameras");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("video");
