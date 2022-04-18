// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Zoran zr36057/zr36067 PCI controller driver, for the
 * Pinnacle/Miro DC10/DC10+/DC30/DC30+, Iomega Buz, Linux
 * Media Labs LML33/LML33R10.
 *
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *
 * Changes for BUZ by Wolfgang Scherr <scherr@net4you.net>
 *
 * Changes for DC10/DC30 by Laurent Pinchart <laurent.pinchart@skynet.be>
 *
 * Changes for LML33R10 by Maxim Yevtyushkin <max@linuxmedialabs.com>
 *
 * Changes for videodev2/v4l2 by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * Based on
 *
 * Miro DC10 driver
 * Copyright (C) 1999 Wolfgang Scherr <scherr@net4you.net>
 *
 * Iomega Buz driver version 1.0
 * Copyright (C) 1999 Rainer Johanni <Rainer@Johanni.de>
 *
 * buz.0.0.3
 * Copyright (C) 1998 Dave Perks <dperks@ibm.net>
 *
 * bttv - Bt848 frame grabber driver
 * Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
 *                        & Marcus Metzler (mocm@thp.uni-koeln.de)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/wait.h>

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

#include <linux/spinlock.h>

#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include "videocodec.h"

#include <linux/io.h>
#include <linux/uaccess.h>

#include <linux/mutex.h>
#include "zoran.h"
#include "zoran_device.h"
#include "zoran_card.h"

const struct zoran_format zoran_formats[] = {
	{
		.name = "15-bit RGB LE",
		.fourcc = V4L2_PIX_FMT_RGB555,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 15,
		.flags = ZORAN_FORMAT_CAPTURE,
		.vfespfr = ZR36057_VFESPFR_RGB555 | ZR36057_VFESPFR_ERR_DIF |
			   ZR36057_VFESPFR_LITTLE_ENDIAN,
	}, {
		.name = "15-bit RGB BE",
		.fourcc = V4L2_PIX_FMT_RGB555X,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 15,
		.flags = ZORAN_FORMAT_CAPTURE,
		.vfespfr = ZR36057_VFESPFR_RGB555 | ZR36057_VFESPFR_ERR_DIF,
	}, {
		.name = "16-bit RGB LE",
		.fourcc = V4L2_PIX_FMT_RGB565,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 16,
		.flags = ZORAN_FORMAT_CAPTURE,
		.vfespfr = ZR36057_VFESPFR_RGB565 | ZR36057_VFESPFR_ERR_DIF |
			   ZR36057_VFESPFR_LITTLE_ENDIAN,
	}, {
		.name = "16-bit RGB BE",
		.fourcc = V4L2_PIX_FMT_RGB565X,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 16,
		.flags = ZORAN_FORMAT_CAPTURE,
		.vfespfr = ZR36057_VFESPFR_RGB565 | ZR36057_VFESPFR_ERR_DIF,
	}, {
		.name = "24-bit RGB",
		.fourcc = V4L2_PIX_FMT_BGR24,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 24,
		.flags = ZORAN_FORMAT_CAPTURE,
		.vfespfr = ZR36057_VFESPFR_RGB888 | ZR36057_VFESPFR_PACK24,
	}, {
		.name = "32-bit RGB LE",
		.fourcc = V4L2_PIX_FMT_BGR32,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 32,
		.flags = ZORAN_FORMAT_CAPTURE,
		.vfespfr = ZR36057_VFESPFR_RGB888 | ZR36057_VFESPFR_LITTLE_ENDIAN,
	}, {
		.name = "32-bit RGB BE",
		.fourcc = V4L2_PIX_FMT_RGB32,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.depth = 32,
		.flags = ZORAN_FORMAT_CAPTURE,
		.vfespfr = ZR36057_VFESPFR_RGB888,
	}, {
		.name = "4:2:2, packed, YUYV",
		.fourcc = V4L2_PIX_FMT_YUYV,
		.colorspace = V4L2_COLORSPACE_SMPTE170M,
		.depth = 16,
		.flags = ZORAN_FORMAT_CAPTURE,
		.vfespfr = ZR36057_VFESPFR_YUV422,
	}, {
		.name = "4:2:2, packed, UYVY",
		.fourcc = V4L2_PIX_FMT_UYVY,
		.colorspace = V4L2_COLORSPACE_SMPTE170M,
		.depth = 16,
		.flags = ZORAN_FORMAT_CAPTURE,
		.vfespfr = ZR36057_VFESPFR_YUV422 | ZR36057_VFESPFR_LITTLE_ENDIAN,
	}, {
		.name = "Hardware-encoded Motion-JPEG",
		.fourcc = V4L2_PIX_FMT_MJPEG,
		.colorspace = V4L2_COLORSPACE_SMPTE170M,
		.depth = 0,
		.flags = ZORAN_FORMAT_CAPTURE |
			 ZORAN_FORMAT_PLAYBACK |
			 ZORAN_FORMAT_COMPRESSED,
	}
};

#define NUM_FORMATS ARRAY_SIZE(zoran_formats)

	/*
	 * small helper function for calculating buffersizes for v4l2
	 * we calculate the nearest higher power-of-two, which
	 * will be the recommended buffersize
	 */
static __u32 zoran_v4l2_calc_bufsize(struct zoran_jpg_settings *settings)
{
	__u8 div = settings->ver_dcm * settings->hor_dcm * settings->tmp_dcm;
	__u32 num = (1024 * 512) / (div);
	__u32 result = 2;

	num--;
	while (num) {
		num >>= 1;
		result <<= 1;
	}

	if (result > jpg_bufsize)
		return jpg_bufsize;
	if (result < 8192)
		return 8192;

	return result;
}

/*
 *   V4L Buffer grabbing
 */
static int zoran_v4l_set_format(struct zoran *zr, int width, int height,
				const struct zoran_format *format)
{
	int bpp;

	/* Check size and format of the grab wanted */

	if (height < BUZ_MIN_HEIGHT || width < BUZ_MIN_WIDTH ||
	    height > BUZ_MAX_HEIGHT || width > BUZ_MAX_WIDTH) {
		pci_err(zr->pci_dev, "%s - wrong frame size (%dx%d)\n", __func__, width, height);
		return -EINVAL;
	}

	bpp = (format->depth + 7) / 8;

	zr->buffer_size = height * width * bpp;

	/* Check against available buffer size */
	if (height * width * bpp > zr->buffer_size) {
		pci_err(zr->pci_dev, "%s - video buffer size (%d kB) is too small\n",
			__func__, zr->buffer_size >> 10);
		return -EINVAL;
	}

	/* The video front end needs 4-byte alinged line sizes */

	if ((bpp == 2 && (width & 1)) || (bpp == 3 && (width & 3))) {
		pci_err(zr->pci_dev, "%s - wrong frame alignment\n", __func__);
		return -EINVAL;
	}

	zr->v4l_settings.width = width;
	zr->v4l_settings.height = height;
	zr->v4l_settings.format = format;
	zr->v4l_settings.bytesperline = bpp * zr->v4l_settings.width;

	return 0;
}

static int zoran_set_norm(struct zoran *zr, v4l2_std_id norm)
{

	if (!(norm & zr->card.norms)) {
		pci_err(zr->pci_dev, "%s - unsupported norm %llx\n", __func__, norm);
		return -EINVAL;
	}

	if (norm & V4L2_STD_SECAM)
		zr->timing = zr->card.tvn[ZR_NORM_SECAM];
	else if (norm & V4L2_STD_NTSC)
		zr->timing = zr->card.tvn[ZR_NORM_NTSC];
	else
		zr->timing = zr->card.tvn[ZR_NORM_PAL];

	decoder_call(zr, video, s_std, norm);
	encoder_call(zr, video, s_std_output, norm);

	/* Make sure the changes come into effect */
	zr->norm = norm;

	return 0;
}

static int zoran_set_input(struct zoran *zr, int input)
{
	if (input == zr->input)
		return 0;

	if (input < 0 || input >= zr->card.inputs) {
		pci_err(zr->pci_dev, "%s - unsupported input %d\n", __func__, input);
		return -EINVAL;
	}

	zr->input = input;

	decoder_call(zr, video, s_routing, zr->card.input[input].muxsel, 0, 0);

	return 0;
}

/*
 *   ioctl routine
 */

static int zoran_querycap(struct file *file, void *__fh, struct v4l2_capability *cap)
{
	struct zoran *zr = video_drvdata(file);

	strscpy(cap->card, ZR_DEVNAME(zr), sizeof(cap->card));
	strscpy(cap->driver, "zoran", sizeof(cap->driver));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "PCI:%s", pci_name(zr->pci_dev));
	return 0;
}

static int zoran_enum_fmt(struct zoran *zr, struct v4l2_fmtdesc *fmt, int flag)
{
	unsigned int num, i;

	if (fmt->index >= ARRAY_SIZE(zoran_formats))
		return -EINVAL;
	if (fmt->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	for (num = i = 0; i < NUM_FORMATS; i++) {
		if (zoran_formats[i].flags & flag && num++ == fmt->index) {
			strscpy(fmt->description, zoran_formats[i].name,
				sizeof(fmt->description));
			/* fmt struct pre-zeroed, so adding '\0' not needed */
			fmt->pixelformat = zoran_formats[i].fourcc;
			if (zoran_formats[i].flags & ZORAN_FORMAT_COMPRESSED)
				fmt->flags |= V4L2_FMT_FLAG_COMPRESSED;
			return 0;
		}
	}
	return -EINVAL;
}

static int zoran_enum_fmt_vid_cap(struct file *file, void *__fh,
				  struct v4l2_fmtdesc *f)
{
	struct zoran *zr = video_drvdata(file);

	return zoran_enum_fmt(zr, f, ZORAN_FORMAT_CAPTURE);
}

#if 0
/* TODO: output does not work yet */
static int zoran_enum_fmt_vid_out(struct file *file, void *__fh,
				  struct v4l2_fmtdesc *f)
{
	struct zoran *zr = video_drvdata(file);

	return zoran_enum_fmt(zr, f, ZORAN_FORMAT_PLAYBACK);
}
#endif

static int zoran_g_fmt_vid_out(struct file *file, void *__fh,
			       struct v4l2_format *fmt)
{
	struct zoran *zr = video_drvdata(file);

	fmt->fmt.pix.width = zr->jpg_settings.img_width / zr->jpg_settings.hor_dcm;
	fmt->fmt.pix.height = zr->jpg_settings.img_height * 2 /
		(zr->jpg_settings.ver_dcm * zr->jpg_settings.tmp_dcm);
	fmt->fmt.pix.sizeimage = zr->buffer_size;
	fmt->fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	if (zr->jpg_settings.tmp_dcm == 1)
		fmt->fmt.pix.field = (zr->jpg_settings.odd_even ?
				V4L2_FIELD_SEQ_TB : V4L2_FIELD_SEQ_BT);
	else
		fmt->fmt.pix.field = (zr->jpg_settings.odd_even ?
				V4L2_FIELD_TOP : V4L2_FIELD_BOTTOM);
	fmt->fmt.pix.bytesperline = 0;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;

	return 0;
}

static int zoran_g_fmt_vid_cap(struct file *file, void *__fh,
			       struct v4l2_format *fmt)
{
	struct zoran *zr = video_drvdata(file);

	if (zr->map_mode != ZORAN_MAP_MODE_RAW)
		return zoran_g_fmt_vid_out(file, __fh, fmt);
	fmt->fmt.pix.width = zr->v4l_settings.width;
	fmt->fmt.pix.height = zr->v4l_settings.height;
	fmt->fmt.pix.sizeimage = zr->buffer_size;
	fmt->fmt.pix.pixelformat = zr->v4l_settings.format->fourcc;
	fmt->fmt.pix.colorspace = zr->v4l_settings.format->colorspace;
	fmt->fmt.pix.bytesperline = zr->v4l_settings.bytesperline;
	if (BUZ_MAX_HEIGHT < (zr->v4l_settings.height * 2))
		fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;
	else
		fmt->fmt.pix.field = V4L2_FIELD_TOP;
	return 0;
}

static int zoran_try_fmt_vid_out(struct file *file, void *__fh,
				 struct v4l2_format *fmt)
{
	struct zoran *zr = video_drvdata(file);
	struct zoran_jpg_settings settings;
	int res = 0;

	if (fmt->fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
		return -EINVAL;

	settings = zr->jpg_settings;

	/* we actually need to set 'real' parameters now */
	if ((fmt->fmt.pix.height * 2) > BUZ_MAX_HEIGHT)
		settings.tmp_dcm = 1;
	else
		settings.tmp_dcm = 2;
	settings.decimation = 0;
	if (fmt->fmt.pix.height <= zr->jpg_settings.img_height / 2)
		settings.ver_dcm = 2;
	else
		settings.ver_dcm = 1;
	if (fmt->fmt.pix.width <= zr->jpg_settings.img_width / 4)
		settings.hor_dcm = 4;
	else if (fmt->fmt.pix.width <= zr->jpg_settings.img_width / 2)
		settings.hor_dcm = 2;
	else
		settings.hor_dcm = 1;
	if (settings.tmp_dcm == 1)
		settings.field_per_buff = 2;
	else
		settings.field_per_buff = 1;

	if (settings.hor_dcm > 1) {
		settings.img_x = (BUZ_MAX_WIDTH == 720) ? 8 : 0;
		settings.img_width = (BUZ_MAX_WIDTH == 720) ? 704 : BUZ_MAX_WIDTH;
	} else {
		settings.img_x = 0;
		settings.img_width = BUZ_MAX_WIDTH;
	}

	/* check */
	res = zoran_check_jpg_settings(zr, &settings, 1);
	if (res)
		return res;

	/* tell the user what we actually did */
	fmt->fmt.pix.width = settings.img_width / settings.hor_dcm;
	fmt->fmt.pix.height = settings.img_height * 2 /
		(settings.tmp_dcm * settings.ver_dcm);
	if (settings.tmp_dcm == 1)
		fmt->fmt.pix.field = (zr->jpg_settings.odd_even ?
				V4L2_FIELD_SEQ_TB : V4L2_FIELD_SEQ_BT);
	else
		fmt->fmt.pix.field = (zr->jpg_settings.odd_even ?
				V4L2_FIELD_TOP : V4L2_FIELD_BOTTOM);

	fmt->fmt.pix.sizeimage = zoran_v4l2_calc_bufsize(&settings);
	zr->buffer_size = fmt->fmt.pix.sizeimage;
	fmt->fmt.pix.bytesperline = 0;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	return res;
}

static int zoran_try_fmt_vid_cap(struct file *file, void *__fh,
				 struct v4l2_format *fmt)
{
	struct zoran *zr = video_drvdata(file);
	int bpp;
	int i;

	if (fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG)
		return zoran_try_fmt_vid_out(file, __fh, fmt);

	for (i = 0; i < NUM_FORMATS; i++)
		if (zoran_formats[i].fourcc == fmt->fmt.pix.pixelformat)
			break;

	if (i == NUM_FORMATS) {
		/* TODO do not return here to fix the TRY_FMT cannot handle an invalid pixelformat*/
		return -EINVAL;
	}

	fmt->fmt.pix.pixelformat = zoran_formats[i].fourcc;
	fmt->fmt.pix.colorspace = zoran_formats[i].colorspace;
	if (BUZ_MAX_HEIGHT < (fmt->fmt.pix.height * 2))
		fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;
	else
		fmt->fmt.pix.field = V4L2_FIELD_TOP;

	bpp = DIV_ROUND_UP(zoran_formats[i].depth, 8);
	v4l_bound_align_image(&fmt->fmt.pix.width, BUZ_MIN_WIDTH, BUZ_MAX_WIDTH, bpp == 2 ? 1 : 2,
		&fmt->fmt.pix.height, BUZ_MIN_HEIGHT, BUZ_MAX_HEIGHT, 0, 0);
	return 0;
}

static int zoran_s_fmt_vid_out(struct file *file, void *__fh,
			       struct v4l2_format *fmt)
{
	struct zoran *zr = video_drvdata(file);
	__le32 printformat = __cpu_to_le32(fmt->fmt.pix.pixelformat);
	struct zoran_jpg_settings settings;
	int res = 0;

	pci_dbg(zr->pci_dev, "size=%dx%d, fmt=0x%x (%4.4s)\n",
		fmt->fmt.pix.width, fmt->fmt.pix.height,
			fmt->fmt.pix.pixelformat,
			(char *)&printformat);
	if (fmt->fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)
		return -EINVAL;

	if (!fmt->fmt.pix.height || !fmt->fmt.pix.width)
		return -EINVAL;

	settings = zr->jpg_settings;

	/* we actually need to set 'real' parameters now */
	if (fmt->fmt.pix.height * 2 > BUZ_MAX_HEIGHT)
		settings.tmp_dcm = 1;
	else
		settings.tmp_dcm = 2;
	settings.decimation = 0;
	if (fmt->fmt.pix.height <= zr->jpg_settings.img_height / 2)
		settings.ver_dcm = 2;
	else
		settings.ver_dcm = 1;
	if (fmt->fmt.pix.width <= zr->jpg_settings.img_width / 4)
		settings.hor_dcm = 4;
	else if (fmt->fmt.pix.width <= zr->jpg_settings.img_width / 2)
		settings.hor_dcm = 2;
	else
		settings.hor_dcm = 1;
	if (settings.tmp_dcm == 1)
		settings.field_per_buff = 2;
	else
		settings.field_per_buff = 1;

	if (settings.hor_dcm > 1) {
		settings.img_x = (BUZ_MAX_WIDTH == 720) ? 8 : 0;
		settings.img_width = (BUZ_MAX_WIDTH == 720) ? 704 : BUZ_MAX_WIDTH;
	} else {
		settings.img_x = 0;
		settings.img_width = BUZ_MAX_WIDTH;
	}

	/* check */
	res = zoran_check_jpg_settings(zr, &settings, 0);
	if (res)
		return res;

	/* it's ok, so set them */
	zr->jpg_settings = settings;

	if (fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		zr->map_mode = ZORAN_MAP_MODE_JPG_REC;
	else
		zr->map_mode = ZORAN_MAP_MODE_JPG_PLAY;

	zr->buffer_size = zoran_v4l2_calc_bufsize(&zr->jpg_settings);

	/* tell the user what we actually did */
	fmt->fmt.pix.width = settings.img_width / settings.hor_dcm;
	fmt->fmt.pix.height = settings.img_height * 2 /
		(settings.tmp_dcm * settings.ver_dcm);
	if (settings.tmp_dcm == 1)
		fmt->fmt.pix.field = (zr->jpg_settings.odd_even ?
				V4L2_FIELD_SEQ_TB : V4L2_FIELD_SEQ_BT);
	else
		fmt->fmt.pix.field = (zr->jpg_settings.odd_even ?
				V4L2_FIELD_TOP : V4L2_FIELD_BOTTOM);
	fmt->fmt.pix.bytesperline = 0;
	fmt->fmt.pix.sizeimage = zr->buffer_size;
	fmt->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	return res;
}

static int zoran_s_fmt_vid_cap(struct file *file, void *__fh,
			       struct v4l2_format *fmt)
{
	struct zoran *zr = video_drvdata(file);
	struct zoran_fh *fh = __fh;
	int i;
	int res = 0;

	if (fmt->fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG)
		return zoran_s_fmt_vid_out(file, fh, fmt);

	for (i = 0; i < NUM_FORMATS; i++)
		if (fmt->fmt.pix.pixelformat == zoran_formats[i].fourcc)
			break;
	if (i == NUM_FORMATS) {
		pci_err(zr->pci_dev, "VIDIOC_S_FMT - unknown/unsupported format 0x%x\n",
			fmt->fmt.pix.pixelformat);
		/* TODO do not return here to fix the TRY_FMT cannot handle an invalid pixelformat*/
		return -EINVAL;
	}

	fmt->fmt.pix.pixelformat = zoran_formats[i].fourcc;
	if (fmt->fmt.pix.height > BUZ_MAX_HEIGHT)
		fmt->fmt.pix.height = BUZ_MAX_HEIGHT;
	if (fmt->fmt.pix.width > BUZ_MAX_WIDTH)
		fmt->fmt.pix.width = BUZ_MAX_WIDTH;
	if (fmt->fmt.pix.height < BUZ_MIN_HEIGHT)
		fmt->fmt.pix.height = BUZ_MIN_HEIGHT;
	if (fmt->fmt.pix.width < BUZ_MIN_WIDTH)
		fmt->fmt.pix.width = BUZ_MIN_WIDTH;

	zr->map_mode = ZORAN_MAP_MODE_RAW;

	res = zoran_v4l_set_format(zr, fmt->fmt.pix.width, fmt->fmt.pix.height,
				   &zoran_formats[i]);
	if (res)
		return res;

	/* tell the user the results/missing stuff */
	fmt->fmt.pix.bytesperline = zr->v4l_settings.bytesperline;
	fmt->fmt.pix.sizeimage = zr->buffer_size;
	fmt->fmt.pix.colorspace = zr->v4l_settings.format->colorspace;
	if (BUZ_MAX_HEIGHT < (zr->v4l_settings.height * 2))
		fmt->fmt.pix.field = V4L2_FIELD_INTERLACED;
	else
		fmt->fmt.pix.field = V4L2_FIELD_TOP;
	return res;
}

static int zoran_g_std(struct file *file, void *__fh, v4l2_std_id *std)
{
	struct zoran *zr = video_drvdata(file);

	*std = zr->norm;
	return 0;
}

static int zoran_s_std(struct file *file, void *__fh, v4l2_std_id std)
{
	struct zoran *zr = video_drvdata(file);
	int res = 0;

	if (zr->norm == std)
		return 0;

	if (zr->running != ZORAN_MAP_MODE_NONE)
		return -EBUSY;

	res = zoran_set_norm(zr, std);
	return res;
}

static int zoran_enum_input(struct file *file, void *__fh,
			    struct v4l2_input *inp)
{
	struct zoran *zr = video_drvdata(file);

	if (inp->index >= zr->card.inputs)
		return -EINVAL;

	strscpy(inp->name, zr->card.input[inp->index].name, sizeof(inp->name));
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->std = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM;

	/* Get status of video decoder */
	decoder_call(zr, video, g_input_status, &inp->status);
	return 0;
}

static int zoran_g_input(struct file *file, void *__fh, unsigned int *input)
{
	struct zoran *zr = video_drvdata(file);

	*input = zr->input;

	return 0;
}

static int zoran_s_input(struct file *file, void *__fh, unsigned int input)
{
	struct zoran *zr = video_drvdata(file);
	int res;

	if (zr->running != ZORAN_MAP_MODE_NONE)
		return -EBUSY;

	res = zoran_set_input(zr, input);
	return res;
}

#if 0
/* TODO: output does not work yet */
static int zoran_enum_output(struct file *file, void *__fh,
			     struct v4l2_output *outp)
{
	if (outp->index != 0)
		return -EINVAL;

	outp->index = 0;
	outp->type = V4L2_OUTPUT_TYPE_ANALOGVGAOVERLAY;
	outp->std = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM;
	outp->capabilities = V4L2_OUT_CAP_STD;
	strscpy(outp->name, "Autodetect", sizeof(outp->name));

	return 0;
}
static int zoran_g_output(struct file *file, void *__fh, unsigned int *output)
{
	*output = 0;

	return 0;
}

static int zoran_s_output(struct file *file, void *__fh, unsigned int output)
{
	if (output != 0)
		return -EINVAL;

	return 0;
}
#endif

/* cropping (sub-frame capture) */
static int zoran_g_selection(struct file *file, void *__fh, struct v4l2_selection *sel)
{
	struct zoran *zr = video_drvdata(file);

	if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		pci_err(zr->pci_dev, "%s invalid combinaison\n", __func__);
		return -EINVAL;
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r.top = zr->jpg_settings.img_y;
		sel->r.left = zr->jpg_settings.img_x;
		sel->r.width = zr->jpg_settings.img_width;
		sel->r.height = zr->jpg_settings.img_height;
		break;
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.top = sel->r.left = 0;
		sel->r.width = BUZ_MIN_WIDTH;
		sel->r.height = BUZ_MIN_HEIGHT;
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = sel->r.left = 0;
		sel->r.width = BUZ_MAX_WIDTH;
		sel->r.height = BUZ_MAX_HEIGHT;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int zoran_s_selection(struct file *file, void *__fh, struct v4l2_selection *sel)
{
	struct zoran *zr = video_drvdata(file);
	struct zoran_jpg_settings settings;
	int res;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    sel->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (!sel->r.width || !sel->r.height)
		return -EINVAL;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	if (zr->map_mode == ZORAN_MAP_MODE_RAW) {
		pci_err(zr->pci_dev, "VIDIOC_S_SELECTION - subcapture only supported for compressed capture\n");
		return -EINVAL;
	}

	settings = zr->jpg_settings;

	/* move into a form that we understand */
	settings.img_x = sel->r.left;
	settings.img_y = sel->r.top;
	settings.img_width = sel->r.width;
	settings.img_height = sel->r.height;

	/* check validity */
	res = zoran_check_jpg_settings(zr, &settings, 0);
	if (res)
		return res;

	/* accept */
	zr->jpg_settings = settings;
	return res;
}

static int zoran_g_parm(struct file *file, void *priv, struct v4l2_streamparm *parm)
{
	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	parm->parm.capture.readbuffers = 9;
	return 0;
}

/*
 * Output is disabled temporarily
 * Zoran is picky about jpeg data it accepts. At least it seems to unsupport COM and APPn.
 * So until a way to filter data will be done, disable output.
 */
static const struct v4l2_ioctl_ops zoran_ioctl_ops = {
	.vidioc_querycap		    = zoran_querycap,
	.vidioc_g_parm			    = zoran_g_parm,
	.vidioc_s_selection		    = zoran_s_selection,
	.vidioc_g_selection		    = zoran_g_selection,
	.vidioc_enum_input		    = zoran_enum_input,
	.vidioc_g_input			    = zoran_g_input,
	.vidioc_s_input			    = zoran_s_input,
/*	.vidioc_enum_output		    = zoran_enum_output,
	.vidioc_g_output		    = zoran_g_output,
	.vidioc_s_output		    = zoran_s_output,*/
	.vidioc_g_std			    = zoran_g_std,
	.vidioc_s_std			    = zoran_s_std,
	.vidioc_create_bufs		    = vb2_ioctl_create_bufs,
	.vidioc_reqbufs			    = vb2_ioctl_reqbufs,
	.vidioc_querybuf		    = vb2_ioctl_querybuf,
	.vidioc_qbuf			    = vb2_ioctl_qbuf,
	.vidioc_dqbuf			    = vb2_ioctl_dqbuf,
	.vidioc_expbuf                      = vb2_ioctl_expbuf,
	.vidioc_streamon		    = vb2_ioctl_streamon,
	.vidioc_streamoff		    = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_vid_cap	    = zoran_enum_fmt_vid_cap,
/*	.vidioc_enum_fmt_vid_out	    = zoran_enum_fmt_vid_out,*/
	.vidioc_g_fmt_vid_cap		    = zoran_g_fmt_vid_cap,
/*	.vidioc_g_fmt_vid_out               = zoran_g_fmt_vid_out,*/
	.vidioc_s_fmt_vid_cap		    = zoran_s_fmt_vid_cap,
/*	.vidioc_s_fmt_vid_out               = zoran_s_fmt_vid_out,*/
	.vidioc_try_fmt_vid_cap		    = zoran_try_fmt_vid_cap,
/*	.vidioc_try_fmt_vid_out		    = zoran_try_fmt_vid_out,*/
	.vidioc_subscribe_event             = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event           = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations zoran_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.read		= vb2_fop_read,
	.write		= vb2_fop_write,
	.mmap		= vb2_fop_mmap,
	.poll		= vb2_fop_poll,
};

const struct video_device zoran_template = {
	.name = ZORAN_NAME,
	.fops = &zoran_fops,
	.ioctl_ops = &zoran_ioctl_ops,
	.release = &zoran_vdev_release,
	.tvnorms = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM,
};

static int zr_vb2_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers, unsigned int *nplanes,
			      unsigned int sizes[], struct device *alloc_devs[])
{
	struct zoran *zr = vb2_get_drv_priv(vq);
	unsigned int size = zr->buffer_size;

	pci_dbg(zr->pci_dev, "%s nbuf=%u nplanes=%u", __func__, *nbuffers, *nplanes);

	zr->buf_in_reserve = 0;

	if (*nbuffers < vq->min_buffers_needed)
		*nbuffers = vq->min_buffers_needed;

	if (*nplanes) {
		if (sizes[0] < size)
			return -EINVAL;
		else
			return 0;
	}

	*nplanes = 1;
	sizes[0] = size;

	return 0;
}

static void zr_vb2_queue(struct vb2_buffer *vb)
{
	struct zoran *zr = vb2_get_drv_priv(vb->vb2_queue);
	struct zr_buffer *buf = vb2_to_zr_buffer(vb);
	unsigned long flags;

	spin_lock_irqsave(&zr->queued_bufs_lock, flags);
	list_add_tail(&buf->queue, &zr->queued_bufs);
	zr->buf_in_reserve++;
	spin_unlock_irqrestore(&zr->queued_bufs_lock, flags);
	if (zr->running == ZORAN_MAP_MODE_JPG_REC)
		zoran_feed_stat_com(zr);
	zr->queued++;
}

static int zr_vb2_prepare(struct vb2_buffer *vb)
{
	struct zoran *zr = vb2_get_drv_priv(vb->vb2_queue);

	if (vb2_plane_size(vb, 0) < zr->buffer_size)
		return -EINVAL;
	zr->prepared++;

	return 0;
}

int zr_set_buf(struct zoran *zr)
{
	struct zr_buffer *buf;
	struct vb2_v4l2_buffer *vbuf;
	dma_addr_t phys_addr;
	unsigned long flags;
	u32 reg;

	if (zr->running == ZORAN_MAP_MODE_NONE)
		return 0;

	if (zr->inuse[0]) {
		buf = zr->inuse[0];
		buf->vbuf.vb2_buf.timestamp = ktime_get_ns();
		buf->vbuf.sequence = zr->vbseq++;
		vbuf = &buf->vbuf;

		buf->vbuf.field = V4L2_FIELD_INTERLACED;
		if (BUZ_MAX_HEIGHT < (zr->v4l_settings.height * 2))
			buf->vbuf.field = V4L2_FIELD_INTERLACED;
		else
			buf->vbuf.field = V4L2_FIELD_TOP;
		vb2_set_plane_payload(&buf->vbuf.vb2_buf, 0, zr->buffer_size);
		vb2_buffer_done(&buf->vbuf.vb2_buf, VB2_BUF_STATE_DONE);
		zr->inuse[0] = NULL;
	}

	spin_lock_irqsave(&zr->queued_bufs_lock, flags);
	if (list_empty(&zr->queued_bufs)) {
		btand(~ZR36057_ICR_INT_PIN_EN, ZR36057_ICR);
		vb2_queue_error(zr->video_dev->queue);
		spin_unlock_irqrestore(&zr->queued_bufs_lock, flags);
		return -EINVAL;
	}
	buf = list_first_entry_or_null(&zr->queued_bufs, struct zr_buffer, queue);
	if (!buf) {
		btand(~ZR36057_ICR_INT_PIN_EN, ZR36057_ICR);
		vb2_queue_error(zr->video_dev->queue);
		spin_unlock_irqrestore(&zr->queued_bufs_lock, flags);
		return -EINVAL;
	}
	list_del(&buf->queue);
	spin_unlock_irqrestore(&zr->queued_bufs_lock, flags);

	vbuf = &buf->vbuf;
	vbuf->vb2_buf.state = VB2_BUF_STATE_ACTIVE;
	phys_addr = vb2_dma_contig_plane_dma_addr(&vbuf->vb2_buf, 0);

	if (!phys_addr)
		return -EINVAL;

	zr->inuse[0] = buf;

	reg = phys_addr;
	btwrite(reg, ZR36057_VDTR);
	if (zr->v4l_settings.height > BUZ_MAX_HEIGHT / 2)
		reg += zr->v4l_settings.bytesperline;
	btwrite(reg, ZR36057_VDBR);

	reg = 0;
	if (zr->v4l_settings.height > BUZ_MAX_HEIGHT / 2)
		reg += zr->v4l_settings.bytesperline;
	reg = (reg << ZR36057_VSSFGR_DISP_STRIDE);
	reg |= ZR36057_VSSFGR_VID_OVF;
	reg |= ZR36057_VSSFGR_SNAP_SHOT;
	reg |= ZR36057_VSSFGR_FRAME_GRAB;
	btwrite(reg, ZR36057_VSSFGR);

	btor(ZR36057_VDCR_VID_EN, ZR36057_VDCR);
	return 0;
}

static int zr_vb2_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct zoran *zr = vq->drv_priv;
	int j;

	for (j = 0; j < BUZ_NUM_STAT_COM; j++) {
		zr->stat_com[j] = cpu_to_le32(1);
		zr->inuse[j] = NULL;
	}
	zr->vbseq = 0;

	if (zr->map_mode != ZORAN_MAP_MODE_RAW) {
		pci_info(zr->pci_dev, "START JPG\n");
		zr36057_restart(zr);
		zoran_init_hardware(zr);
		if (zr->map_mode == ZORAN_MAP_MODE_JPG_REC)
			zr36057_enable_jpg(zr, BUZ_MODE_MOTION_DECOMPRESS);
		else
			zr36057_enable_jpg(zr, BUZ_MODE_MOTION_COMPRESS);
		zoran_feed_stat_com(zr);
		jpeg_start(zr);
		zr->running = zr->map_mode;
		btor(ZR36057_ICR_INT_PIN_EN, ZR36057_ICR);
		return 0;
	}

	pci_info(zr->pci_dev, "START RAW\n");
	zr36057_restart(zr);
	zoran_init_hardware(zr);

	zr36057_enable_jpg(zr, BUZ_MODE_IDLE);
	zr36057_set_memgrab(zr, 1);
	zr->running = zr->map_mode;
	btor(ZR36057_ICR_INT_PIN_EN, ZR36057_ICR);
	return 0;
}

static void zr_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct zoran *zr = vq->drv_priv;
	struct zr_buffer *buf;
	unsigned long flags;
	int j;

	btand(~ZR36057_ICR_INT_PIN_EN, ZR36057_ICR);
	if (zr->map_mode != ZORAN_MAP_MODE_RAW)
		zr36057_enable_jpg(zr, BUZ_MODE_IDLE);
	zr36057_set_memgrab(zr, 0);
	zr->running = ZORAN_MAP_MODE_NONE;

	zoran_set_pci_master(zr, 0);

	if (!pass_through) {	/* Switch to color bar */
		decoder_call(zr, video, s_stream, 0);
		encoder_call(zr, video, s_routing, 2, 0, 0);
	}

	for (j = 0; j < BUZ_NUM_STAT_COM; j++) {
		zr->stat_com[j] = cpu_to_le32(1);
		if (!zr->inuse[j])
			continue;
		buf = zr->inuse[j];
		pci_dbg(zr->pci_dev, "%s clean buf %d\n", __func__, j);
		vb2_buffer_done(&buf->vbuf.vb2_buf, VB2_BUF_STATE_ERROR);
		zr->inuse[j] = NULL;
	}

	spin_lock_irqsave(&zr->queued_bufs_lock, flags);
	while (!list_empty(&zr->queued_bufs)) {
		buf = list_entry(zr->queued_bufs.next, struct zr_buffer, queue);
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vbuf.vb2_buf, VB2_BUF_STATE_ERROR);
		zr->buf_in_reserve--;
	}
	spin_unlock_irqrestore(&zr->queued_bufs_lock, flags);
	if (zr->buf_in_reserve)
		pci_err(zr->pci_dev, "Buffer remaining %d\n", zr->buf_in_reserve);
	zr->map_mode = ZORAN_MAP_MODE_RAW;
}

static const struct vb2_ops zr_video_qops = {
	.queue_setup            = zr_vb2_queue_setup,
	.buf_queue              = zr_vb2_queue,
	.buf_prepare            = zr_vb2_prepare,
	.start_streaming        = zr_vb2_start_streaming,
	.stop_streaming         = zr_vb2_stop_streaming,
	.wait_prepare           = vb2_ops_wait_prepare,
	.wait_finish            = vb2_ops_wait_finish,
};

int zoran_queue_init(struct zoran *zr, struct vb2_queue *vq, int dir)
{
	int err;

	spin_lock_init(&zr->queued_bufs_lock);
	INIT_LIST_HEAD(&zr->queued_bufs);

	vq->dev = &zr->pci_dev->dev;
	vq->type = dir;

	vq->io_modes = VB2_DMABUF | VB2_MMAP | VB2_READ | VB2_WRITE;
	vq->drv_priv = zr;
	vq->buf_struct_size = sizeof(struct zr_buffer);
	vq->ops = &zr_video_qops;
	vq->mem_ops = &vb2_dma_contig_memops;
	vq->gfp_flags = GFP_DMA32,
	vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	vq->min_buffers_needed = 9;
	vq->lock = &zr->lock;
	err = vb2_queue_init(vq);
	if (err)
		return err;
	zr->video_dev->queue = vq;
	return 0;
}

void zoran_queue_exit(struct zoran *zr)
{
	vb2_queue_release(zr->video_dev->queue);
}
