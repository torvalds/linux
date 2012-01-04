/* Driver for Philips webcam
   Functions that send various control messages to the webcam, including
   video modes.
   (C) 1999-2003 Nemosoft Unv.
   (C) 2004-2006 Luc Saillard (luc@saillard.org)
   (C) 2011 Hans de Goede <hdegoede@redhat.com>

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
   Changes
   2001/08/03  Alvarado   Added methods for changing white balance and
			  red/green gains
 */

/* Control functions for the cam; brightness, contrast, video mode, etc. */

#ifdef __KERNEL__
#include <asm/uaccess.h>
#endif
#include <asm/errno.h>

#include "pwc.h"
#include "pwc-kiara.h"
#include "pwc-timon.h"
#include "pwc-dec1.h"
#include "pwc-dec23.h"

/* Selectors for status controls used only in this file */
#define GET_STATUS_B00				0x0B00
#define SENSOR_TYPE_FORMATTER1			0x0C00
#define GET_STATUS_3000				0x3000
#define READ_RAW_Y_MEAN_FORMATTER		0x3100
#define SET_POWER_SAVE_MODE_FORMATTER		0x3200
#define MIRROR_IMAGE_FORMATTER			0x3300
#define LED_FORMATTER				0x3400
#define LOWLIGHT				0x3500
#define GET_STATUS_3600				0x3600
#define SENSOR_TYPE_FORMATTER2			0x3700
#define GET_STATUS_3800				0x3800
#define GET_STATUS_4000				0x4000
#define GET_STATUS_4100				0x4100	/* Get */
#define CTL_STATUS_4200				0x4200	/* [GS] 1 */

/* Formatters for the Video Endpoint controls [GS]ET_EP_STREAM_CTL */
#define VIDEO_OUTPUT_CONTROL_FORMATTER		0x0100

static const char *size2name[PSZ_MAX] =
{
	"subQCIF",
	"QSIF",
	"QCIF",
	"SIF",
	"CIF",
	"VGA",
};

/********/

/* Entries for the Nala (645/646) camera; the Nala doesn't have compression
   preferences, so you either get compressed or non-compressed streams.

   An alternate value of 0 means this mode is not available at all.
 */

#define PWC_FPS_MAX_NALA 8

struct Nala_table_entry {
	char alternate;			/* USB alternate setting */
	int compressed;			/* Compressed yes/no */

	unsigned char mode[3];		/* precomputed mode table */
};

static unsigned int Nala_fps_vector[PWC_FPS_MAX_NALA] = { 4, 5, 7, 10, 12, 15, 20, 24 };

static struct Nala_table_entry Nala_table[PSZ_MAX][PWC_FPS_MAX_NALA] =
{
#include "pwc-nala.h"
};

/****************************************************************************/

static int _send_control_msg(struct pwc_device *pdev,
	u8 request, u16 value, int index, void *buf, int buflen)
{
	int rc;
	void *kbuf = NULL;

	if (buflen) {
		kbuf = kmemdup(buf, buflen, GFP_KERNEL); /* not allowed on stack */
		if (kbuf == NULL)
			return -ENOMEM;
	}

	rc = usb_control_msg(pdev->udev, usb_sndctrlpipe(pdev->udev, 0),
		request,
		USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		value,
		index,
		kbuf, buflen, USB_CTRL_SET_TIMEOUT);

	kfree(kbuf);
	return rc;
}

static int recv_control_msg(struct pwc_device *pdev,
	u8 request, u16 value, void *buf, int buflen)
{
	int rc;
	void *kbuf = kmalloc(buflen, GFP_KERNEL); /* not allowed on stack */

	if (kbuf == NULL)
		return -ENOMEM;

	rc = usb_control_msg(pdev->udev, usb_rcvctrlpipe(pdev->udev, 0),
		request,
		USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		value,
		pdev->vcinterface,
		kbuf, buflen, USB_CTRL_GET_TIMEOUT);
	memcpy(buf, kbuf, buflen);
	kfree(kbuf);

	if (rc < 0)
		PWC_ERROR("recv_control_msg error %d req %02x val %04x\n",
			  rc, request, value);
	return rc;
}

static inline int send_video_command(struct pwc_device *pdev,
	int index, void *buf, int buflen)
{
	return _send_control_msg(pdev,
		SET_EP_STREAM_CTL,
		VIDEO_OUTPUT_CONTROL_FORMATTER,
		index,
		buf, buflen);
}

int send_control_msg(struct pwc_device *pdev,
	u8 request, u16 value, void *buf, int buflen)
{
	return _send_control_msg(pdev,
		request, value, pdev->vcinterface, buf, buflen);
}

static int set_video_mode_Nala(struct pwc_device *pdev, int size, int frames)
{
	unsigned char buf[3];
	int ret, fps;
	struct Nala_table_entry *pEntry;
	int frames2frames[31] =
	{ /* closest match of framerate */
	   0,  0,  0,  0,  4,  /*  0-4  */
	   5,  5,  7,  7, 10,  /*  5-9  */
	  10, 10, 12, 12, 15,  /* 10-14 */
	  15, 15, 15, 20, 20,  /* 15-19 */
	  20, 20, 20, 24, 24,  /* 20-24 */
	  24, 24, 24, 24, 24,  /* 25-29 */
	  24                   /* 30    */
	};
	int frames2table[31] =
	{ 0, 0, 0, 0, 0, /*  0-4  */
	  1, 1, 1, 2, 2, /*  5-9  */
	  3, 3, 4, 4, 4, /* 10-14 */
	  5, 5, 5, 5, 5, /* 15-19 */
	  6, 6, 6, 6, 7, /* 20-24 */
	  7, 7, 7, 7, 7, /* 25-29 */
	  7              /* 30    */
	};

	if (size < 0 || size > PSZ_CIF || frames < 4 || frames > 25)
		return -EINVAL;
	frames = frames2frames[frames];
	fps = frames2table[frames];
	pEntry = &Nala_table[size][fps];
	if (pEntry->alternate == 0)
		return -EINVAL;

	memcpy(buf, pEntry->mode, 3);
	ret = send_video_command(pdev, pdev->vendpoint, buf, 3);
	if (ret < 0) {
		PWC_DEBUG_MODULE("Failed to send video command... %d\n", ret);
		return ret;
	}
	if (pEntry->compressed && pdev->pixfmt == V4L2_PIX_FMT_YUV420) {
		ret = pwc_dec1_init(pdev, pdev->type, pdev->release, buf);
		if (ret < 0)
			return ret;
	}

	pdev->cmd_len = 3;
	memcpy(pdev->cmd_buf, buf, 3);

	/* Set various parameters */
	pdev->vframes = frames;
	pdev->valternate = pEntry->alternate;
	pdev->width  = pwc_image_sizes[size][0];
	pdev->height = pwc_image_sizes[size][1];
	pdev->frame_size = (pdev->width * pdev->height * 3) / 2;
	if (pEntry->compressed) {
		if (pdev->release < 5) { /* 4 fold compression */
			pdev->vbandlength = 528;
			pdev->frame_size /= 4;
		}
		else {
			pdev->vbandlength = 704;
			pdev->frame_size /= 3;
		}
	}
	else
		pdev->vbandlength = 0;
	return 0;
}


static int set_video_mode_Timon(struct pwc_device *pdev, int size, int frames,
	int compression)
{
	unsigned char buf[13];
	const struct Timon_table_entry *pChoose;
	int ret, fps;

	if (size >= PSZ_MAX || frames < 5 || frames > 30 || compression < 0 || compression > 3)
		return -EINVAL;
	if (size == PSZ_VGA && frames > 15)
		return -EINVAL;
	fps = (frames / 5) - 1;

	/* Find a supported framerate with progressively higher compression ratios
	   if the preferred ratio is not available.
	*/
	pChoose = NULL;
	while (compression <= 3) {
	   pChoose = &Timon_table[size][fps][compression];
	   if (pChoose->alternate != 0)
	     break;
	   compression++;
	}
	if (pChoose == NULL || pChoose->alternate == 0)
		return -ENOENT; /* Not supported. */

	memcpy(buf, pChoose->mode, 13);
	ret = send_video_command(pdev, pdev->vendpoint, buf, 13);
	if (ret < 0)
		return ret;

	if (pChoose->bandlength > 0 && pdev->pixfmt == V4L2_PIX_FMT_YUV420) {
		ret = pwc_dec23_init(pdev, pdev->type, buf);
		if (ret < 0)
			return ret;
	}

	pdev->cmd_len = 13;
	memcpy(pdev->cmd_buf, buf, 13);

	/* Set various parameters */
	pdev->vframes = frames;
	pdev->valternate = pChoose->alternate;
	pdev->width  = pwc_image_sizes[size][0];
	pdev->height = pwc_image_sizes[size][1];
	pdev->vbandlength = pChoose->bandlength;
	if (pChoose->bandlength > 0)
		pdev->frame_size = (pChoose->bandlength * pdev->height) / 4;
	else
		pdev->frame_size = (pdev->width * pdev->height * 12) / 8;
	return 0;
}


static int set_video_mode_Kiara(struct pwc_device *pdev, int size, int frames,
	int compression)
{
	const struct Kiara_table_entry *pChoose = NULL;
	int fps, ret;
	unsigned char buf[12];

	if (size >= PSZ_MAX || frames < 5 || frames > 30 || compression < 0 || compression > 3)
		return -EINVAL;
	if (size == PSZ_VGA && frames > 15)
		return -EINVAL;
	fps = (frames / 5) - 1;

	/* Find a supported framerate with progressively higher compression
	   ratios if the preferred ratio is not available.
	   Skip this step when using RAW modes.
	*/
	while (compression <= 3) {
		pChoose = &Kiara_table[size][fps][compression];
		if (pChoose->alternate != 0)
			break;
		compression++;
	}
	if (pChoose == NULL || pChoose->alternate == 0)
		return -ENOENT; /* Not supported. */

	PWC_TRACE("Using alternate setting %d.\n", pChoose->alternate);

	/* usb_control_msg won't take staticly allocated arrays as argument?? */
	memcpy(buf, pChoose->mode, 12);

	/* Firmware bug: video endpoint is 5, but commands are sent to endpoint 4 */
	ret = send_video_command(pdev, 4 /* pdev->vendpoint */, buf, 12);
	if (ret < 0)
		return ret;

	if (pChoose->bandlength > 0 && pdev->pixfmt == V4L2_PIX_FMT_YUV420) {
		ret = pwc_dec23_init(pdev, pdev->type, buf);
		if (ret < 0)
			return ret;
	}

	pdev->cmd_len = 12;
	memcpy(pdev->cmd_buf, buf, 12);
	/* All set and go */
	pdev->vframes = frames;
	pdev->valternate = pChoose->alternate;
	pdev->width  = pwc_image_sizes[size][0];
	pdev->height = pwc_image_sizes[size][1];
	pdev->vbandlength = pChoose->bandlength;
	if (pdev->vbandlength > 0)
		pdev->frame_size = (pdev->vbandlength * pdev->height) / 4;
	else
		pdev->frame_size = (pdev->width * pdev->height * 12) / 8;
	PWC_TRACE("frame_size=%d, vframes=%d, vsize=%d, vbandlength=%d\n",
	    pdev->frame_size, pdev->vframes, size, pdev->vbandlength);
	return 0;
}

int pwc_set_video_mode(struct pwc_device *pdev, int width, int height,
	int frames, int compression)
{
	int ret, size;

	PWC_DEBUG_FLOW("set_video_mode(%dx%d @ %d, pixfmt %08x).\n", width, height, frames, pdev->pixfmt);
	size = pwc_get_size(pdev, width, height);
	PWC_TRACE("decode_size = %d.\n", size);

	if (DEVICE_USE_CODEC1(pdev->type)) {
		ret = set_video_mode_Nala(pdev, size, frames);

	} else if (DEVICE_USE_CODEC3(pdev->type)) {
		ret = set_video_mode_Kiara(pdev, size, frames, compression);

	} else {
		ret = set_video_mode_Timon(pdev, size, frames, compression);
	}
	if (ret < 0) {
		PWC_ERROR("Failed to set video mode %s@%d fps; return code = %d\n", size2name[size], frames, ret);
		return ret;
	}
	pdev->vcompression = compression;
	pdev->frame_total_size = pdev->frame_size + pdev->frame_header_size + pdev->frame_trailer_size;
	PWC_DEBUG_SIZE("Set resolution to %dx%d\n", pdev->width, pdev->height);
	return 0;
}

static unsigned int pwc_get_fps_Nala(struct pwc_device *pdev, unsigned int index, unsigned int size)
{
	unsigned int i;

	for (i = 0; i < PWC_FPS_MAX_NALA; i++) {
		if (Nala_table[size][i].alternate) {
			if (index--==0) return Nala_fps_vector[i];
		}
	}
	return 0;
}

static unsigned int pwc_get_fps_Kiara(struct pwc_device *pdev, unsigned int index, unsigned int size)
{
	unsigned int i;

	for (i = 0; i < PWC_FPS_MAX_KIARA; i++) {
		if (Kiara_table[size][i][3].alternate) {
			if (index--==0) return Kiara_fps_vector[i];
		}
	}
	return 0;
}

static unsigned int pwc_get_fps_Timon(struct pwc_device *pdev, unsigned int index, unsigned int size)
{
	unsigned int i;

	for (i=0; i < PWC_FPS_MAX_TIMON; i++) {
		if (Timon_table[size][i][3].alternate) {
			if (index--==0) return Timon_fps_vector[i];
		}
	}
	return 0;
}

unsigned int pwc_get_fps(struct pwc_device *pdev, unsigned int index, unsigned int size)
{
	unsigned int ret;

	if (DEVICE_USE_CODEC1(pdev->type)) {
		ret = pwc_get_fps_Nala(pdev, index, size);

	} else if (DEVICE_USE_CODEC3(pdev->type)) {
		ret = pwc_get_fps_Kiara(pdev, index, size);

	} else {
		ret = pwc_get_fps_Timon(pdev, index, size);
	}

	return ret;
}

int pwc_get_u8_ctrl(struct pwc_device *pdev, u8 request, u16 value, int *data)
{
	int ret;
	u8 buf;

	ret = recv_control_msg(pdev, request, value, &buf, sizeof(buf));
	if (ret < 0)
		return ret;

	*data = buf;
	return 0;
}

int pwc_set_u8_ctrl(struct pwc_device *pdev, u8 request, u16 value, u8 data)
{
	int ret;

	ret = send_control_msg(pdev, request, value, &data, sizeof(data));
	if (ret < 0)
		return ret;

	return 0;
}

int pwc_get_s8_ctrl(struct pwc_device *pdev, u8 request, u16 value, int *data)
{
	int ret;
	s8 buf;

	ret = recv_control_msg(pdev, request, value, &buf, sizeof(buf));
	if (ret < 0)
		return ret;

	*data = buf;
	return 0;
}

int pwc_get_u16_ctrl(struct pwc_device *pdev, u8 request, u16 value, int *data)
{
	int ret;
	u8 buf[2];

	ret = recv_control_msg(pdev, request, value, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	*data = (buf[1] << 8) | buf[0];
	return 0;
}

int pwc_set_u16_ctrl(struct pwc_device *pdev, u8 request, u16 value, u16 data)
{
	int ret;
	u8 buf[2];

	buf[0] = data & 0xff;
	buf[1] = data >> 8;
	ret = send_control_msg(pdev, request, value, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	return 0;
}

int pwc_button_ctrl(struct pwc_device *pdev, u16 value)
{
	int ret;

	ret = send_control_msg(pdev, SET_STATUS_CTL, value, NULL, 0);
	if (ret < 0)
		return ret;

	return 0;
}

/* POWER */
void pwc_camera_power(struct pwc_device *pdev, int power)
{
	char buf;
	int r;

	if (!pdev->power_save)
		return;

	if (pdev->type < 675 || (pdev->type < 730 && pdev->release < 6))
		return;	/* Not supported by Nala or Timon < release 6 */

	if (power)
		buf = 0x00; /* active */
	else
		buf = 0xFF; /* power save */
	r = send_control_msg(pdev,
		SET_STATUS_CTL, SET_POWER_SAVE_MODE_FORMATTER,
		&buf, sizeof(buf));

	if (r < 0)
		PWC_ERROR("Failed to power %s camera (%d)\n",
			  power ? "on" : "off", r);
}

int pwc_set_leds(struct pwc_device *pdev, int on_value, int off_value)
{
	unsigned char buf[2];
	int r;

	if (pdev->type < 730)
		return 0;
	on_value /= 100;
	off_value /= 100;
	if (on_value < 0)
		on_value = 0;
	if (on_value > 0xff)
		on_value = 0xff;
	if (off_value < 0)
		off_value = 0;
	if (off_value > 0xff)
		off_value = 0xff;

	buf[0] = on_value;
	buf[1] = off_value;

	r = send_control_msg(pdev,
		SET_STATUS_CTL, LED_FORMATTER, &buf, sizeof(buf));
	if (r < 0)
		PWC_ERROR("Failed to set LED on/off time (%d)\n", r);

	return r;
}

#ifdef CONFIG_USB_PWC_DEBUG
int pwc_get_cmos_sensor(struct pwc_device *pdev, int *sensor)
{
	unsigned char buf;
	int ret = -1, request;

	if (pdev->type < 675)
		request = SENSOR_TYPE_FORMATTER1;
	else if (pdev->type < 730)
		return -1; /* The Vesta series doesn't have this call */
	else
		request = SENSOR_TYPE_FORMATTER2;

	ret = recv_control_msg(pdev,
		GET_STATUS_CTL, request, &buf, sizeof(buf));
	if (ret < 0)
		return ret;
	if (pdev->type < 675)
		*sensor = buf | 0x100;
	else
		*sensor = buf;
	return 0;
}
#endif
