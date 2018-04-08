/****************************************************************************
 *
 *  Filename: cpia2_v4l.c
 *
 *  Copyright 2001, STMicrolectronics, Inc.
 *      Contact:  steve.miller@st.com
 *  Copyright 2001,2005, Scott J. Bertin <scottbertin@yahoo.com>
 *
 *  Description:
 *     This is a USB driver for CPia2 based video cameras.
 *     The infrastructure of this driver is based on the cpia usb driver by
 *     Jochen Scharrlach and Johannes Erdfeldt.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Stripped of 2.4 stuff ready for main kernel submit by
 *		Alan Cox <alan@lxorguk.ukuu.org.uk>
 ****************************************************************************/

#define CPIA_VERSION "3.0.1"

#include <linux/module.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/videodev2.h>
#include <linux/stringify.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>

#include "cpia2.h"

static int video_nr = -1;
module_param(video_nr, int, 0);
MODULE_PARM_DESC(video_nr, "video device to register (0=/dev/video0, etc)");

static int buffer_size = 68 * 1024;
module_param(buffer_size, int, 0);
MODULE_PARM_DESC(buffer_size, "Size for each frame buffer in bytes (default 68k)");

static int num_buffers = 3;
module_param(num_buffers, int, 0);
MODULE_PARM_DESC(num_buffers, "Number of frame buffers (1-"
		 __stringify(VIDEO_MAX_FRAME) ", default 3)");

static int alternate = DEFAULT_ALT;
module_param(alternate, int, 0);
MODULE_PARM_DESC(alternate, "USB Alternate (" __stringify(USBIF_ISO_1) "-"
		 __stringify(USBIF_ISO_6) ", default "
		 __stringify(DEFAULT_ALT) ")");

static int flicker_mode;
module_param(flicker_mode, int, 0);
MODULE_PARM_DESC(flicker_mode, "Flicker frequency (0 (disabled), " __stringify(50) " or "
		 __stringify(60) ", default 0)");

MODULE_AUTHOR("Steve Miller (STMicroelectronics) <steve.miller@st.com>");
MODULE_DESCRIPTION("V4L-driver for STMicroelectronics CPiA2 based cameras");
MODULE_SUPPORTED_DEVICE("video");
MODULE_LICENSE("GPL");
MODULE_VERSION(CPIA_VERSION);

#define ABOUT "V4L-Driver for Vision CPiA2 based cameras"
#define CPIA2_CID_USB_ALT (V4L2_CID_USER_BASE | 0xf000)

/******************************************************************************
 *
 *  cpia2_open
 *
 *****************************************************************************/
static int cpia2_open(struct file *file)
{
	struct camera_data *cam = video_drvdata(file);
	int retval;

	if (mutex_lock_interruptible(&cam->v4l2_lock))
		return -ERESTARTSYS;
	retval = v4l2_fh_open(file);
	if (retval)
		goto open_unlock;

	if (v4l2_fh_is_singular_file(file)) {
		if (cpia2_allocate_buffers(cam)) {
			v4l2_fh_release(file);
			retval = -ENOMEM;
			goto open_unlock;
		}

		/* reset the camera */
		if (cpia2_reset_camera(cam) < 0) {
			v4l2_fh_release(file);
			retval = -EIO;
			goto open_unlock;
		}

		cam->APP_len = 0;
		cam->COM_len = 0;
	}

	cpia2_dbg_dump_registers(cam);
open_unlock:
	mutex_unlock(&cam->v4l2_lock);
	return retval;
}

/******************************************************************************
 *
 *  cpia2_close
 *
 *****************************************************************************/
static int cpia2_close(struct file *file)
{
	struct video_device *dev = video_devdata(file);
	struct camera_data *cam = video_get_drvdata(dev);

	mutex_lock(&cam->v4l2_lock);
	if (video_is_registered(&cam->vdev) && v4l2_fh_is_singular_file(file)) {
		cpia2_usb_stream_stop(cam);

		/* save camera state for later open */
		cpia2_save_camera_state(cam);

		cpia2_set_low_power(cam);
		cpia2_free_buffers(cam);
	}

	if (cam->stream_fh == file->private_data) {
		cam->stream_fh = NULL;
		cam->mmapped = 0;
	}
	mutex_unlock(&cam->v4l2_lock);
	return v4l2_fh_release(file);
}

/******************************************************************************
 *
 *  cpia2_v4l_read
 *
 *****************************************************************************/
static ssize_t cpia2_v4l_read(struct file *file, char __user *buf, size_t count,
			      loff_t *off)
{
	struct camera_data *cam = video_drvdata(file);
	int noblock = file->f_flags&O_NONBLOCK;
	ssize_t ret;

	if(!cam)
		return -EINVAL;

	if (mutex_lock_interruptible(&cam->v4l2_lock))
		return -ERESTARTSYS;
	ret = cpia2_read(cam, buf, count, noblock);
	mutex_unlock(&cam->v4l2_lock);
	return ret;
}


/******************************************************************************
 *
 *  cpia2_v4l_poll
 *
 *****************************************************************************/
static unsigned int cpia2_v4l_poll(struct file *filp, struct poll_table_struct *wait)
{
	struct camera_data *cam = video_drvdata(filp);
	unsigned int res;

	mutex_lock(&cam->v4l2_lock);
	res = cpia2_poll(cam, filp, wait);
	mutex_unlock(&cam->v4l2_lock);
	return res;
}


static int sync(struct camera_data *cam, int frame_nr)
{
	struct framebuf *frame = &cam->buffers[frame_nr];

	while (1) {
		if (frame->status == FRAME_READY)
			return 0;

		if (!cam->streaming) {
			frame->status = FRAME_READY;
			frame->length = 0;
			return 0;
		}

		mutex_unlock(&cam->v4l2_lock);
		wait_event_interruptible(cam->wq_stream,
					 !cam->streaming ||
					 frame->status == FRAME_READY);
		mutex_lock(&cam->v4l2_lock);
		if (signal_pending(current))
			return -ERESTARTSYS;
		if (!video_is_registered(&cam->vdev))
			return -ENOTTY;
	}
}

/******************************************************************************
 *
 *  ioctl_querycap
 *
 *  V4L2 device capabilities
 *
 *****************************************************************************/

static int cpia2_querycap(struct file *file, void *fh, struct v4l2_capability *vc)
{
	struct camera_data *cam = video_drvdata(file);

	strcpy(vc->driver, "cpia2");

	if (cam->params.pnp_id.product == 0x151)
		strcpy(vc->card, "QX5 Microscope");
	else
		strcpy(vc->card, "CPiA2 Camera");
	switch (cam->params.pnp_id.device_type) {
	case DEVICE_STV_672:
		strcat(vc->card, " (672/");
		break;
	case DEVICE_STV_676:
		strcat(vc->card, " (676/");
		break;
	default:
		strcat(vc->card, " (XXX/");
		break;
	}
	switch (cam->params.version.sensor_flags) {
	case CPIA2_VP_SENSOR_FLAGS_404:
		strcat(vc->card, "404)");
		break;
	case CPIA2_VP_SENSOR_FLAGS_407:
		strcat(vc->card, "407)");
		break;
	case CPIA2_VP_SENSOR_FLAGS_409:
		strcat(vc->card, "409)");
		break;
	case CPIA2_VP_SENSOR_FLAGS_410:
		strcat(vc->card, "410)");
		break;
	case CPIA2_VP_SENSOR_FLAGS_500:
		strcat(vc->card, "500)");
		break;
	default:
		strcat(vc->card, "XXX)");
		break;
	}

	if (usb_make_path(cam->dev, vc->bus_info, sizeof(vc->bus_info)) <0)
		memset(vc->bus_info,0, sizeof(vc->bus_info));

	vc->device_caps = V4L2_CAP_VIDEO_CAPTURE |
			   V4L2_CAP_READWRITE |
			   V4L2_CAP_STREAMING;
	vc->capabilities = vc->device_caps |
			   V4L2_CAP_DEVICE_CAPS;

	return 0;
}

/******************************************************************************
 *
 *  ioctl_input
 *
 *  V4L2 input get/set/enumerate
 *
 *****************************************************************************/

static int cpia2_enum_input(struct file *file, void *fh, struct v4l2_input *i)
{
	if (i->index)
		return -EINVAL;
	strcpy(i->name, "Camera");
	i->type = V4L2_INPUT_TYPE_CAMERA;
	return 0;
}

static int cpia2_g_input(struct file *file, void *fh, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int cpia2_s_input(struct file *file, void *fh, unsigned int i)
{
	return i ? -EINVAL : 0;
}

/******************************************************************************
 *
 *  ioctl_enum_fmt
 *
 *  V4L2 format enumerate
 *
 *****************************************************************************/

static int cpia2_enum_fmt_vid_cap(struct file *file, void *fh,
					    struct v4l2_fmtdesc *f)
{
	int index = f->index;

	if (index < 0 || index > 1)
	       return -EINVAL;

	memset(f, 0, sizeof(*f));
	f->index = index;
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f->flags = V4L2_FMT_FLAG_COMPRESSED;
	switch(index) {
	case 0:
		strcpy(f->description, "MJPEG");
		f->pixelformat = V4L2_PIX_FMT_MJPEG;
		break;
	case 1:
		strcpy(f->description, "JPEG");
		f->pixelformat = V4L2_PIX_FMT_JPEG;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/******************************************************************************
 *
 *  ioctl_try_fmt
 *
 *  V4L2 format try
 *
 *****************************************************************************/

static int cpia2_try_fmt_vid_cap(struct file *file, void *fh,
					  struct v4l2_format *f)
{
	struct camera_data *cam = video_drvdata(file);

	if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG &&
	    f->fmt.pix.pixelformat != V4L2_PIX_FMT_JPEG)
	       return -EINVAL;

	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage = cam->frame_size;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	f->fmt.pix.priv = 0;

	switch (cpia2_match_video_size(f->fmt.pix.width, f->fmt.pix.height)) {
	case VIDEOSIZE_VGA:
		f->fmt.pix.width = 640;
		f->fmt.pix.height = 480;
		break;
	case VIDEOSIZE_CIF:
		f->fmt.pix.width = 352;
		f->fmt.pix.height = 288;
		break;
	case VIDEOSIZE_QVGA:
		f->fmt.pix.width = 320;
		f->fmt.pix.height = 240;
		break;
	case VIDEOSIZE_288_216:
		f->fmt.pix.width = 288;
		f->fmt.pix.height = 216;
		break;
	case VIDEOSIZE_256_192:
		f->fmt.pix.width = 256;
		f->fmt.pix.height = 192;
		break;
	case VIDEOSIZE_224_168:
		f->fmt.pix.width = 224;
		f->fmt.pix.height = 168;
		break;
	case VIDEOSIZE_192_144:
		f->fmt.pix.width = 192;
		f->fmt.pix.height = 144;
		break;
	case VIDEOSIZE_QCIF:
	default:
		f->fmt.pix.width = 176;
		f->fmt.pix.height = 144;
		break;
	}

	return 0;
}

/******************************************************************************
 *
 *  ioctl_set_fmt
 *
 *  V4L2 format set
 *
 *****************************************************************************/

static int cpia2_s_fmt_vid_cap(struct file *file, void *_fh,
					struct v4l2_format *f)
{
	struct camera_data *cam = video_drvdata(file);
	int err, frame;

	err = cpia2_try_fmt_vid_cap(file, _fh, f);
	if(err != 0)
		return err;

	cam->pixelformat = f->fmt.pix.pixelformat;

	/* NOTE: This should be set to 1 for MJPEG, but some apps don't handle
	 * the missing Huffman table properly. */
	cam->params.compression.inhibit_htables = 0;
		/*f->fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG;*/

	/* we set the video window to something smaller or equal to what
	 * is requested by the user???
	 */
	DBG("Requested width = %d, height = %d\n",
	    f->fmt.pix.width, f->fmt.pix.height);
	if (f->fmt.pix.width != cam->width ||
	    f->fmt.pix.height != cam->height) {
		cam->width = f->fmt.pix.width;
		cam->height = f->fmt.pix.height;
		cam->params.roi.width = f->fmt.pix.width;
		cam->params.roi.height = f->fmt.pix.height;
		cpia2_set_format(cam);
	}

	for (frame = 0; frame < cam->num_frames; ++frame) {
		if (cam->buffers[frame].status == FRAME_READING)
			if ((err = sync(cam, frame)) < 0)
				return err;

		cam->buffers[frame].status = FRAME_EMPTY;
	}

	return 0;
}

/******************************************************************************
 *
 *  ioctl_get_fmt
 *
 *  V4L2 format get
 *
 *****************************************************************************/

static int cpia2_g_fmt_vid_cap(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct camera_data *cam = video_drvdata(file);

	f->fmt.pix.width = cam->width;
	f->fmt.pix.height = cam->height;
	f->fmt.pix.pixelformat = cam->pixelformat;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage = cam->frame_size;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG;
	f->fmt.pix.priv = 0;

	return 0;
}

/******************************************************************************
 *
 *  ioctl_cropcap
 *
 *  V4L2 query cropping capabilities
 *  NOTE: cropping is currently disabled
 *
 *****************************************************************************/

static int cpia2_cropcap(struct file *file, void *fh, struct v4l2_cropcap *c)
{
	struct camera_data *cam = video_drvdata(file);

	if (c->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
	       return -EINVAL;

	c->bounds.left = 0;
	c->bounds.top = 0;
	c->bounds.width = cam->width;
	c->bounds.height = cam->height;
	c->defrect.left = 0;
	c->defrect.top = 0;
	c->defrect.width = cam->width;
	c->defrect.height = cam->height;
	c->pixelaspect.numerator = 1;
	c->pixelaspect.denominator = 1;

	return 0;
}

struct framerate_info {
	int value;
	struct v4l2_fract period;
};

static const struct framerate_info framerate_controls[] = {
	{ CPIA2_VP_FRAMERATE_6_25, { 4, 25 } },
	{ CPIA2_VP_FRAMERATE_7_5,  { 2, 15 } },
	{ CPIA2_VP_FRAMERATE_12_5, { 2, 25 } },
	{ CPIA2_VP_FRAMERATE_15,   { 1, 15 } },
	{ CPIA2_VP_FRAMERATE_25,   { 1, 25 } },
	{ CPIA2_VP_FRAMERATE_30,   { 1, 30 } },
};

static int cpia2_g_parm(struct file *file, void *fh, struct v4l2_streamparm *p)
{
	struct camera_data *cam = video_drvdata(file);
	struct v4l2_captureparm *cap = &p->parm.capture;
	int i;

	if (p->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	cap->capability = V4L2_CAP_TIMEPERFRAME;
	cap->readbuffers = cam->num_frames;
	for (i = 0; i < ARRAY_SIZE(framerate_controls); i++)
		if (cam->params.vp_params.frame_rate == framerate_controls[i].value) {
			cap->timeperframe = framerate_controls[i].period;
			break;
		}
	return 0;
}

static int cpia2_s_parm(struct file *file, void *fh, struct v4l2_streamparm *p)
{
	struct camera_data *cam = video_drvdata(file);
	struct v4l2_captureparm *cap = &p->parm.capture;
	struct v4l2_fract tpf = cap->timeperframe;
	int max = ARRAY_SIZE(framerate_controls) - 1;
	int ret;
	int i;

	ret = cpia2_g_parm(file, fh, p);
	if (ret || !tpf.denominator || !tpf.numerator)
		return ret;

	/* Maximum 15 fps for this model */
	if (cam->params.pnp_id.device_type == DEVICE_STV_672 &&
	    cam->params.version.sensor_flags == CPIA2_VP_SENSOR_FLAGS_500)
		max -= 2;
	for (i = 0; i <= max; i++) {
		struct v4l2_fract f1 = tpf;
		struct v4l2_fract f2 = framerate_controls[i].period;

		f1.numerator *= f2.denominator;
		f2.numerator *= f1.denominator;
		if (f1.numerator >= f2.numerator)
			break;
	}
	if (i > max)
		i = max;
	cap->timeperframe = framerate_controls[i].period;
	return cpia2_set_fps(cam, framerate_controls[i].value);
}

static const struct {
	u32 width;
	u32 height;
} cpia2_framesizes[] = {
	{ 640, 480 },
	{ 352, 288 },
	{ 320, 240 },
	{ 288, 216 },
	{ 256, 192 },
	{ 224, 168 },
	{ 192, 144 },
	{ 176, 144 },
};

static int cpia2_enum_framesizes(struct file *file, void *fh,
					 struct v4l2_frmsizeenum *fsize)
{

	if (fsize->pixel_format != V4L2_PIX_FMT_MJPEG &&
	    fsize->pixel_format != V4L2_PIX_FMT_JPEG)
		return -EINVAL;
	if (fsize->index >= ARRAY_SIZE(cpia2_framesizes))
		return -EINVAL;
	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = cpia2_framesizes[fsize->index].width;
	fsize->discrete.height = cpia2_framesizes[fsize->index].height;

	return 0;
}

static int cpia2_enum_frameintervals(struct file *file, void *fh,
					   struct v4l2_frmivalenum *fival)
{
	struct camera_data *cam = video_drvdata(file);
	int max = ARRAY_SIZE(framerate_controls) - 1;
	int i;

	if (fival->pixel_format != V4L2_PIX_FMT_MJPEG &&
	    fival->pixel_format != V4L2_PIX_FMT_JPEG)
		return -EINVAL;

	/* Maximum 15 fps for this model */
	if (cam->params.pnp_id.device_type == DEVICE_STV_672 &&
	    cam->params.version.sensor_flags == CPIA2_VP_SENSOR_FLAGS_500)
		max -= 2;
	if (fival->index > max)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(cpia2_framesizes); i++)
		if (fival->width == cpia2_framesizes[i].width &&
		    fival->height == cpia2_framesizes[i].height)
			break;
	if (i == ARRAY_SIZE(cpia2_framesizes))
		return -EINVAL;
	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete = framerate_controls[fival->index].period;
	return 0;
}

/******************************************************************************
 *
 *  ioctl_s_ctrl
 *
 *  V4L2 set the value of a control variable
 *
 *****************************************************************************/

static int cpia2_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct camera_data *cam =
		container_of(ctrl->handler, struct camera_data, hdl);
	static const int flicker_table[] = {
		NEVER_FLICKER,
		FLICKER_50,
		FLICKER_60,
	};

	DBG("Set control id:%d, value:%d\n", ctrl->id, ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		cpia2_set_brightness(cam, ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		cpia2_set_contrast(cam, ctrl->val);
		break;
	case V4L2_CID_SATURATION:
		cpia2_set_saturation(cam, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		cpia2_set_property_mirror(cam, ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		cpia2_set_property_flip(cam, ctrl->val);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY:
		return cpia2_set_flicker_mode(cam, flicker_table[ctrl->val]);
	case V4L2_CID_ILLUMINATORS_1:
		return cpia2_set_gpio(cam, (cam->top_light->val << 6) |
					   (cam->bottom_light->val << 7));
	case V4L2_CID_JPEG_ACTIVE_MARKER:
		cam->params.compression.inhibit_htables =
			!(ctrl->val & V4L2_JPEG_ACTIVE_MARKER_DHT);
		break;
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		cam->params.vc_params.quality = ctrl->val;
		break;
	case CPIA2_CID_USB_ALT:
		cam->params.camera_state.stream_mode = ctrl->val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/******************************************************************************
 *
 *  ioctl_g_jpegcomp
 *
 *  V4L2 get the JPEG compression parameters
 *
 *****************************************************************************/

static int cpia2_g_jpegcomp(struct file *file, void *fh, struct v4l2_jpegcompression *parms)
{
	struct camera_data *cam = video_drvdata(file);

	memset(parms, 0, sizeof(*parms));

	parms->quality = 80; // TODO: Can this be made meaningful?

	parms->jpeg_markers = V4L2_JPEG_MARKER_DQT | V4L2_JPEG_MARKER_DRI;
	if(!cam->params.compression.inhibit_htables) {
		parms->jpeg_markers |= V4L2_JPEG_MARKER_DHT;
	}

	parms->APPn = cam->APPn;
	parms->APP_len = cam->APP_len;
	if(cam->APP_len > 0) {
		memcpy(parms->APP_data, cam->APP_data, cam->APP_len);
		parms->jpeg_markers |= V4L2_JPEG_MARKER_APP;
	}

	parms->COM_len = cam->COM_len;
	if(cam->COM_len > 0) {
		memcpy(parms->COM_data, cam->COM_data, cam->COM_len);
		parms->jpeg_markers |= JPEG_MARKER_COM;
	}

	DBG("G_JPEGCOMP APP_len:%d COM_len:%d\n",
	    parms->APP_len, parms->COM_len);

	return 0;
}

/******************************************************************************
 *
 *  ioctl_s_jpegcomp
 *
 *  V4L2 set the JPEG compression parameters
 *  NOTE: quality and some jpeg_markers are ignored.
 *
 *****************************************************************************/

static int cpia2_s_jpegcomp(struct file *file, void *fh,
		const struct v4l2_jpegcompression *parms)
{
	struct camera_data *cam = video_drvdata(file);

	DBG("S_JPEGCOMP APP_len:%d COM_len:%d\n",
	    parms->APP_len, parms->COM_len);

	cam->params.compression.inhibit_htables =
		!(parms->jpeg_markers & V4L2_JPEG_MARKER_DHT);

	if(parms->APP_len != 0) {
		if(parms->APP_len > 0 &&
		   parms->APP_len <= sizeof(cam->APP_data) &&
		   parms->APPn >= 0 && parms->APPn <= 15) {
			cam->APPn = parms->APPn;
			cam->APP_len = parms->APP_len;
			memcpy(cam->APP_data, parms->APP_data, parms->APP_len);
		} else {
			LOG("Bad APPn Params n=%d len=%d\n",
			    parms->APPn, parms->APP_len);
			return -EINVAL;
		}
	} else {
		cam->APP_len = 0;
	}

	if(parms->COM_len != 0) {
		if(parms->COM_len > 0 &&
		   parms->COM_len <= sizeof(cam->COM_data)) {
			cam->COM_len = parms->COM_len;
			memcpy(cam->COM_data, parms->COM_data, parms->COM_len);
		} else {
			LOG("Bad COM_len=%d\n", parms->COM_len);
			return -EINVAL;
		}
	}

	return 0;
}

/******************************************************************************
 *
 *  ioctl_reqbufs
 *
 *  V4L2 Initiate memory mapping.
 *  NOTE: The user's request is ignored. For now the buffers are fixed.
 *
 *****************************************************************************/

static int cpia2_reqbufs(struct file *file, void *fh, struct v4l2_requestbuffers *req)
{
	struct camera_data *cam = video_drvdata(file);

	if(req->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	   req->memory != V4L2_MEMORY_MMAP)
		return -EINVAL;

	DBG("REQBUFS requested:%d returning:%d\n", req->count, cam->num_frames);
	req->count = cam->num_frames;
	memset(&req->reserved, 0, sizeof(req->reserved));

	return 0;
}

/******************************************************************************
 *
 *  ioctl_querybuf
 *
 *  V4L2 Query memory buffer status.
 *
 *****************************************************************************/

static int cpia2_querybuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	struct camera_data *cam = video_drvdata(file);

	if(buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	   buf->index >= cam->num_frames)
		return -EINVAL;

	buf->m.offset = cam->buffers[buf->index].data - cam->frame_buffer;
	buf->length = cam->frame_size;

	buf->memory = V4L2_MEMORY_MMAP;

	if(cam->mmapped)
		buf->flags = V4L2_BUF_FLAG_MAPPED;
	else
		buf->flags = 0;

	buf->flags |= V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;

	switch (cam->buffers[buf->index].status) {
	case FRAME_EMPTY:
	case FRAME_ERROR:
	case FRAME_READING:
		buf->bytesused = 0;
		buf->flags = V4L2_BUF_FLAG_QUEUED;
		break;
	case FRAME_READY:
		buf->bytesused = cam->buffers[buf->index].length;
		buf->timestamp = cam->buffers[buf->index].timestamp;
		buf->sequence = cam->buffers[buf->index].seq;
		buf->flags = V4L2_BUF_FLAG_DONE;
		break;
	}

	DBG("QUERYBUF index:%d offset:%d flags:%d seq:%d bytesused:%d\n",
	     buf->index, buf->m.offset, buf->flags, buf->sequence,
	     buf->bytesused);

	return 0;
}

/******************************************************************************
 *
 *  ioctl_qbuf
 *
 *  V4L2 User is freeing buffer
 *
 *****************************************************************************/

static int cpia2_qbuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	struct camera_data *cam = video_drvdata(file);

	if(buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	   buf->memory != V4L2_MEMORY_MMAP ||
	   buf->index >= cam->num_frames)
		return -EINVAL;

	DBG("QBUF #%d\n", buf->index);

	if(cam->buffers[buf->index].status == FRAME_READY)
		cam->buffers[buf->index].status = FRAME_EMPTY;

	return 0;
}

/******************************************************************************
 *
 *  find_earliest_filled_buffer
 *
 *  Helper for ioctl_dqbuf. Find the next ready buffer.
 *
 *****************************************************************************/

static int find_earliest_filled_buffer(struct camera_data *cam)
{
	int i;
	int found = -1;
	for (i=0; i<cam->num_frames; i++) {
		if(cam->buffers[i].status == FRAME_READY) {
			if(found < 0) {
				found = i;
			} else {
				/* find which buffer is earlier */
				struct timeval *tv1, *tv2;
				tv1 = &cam->buffers[i].timestamp;
				tv2 = &cam->buffers[found].timestamp;
				if(tv1->tv_sec < tv2->tv_sec ||
				   (tv1->tv_sec == tv2->tv_sec &&
				    tv1->tv_usec < tv2->tv_usec))
					found = i;
			}
		}
	}
	return found;
}

/******************************************************************************
 *
 *  ioctl_dqbuf
 *
 *  V4L2 User is asking for a filled buffer.
 *
 *****************************************************************************/

static int cpia2_dqbuf(struct file *file, void *fh, struct v4l2_buffer *buf)
{
	struct camera_data *cam = video_drvdata(file);
	int frame;

	if(buf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE ||
	   buf->memory != V4L2_MEMORY_MMAP)
		return -EINVAL;

	frame = find_earliest_filled_buffer(cam);

	if(frame < 0 && file->f_flags&O_NONBLOCK)
		return -EAGAIN;

	if(frame < 0) {
		/* Wait for a frame to become available */
		struct framebuf *cb=cam->curbuff;
		mutex_unlock(&cam->v4l2_lock);
		wait_event_interruptible(cam->wq_stream,
					 !video_is_registered(&cam->vdev) ||
					 (cb=cam->curbuff)->status == FRAME_READY);
		mutex_lock(&cam->v4l2_lock);
		if (signal_pending(current))
			return -ERESTARTSYS;
		if (!video_is_registered(&cam->vdev))
			return -ENOTTY;
		frame = cb->num;
	}


	buf->index = frame;
	buf->bytesused = cam->buffers[buf->index].length;
	buf->flags = V4L2_BUF_FLAG_MAPPED | V4L2_BUF_FLAG_DONE
		| V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	buf->field = V4L2_FIELD_NONE;
	buf->timestamp = cam->buffers[buf->index].timestamp;
	buf->sequence = cam->buffers[buf->index].seq;
	buf->m.offset = cam->buffers[buf->index].data - cam->frame_buffer;
	buf->length = cam->frame_size;
	buf->config_store = 0;
	buf->reserved = 0;
	memset(&buf->timecode, 0, sizeof(buf->timecode));

	DBG("DQBUF #%d status:%d seq:%d length:%d\n", buf->index,
	    cam->buffers[buf->index].status, buf->sequence, buf->bytesused);

	return 0;
}

static int cpia2_streamon(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct camera_data *cam = video_drvdata(file);
	int ret = -EINVAL;

	DBG("VIDIOC_STREAMON, streaming=%d\n", cam->streaming);
	if (!cam->mmapped || type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (!cam->streaming) {
		ret = cpia2_usb_stream_start(cam,
				cam->params.camera_state.stream_mode);
		if (!ret)
			v4l2_ctrl_grab(cam->usb_alt, true);
	}
	return ret;
}

static int cpia2_streamoff(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct camera_data *cam = video_drvdata(file);
	int ret = -EINVAL;

	DBG("VIDIOC_STREAMOFF, streaming=%d\n", cam->streaming);
	if (!cam->mmapped || type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (cam->streaming) {
		ret = cpia2_usb_stream_stop(cam);
		if (!ret)
			v4l2_ctrl_grab(cam->usb_alt, false);
	}
	return ret;
}

/******************************************************************************
 *
 *  cpia2_mmap
 *
 *****************************************************************************/
static int cpia2_mmap(struct file *file, struct vm_area_struct *area)
{
	struct camera_data *cam = video_drvdata(file);
	int retval;

	if (mutex_lock_interruptible(&cam->v4l2_lock))
		return -ERESTARTSYS;
	retval = cpia2_remap_buffer(cam, area);

	if(!retval)
		cam->stream_fh = file->private_data;
	mutex_unlock(&cam->v4l2_lock);
	return retval;
}

/******************************************************************************
 *
 *  reset_camera_struct_v4l
 *
 *  Sets all values to the defaults
 *****************************************************************************/
static void reset_camera_struct_v4l(struct camera_data *cam)
{
	cam->width = cam->params.roi.width;
	cam->height = cam->params.roi.height;

	cam->frame_size = buffer_size;
	cam->num_frames = num_buffers;

	/* Flicker modes */
	cam->params.flicker_control.flicker_mode_req = flicker_mode;

	/* stream modes */
	cam->params.camera_state.stream_mode = alternate;

	cam->pixelformat = V4L2_PIX_FMT_JPEG;
}

static const struct v4l2_ioctl_ops cpia2_ioctl_ops = {
	.vidioc_querycap		    = cpia2_querycap,
	.vidioc_enum_input		    = cpia2_enum_input,
	.vidioc_g_input			    = cpia2_g_input,
	.vidioc_s_input			    = cpia2_s_input,
	.vidioc_enum_fmt_vid_cap	    = cpia2_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap		    = cpia2_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap		    = cpia2_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		    = cpia2_try_fmt_vid_cap,
	.vidioc_g_jpegcomp		    = cpia2_g_jpegcomp,
	.vidioc_s_jpegcomp		    = cpia2_s_jpegcomp,
	.vidioc_cropcap			    = cpia2_cropcap,
	.vidioc_reqbufs			    = cpia2_reqbufs,
	.vidioc_querybuf		    = cpia2_querybuf,
	.vidioc_qbuf			    = cpia2_qbuf,
	.vidioc_dqbuf			    = cpia2_dqbuf,
	.vidioc_streamon		    = cpia2_streamon,
	.vidioc_streamoff		    = cpia2_streamoff,
	.vidioc_s_parm			    = cpia2_s_parm,
	.vidioc_g_parm			    = cpia2_g_parm,
	.vidioc_enum_framesizes		    = cpia2_enum_framesizes,
	.vidioc_enum_frameintervals	    = cpia2_enum_frameintervals,
	.vidioc_subscribe_event		    = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	    = v4l2_event_unsubscribe,
};

/***
 * The v4l video device structure initialized for this device
 ***/
static const struct v4l2_file_operations cpia2_fops = {
	.owner		= THIS_MODULE,
	.open		= cpia2_open,
	.release	= cpia2_close,
	.read		= cpia2_v4l_read,
	.poll		= cpia2_v4l_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= cpia2_mmap,
};

static struct video_device cpia2_template = {
	/* I could not find any place for the old .initialize initializer?? */
	.name =		"CPiA2 Camera",
	.fops =		&cpia2_fops,
	.ioctl_ops =	&cpia2_ioctl_ops,
	.release =	video_device_release_empty,
};

void cpia2_camera_release(struct v4l2_device *v4l2_dev)
{
	struct camera_data *cam =
		container_of(v4l2_dev, struct camera_data, v4l2_dev);

	v4l2_ctrl_handler_free(&cam->hdl);
	v4l2_device_unregister(&cam->v4l2_dev);
	kfree(cam);
}

static const struct v4l2_ctrl_ops cpia2_ctrl_ops = {
	.s_ctrl = cpia2_s_ctrl,
};

/******************************************************************************
 *
 *  cpia2_register_camera
 *
 *****************************************************************************/
int cpia2_register_camera(struct camera_data *cam)
{
	struct v4l2_ctrl_handler *hdl = &cam->hdl;
	struct v4l2_ctrl_config cpia2_usb_alt = {
		.ops = &cpia2_ctrl_ops,
		.id = CPIA2_CID_USB_ALT,
		.name = "USB Alternate",
		.type = V4L2_CTRL_TYPE_INTEGER,
		.min = USBIF_ISO_1,
		.max = USBIF_ISO_6,
		.step = 1,
	};
	int ret;

	v4l2_ctrl_handler_init(hdl, 12);
	v4l2_ctrl_new_std(hdl, &cpia2_ctrl_ops,
			V4L2_CID_BRIGHTNESS,
			cam->params.pnp_id.device_type == DEVICE_STV_672 ? 1 : 0,
			255, 1, DEFAULT_BRIGHTNESS);
	v4l2_ctrl_new_std(hdl, &cpia2_ctrl_ops,
			V4L2_CID_CONTRAST, 0, 255, 1, DEFAULT_CONTRAST);
	v4l2_ctrl_new_std(hdl, &cpia2_ctrl_ops,
			V4L2_CID_SATURATION, 0, 255, 1, DEFAULT_SATURATION);
	v4l2_ctrl_new_std(hdl, &cpia2_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(hdl, &cpia2_ctrl_ops,
			V4L2_CID_JPEG_ACTIVE_MARKER, 0,
			V4L2_JPEG_ACTIVE_MARKER_DHT, 0,
			V4L2_JPEG_ACTIVE_MARKER_DHT);
	v4l2_ctrl_new_std(hdl, &cpia2_ctrl_ops,
			V4L2_CID_JPEG_COMPRESSION_QUALITY, 1,
			100, 1, 100);
	cpia2_usb_alt.def = alternate;
	cam->usb_alt = v4l2_ctrl_new_custom(hdl, &cpia2_usb_alt, NULL);
	/* VP5 Only */
	if (cam->params.pnp_id.device_type != DEVICE_STV_672)
		v4l2_ctrl_new_std(hdl, &cpia2_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);
	/* Flicker control only valid for 672 */
	if (cam->params.pnp_id.device_type == DEVICE_STV_672)
		v4l2_ctrl_new_std_menu(hdl, &cpia2_ctrl_ops,
			V4L2_CID_POWER_LINE_FREQUENCY,
			V4L2_CID_POWER_LINE_FREQUENCY_60HZ, 0, 0);
	/* Light control only valid for the QX5 Microscope */
	if (cam->params.pnp_id.product == 0x151) {
		cam->top_light = v4l2_ctrl_new_std(hdl, &cpia2_ctrl_ops,
				V4L2_CID_ILLUMINATORS_1, 0, 1, 1, 0);
		cam->bottom_light = v4l2_ctrl_new_std(hdl, &cpia2_ctrl_ops,
				V4L2_CID_ILLUMINATORS_2, 0, 1, 1, 0);
		v4l2_ctrl_cluster(2, &cam->top_light);
	}

	if (hdl->error) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}

	cam->vdev = cpia2_template;
	video_set_drvdata(&cam->vdev, cam);
	cam->vdev.lock = &cam->v4l2_lock;
	cam->vdev.ctrl_handler = hdl;
	cam->vdev.v4l2_dev = &cam->v4l2_dev;

	reset_camera_struct_v4l(cam);

	/* register v4l device */
	if (video_register_device(&cam->vdev, VFL_TYPE_GRABBER, video_nr) < 0) {
		ERR("video_register_device failed\n");
		return -ENODEV;
	}

	return 0;
}

/******************************************************************************
 *
 *  cpia2_unregister_camera
 *
 *****************************************************************************/
void cpia2_unregister_camera(struct camera_data *cam)
{
	video_unregister_device(&cam->vdev);
}

/******************************************************************************
 *
 *  check_parameters
 *
 *  Make sure that all user-supplied parameters are sensible
 *****************************************************************************/
static void __init check_parameters(void)
{
	if(buffer_size < PAGE_SIZE) {
		buffer_size = PAGE_SIZE;
		LOG("buffer_size too small, setting to %d\n", buffer_size);
	} else if(buffer_size > 1024*1024) {
		/* arbitrary upper limiit */
		buffer_size = 1024*1024;
		LOG("buffer_size ridiculously large, setting to %d\n",
		    buffer_size);
	} else {
		buffer_size += PAGE_SIZE-1;
		buffer_size &= ~(PAGE_SIZE-1);
	}

	if(num_buffers < 1) {
		num_buffers = 1;
		LOG("num_buffers too small, setting to %d\n", num_buffers);
	} else if(num_buffers > VIDEO_MAX_FRAME) {
		num_buffers = VIDEO_MAX_FRAME;
		LOG("num_buffers too large, setting to %d\n", num_buffers);
	}

	if(alternate < USBIF_ISO_1 || alternate > USBIF_ISO_6) {
		alternate = DEFAULT_ALT;
		LOG("alternate specified is invalid, using %d\n", alternate);
	}

	if (flicker_mode != 0 && flicker_mode != FLICKER_50 && flicker_mode != FLICKER_60) {
		flicker_mode = 0;
		LOG("Flicker mode specified is invalid, using %d\n",
		    flicker_mode);
	}

	DBG("Using %d buffers, each %d bytes, alternate=%d\n",
	    num_buffers, buffer_size, alternate);
}

/************   Module Stuff ***************/


/******************************************************************************
 *
 * cpia2_init/module_init
 *
 *****************************************************************************/
static int __init cpia2_init(void)
{
	LOG("%s v%s\n",
	    ABOUT, CPIA_VERSION);
	check_parameters();
	cpia2_usb_init();
	return 0;
}


/******************************************************************************
 *
 * cpia2_exit/module_exit
 *
 *****************************************************************************/
static void __exit cpia2_exit(void)
{
	cpia2_usb_cleanup();
	schedule_timeout(2 * HZ);
}

module_init(cpia2_init);
module_exit(cpia2_exit);
