/*
 * Pixart PAC207BCA library
 *
 * Copyright (C) 2008 Hans de Goede <j.w.r.degoede@hhs.nl>
 * Copyright (C) 2005 Thomas Kaiser thomas@kaiser-linux.li
 * Copyleft (C) 2005 Michel Xhaard mxhaard@magic.fr
 *
 * V4L2 by Jean-Francois Moine <http://moinejf.free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define MODULE_NAME "pac207"

#include "gspca.h"

#define DRIVER_VERSION_NUMBER	KERNEL_VERSION(2, 1, 4)
static const char version[] = "2.1.4";

MODULE_AUTHOR("Hans de Goede <j.w.r.degoede@hhs.nl>");
MODULE_DESCRIPTION("Pixart PAC207");
MODULE_LICENSE("GPL");

#define PAC207_CTRL_TIMEOUT		100  /* ms */

#define PAC207_BRIGHTNESS_MIN		0
#define PAC207_BRIGHTNESS_MAX		255
#define PAC207_BRIGHTNESS_DEFAULT	4 /* power on default: 4 */

#define PAC207_EXPOSURE_MIN		4
#define PAC207_EXPOSURE_MAX		26
#define PAC207_EXPOSURE_DEFAULT		4 /* power on default: 3 ?? */
#define PAC207_EXPOSURE_KNEE		11 /* 4 = 30 fps, 11 = 8, 15 = 6 */

#define PAC207_GAIN_MIN			0
#define PAC207_GAIN_MAX			31
#define PAC207_GAIN_DEFAULT         	9 /* power on default: 9 */
#define PAC207_GAIN_KNEE		20

#define PAC207_AUTOGAIN_DEADZONE	30
/* We calculating the autogain at the end of the transfer of a frame, at this
   moment a frame with the old settings is being transmitted, and a frame is
   being captured with the old settings. So if we adjust the autogain we must
   ignore atleast the 2 next frames for the new settings to come into effect
   before doing any other adjustments */
#define PAC207_AUTOGAIN_IGNORE_FRAMES	3

enum pac207_line_state {
	LINE_HEADER1,
	LINE_HEADER2,
	LINE_UNCOMPRESSED,
	LINE_COMPRESSED,
};

struct pac207_decoder_state {
	/* generic state */
	u16 line_read;
	u16 line_marker;
	u8 line_state;
	u8 header_read;
	/* compression state */
	u16 processed_bytes;
	u8 remaining_bits;
	s8 no_remaining_bits;
	u8 get_abs;
	u8 discard_byte;
	u8 line_decode_buf[352];
};

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;		/* !! must be the first item */

	struct pac207_decoder_state decoder_state;

	u8 mode;

	u8 brightness;
	u8 exposure;
	u8 autogain;
	u8 gain;

	u8 sof_read;
	u8 autogain_ignore_frames;

	atomic_t avg_lum;
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setexposure(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getexposure(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setautogain(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getautogain(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setgain(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getgain(struct gspca_dev *gspca_dev, __s32 *val);

static struct ctrl sd_ctrls[] = {
#define SD_BRIGHTNESS 0
	{
	    {
		.id      = V4L2_CID_BRIGHTNESS,
		.type    = V4L2_CTRL_TYPE_INTEGER,
		.name    = "Brightness",
		.minimum = PAC207_BRIGHTNESS_MIN,
		.maximum = PAC207_BRIGHTNESS_MAX,
		.step = 1,
		.default_value = PAC207_BRIGHTNESS_DEFAULT,
		.flags = 0,
	    },
	    .set = sd_setbrightness,
	    .get = sd_getbrightness,
	},
#define SD_EXPOSURE 1
	{
	    {
		.id = V4L2_CID_EXPOSURE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "exposure",
		.minimum = PAC207_EXPOSURE_MIN,
		.maximum = PAC207_EXPOSURE_MAX,
		.step = 1,
		.default_value = PAC207_EXPOSURE_DEFAULT,
		.flags = 0,
	    },
	    .set = sd_setexposure,
	    .get = sd_getexposure,
	},
#define SD_AUTOGAIN 2
	{
	    {
		.id	  = V4L2_CID_AUTOGAIN,
		.type	= V4L2_CTRL_TYPE_BOOLEAN,
		.name	= "Auto Gain",
		.minimum = 0,
		.maximum = 1,
		.step	= 1,
		.default_value = 1,
		.flags = 0,
	    },
	    .set = sd_setautogain,
	    .get = sd_getautogain,
	},
#define SD_GAIN 3
	{
	    {
		.id = V4L2_CID_GAIN,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "gain",
		.minimum = PAC207_GAIN_MIN,
		.maximum = PAC207_GAIN_MAX,
		.step = 1,
		.default_value = PAC207_GAIN_DEFAULT,
		.flags = 0,
	    },
	    .set = sd_setgain,
	    .get = sd_getgain,
	},
};

static struct cam_mode sif_mode[] = {
	{V4L2_PIX_FMT_SBGGR8, 176, 144, 1},
	{V4L2_PIX_FMT_SBGGR8, 352, 288, 0},
};

static const __u8 pac207_sensor_init[][8] = {
	{0x10, 0x12, 0x0d, 0x12, 0x0c, 0x01, 0x29, 0xf0},
	{0x00, 0x64, 0x64, 0x64, 0x04, 0x10, 0xf0, 0x30},
	{0x00, 0x00, 0x00, 0x70, 0xa0, 0xf8, 0x00, 0x00},
	{0x00, 0x00, 0x32, 0x00, 0x96, 0x00, 0xa2, 0x02},
	{0x32, 0x00, 0x96, 0x00, 0xA2, 0x02, 0xaf, 0x00},
};

			/* 48 reg_72 Rate Control end BalSize_4a =0x36 */
static const __u8 PacReg72[] = { 0x00, 0x00, 0x36, 0x00 };

static const unsigned char pac207_sof_marker[5] =
		{ 0xff, 0xff, 0x00, 0xff, 0x96 };

int pac207_write_regs(struct gspca_dev *gspca_dev, u16 index,
	const u8 *buffer, u16 length)
{
	struct usb_device *udev = gspca_dev->dev;
	int err;
	u8 kbuf[8];

	memcpy(kbuf, buffer, length);

	err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x01,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			0x00, index, kbuf, length, PAC207_CTRL_TIMEOUT);
	if (err < 0)
		PDEBUG(D_ERR,
			"Failed to write registers to index 0x%04X, error %d)",
			index, err);

	return err;
}


int pac207_write_reg(struct gspca_dev *gspca_dev, u16 index, u16 value)
{
	struct usb_device *udev = gspca_dev->dev;
	int err;

	err = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x00,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			value, index, NULL, 0, PAC207_CTRL_TIMEOUT);
	if (err)
		PDEBUG(D_ERR, "Failed to write a register (index 0x%04X,"
			" value 0x%02X, error %d)", index, value, err);

	return err;
}


int pac207_read_reg(struct gspca_dev *gspca_dev, u16 index)
{
	struct usb_device *udev = gspca_dev->dev;
	u8 buff;
	int res;

	res = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), 0x00,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_INTERFACE,
			0x00, index, &buff, 1, PAC207_CTRL_TIMEOUT);
	if (res < 0) {
		PDEBUG(D_ERR,
			"Failed to read a register (index 0x%04X, error %d)",
			index, res);
		return res;
	}

	return buff;
}


/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
			const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;
	u8 idreg[2];

	idreg[0] = pac207_read_reg(gspca_dev, 0x0000);
	idreg[1] = pac207_read_reg(gspca_dev, 0x0001);
	idreg[0] = ((idreg[0] & 0x0F) << 4) | ((idreg[1] & 0xf0) >> 4);
	idreg[1] = idreg[1] & 0x0f;
	PDEBUG(D_PROBE, "Pixart Sensor ID 0x%02X Chips ID 0x%02X",
		idreg[0], idreg[1]);

	if (idreg[0] != 0x27) {
		PDEBUG(D_PROBE, "Error invalid sensor ID!");
		return -ENODEV;
	}

	pac207_write_reg(gspca_dev, 0x41, 0x00);
				/* Bit_0=Image Format,
				 * Bit_1=LED,
				 * Bit_2=Compression test mode enable */
	pac207_write_reg(gspca_dev, 0x0f, 0x00); /* Power Control */
	pac207_write_reg(gspca_dev, 0x11, 0x30); /* Analog Bias */

	PDEBUG(D_PROBE,
		"Pixart PAC207BCA Image Processor and Control Chip detected"
		" (vid/pid 0x%04X:0x%04X)", id->idVendor, id->idProduct);

	cam = &gspca_dev->cam;
	cam->dev_name = (char *) id->driver_info;
	cam->epaddr = 0x05;
	cam->cam_mode = sif_mode;
	cam->nmodes = ARRAY_SIZE(sif_mode);
	sd->brightness = PAC207_BRIGHTNESS_DEFAULT;
	sd->exposure = PAC207_EXPOSURE_DEFAULT;
	sd->gain = PAC207_GAIN_DEFAULT;

	return 0;
}

/* this function is called at open time */
static int sd_open(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->autogain = 1;
	return 0;
}

/* -- start the camera -- */
static void sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 mode;

	pac207_write_reg(gspca_dev, 0x0f, 0x10); /* Power control (Bit 6-0) */
	pac207_write_regs(gspca_dev, 0x0002, pac207_sensor_init[0], 8);
	pac207_write_regs(gspca_dev, 0x000a, pac207_sensor_init[1], 8);
	pac207_write_regs(gspca_dev, 0x0012, pac207_sensor_init[2], 8);
	pac207_write_regs(gspca_dev, 0x0040, pac207_sensor_init[3], 8);
	pac207_write_regs(gspca_dev, 0x0042, pac207_sensor_init[4], 8);
	pac207_write_regs(gspca_dev, 0x0048, PacReg72, 4);

	/* Compression Balance */
	if (gspca_dev->width == 176)
		pac207_write_reg(gspca_dev, 0x4a, 0xff);
	else
		pac207_write_reg(gspca_dev, 0x4a, 0x88);
	pac207_write_reg(gspca_dev, 0x4b, 0x00); /* Sram test value */
	pac207_write_reg(gspca_dev, 0x08, sd->brightness);

	/* PGA global gain (Bit 4-0) */
	pac207_write_reg(gspca_dev, 0x0e, sd->gain);
	pac207_write_reg(gspca_dev, 0x02, sd->exposure); /* PXCK = 12MHz /n */

	mode = 0x02; /* Image Format (Bit 0), LED (1), Compr. test mode (2) */
	if (gspca_dev->width == 176) {	/* 176x144 */
		mode |= 0x01;
		PDEBUG(D_STREAM, "pac207_start mode 176x144");
	} else {				/* 352x288 */
		PDEBUG(D_STREAM, "pac207_start mode 352x288");
	}
	pac207_write_reg(gspca_dev, 0x41, mode);

	pac207_write_reg(gspca_dev, 0x13, 0x01); /* Bit 0, auto clear */
	pac207_write_reg(gspca_dev, 0x1c, 0x01); /* not documented */
	msleep(10);
	pac207_write_reg(gspca_dev, 0x40, 0x01); /* Start ISO pipe */

	sd->sof_read = 0;
	sd->autogain_ignore_frames = 0;
	atomic_set(&sd->avg_lum, -1);
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	pac207_write_reg(gspca_dev, 0x40, 0x00); /* Stop ISO pipe */
	pac207_write_reg(gspca_dev, 0x41, 0x00); /* Turn of LED */
	pac207_write_reg(gspca_dev, 0x0f, 0x00); /* Power Control */
}

static void sd_stop0(struct gspca_dev *gspca_dev)
{
}

/* this function is called at close time */
static void sd_close(struct gspca_dev *gspca_dev)
{
}

/* -- convert pixart frames to Bayer -- */
/* Sonix decompressor struct B.S.(2004) */
static struct {
	u8 is_abs;
	u8 len;
	s8 val;
} table[256];

void init_pixart_decoder(void)
{
	int i, is_abs, val, len;

	for (i = 0; i < 256; i++) {
		is_abs = 0;
		val = 0;
		len = 0;
		if ((i & 0xC0) == 0) {
			/* code 00 */
			val = 0;
			len = 2;
		} else if ((i & 0xC0) == 0x40) {
			/* code 01 */
			val = -5;
			len = 2;
		} else if ((i & 0xC0) == 0x80) {
			/* code 10 */
			val = 5;
			len = 2;
		} else if ((i & 0xF0) == 0xC0) {
			/* code 1100 */
			val = -10;
			len = 4;
		} else if ((i & 0xF0) == 0xD0) {
			/* code 1101 */
			val = 10;
			len = 4;
		} else if ((i & 0xF8) == 0xE0) {
			/* code 11100 */
			val = -15;
			len = 5;
		} else if ((i & 0xF8) == 0xE8) {
			/* code 11101 */
			val = 15;
			len = 5;
		} else if ((i & 0xFC) == 0xF0) {
			/* code 111100 */
			val = -20;
			len = 6;
		} else if ((i & 0xFC) == 0xF4) {
			/* code 111101 */
			val = 20;
			len = 6;
		} else if ((i & 0xF8) == 0xF8) {
			/* code 11111xxxxxx */
			is_abs = 1;
			val = 0;
			len = 5;
		}
		table[i].is_abs = is_abs;
		table[i].val = val;
		table[i].len = len;
	}
}

/* auto gain and exposure algorithm based on the knee algorithm described here:
 * <http://ytse.tricolour.net/docs/LowLightOptimization.html> */
static void pac207_do_auto_gain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i, steps, desired_avg_lum;
	int orig_gain = sd->gain;
	int orig_exposure = sd->exposure;
	int avg_lum = atomic_read(&sd->avg_lum);

	if (!sd->autogain || avg_lum == -1)
		return;

	if (sd->autogain_ignore_frames > 0) {
		sd->autogain_ignore_frames--;
		return;
	}

	/* correct desired lumination for the configured brightness */
	desired_avg_lum = 100 + sd->brightness / 2;

	/* If we are of a multiple of deadzone, do multiple step to reach the
	   desired lumination fast (with the risc of a slight overshoot) */
	steps = abs(desired_avg_lum - avg_lum) / PAC207_AUTOGAIN_DEADZONE;

	for (i = 0; i < steps; i++) {
		if (avg_lum > desired_avg_lum) {
			if (sd->gain > PAC207_GAIN_KNEE)
				sd->gain--;
			else if (sd->exposure > PAC207_EXPOSURE_KNEE)
				sd->exposure--;
			else if (sd->gain > PAC207_GAIN_DEFAULT)
				sd->gain--;
			else if (sd->exposure > PAC207_EXPOSURE_MIN)
				sd->exposure--;
			else if (sd->gain > PAC207_GAIN_MIN)
				sd->gain--;
			else
				break;
		} else {
			if (sd->gain < PAC207_GAIN_DEFAULT)
				sd->gain++;
			else if (sd->exposure < PAC207_EXPOSURE_KNEE)
				sd->exposure++;
			else if (sd->gain < PAC207_GAIN_KNEE)
				sd->gain++;
			else if (sd->exposure < PAC207_EXPOSURE_MAX)
				sd->exposure++;
			else if (sd->gain < PAC207_GAIN_MAX)
				sd->gain++;
			else
				break;
		}
	}

	if (sd->exposure != orig_exposure || sd->gain != orig_gain) {
		if (sd->exposure != orig_exposure)
			pac207_write_reg(gspca_dev, 0x0002, sd->exposure);
		if (sd->gain != orig_gain)
			pac207_write_reg(gspca_dev, 0x000e, sd->gain);
		pac207_write_reg(gspca_dev, 0x13, 0x01); /* load reg to sen */
		pac207_write_reg(gspca_dev, 0x1c, 0x01); /* not documented */
		sd->autogain_ignore_frames = PAC207_AUTOGAIN_IGNORE_FRAMES;
	}
}

static unsigned char *pac207_find_sof(struct gspca_dev *gspca_dev,
					unsigned char *m, int len)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int i;

	/* Search for the SOF marker (fixed part) in the header */
	for (i = 0; i < len; i++) {
		if (m[i] == pac207_sof_marker[sd->sof_read]) {
			sd->sof_read++;
			if (sd->sof_read == sizeof(pac207_sof_marker)) {
				PDEBUG(D_STREAM,
					"SOF found, bytes to analyze: %u."
					" Frame starts at byte #%u",
					len, i + 1);
				sd->sof_read = 0;
				return m + i + 1;
			}
		} else {
			sd->sof_read = 0;
		}
	}

	return NULL;
}

static int pac207_decompress_row(struct gspca_dev *gspca_dev,
				struct gspca_frame *f,
				__u8 *cdata,
				int len)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct pac207_decoder_state *decoder_state = &sd->decoder_state;
	unsigned char *outp = decoder_state->line_decode_buf +
				decoder_state->line_read;
	int val, bitlen, bitpos = -decoder_state->no_remaining_bits;
	u8 code;

	/* first two pixels are stored as raw 8-bit */
	while (decoder_state->line_read < 2) {
		*outp++ = *cdata++;
		decoder_state->line_read++;
		len--;
		if (len == 0)
			return 0;
	}

	while (decoder_state->line_read < gspca_dev->width) {
		if (bitpos < 0) {
			code = decoder_state->remaining_bits << (8 + bitpos) |
				cdata[0] >> -bitpos;
		} else {
			u8 *addr = cdata + bitpos / 8;
			code = addr[0] << (bitpos & 7) |
				addr[1] >> (8 - (bitpos & 7));
		}

		bitlen = decoder_state->get_abs ?
				6 : table[code].len;

		/* Stop decompressing if we're out of input data */
		if ((bitpos + bitlen) > (len * 8))
			break;

		if (decoder_state->get_abs) {
			*outp++ = code & 0xFC;
			decoder_state->line_read++;
			decoder_state->get_abs = 0;
		} else {
			if (table[code].is_abs) {
				decoder_state->get_abs = 1;
			} else {
				/* relative to left pixel */
				val = outp[-2] +
					table[code].val;
				if (val > 0xff)
					val = 0xff;
				else if (val < 0)
					val = 0;
				*outp++ = val;
				decoder_state->line_read++;
			}
		}
		bitpos += bitlen;
	}

	if (decoder_state->line_read == gspca_dev->width) {
		int compressed_line_len;

		gspca_frame_add(gspca_dev, INTER_PACKET, f,
				decoder_state->line_decode_buf,
				gspca_dev->width);

		/* completely decompressed line, round pos to nearest word */
		compressed_line_len = ((decoder_state->processed_bytes * 8 +
			bitpos + 15) / 16) * 2;

		len -= compressed_line_len - decoder_state->processed_bytes;
		if (len < 0) {
			decoder_state->discard_byte = 1;
			len = 0;
		}
	} else {
		decoder_state->processed_bytes += len;
		decoder_state->remaining_bits = cdata[bitpos/8];
		decoder_state->no_remaining_bits = (8 - bitpos) & 7;
		len = 0;
	}

	return len;
}

static void pac207_decode_line_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct pac207_decoder_state *decoder_state = &sd->decoder_state;

	decoder_state->line_read = 0;
	decoder_state->line_state = LINE_HEADER1;
	decoder_state->processed_bytes = 0;
	decoder_state->no_remaining_bits = 0;
	decoder_state->get_abs = 0;
}

static void pac207_decode_frame_init(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct pac207_decoder_state *decoder_state = &sd->decoder_state;

	decoder_state->header_read = 0;
	decoder_state->discard_byte = 0;

	pac207_decode_line_init(gspca_dev);
}

static int pac207_decode_frame_data(struct gspca_dev *gspca_dev,
	struct gspca_frame *f, unsigned char *data, int len)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct pac207_decoder_state *decoder_state = &sd->decoder_state;
	int needed = 0;

	/* first 11 bytes after sof marker: frame header */
	if (decoder_state->header_read < 11) {
		/* get average lumination from frame header (byte 5) */
		if (decoder_state->header_read < 5) {
			needed = 5 - decoder_state->header_read;
			if (len >= needed)
				atomic_set(&sd->avg_lum, data[needed-1]);
		}
		/* skip the rest of the header */
		needed = 11 - decoder_state->header_read;
		if (len <= needed) {
			decoder_state->header_read += len;
			return 0;
		}
		data += needed;
		len -= needed;
		decoder_state->header_read = 11;
	}

	while (len) {
		if (decoder_state->discard_byte) {
			data++;
			len--;
			decoder_state->discard_byte = 0;
			continue;
		}

		switch (decoder_state->line_state) {
		case LINE_HEADER1:
			decoder_state->line_marker = data[0] << 8;
			decoder_state->line_state = LINE_HEADER2;
			needed = 1;
			break;
		case LINE_HEADER2:
			decoder_state->line_marker |= data[0];
			switch (decoder_state->line_marker) {
			case 0x0ff0:
				decoder_state->line_state = LINE_UNCOMPRESSED;
				break;
			case 0x1ee1:
				decoder_state->line_state = LINE_COMPRESSED;
				break;
			default:
				PDEBUG(D_STREAM,
					"Error unknown line-header %04X",
					(int) decoder_state->line_marker);
				gspca_dev->last_packet_type = DISCARD_PACKET;
				return 0;
			}
			needed = 1;
			break;
		case LINE_UNCOMPRESSED:
			needed = gspca_dev->width - decoder_state->line_read;
			if (needed > len)
				needed = len;
			gspca_frame_add(gspca_dev, INTER_PACKET, f, data,
				needed);
			decoder_state->line_read += needed;
			break;
		case LINE_COMPRESSED:
			needed = len -
				pac207_decompress_row(gspca_dev, f, data, len);
			break;
		}

		data += needed;
		len -= needed;

		if (decoder_state->line_read == gspca_dev->width) {
			if ((f->data_end - f->data) ==
				(gspca_dev->width * gspca_dev->height)) {
				/* eureka we've got a frame */
				return 1;
			}
			pac207_decode_line_init(gspca_dev);
		}
	}

	return 0;
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,
			__u8 *data,
			int len)
{
	unsigned char *sof;
	int n;

	sof = pac207_find_sof(gspca_dev, data, len);

	if (sof) {
		/* finish decoding current frame */
		if (gspca_dev->last_packet_type == INTER_PACKET) {
			n = sof - data;
			if (n > sizeof(pac207_sof_marker))
				n -= sizeof(pac207_sof_marker);
			else
				n = 0;
			n = pac207_decode_frame_data(gspca_dev, frame,
							data, n);
			if (n)
				frame = gspca_frame_add(gspca_dev,
						LAST_PACKET,
						frame,
						NULL,
						0);
			else
				PDEBUG(D_STREAM, "Incomplete frame");
		}
		pac207_decode_frame_init(gspca_dev);
		gspca_frame_add(gspca_dev, FIRST_PACKET, frame, NULL, 0);
		len -= sof - data;
		data = sof;
	}

	if (gspca_dev->last_packet_type == DISCARD_PACKET)
		return;

	n = pac207_decode_frame_data(gspca_dev, frame, data, len);
	if (n)
		gspca_frame_add(gspca_dev, LAST_PACKET,
				frame, NULL, 0);
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	pac207_write_reg(gspca_dev, 0x08, sd->brightness);
	pac207_write_reg(gspca_dev, 0x13, 0x01);	/* Bit 0, auto clear */
	pac207_write_reg(gspca_dev, 0x1c, 0x01);	/* not documented */
}

static void setexposure(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	pac207_write_reg(gspca_dev, 0x02, sd->exposure);
	pac207_write_reg(gspca_dev, 0x13, 0x01);	/* Bit 0, auto clear */
	pac207_write_reg(gspca_dev, 0x1c, 0x01);	/* not documented */
}

static void setgain(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	pac207_write_reg(gspca_dev, 0x0e, sd->gain);
	pac207_write_reg(gspca_dev, 0x13, 0x01);	/* Bit 0, auto clear */
	pac207_write_reg(gspca_dev, 0x1c, 0x01);	/* not documented */
}

static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->brightness = val;
	if (gspca_dev->streaming)
		setbrightness(gspca_dev);
	return 0;
}

static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->brightness;
	return 0;
}

static int sd_setexposure(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* don't allow mucking with exposure when using autogain */
	if (sd->autogain)
		return -EINVAL;

	sd->exposure = val;
	if (gspca_dev->streaming)
		setexposure(gspca_dev);
	return 0;
}

static int sd_getexposure(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->exposure;
	return 0;
}

static int sd_setgain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	/* don't allow mucking with gain when using autogain */
	if (sd->autogain)
		return -EINVAL;

	sd->gain = val;
	if (gspca_dev->streaming)
		setgain(gspca_dev);
	return 0;
}

static int sd_getgain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->gain;
	return 0;
}

static int sd_setautogain(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->autogain = val;
	/* when switching to autogain set defaults to make sure
	   we are on a valid point of the autogain gain /
	   exposure knee graph, and give this change time to
	   take effect before doing autogain. */
	if (sd->autogain) {
		sd->exposure = PAC207_EXPOSURE_DEFAULT;
		sd->gain = PAC207_GAIN_DEFAULT;
		if (gspca_dev->streaming) {
			sd->autogain_ignore_frames =
				PAC207_AUTOGAIN_IGNORE_FRAMES;
			setexposure(gspca_dev);
			setgain(gspca_dev);
		}
	}

	return 0;
}

static int sd_getautogain(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->autogain;
	return 0;
}

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls,
	.nctrls = ARRAY_SIZE(sd_ctrls),
	.config = sd_config,
	.open = sd_open,
	.start = sd_start,
	.stopN = sd_stopN,
	.stop0 = sd_stop0,
	.close = sd_close,
	.dq_callback = pac207_do_auto_gain,
	.pkt_scan = sd_pkt_scan,
};

/* -- module initialisation -- */
#define DVNM(name) .driver_info = (kernel_ulong_t) name
static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x041e, 0x4028), DVNM("Creative Webcam Vista Plus")},
	{USB_DEVICE(0x093a, 0x2460), DVNM("Q-Tec Webcam 100")},
	{USB_DEVICE(0x093a, 0x2463), DVNM("Philips spc200nc pac207")},
	{USB_DEVICE(0x093a, 0x2464), DVNM("Labtec Webcam 1200")},
	{USB_DEVICE(0x093a, 0x2468), DVNM("PAC207")},
	{USB_DEVICE(0x093a, 0x2470), DVNM("Genius GF112")},
	{USB_DEVICE(0x093a, 0x2471), DVNM("Genius VideoCam GE111")},
	{USB_DEVICE(0x093a, 0x2472), DVNM("Genius VideoCam GE110")},
	{USB_DEVICE(0x2001, 0xf115), DVNM("D-Link DSB-C120")},
	{}
};
MODULE_DEVICE_TABLE(usb, device_table);

/* -- device connect -- */
static int sd_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	return gspca_dev_probe(intf, id, &sd_desc, sizeof(struct sd),
				THIS_MODULE);
}

static struct usb_driver sd_driver = {
	.name = MODULE_NAME,
	.id_table = device_table,
	.probe = sd_probe,
	.disconnect = gspca_disconnect,
};

/* -- module insert / remove -- */
static int __init sd_mod_init(void)
{
	init_pixart_decoder();
	if (usb_register(&sd_driver) < 0)
		return -1;
	PDEBUG(D_PROBE, "v%s registered", version);
	return 0;
}
static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
	PDEBUG(D_PROBE, "deregistered");
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);
