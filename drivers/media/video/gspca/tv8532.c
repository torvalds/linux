/*
 * Quickcam cameras initialization data
 *
 * V4L2 by Jean-Francois Moine <http://moinejf.free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#define MODULE_NAME "tv8532"

#include "gspca.h"

MODULE_AUTHOR("Michel Xhaard <mxhaard@users.sourceforge.net>");
MODULE_DESCRIPTION("TV8532 USB Camera Driver");
MODULE_LICENSE("GPL");

/* specific webcam descriptor */
struct sd {
	struct gspca_dev gspca_dev;	/* !! must be the first item */

	int buflen;			/* current length of tmpbuf */
	__u8 tmpbuf[352 * 288 + 10 * 288];	/* no protection... */
	__u8 tmpbuf2[352 * 288];		/* no protection... */

	unsigned short brightness;
	unsigned short contrast;

	char packet;
	char synchro;
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);
static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val);

static struct ctrl sd_ctrls[] = {
#define SD_BRIGHTNESS 0
	{
	 {
	  .id = V4L2_CID_BRIGHTNESS,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Brightness",
	  .minimum = 1,
	  .maximum = 0x2ff,
	  .step = 1,
	  .default_value = 0x18f,
	  },
	 .set = sd_setbrightness,
	 .get = sd_getbrightness,
	 },
#define SD_CONTRAST 1
	{
	 {
	  .id = V4L2_CID_CONTRAST,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Contrast",
	  .minimum = 0,
	  .maximum = 0xffff,
	  .step = 1,
	  .default_value = 0x7fff,
	  },
	 .set = sd_setcontrast,
	 .get = sd_getcontrast,
	 },
};

static struct v4l2_pix_format sif_mode[] = {
	{176, 144, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 176,
		.sizeimage = 176 * 144,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 1},
	{352, 288, V4L2_PIX_FMT_SBGGR8, V4L2_FIELD_NONE,
		.bytesperline = 352,
		.sizeimage = 352 * 288,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.priv = 0},
};

/*
 * Initialization data: this is the first set-up data written to the
 * device (before the open data).
 */
#define TESTCLK 0x10		/* reg 0x2c -> 0x12 //10 */
#define TESTCOMP 0x90		/* reg 0x28 -> 0x80 */
#define TESTLINE 0x81		/* reg 0x29 -> 0x81 */
#define QCIFLINE 0x41		/* reg 0x29 -> 0x81 */
#define TESTPTL 0x14		/* reg 0x2D -> 0x14 */
#define TESTPTH 0x01		/* reg 0x2E -> 0x01 */
#define TESTPTBL 0x12		/* reg 0x2F -> 0x0a */
#define TESTPTBH 0x01		/* reg 0x30 -> 0x01 */
#define ADWIDTHL 0xe8		/* reg 0x0c -> 0xe8 */
#define ADWIDTHH 0x03		/* reg 0x0d -> 0x03 */
#define ADHEIGHL 0x90		/* reg 0x0e -> 0x91 //93 */
#define ADHEIGHH 0x01		/* reg 0x0f -> 0x01 */
#define EXPOL 0x8f		/* reg 0x1c -> 0x8f */
#define EXPOH 0x01		/* reg 0x1d -> 0x01 */
#define ADCBEGINL 0x44		/* reg 0x10 -> 0x46 //47 */
#define ADCBEGINH 0x00		/* reg 0x11 -> 0x00 */
#define ADRBEGINL 0x0a		/* reg 0x14 -> 0x0b //0x0c */
#define ADRBEGINH 0x00		/* reg 0x15 -> 0x00 */
#define TV8532_CMD_UPDATE 0x84

#define TV8532_EEprom_Add 0x03
#define TV8532_EEprom_DataL 0x04
#define TV8532_EEprom_DataM 0x05
#define TV8532_EEprom_DataH 0x06
#define TV8532_EEprom_TableLength 0x07
#define TV8532_EEprom_Write 0x08
#define TV8532_PART_CTRL 0x00
#define TV8532_CTRL 0x01
#define TV8532_CMD_EEprom_Open 0x30
#define TV8532_CMD_EEprom_Close 0x29
#define TV8532_UDP_UPDATE 0x31
#define TV8532_GPIO 0x39
#define TV8532_GPIO_OE 0x3B
#define TV8532_REQ_RegWrite 0x02
#define TV8532_REQ_RegRead 0x03

#define TV8532_ADWIDTH_L 0x0C
#define TV8532_ADWIDTH_H 0x0D
#define TV8532_ADHEIGHT_L 0x0E
#define TV8532_ADHEIGHT_H 0x0F
#define TV8532_EXPOSURE 0x1C
#define TV8532_QUANT_COMP 0x28
#define TV8532_MODE_PACKET 0x29
#define TV8532_SETCLK 0x2C
#define TV8532_POINT_L 0x2D
#define TV8532_POINT_H 0x2E
#define TV8532_POINTB_L 0x2F
#define TV8532_POINTB_H 0x30
#define TV8532_BUDGET_L 0x2A
#define TV8532_BUDGET_H 0x2B
#define TV8532_VID_L 0x34
#define TV8532_VID_H 0x35
#define TV8532_PID_L 0x36
#define TV8532_PID_H 0x37
#define TV8532_DeviceID 0x83
#define TV8532_AD_SLOPE 0x91
#define TV8532_AD_BITCTRL 0x94
#define TV8532_AD_COLBEGIN_L 0x10
#define TV8532_AD_COLBEGIN_H 0x11
#define TV8532_AD_ROWBEGIN_L 0x14
#define TV8532_AD_ROWBEGIN_H 0x15

static const __u32 tv_8532_eeprom_data[] = {
/*	add		dataL	   dataM	dataH */
	0x00010001, 0x01018011, 0x02050014, 0x0305001c,
	0x040d001e, 0x0505001f, 0x06050519, 0x0705011b,
	0x0805091e, 0x090d892e, 0x0a05892f, 0x0b050dd9,
	0x0c0509f1, 0
};

static int reg_r(struct gspca_dev *gspca_dev,
		 __u16 index)
{
	usb_control_msg(gspca_dev->dev,
			usb_rcvctrlpipe(gspca_dev->dev, 0),
			TV8532_REQ_RegRead,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,	/* value */
			index, gspca_dev->usb_buf, 1,
			500);
	return gspca_dev->usb_buf[0];
}

/* write 1 byte */
static void reg_w_1(struct gspca_dev *gspca_dev,
		  __u16 index, __u8 value)
{
	gspca_dev->usb_buf[0] = value;
	usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			TV8532_REQ_RegWrite,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,	/* value */
			index, gspca_dev->usb_buf, 1, 500);
}

/* write 2 bytes */
static void reg_w_2(struct gspca_dev *gspca_dev,
		  __u16 index, __u8 val1, __u8 val2)
{
	gspca_dev->usb_buf[0] = val1;
	gspca_dev->usb_buf[1] = val2;
	usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			TV8532_REQ_RegWrite,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,	/* value */
			index, gspca_dev->usb_buf, 2, 500);
}

static void tv_8532WriteEEprom(struct gspca_dev *gspca_dev)
{
	int i = 0;
	__u8 reg, data0, data1, data2;

	reg_w_1(gspca_dev, TV8532_GPIO, 0xb0);
	reg_w_1(gspca_dev, TV8532_CTRL, TV8532_CMD_EEprom_Open);
/*	msleep(1); */
	while (tv_8532_eeprom_data[i]) {
		reg = (tv_8532_eeprom_data[i] & 0xff000000) >> 24;
		reg_w_1(gspca_dev, TV8532_EEprom_Add, reg);
		/* msleep(1); */
		data0 = (tv_8532_eeprom_data[i] & 0x000000ff);
		reg_w_1(gspca_dev, TV8532_EEprom_DataL, data0);
		/* msleep(1); */
		data1 = (tv_8532_eeprom_data[i] & 0x0000ff00) >> 8;
		reg_w_1(gspca_dev, TV8532_EEprom_DataM, data1);
		/* msleep(1); */
		data2 = (tv_8532_eeprom_data[i] & 0x00ff0000) >> 16;
		reg_w_1(gspca_dev, TV8532_EEprom_DataH, data2);
		/* msleep(1); */
		reg_w_1(gspca_dev, TV8532_EEprom_Write, 0);
		/* msleep(10); */
		i++;
	}
	reg_w_1(gspca_dev, TV8532_EEprom_TableLength, i);
/*	msleep(1); */
	reg_w_1(gspca_dev, TV8532_CTRL, TV8532_CMD_EEprom_Close);
	msleep(10);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		     const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;

	tv_8532WriteEEprom(gspca_dev);

	cam = &gspca_dev->cam;
	cam->epaddr = 1;
	cam->cam_mode = sif_mode;
	cam->nmodes = sizeof sif_mode / sizeof sif_mode[0];

	sd->brightness = sd_ctrls[SD_BRIGHTNESS].qctrl.default_value;
	sd->contrast = sd_ctrls[SD_CONTRAST].qctrl.default_value;
	return 0;
}

static void tv_8532ReadRegisters(struct gspca_dev *gspca_dev)
{
	__u8 data;

	data = reg_r(gspca_dev, 0x0001);
	PDEBUG(D_USBI, "register 0x01-> %x", data);
	data = reg_r(gspca_dev, 0x0002);
	PDEBUG(D_USBI, "register 0x02-> %x", data);
	reg_r(gspca_dev, TV8532_ADWIDTH_L);
	reg_r(gspca_dev, TV8532_ADWIDTH_H);
	reg_r(gspca_dev, TV8532_QUANT_COMP);
	reg_r(gspca_dev, TV8532_MODE_PACKET);
	reg_r(gspca_dev, TV8532_SETCLK);
	reg_r(gspca_dev, TV8532_POINT_L);
	reg_r(gspca_dev, TV8532_POINT_H);
	reg_r(gspca_dev, TV8532_POINTB_L);
	reg_r(gspca_dev, TV8532_POINTB_H);
	reg_r(gspca_dev, TV8532_BUDGET_L);
	reg_r(gspca_dev, TV8532_BUDGET_H);
	reg_r(gspca_dev, TV8532_VID_L);
	reg_r(gspca_dev, TV8532_VID_H);
	reg_r(gspca_dev, TV8532_PID_L);
	reg_r(gspca_dev, TV8532_PID_H);
	reg_r(gspca_dev, TV8532_DeviceID);
	reg_r(gspca_dev, TV8532_AD_COLBEGIN_L);
	reg_r(gspca_dev, TV8532_AD_COLBEGIN_H);
	reg_r(gspca_dev, TV8532_AD_ROWBEGIN_L);
	reg_r(gspca_dev, TV8532_AD_ROWBEGIN_H);
}

static void tv_8532_setReg(struct gspca_dev *gspca_dev)
{
	reg_w_1(gspca_dev, TV8532_AD_COLBEGIN_L,
			ADCBEGINL);			/* 0x10 */
	reg_w_1(gspca_dev, TV8532_AD_COLBEGIN_H,
			ADCBEGINH);			/* also digital gain */
	reg_w_1(gspca_dev, TV8532_PART_CTRL,
			TV8532_CMD_UPDATE);		/* 0x00<-0x84 */

	reg_w_1(gspca_dev, TV8532_GPIO_OE, 0x0a);
	/******************************************************/
	reg_w_1(gspca_dev, TV8532_ADHEIGHT_L, ADHEIGHL); /* 0e */
	reg_w_1(gspca_dev, TV8532_ADHEIGHT_H, ADHEIGHH); /* 0f */
	reg_w_2(gspca_dev, TV8532_EXPOSURE,
			EXPOL, EXPOH);			/* 350d 0x014c; 1c */
	reg_w_1(gspca_dev, TV8532_AD_COLBEGIN_L,
			ADCBEGINL);			/* 0x10 */
	reg_w_1(gspca_dev, TV8532_AD_COLBEGIN_H,
			ADCBEGINH);			/* also digital gain */
	reg_w_1(gspca_dev, TV8532_AD_ROWBEGIN_L,
			ADRBEGINL);			/* 0x14 */

	reg_w_1(gspca_dev, TV8532_AD_SLOPE, 0x00);	/* 0x91 */
	reg_w_1(gspca_dev, TV8532_AD_BITCTRL, 0x02);	/* 0x94 */

	reg_w_1(gspca_dev, TV8532_CTRL,
			TV8532_CMD_EEprom_Close);	/* 0x01 */

	reg_w_1(gspca_dev, TV8532_AD_SLOPE, 0x00);	/* 0x91 */
	reg_w_1(gspca_dev, TV8532_PART_CTRL,
			TV8532_CMD_UPDATE);		/* 0x00<-0x84 */
}

static void tv_8532_PollReg(struct gspca_dev *gspca_dev)
{
	int i;

	/* strange polling from tgc */
	for (i = 0; i < 10; i++) {
		reg_w_1(gspca_dev, TV8532_SETCLK,
			TESTCLK);		/* 0x48; //0x08; 0x2c */
		reg_w_1(gspca_dev, TV8532_PART_CTRL, TV8532_CMD_UPDATE);
		reg_w_1(gspca_dev, TV8532_UDP_UPDATE, 0x01);	/* 0x31 */
	}
}

/* this function is called at open time */
static int sd_open(struct gspca_dev *gspca_dev)
{
	reg_w_1(gspca_dev, TV8532_AD_SLOPE, 0x32);
	reg_w_1(gspca_dev, TV8532_AD_BITCTRL, 0x00);
	tv_8532ReadRegisters(gspca_dev);
	reg_w_1(gspca_dev, TV8532_GPIO_OE, 0x0b);
	reg_w_2(gspca_dev, TV8532_ADHEIGHT_L, ADHEIGHL,
				ADHEIGHH);	/* 401d 0x0169; 0e */
	reg_w_2(gspca_dev, TV8532_EXPOSURE, EXPOL,
				EXPOH);		/* 350d 0x014c; 1c */
	reg_w_1(gspca_dev, TV8532_ADWIDTH_L, ADWIDTHL);	/* 0x20; 0x0c */
	reg_w_1(gspca_dev, TV8532_ADWIDTH_H, ADWIDTHH);	/* 0x0d */

	/*******************************************************************/
	reg_w_1(gspca_dev, TV8532_QUANT_COMP,
			TESTCOMP);	/* 0x72 compressed mode 0x28 */
	reg_w_1(gspca_dev, TV8532_MODE_PACKET,
			TESTLINE);	/* 0x84; // CIF | 4 packet 0x29 */

	/************************************************/
	reg_w_1(gspca_dev, TV8532_SETCLK,
			TESTCLK);		/* 0x48; //0x08; 0x2c */
	reg_w_1(gspca_dev, TV8532_POINT_L,
			TESTPTL);		/* 0x38; 0x2d */
	reg_w_1(gspca_dev, TV8532_POINT_H,
			TESTPTH);		/* 0x04; 0x2e */
	reg_w_1(gspca_dev, TV8532_POINTB_L,
			TESTPTBL);		/* 0x04; 0x2f */
	reg_w_1(gspca_dev, TV8532_POINTB_H,
			TESTPTBH);		/* 0x04; 0x30 */
	reg_w_1(gspca_dev, TV8532_PART_CTRL,
			TV8532_CMD_UPDATE);	/* 0x00<-0x84 */
	/*************************************************/
	reg_w_1(gspca_dev, TV8532_UDP_UPDATE, 0x01);	/* 0x31 */
	msleep(200);
	reg_w_1(gspca_dev, TV8532_UDP_UPDATE, 0x00);	/* 0x31 */
	/*************************************************/
	tv_8532_setReg(gspca_dev);
	/*************************************************/
	reg_w_1(gspca_dev, TV8532_GPIO_OE, 0x0b);
	/*************************************************/
	tv_8532_setReg(gspca_dev);
	/*************************************************/
	tv_8532_PollReg(gspca_dev);
	return 0;
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	int brightness = sd->brightness;

	reg_w_2(gspca_dev, TV8532_EXPOSURE,
		brightness >> 8, brightness);		/* 1c */
	reg_w_1(gspca_dev, TV8532_PART_CTRL, TV8532_CMD_UPDATE);
}

/* -- start the camera -- */
static void sd_start(struct gspca_dev *gspca_dev)
{
	reg_w_1(gspca_dev, TV8532_AD_SLOPE, 0x32);
	reg_w_1(gspca_dev, TV8532_AD_BITCTRL, 0x00);
	tv_8532ReadRegisters(gspca_dev);
	reg_w_1(gspca_dev, TV8532_GPIO_OE, 0x0b);
	reg_w_2(gspca_dev, TV8532_ADHEIGHT_L,
		ADHEIGHL, ADHEIGHH);	/* 401d 0x0169; 0e */
/*	reg_w_2(gspca_dev, TV8532_EXPOSURE,
		EXPOL, EXPOH);		 * 350d 0x014c; 1c */
	setbrightness(gspca_dev);

	reg_w_1(gspca_dev, TV8532_ADWIDTH_L, ADWIDTHL);	/* 0x20; 0x0c */
	reg_w_1(gspca_dev, TV8532_ADWIDTH_H, ADWIDTHH);	/* 0x0d */

	/************************************************/
	reg_w_1(gspca_dev, TV8532_QUANT_COMP,
			TESTCOMP);	/* 0x72 compressed mode 0x28 */
	if (gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv) {
		/* 176x144 */
		reg_w_1(gspca_dev, TV8532_MODE_PACKET,
			QCIFLINE);	/* 0x84; // CIF | 4 packet 0x29 */
	} else {
		/* 352x288 */
		reg_w_1(gspca_dev, TV8532_MODE_PACKET,
			TESTLINE);	/* 0x84; // CIF | 4 packet 0x29 */
	}
	/************************************************/
	reg_w_1(gspca_dev, TV8532_SETCLK,
			TESTCLK);		/* 0x48; //0x08; 0x2c */
	reg_w_1(gspca_dev, TV8532_POINT_L,
			TESTPTL);		/* 0x38; 0x2d */
	reg_w_1(gspca_dev, TV8532_POINT_H,
			TESTPTH);		/* 0x04; 0x2e */
	reg_w_1(gspca_dev, TV8532_POINTB_L,
			TESTPTBL);		/* 0x04; 0x2f */
	reg_w_1(gspca_dev, TV8532_POINTB_H,
			TESTPTBH);		/* 0x04; 0x30 */
	reg_w_1(gspca_dev, TV8532_PART_CTRL,
			TV8532_CMD_UPDATE);	/* 0x00<-0x84 */
	/************************************************/
	reg_w_1(gspca_dev, TV8532_UDP_UPDATE, 0x01);	/* 0x31 */
	msleep(200);
	reg_w_1(gspca_dev, TV8532_UDP_UPDATE, 0x00);	/* 0x31 */
	/************************************************/
	tv_8532_setReg(gspca_dev);
	/************************************************/
	reg_w_1(gspca_dev, TV8532_GPIO_OE, 0x0b);
	/************************************************/
	tv_8532_setReg(gspca_dev);
	/************************************************/
	tv_8532_PollReg(gspca_dev);
	reg_w_1(gspca_dev, TV8532_UDP_UPDATE, 0x00);	/* 0x31 */
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	reg_w_1(gspca_dev, TV8532_GPIO_OE, 0x0b);
}

static void sd_stop0(struct gspca_dev *gspca_dev)
{
}

static void sd_close(struct gspca_dev *gspca_dev)
{
}

static void tv8532_preprocess(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
/* we should received a whole frame with header and EOL marker
 * in gspca_dev->tmpbuf and return a GBRG pattern in gspca_dev->tmpbuf2
 * sequence 2bytes header the Alternate pixels bayer GB 4 bytes
 * Alternate pixels bayer RG 4 bytes EOL */
	int width = gspca_dev->width;
	int height = gspca_dev->height;
	unsigned char *dst = sd->tmpbuf2;
	unsigned char *data = sd->tmpbuf;
	int i;

	/* precompute where is the good bayer line */
	if (((data[3] + data[width + 7]) >> 1)
	    + (data[4] >> 2)
	    + (data[width + 6] >> 1) >= ((data[2] + data[width + 6]) >> 1)
	    + (data[3] >> 2)
	    + (data[width + 5] >> 1))
		data += 3;
	else
		data += 2;
	for (i = 0; i < height / 2; i++) {
		memcpy(dst, data, width);
		data += width + 3;
		dst += width;
		memcpy(dst, data, width);
		data += width + 7;
		dst += width;
	}
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,	/* target */
			__u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;

	if (data[0] != 0x80) {
		sd->packet++;
		if (sd->buflen + len > sizeof sd->tmpbuf) {
			if (gspca_dev->last_packet_type != DISCARD_PACKET) {
				PDEBUG(D_PACK, "buffer overflow");
				gspca_dev->last_packet_type = DISCARD_PACKET;
			}
			return;
		}
		memcpy(&sd->tmpbuf[sd->buflen], data, len);
		sd->buflen += len;
		return;
	}

	/* here we detect 0x80 */
	/* counter is limited so we need few header for a frame :) */

	/* header 0x80 0x80 0x80 0x80 0x80 */
	/* packet  00   63  127  145  00   */
	/* sof     0     1   1    0    0   */

	/* update sequence */
	if (sd->packet == 63 || sd->packet == 127)
		sd->synchro = 1;

	/* is there a frame start ? */
	if (sd->packet >= (gspca_dev->height >> 1) - 1) {
		PDEBUG(D_PACK, "SOF > %d packet %d", sd->synchro,
		       sd->packet);
		if (!sd->synchro) {	/* start of frame */
			if (gspca_dev->last_packet_type == FIRST_PACKET) {
				tv8532_preprocess(gspca_dev);
				frame = gspca_frame_add(gspca_dev,
							LAST_PACKET,
							frame, sd->tmpbuf2,
							gspca_dev->width *
							    gspca_dev->width);
			}
			gspca_frame_add(gspca_dev, FIRST_PACKET,
					frame, data, 0);
			memcpy(sd->tmpbuf, data, len);
			sd->buflen = len;
			sd->packet = 0;
			return;
		}
		if (gspca_dev->last_packet_type != DISCARD_PACKET) {
			PDEBUG(D_PACK,
			       "Warning wrong TV8532 frame detection %d",
			       sd->packet);
			gspca_dev->last_packet_type = DISCARD_PACKET;
		}
		return;
	}

	if (!sd->synchro) {
		/* Drop packet frame corrupt */
		PDEBUG(D_PACK, "DROP SOF %d packet %d",
		       sd->synchro, sd->packet);
		sd->packet = 0;
		gspca_dev->last_packet_type = DISCARD_PACKET;
		return;
	}
	sd->synchro = 1;
	sd->packet++;
	memcpy(&sd->tmpbuf[sd->buflen], data, len);
	sd->buflen += len;
}

static void setcontrast(struct gspca_dev *gspca_dev)
{
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

static int sd_setcontrast(struct gspca_dev *gspca_dev, __s32 val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->contrast = val;
	if (gspca_dev->streaming)
		setcontrast(gspca_dev);
	return 0;
}

static int sd_getcontrast(struct gspca_dev *gspca_dev, __s32 *val)
{
	struct sd *sd = (struct sd *) gspca_dev;

	*val = sd->contrast;
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
	.pkt_scan = sd_pkt_scan,
};

/* -- module initialisation -- */
static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x046d, 0x0920)},
	{USB_DEVICE(0x046d, 0x0921)},
	{USB_DEVICE(0x0545, 0x808b)},
	{USB_DEVICE(0x0545, 0x8333)},
	{USB_DEVICE(0x0923, 0x010f)},
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
#ifdef CONFIG_PM
	.suspend = gspca_suspend,
	.resume = gspca_resume,
#endif
};

/* -- module insert / remove -- */
static int __init sd_mod_init(void)
{
	if (usb_register(&sd_driver) < 0)
		return -1;
	PDEBUG(D_PROBE, "registered");
	return 0;
}

static void __exit sd_mod_exit(void)
{
	usb_deregister(&sd_driver);
	PDEBUG(D_PROBE, "deregistered");
}

module_init(sd_mod_init);
module_exit(sd_mod_exit);
