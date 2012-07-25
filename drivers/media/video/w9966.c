/*
	Winbond w9966cf Webcam parport driver.

	Version 0.33

	Copyright (C) 2001 Jakob Kemi <jakob.kemi@post.utfors.se>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
/*
	Supported devices:
	*Lifeview FlyCam Supra (using the Philips saa7111a chip)

	Does any other model using the w9966 interface chip exist ?

	Todo:

	*Add a working EPP mode, since DMA ECP read isn't implemented
	in the parport drivers. (That's why it's so sloow)

	*Add support for other ccd-control chips than the saa7111
	please send me feedback on what kind of chips you have.

	*Add proper probing. I don't know what's wrong with the IEEE1284
	parport drivers but (IEEE1284_MODE_NIBBLE|IEEE1284_DEVICE_ID)
	and nibble read seems to be broken for some peripherals.

	*Add probing for onboard SRAM, port directions etc. (if possible)

	*Add support for the hardware compressed modes (maybe using v4l2)

	*Fix better support for the capture window (no skewed images, v4l
	interface to capt. window)

	*Probably some bugs that I don't know of

	Please support me by sending feedback!

	Changes:

	Alan Cox:	Removed RGB mode for kernel merge, added THIS_MODULE
			and owner support for newer module locks
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/slab.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <linux/parport.h>

/*#define DEBUG*/				/* Undef me for production */

#ifdef DEBUG
#define DPRINTF(x, a...) printk(KERN_DEBUG "W9966: %s(): "x, __func__ , ##a)
#else
#define DPRINTF(x...)
#endif

/*
 *	Defines, simple typedefs etc.
 */

#define W9966_DRIVERNAME	"W9966CF Webcam"
#define W9966_MAXCAMS		4	/* Maximum number of cameras */
#define W9966_RBUFFER		2048	/* Read buffer (must be an even number) */
#define W9966_SRAMSIZE		131072	/* 128kb */
#define W9966_SRAMID		0x02	/* check w9966cf.pdf */

/* Empirically determined window limits */
#define W9966_WND_MIN_X		16
#define W9966_WND_MIN_Y		14
#define W9966_WND_MAX_X		705
#define W9966_WND_MAX_Y		253
#define W9966_WND_MAX_W		(W9966_WND_MAX_X - W9966_WND_MIN_X)
#define W9966_WND_MAX_H		(W9966_WND_MAX_Y - W9966_WND_MIN_Y)

/* Keep track of our current state */
#define W9966_STATE_PDEV	0x01
#define W9966_STATE_CLAIMED	0x02
#define W9966_STATE_VDEV	0x04

#define W9966_I2C_W_ID		0x48
#define W9966_I2C_R_ID		0x49
#define W9966_I2C_R_DATA	0x08
#define W9966_I2C_R_CLOCK	0x04
#define W9966_I2C_W_DATA	0x02
#define W9966_I2C_W_CLOCK	0x01

struct w9966 {
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler hdl;
	unsigned char dev_state;
	unsigned char i2c_state;
	unsigned short ppmode;
	struct parport *pport;
	struct pardevice *pdev;
	struct video_device vdev;
	unsigned short width;
	unsigned short height;
	unsigned char brightness;
	signed char contrast;
	signed char color;
	signed char hue;
	struct mutex lock;
};

/*
 *	Module specific properties
 */

MODULE_AUTHOR("Jakob Kemi <jakob.kemi@post.utfors.se>");
MODULE_DESCRIPTION("Winbond w9966cf WebCam driver (0.32)");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.33.1");

#ifdef MODULE
static char *pardev[] = {[0 ... W9966_MAXCAMS] = ""};
#else
static char *pardev[] = {[0 ... W9966_MAXCAMS] = "aggressive"};
#endif
module_param_array(pardev, charp, NULL, 0);
MODULE_PARM_DESC(pardev, "pardev: where to search for\n"
		"\teach camera. 'aggressive' means brute-force search.\n"
		"\tEg: >pardev=parport3,aggressive,parport2,parport1< would assign\n"
		"\tcam 1 to parport3 and search every parport for cam 2 etc...");

static int parmode;
module_param(parmode, int, 0);
MODULE_PARM_DESC(parmode, "parmode: transfer mode (0=auto, 1=ecp, 2=epp");

static int video_nr = -1;
module_param(video_nr, int, 0);

static struct w9966 w9966_cams[W9966_MAXCAMS];

/*
 *	Private function defines
 */


/* Set camera phase flags, so we know what to uninit when terminating */
static inline void w9966_set_state(struct w9966 *cam, int mask, int val)
{
	cam->dev_state = (cam->dev_state & ~mask) ^ val;
}

/* Get camera phase flags */
static inline int w9966_get_state(struct w9966 *cam, int mask, int val)
{
	return ((cam->dev_state & mask) == val);
}

/* Claim parport for ourself */
static void w9966_pdev_claim(struct w9966 *cam)
{
	if (w9966_get_state(cam, W9966_STATE_CLAIMED, W9966_STATE_CLAIMED))
		return;
	parport_claim_or_block(cam->pdev);
	w9966_set_state(cam, W9966_STATE_CLAIMED, W9966_STATE_CLAIMED);
}

/* Release parport for others to use */
static void w9966_pdev_release(struct w9966 *cam)
{
	if (w9966_get_state(cam, W9966_STATE_CLAIMED, 0))
		return;
	parport_release(cam->pdev);
	w9966_set_state(cam, W9966_STATE_CLAIMED, 0);
}

/* Read register from W9966 interface-chip
   Expects a claimed pdev
   -1 on error, else register data (byte) */
static int w9966_read_reg(struct w9966 *cam, int reg)
{
	/* ECP, read, regtransfer, REG, REG, REG, REG, REG */
	const unsigned char addr = 0x80 | (reg & 0x1f);
	unsigned char val;

	if (parport_negotiate(cam->pport, cam->ppmode | IEEE1284_ADDR) != 0)
		return -1;
	if (parport_write(cam->pport, &addr, 1) != 1)
		return -1;
	if (parport_negotiate(cam->pport, cam->ppmode | IEEE1284_DATA) != 0)
		return -1;
	if (parport_read(cam->pport, &val, 1) != 1)
		return -1;

	return val;
}

/* Write register to W9966 interface-chip
   Expects a claimed pdev
   -1 on error */
static int w9966_write_reg(struct w9966 *cam, int reg, int data)
{
	/* ECP, write, regtransfer, REG, REG, REG, REG, REG */
	const unsigned char addr = 0xc0 | (reg & 0x1f);
	const unsigned char val = data;

	if (parport_negotiate(cam->pport, cam->ppmode | IEEE1284_ADDR) != 0)
		return -1;
	if (parport_write(cam->pport, &addr, 1) != 1)
		return -1;
	if (parport_negotiate(cam->pport, cam->ppmode | IEEE1284_DATA) != 0)
		return -1;
	if (parport_write(cam->pport, &val, 1) != 1)
		return -1;

	return 0;
}

/*
 *	Ugly and primitive i2c protocol functions
 */

/* Sets the data line on the i2c bus.
   Expects a claimed pdev. */
static void w9966_i2c_setsda(struct w9966 *cam, int state)
{
	if (state)
		cam->i2c_state |= W9966_I2C_W_DATA;
	else
		cam->i2c_state &= ~W9966_I2C_W_DATA;

	w9966_write_reg(cam, 0x18, cam->i2c_state);
	udelay(5);
}

/* Get peripheral clock line
   Expects a claimed pdev. */
static int w9966_i2c_getscl(struct w9966 *cam)
{
	const unsigned char state = w9966_read_reg(cam, 0x18);
	return ((state & W9966_I2C_R_CLOCK) > 0);
}

/* Sets the clock line on the i2c bus.
   Expects a claimed pdev. -1 on error */
static int w9966_i2c_setscl(struct w9966 *cam, int state)
{
	unsigned long timeout;

	if (state)
		cam->i2c_state |= W9966_I2C_W_CLOCK;
	else
		cam->i2c_state &= ~W9966_I2C_W_CLOCK;

	w9966_write_reg(cam, 0x18, cam->i2c_state);
	udelay(5);

	/* we go to high, we also expect the peripheral to ack. */
	if (state) {
		timeout = jiffies + 100;
		while (!w9966_i2c_getscl(cam)) {
			if (time_after(jiffies, timeout))
				return -1;
		}
	}
	return 0;
}

#if 0
/* Get peripheral data line
   Expects a claimed pdev. */
static int w9966_i2c_getsda(struct w9966 *cam)
{
	const unsigned char state = w9966_read_reg(cam, 0x18);
	return ((state & W9966_I2C_R_DATA) > 0);
}
#endif

/* Write a byte with ack to the i2c bus.
   Expects a claimed pdev. -1 on error */
static int w9966_i2c_wbyte(struct w9966 *cam, int data)
{
	int i;

	for (i = 7; i >= 0; i--) {
		w9966_i2c_setsda(cam, (data >> i) & 0x01);

		if (w9966_i2c_setscl(cam, 1) == -1)
			return -1;
		w9966_i2c_setscl(cam, 0);
	}

	w9966_i2c_setsda(cam, 1);

	if (w9966_i2c_setscl(cam, 1) == -1)
		return -1;
	w9966_i2c_setscl(cam, 0);

	return 0;
}

/* Read a data byte with ack from the i2c-bus
   Expects a claimed pdev. -1 on error */
#if 0
static int w9966_i2c_rbyte(struct w9966 *cam)
{
	unsigned char data = 0x00;
	int i;

	w9966_i2c_setsda(cam, 1);

	for (i = 0; i < 8; i++) {
		if (w9966_i2c_setscl(cam, 1) == -1)
			return -1;
		data = data << 1;
		if (w9966_i2c_getsda(cam))
			data |= 0x01;

		w9966_i2c_setscl(cam, 0);
	}
	return data;
}
#endif

/* Read a register from the i2c device.
   Expects claimed pdev. -1 on error */
#if 0
static int w9966_read_reg_i2c(struct w9966 *cam, int reg)
{
	int data;

	w9966_i2c_setsda(cam, 0);
	w9966_i2c_setscl(cam, 0);

	if (w9966_i2c_wbyte(cam, W9966_I2C_W_ID) == -1 ||
	    w9966_i2c_wbyte(cam, reg) == -1)
		return -1;

	w9966_i2c_setsda(cam, 1);
	if (w9966_i2c_setscl(cam, 1) == -1)
		return -1;
	w9966_i2c_setsda(cam, 0);
	w9966_i2c_setscl(cam, 0);

	if (w9966_i2c_wbyte(cam, W9966_I2C_R_ID) == -1)
		return -1;
	data = w9966_i2c_rbyte(cam);
	if (data == -1)
		return -1;

	w9966_i2c_setsda(cam, 0);

	if (w9966_i2c_setscl(cam, 1) == -1)
		return -1;
	w9966_i2c_setsda(cam, 1);

	return data;
}
#endif

/* Write a register to the i2c device.
   Expects claimed pdev. -1 on error */
static int w9966_write_reg_i2c(struct w9966 *cam, int reg, int data)
{
	w9966_i2c_setsda(cam, 0);
	w9966_i2c_setscl(cam, 0);

	if (w9966_i2c_wbyte(cam, W9966_I2C_W_ID) == -1 ||
			w9966_i2c_wbyte(cam, reg) == -1 ||
			w9966_i2c_wbyte(cam, data) == -1)
		return -1;

	w9966_i2c_setsda(cam, 0);
	if (w9966_i2c_setscl(cam, 1) == -1)
		return -1;

	w9966_i2c_setsda(cam, 1);

	return 0;
}

/* Find a good length for capture window (used both for W and H)
   A bit ugly but pretty functional. The capture length
   have to match the downscale */
static int w9966_findlen(int near, int size, int maxlen)
{
	int bestlen = size;
	int besterr = abs(near - bestlen);
	int len;

	for (len = size + 1; len < maxlen; len++) {
		int err;
		if (((64 * size) % len) != 0)
			continue;

		err = abs(near - len);

		/* Only continue as long as we keep getting better values */
		if (err > besterr)
			break;

		besterr = err;
		bestlen = len;
	}

	return bestlen;
}

/* Modify capture window (if necessary)
   and calculate downscaling
   Return -1 on error */
static int w9966_calcscale(int size, int min, int max, int *beg, int *end, unsigned char *factor)
{
	int maxlen = max - min;
	int len = *end - *beg + 1;
	int newlen = w9966_findlen(len, size, maxlen);
	int err = newlen - len;

	/* Check for bad format */
	if (newlen > maxlen || newlen < size)
		return -1;

	/* Set factor (6 bit fixed) */
	*factor = (64 * size) / newlen;
	if (*factor == 64)
		*factor = 0x00;	/* downscale is disabled */
	else
		*factor |= 0x80; /* set downscale-enable bit */

	/* Modify old beginning and end */
	*beg -= err / 2;
	*end += err - (err / 2);

	/* Move window if outside borders */
	if (*beg < min) {
		*end += min - *beg;
		*beg += min - *beg;
	}
	if (*end > max) {
		*beg -= *end - max;
		*end -= *end - max;
	}

	return 0;
}

/* Setup the cameras capture window etc.
   Expects a claimed pdev
   return -1 on error */
static int w9966_setup(struct w9966 *cam, int x1, int y1, int x2, int y2, int w, int h)
{
	unsigned int i;
	unsigned int enh_s, enh_e;
	unsigned char scale_x, scale_y;
	unsigned char regs[0x1c];
	unsigned char saa7111_regs[] = {
		0x21, 0x00, 0xd8, 0x23, 0x00, 0x80, 0x80, 0x00,
		0x88, 0x10, 0x80, 0x40, 0x40, 0x00, 0x01, 0x00,
		0x48, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x71, 0xe7, 0x00, 0x00, 0xc0
	};


	if (w * h * 2 > W9966_SRAMSIZE) {
		DPRINTF("capture window exceeds SRAM size!.\n");
		w = 200; h = 160;	/* Pick default values */
	}

	w &= ~0x1;
	if (w < 2)
		w = 2;
	if (h < 1)
		h = 1;
	if (w > W9966_WND_MAX_W)
		w = W9966_WND_MAX_W;
	if (h > W9966_WND_MAX_H)
		h = W9966_WND_MAX_H;

	cam->width = w;
	cam->height = h;

	enh_s = 0;
	enh_e = w * h * 2;

	/* Modify capture window if necessary and calculate downscaling */
	if (w9966_calcscale(w, W9966_WND_MIN_X, W9966_WND_MAX_X, &x1, &x2, &scale_x) != 0 ||
			w9966_calcscale(h, W9966_WND_MIN_Y, W9966_WND_MAX_Y, &y1, &y2, &scale_y) != 0)
		return -1;

	DPRINTF("%dx%d, x: %d<->%d, y: %d<->%d, sx: %d/64, sy: %d/64.\n",
			w, h, x1, x2, y1, y2, scale_x & ~0x80, scale_y & ~0x80);

	/* Setup registers */
	regs[0x00] = 0x00;			/* Set normal operation */
	regs[0x01] = 0x18;			/* Capture mode */
	regs[0x02] = scale_y;			/* V-scaling */
	regs[0x03] = scale_x;			/* H-scaling */

	/* Capture window */
	regs[0x04] = (x1 & 0x0ff);		/* X-start (8 low bits) */
	regs[0x05] = (x1 & 0x300)>>8;		/* X-start (2 high bits) */
	regs[0x06] = (y1 & 0x0ff);		/* Y-start (8 low bits) */
	regs[0x07] = (y1 & 0x300)>>8;		/* Y-start (2 high bits) */
	regs[0x08] = (x2 & 0x0ff);		/* X-end (8 low bits) */
	regs[0x09] = (x2 & 0x300)>>8;		/* X-end (2 high bits) */
	regs[0x0a] = (y2 & 0x0ff);		/* Y-end (8 low bits) */

	regs[0x0c] = W9966_SRAMID;		/* SRAM-banks (1x 128kb) */

	/* Enhancement layer */
	regs[0x0d] = (enh_s & 0x000ff);		/* Enh. start (0-7) */
	regs[0x0e] = (enh_s & 0x0ff00) >> 8;	/* Enh. start (8-15) */
	regs[0x0f] = (enh_s & 0x70000) >> 16;	/* Enh. start (16-17/18??) */
	regs[0x10] = (enh_e & 0x000ff);		/* Enh. end (0-7) */
	regs[0x11] = (enh_e & 0x0ff00) >> 8;	/* Enh. end (8-15) */
	regs[0x12] = (enh_e & 0x70000) >> 16;	/* Enh. end (16-17/18??) */

	/* Misc */
	regs[0x13] = 0x40;			/* VEE control (raw 4:2:2) */
	regs[0x17] = 0x00;			/* ??? */
	regs[0x18] = cam->i2c_state = 0x00;	/* Serial bus */
	regs[0x19] = 0xff;			/* I/O port direction control */
	regs[0x1a] = 0xff;			/* I/O port data register */
	regs[0x1b] = 0x10;			/* ??? */

	/* SAA7111 chip settings */
	saa7111_regs[0x0a] = cam->brightness;
	saa7111_regs[0x0b] = cam->contrast;
	saa7111_regs[0x0c] = cam->color;
	saa7111_regs[0x0d] = cam->hue;

	/* Reset (ECP-fifo & serial-bus) */
	if (w9966_write_reg(cam, 0x00, 0x03) == -1)
		return -1;

	/* Write regs to w9966cf chip */
	for (i = 0; i < 0x1c; i++)
		if (w9966_write_reg(cam, i, regs[i]) == -1)
			return -1;

	/* Write regs to saa7111 chip */
	for (i = 0; i < 0x20; i++)
		if (w9966_write_reg_i2c(cam, i, saa7111_regs[i]) == -1)
			return -1;

	return 0;
}

/*
 *	Video4linux interfacing
 */

static int cam_querycap(struct file *file, void  *priv,
					struct v4l2_capability *vcap)
{
	struct w9966 *cam = video_drvdata(file);

	strlcpy(vcap->driver, cam->v4l2_dev.name, sizeof(vcap->driver));
	strlcpy(vcap->card, W9966_DRIVERNAME, sizeof(vcap->card));
	strlcpy(vcap->bus_info, "parport", sizeof(vcap->bus_info));
	vcap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE;
	vcap->capabilities = vcap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int cam_enum_input(struct file *file, void *fh, struct v4l2_input *vin)
{
	if (vin->index > 0)
		return -EINVAL;
	strlcpy(vin->name, "Camera", sizeof(vin->name));
	vin->type = V4L2_INPUT_TYPE_CAMERA;
	vin->audioset = 0;
	vin->tuner = 0;
	vin->std = 0;
	vin->status = 0;
	return 0;
}

static int cam_g_input(struct file *file, void *fh, unsigned int *inp)
{
	*inp = 0;
	return 0;
}

static int cam_s_input(struct file *file, void *fh, unsigned int inp)
{
	return (inp > 0) ? -EINVAL : 0;
}

static int cam_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct w9966 *cam =
		container_of(ctrl->handler, struct w9966, hdl);
	int ret = 0;

	mutex_lock(&cam->lock);
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		cam->brightness = ctrl->val;
		break;
	case V4L2_CID_CONTRAST:
		cam->contrast = ctrl->val;
		break;
	case V4L2_CID_SATURATION:
		cam->color = ctrl->val;
		break;
	case V4L2_CID_HUE:
		cam->hue = ctrl->val;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret == 0) {
		w9966_pdev_claim(cam);

		if (w9966_write_reg_i2c(cam, 0x0a, cam->brightness) == -1 ||
		    w9966_write_reg_i2c(cam, 0x0b, cam->contrast) == -1 ||
		    w9966_write_reg_i2c(cam, 0x0c, cam->color) == -1 ||
		    w9966_write_reg_i2c(cam, 0x0d, cam->hue) == -1) {
			ret = -EIO;
		}

		w9966_pdev_release(cam);
	}
	mutex_unlock(&cam->lock);
	return ret;
}

static int cam_g_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct w9966 *cam = video_drvdata(file);
	struct v4l2_pix_format *pix = &fmt->fmt.pix;

	pix->width = cam->width;
	pix->height = cam->height;
	pix->pixelformat = V4L2_PIX_FMT_YUYV;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = 2 * cam->width;
	pix->sizeimage = 2 * cam->width * cam->height;
	/* Just a guess */
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	return 0;
}

static int cam_try_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct v4l2_pix_format *pix = &fmt->fmt.pix;

	if (pix->width < 2)
		pix->width = 2;
	if (pix->height < 1)
		pix->height = 1;
	if (pix->width > W9966_WND_MAX_W)
		pix->width = W9966_WND_MAX_W;
	if (pix->height > W9966_WND_MAX_H)
		pix->height = W9966_WND_MAX_H;
	pix->pixelformat = V4L2_PIX_FMT_YUYV;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = 2 * pix->width;
	pix->sizeimage = 2 * pix->width * pix->height;
	/* Just a guess */
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	return 0;
}

static int cam_s_fmt_vid_cap(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct w9966 *cam = video_drvdata(file);
	struct v4l2_pix_format *pix = &fmt->fmt.pix;
	int ret = cam_try_fmt_vid_cap(file, fh, fmt);

	if (ret)
		return ret;

	mutex_lock(&cam->lock);
	/* Update camera regs */
	w9966_pdev_claim(cam);
	ret = w9966_setup(cam, 0, 0, 1023, 1023, pix->width, pix->height);
	w9966_pdev_release(cam);
	mutex_unlock(&cam->lock);
	return ret;
}

static int cam_enum_fmt_vid_cap(struct file *file, void *fh, struct v4l2_fmtdesc *fmt)
{
	static struct v4l2_fmtdesc formats[] = {
		{ 0, 0, 0,
		  "YUV 4:2:2", V4L2_PIX_FMT_YUYV,
		  { 0, 0, 0, 0 }
		},
	};
	enum v4l2_buf_type type = fmt->type;

	if (fmt->index > 0)
		return -EINVAL;

	*fmt = formats[fmt->index];
	fmt->type = type;
	return 0;
}

/* Capture data */
static ssize_t w9966_v4l_read(struct file *file, char  __user *buf,
		size_t count, loff_t *ppos)
{
	struct w9966 *cam = video_drvdata(file);
	unsigned char addr = 0xa0;	/* ECP, read, CCD-transfer, 00000 */
	unsigned char __user *dest = (unsigned char __user *)buf;
	unsigned long dleft = count;
	unsigned char *tbuf;

	/* Why would anyone want more than this?? */
	if (count > cam->width * cam->height * 2)
		return -EINVAL;

	mutex_lock(&cam->lock);
	w9966_pdev_claim(cam);
	w9966_write_reg(cam, 0x00, 0x02);	/* Reset ECP-FIFO buffer */
	w9966_write_reg(cam, 0x00, 0x00);	/* Return to normal operation */
	w9966_write_reg(cam, 0x01, 0x98);	/* Enable capture */

	/* write special capture-addr and negotiate into data transfer */
	if ((parport_negotiate(cam->pport, cam->ppmode|IEEE1284_ADDR) != 0) ||
			(parport_write(cam->pport, &addr, 1) != 1) ||
			(parport_negotiate(cam->pport, cam->ppmode|IEEE1284_DATA) != 0)) {
		w9966_pdev_release(cam);
		mutex_unlock(&cam->lock);
		return -EFAULT;
	}

	tbuf = kmalloc(W9966_RBUFFER, GFP_KERNEL);
	if (tbuf == NULL) {
		count = -ENOMEM;
		goto out;
	}

	while (dleft > 0) {
		unsigned long tsize = (dleft > W9966_RBUFFER) ? W9966_RBUFFER : dleft;

		if (parport_read(cam->pport, tbuf, tsize) < tsize) {
			count = -EFAULT;
			goto out;
		}
		if (copy_to_user(dest, tbuf, tsize) != 0) {
			count = -EFAULT;
			goto out;
		}
		dest += tsize;
		dleft -= tsize;
	}

	w9966_write_reg(cam, 0x01, 0x18);	/* Disable capture */

out:
	kfree(tbuf);
	w9966_pdev_release(cam);
	mutex_unlock(&cam->lock);

	return count;
}

static const struct v4l2_file_operations w9966_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= v4l2_fh_release,
	.poll		= v4l2_ctrl_poll,
	.unlocked_ioctl = video_ioctl2,
	.read           = w9966_v4l_read,
};

static const struct v4l2_ioctl_ops w9966_ioctl_ops = {
	.vidioc_querycap    		    = cam_querycap,
	.vidioc_g_input      		    = cam_g_input,
	.vidioc_s_input      		    = cam_s_input,
	.vidioc_enum_input   		    = cam_enum_input,
	.vidioc_enum_fmt_vid_cap 	    = cam_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap 		    = cam_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap  		    = cam_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap  	    = cam_try_fmt_vid_cap,
	.vidioc_log_status		    = v4l2_ctrl_log_status,
	.vidioc_subscribe_event		    = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	    = v4l2_event_unsubscribe,
};

static const struct v4l2_ctrl_ops cam_ctrl_ops = {
	.s_ctrl = cam_s_ctrl,
};


/* Initialize camera device. Setup all internal flags, set a
   default video mode, setup ccd-chip, register v4l device etc..
   Also used for 'probing' of hardware.
   -1 on error */
static int w9966_init(struct w9966 *cam, struct parport *port)
{
	struct v4l2_device *v4l2_dev = &cam->v4l2_dev;

	if (cam->dev_state != 0)
		return -1;

	strlcpy(v4l2_dev->name, "w9966", sizeof(v4l2_dev->name));

	if (v4l2_device_register(NULL, v4l2_dev) < 0) {
		v4l2_err(v4l2_dev, "Could not register v4l2_device\n");
		return -1;
	}

	v4l2_ctrl_handler_init(&cam->hdl, 4);
	v4l2_ctrl_new_std(&cam->hdl, &cam_ctrl_ops,
			  V4L2_CID_BRIGHTNESS, 0, 255, 1, 128);
	v4l2_ctrl_new_std(&cam->hdl, &cam_ctrl_ops,
			  V4L2_CID_CONTRAST, -64, 64, 1, 64);
	v4l2_ctrl_new_std(&cam->hdl, &cam_ctrl_ops,
			  V4L2_CID_SATURATION, -64, 64, 1, 64);
	v4l2_ctrl_new_std(&cam->hdl, &cam_ctrl_ops,
			  V4L2_CID_HUE, -128, 127, 1, 0);
	if (cam->hdl.error) {
		v4l2_err(v4l2_dev, "couldn't register controls\n");
		return -1;
	}
	cam->pport = port;
	cam->brightness = 128;
	cam->contrast = 64;
	cam->color = 64;
	cam->hue = 0;

	/* Select requested transfer mode */
	switch (parmode) {
	default:	/* Auto-detect (priority: hw-ecp, hw-epp, sw-ecp) */
	case 0:
		if (port->modes & PARPORT_MODE_ECP)
			cam->ppmode = IEEE1284_MODE_ECP;
		else if (port->modes & PARPORT_MODE_EPP)
			cam->ppmode = IEEE1284_MODE_EPP;
		else
			cam->ppmode = IEEE1284_MODE_ECP;
		break;
	case 1:		/* hw- or sw-ecp */
		cam->ppmode = IEEE1284_MODE_ECP;
		break;
	case 2:		/* hw- or sw-epp */
		cam->ppmode = IEEE1284_MODE_EPP;
		break;
	}

	/* Tell the parport driver that we exists */
	cam->pdev = parport_register_device(port, "w9966", NULL, NULL, NULL, 0, NULL);
	if (cam->pdev == NULL) {
		DPRINTF("parport_register_device() failed\n");
		return -1;
	}
	w9966_set_state(cam, W9966_STATE_PDEV, W9966_STATE_PDEV);

	w9966_pdev_claim(cam);

	/* Setup a default capture mode */
	if (w9966_setup(cam, 0, 0, 1023, 1023, 200, 160) != 0) {
		DPRINTF("w9966_setup() failed.\n");
		return -1;
	}

	w9966_pdev_release(cam);

	/* Fill in the video_device struct and register us to v4l */
	strlcpy(cam->vdev.name, W9966_DRIVERNAME, sizeof(cam->vdev.name));
	cam->vdev.v4l2_dev = v4l2_dev;
	cam->vdev.fops = &w9966_fops;
	cam->vdev.ioctl_ops = &w9966_ioctl_ops;
	cam->vdev.release = video_device_release_empty;
	cam->vdev.ctrl_handler = &cam->hdl;
	set_bit(V4L2_FL_USE_FH_PRIO, &cam->vdev.flags);
	video_set_drvdata(&cam->vdev, cam);

	mutex_init(&cam->lock);

	if (video_register_device(&cam->vdev, VFL_TYPE_GRABBER, video_nr) < 0)
		return -1;

	w9966_set_state(cam, W9966_STATE_VDEV, W9966_STATE_VDEV);

	/* All ok */
	v4l2_info(v4l2_dev, "Found and initialized a webcam on %s.\n",
			cam->pport->name);
	return 0;
}


/* Terminate everything gracefully */
static void w9966_term(struct w9966 *cam)
{
	/* Unregister from v4l */
	if (w9966_get_state(cam, W9966_STATE_VDEV, W9966_STATE_VDEV)) {
		video_unregister_device(&cam->vdev);
		w9966_set_state(cam, W9966_STATE_VDEV, 0);
	}

	v4l2_ctrl_handler_free(&cam->hdl);

	/* Terminate from IEEE1284 mode and release pdev block */
	if (w9966_get_state(cam, W9966_STATE_PDEV, W9966_STATE_PDEV)) {
		w9966_pdev_claim(cam);
		parport_negotiate(cam->pport, IEEE1284_MODE_COMPAT);
		w9966_pdev_release(cam);
	}

	/* Unregister from parport */
	if (w9966_get_state(cam, W9966_STATE_PDEV, W9966_STATE_PDEV)) {
		parport_unregister_device(cam->pdev);
		w9966_set_state(cam, W9966_STATE_PDEV, 0);
	}
	memset(cam, 0, sizeof(*cam));
}


/* Called once for every parport on init */
static void w9966_attach(struct parport *port)
{
	int i;

	for (i = 0; i < W9966_MAXCAMS; i++) {
		if (w9966_cams[i].dev_state != 0)	/* Cam is already assigned */
			continue;
		if (strcmp(pardev[i], "aggressive") == 0 || strcmp(pardev[i], port->name) == 0) {
			if (w9966_init(&w9966_cams[i], port) != 0)
				w9966_term(&w9966_cams[i]);
			break;	/* return */
		}
	}
}

/* Called once for every parport on termination */
static void w9966_detach(struct parport *port)
{
	int i;

	for (i = 0; i < W9966_MAXCAMS; i++)
		if (w9966_cams[i].dev_state != 0 && w9966_cams[i].pport == port)
			w9966_term(&w9966_cams[i]);
}


static struct parport_driver w9966_ppd = {
	.name = W9966_DRIVERNAME,
	.attach = w9966_attach,
	.detach = w9966_detach,
};

/* Module entry point */
static int __init w9966_mod_init(void)
{
	int i;

	for (i = 0; i < W9966_MAXCAMS; i++)
		w9966_cams[i].dev_state = 0;

	return parport_register_driver(&w9966_ppd);
}

/* Module cleanup */
static void __exit w9966_mod_term(void)
{
	parport_unregister_driver(&w9966_ppd);
}

module_init(w9966_mod_init);
module_exit(w9966_mod_term);
