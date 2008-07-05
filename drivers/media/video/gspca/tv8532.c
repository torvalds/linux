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

#define DRIVER_VERSION_NUMBER	KERNEL_VERSION(2, 1, 5)
static const char version[] = "2.1.5";

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

static void reg_r(struct usb_device *dev,
		  __u16 index, __u8 *buffer)
{
	usb_control_msg(dev,
			usb_rcvctrlpipe(dev, 0),
			TV8532_REQ_RegRead,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,	/* value */
			index, buffer, sizeof(__u8),
			500);
}

static void reg_w(struct usb_device *dev,
		  __u16 index, __u8 *buffer, __u16 length)
{
	usb_control_msg(dev,
			usb_sndctrlpipe(dev, 0),
			TV8532_REQ_RegWrite,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,	/* value */
			index, buffer, length, 500);
}

static void tv_8532WriteEEprom(struct gspca_dev *gspca_dev)
{
	int i = 0;
	__u8 reg, data0, data1, data2, datacmd;
	struct usb_device *dev = gspca_dev->dev;

	datacmd = 0xb0;;
	reg_w(dev, TV8532_GPIO, &datacmd, 1);
	datacmd = TV8532_CMD_EEprom_Open;
	reg_w(dev, TV8532_CTRL, &datacmd, 1);
/*	msleep(1); */
	while (tv_8532_eeprom_data[i]) {
		reg = (tv_8532_eeprom_data[i] & 0xff000000) >> 24;
		reg_w(dev, TV8532_EEprom_Add, &reg, 1);
		/* msleep(1); */
		data0 = (tv_8532_eeprom_data[i] & 0x000000ff);
		reg_w(dev, TV8532_EEprom_DataL, &data0, 1);
		/* msleep(1); */
		data1 = (tv_8532_eeprom_data[i] & 0x0000FF00) >> 8;
		reg_w(dev, TV8532_EEprom_DataM, &data1, 1);
		/* msleep(1); */
		data2 = (tv_8532_eeprom_data[i] & 0x00FF0000) >> 16;
		reg_w(dev, TV8532_EEprom_DataH, &data2, 1);
		/* msleep(1); */
		datacmd = 0;
		reg_w(dev, TV8532_EEprom_Write, &datacmd, 1);
		/* msleep(10); */
		i++;
	}
	datacmd = i;
	reg_w(dev, TV8532_EEprom_TableLength, &datacmd, 1);
/*	msleep(1); */
	datacmd = TV8532_CMD_EEprom_Close;
	reg_w(dev, TV8532_CTRL, &datacmd, 1);
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
	cam->dev_name = (char *) id->driver_info;
	cam->epaddr = 1;
	cam->cam_mode = sif_mode;
	cam->nmodes = sizeof sif_mode / sizeof sif_mode[0];

	sd->brightness = sd_ctrls[SD_BRIGHTNESS].qctrl.default_value;
	sd->contrast = sd_ctrls[SD_CONTRAST].qctrl.default_value;
	return 0;
}

static void tv_8532ReadRegisters(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	__u8 data;
/*	__u16 vid, pid; */

	reg_r(dev, 0x0001, &data);
	PDEBUG(D_USBI, "register 0x01-> %x", data);
	reg_r(dev, 0x0002, &data);
	PDEBUG(D_USBI, "register 0x02-> %x", data);
	reg_r(dev, TV8532_ADWIDTH_L, &data);
	reg_r(dev, TV8532_ADWIDTH_H, &data);
	reg_r(dev, TV8532_QUANT_COMP, &data);
	reg_r(dev, TV8532_MODE_PACKET, &data);
	reg_r(dev, TV8532_SETCLK, &data);
	reg_r(dev, TV8532_POINT_L, &data);
	reg_r(dev, TV8532_POINT_H, &data);
	reg_r(dev, TV8532_POINTB_L, &data);
	reg_r(dev, TV8532_POINTB_H, &data);
	reg_r(dev, TV8532_BUDGET_L, &data);
	reg_r(dev, TV8532_BUDGET_H, &data);
	reg_r(dev, TV8532_VID_L, &data);
	reg_r(dev, TV8532_VID_H, &data);
	reg_r(dev, TV8532_PID_L, &data);
	reg_r(dev, TV8532_PID_H, &data);
	reg_r(dev, TV8532_DeviceID, &data);
	reg_r(dev, TV8532_AD_COLBEGIN_L, &data);
	reg_r(dev, TV8532_AD_COLBEGIN_H, &data);
	reg_r(dev, TV8532_AD_ROWBEGIN_L, &data);
	reg_r(dev, TV8532_AD_ROWBEGIN_H, &data);
}

static void tv_8532_setReg(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	__u8 data;
	__u8 value[2] = { 0, 0 };

	data = ADCBEGINL;
	reg_w(dev, TV8532_AD_COLBEGIN_L, &data, 1);	/* 0x10 */
	data = ADCBEGINH;	/* also digital gain */
	reg_w(dev, TV8532_AD_COLBEGIN_H, &data, 1);
	data = TV8532_CMD_UPDATE;
	reg_w(dev, TV8532_PART_CTRL, &data, 1);	/* 0x00<-0x84 */

	data = 0x0a;
	reg_w(dev, TV8532_GPIO_OE, &data, 1);
	/******************************************************/
	data = ADHEIGHL;
	reg_w(dev, TV8532_ADHEIGHT_L, &data, 1);	/* 0e */
	data = ADHEIGHH;
	reg_w(dev, TV8532_ADHEIGHT_H, &data, 1);	/* 0f */
	value[0] = EXPOL;
	value[1] = EXPOH;	/* 350d 0x014c; */
	reg_w(dev, TV8532_EXPOSURE, value, 2);	/* 1c */
	data = ADCBEGINL;
	reg_w(dev, TV8532_AD_COLBEGIN_L, &data, 1);	/* 0x10 */
	data = ADCBEGINH;	/* also digital gain */
	reg_w(dev, TV8532_AD_COLBEGIN_H, &data, 1);
	data = ADRBEGINL;
	reg_w(dev, TV8532_AD_ROWBEGIN_L, &data, 1);	/* 0x14 */

	data = 0x00;
	reg_w(dev, TV8532_AD_SLOPE, &data, 1);	/* 0x91 */
	data = 0x02;
	reg_w(dev, TV8532_AD_BITCTRL, &data, 1);	/* 0x94 */


	data = TV8532_CMD_EEprom_Close;
	reg_w(dev, TV8532_CTRL, &data, 1);	/* 0x01 */

	data = 0x00;
	reg_w(dev, TV8532_AD_SLOPE, &data, 1);	/* 0x91 */
	data = TV8532_CMD_UPDATE;
	reg_w(dev, TV8532_PART_CTRL, &data, 1);	/* 0x00<-0x84 */
}

static void tv_8532_PollReg(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	__u8 data;
	int i;

	/* strange polling from tgc */
	for (i = 0; i < 10; i++) {
		data = TESTCLK;	/* 0x48; //0x08; */
		reg_w(dev, TV8532_SETCLK, &data, 1);	/* 0x2c */
		data = TV8532_CMD_UPDATE;
		reg_w(dev, TV8532_PART_CTRL, &data, 1);
		data = 0x01;
		reg_w(dev, TV8532_UDP_UPDATE, &data, 1);	/* 0x31 */
	}
}

/* this function is called at open time */
static int sd_open(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	__u8 data;
	__u8 dataStart;
	__u8 value[2];

	data = 0x32;
	reg_w(dev, TV8532_AD_SLOPE, &data, 1);
	data = 0;
	reg_w(dev, TV8532_AD_BITCTRL, &data, 1);
	tv_8532ReadRegisters(gspca_dev);
	data = 0x0b;
	reg_w(dev, TV8532_GPIO_OE, &data, 1);
	value[0] = ADHEIGHL;
	value[1] = ADHEIGHH;	/* 401d 0x0169; */
	reg_w(dev, TV8532_ADHEIGHT_L, value, 2);	/* 0e */
	value[0] = EXPOL;
	value[1] = EXPOH;	/* 350d 0x014c; */
	reg_w(dev, TV8532_EXPOSURE, value, 2);	/* 1c */
	data = ADWIDTHL;	/* 0x20; */
	reg_w(dev, TV8532_ADWIDTH_L, &data, 1);	/* 0x0c */
	data = ADWIDTHH;
	reg_w(dev, TV8532_ADWIDTH_H, &data, 1);	/* 0x0d */

	/*******************************************************************/
	data = TESTCOMP;	/* 0x72 compressed mode */
	reg_w(dev, TV8532_QUANT_COMP, &data, 1);	/* 0x28 */
	data = TESTLINE;	/* 0x84; // CIF | 4 packet */
	reg_w(dev, TV8532_MODE_PACKET, &data, 1);	/* 0x29 */

	/************************************************/
	data = TESTCLK;		/* 0x48; //0x08; */
	reg_w(dev, TV8532_SETCLK, &data, 1);	/* 0x2c */
	data = TESTPTL;		/* 0x38; */
	reg_w(dev, TV8532_POINT_L, &data, 1);	/* 0x2d */
	data = TESTPTH;		/* 0x04; */
	reg_w(dev, TV8532_POINT_H, &data, 1);	/* 0x2e */
	dataStart = TESTPTBL;	/* 0x04; */
	reg_w(dev, TV8532_POINTB_L, &dataStart, 1);	/* 0x2f */
	data = TESTPTBH;	/* 0x04; */
	reg_w(dev, TV8532_POINTB_H, &data, 1);	/* 0x30 */
	data = TV8532_CMD_UPDATE;
	reg_w(dev, TV8532_PART_CTRL, &data, 1);	/* 0x00<-0x84 */
	/*************************************************/
	data = 0x01;
	reg_w(dev, TV8532_UDP_UPDATE, &data, 1);	/* 0x31 */
	msleep(200);
	data = 0x00;
	reg_w(dev, TV8532_UDP_UPDATE, &data, 1);	/* 0x31 */
	/*************************************************/
	tv_8532_setReg(gspca_dev);
	/*************************************************/
	data = 0x0b;
	reg_w(dev, TV8532_GPIO_OE, &data, 1);
	/*************************************************/
	tv_8532_setReg(gspca_dev);
	/*************************************************/
	tv_8532_PollReg(gspca_dev);
	return 0;
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	__u8 value[2];
	__u8 data;
	int brightness = sd->brightness;

	value[1] = (brightness >> 8) & 0xff;
	value[0] = (brightness) & 0xff;
	reg_w(gspca_dev->dev, TV8532_EXPOSURE, value, 2);	/* 1c */
	data = TV8532_CMD_UPDATE;
	reg_w(gspca_dev->dev, TV8532_PART_CTRL, &data, 1);
}

/* -- start the camera -- */
static void sd_start(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	__u8 data;
	__u8 value[2];

	data = 0x32;
	reg_w(dev, TV8532_AD_SLOPE, &data, 1);
	data = 0;
	reg_w(dev, TV8532_AD_BITCTRL, &data, 1);
	tv_8532ReadRegisters(gspca_dev);
	data = 0x0b;
	reg_w(dev, TV8532_GPIO_OE, &data, 1);
	value[0] = ADHEIGHL;
	value[1] = ADHEIGHH;	/* 401d 0x0169; */
	reg_w(dev, TV8532_ADHEIGHT_L, value, 2);	/* 0e */
/*	value[0] = EXPOL; value[1] =EXPOH; 		 * 350d 0x014c; */
/*	reg_w(dev,TV8532_REQ_RegWrite,0,TV8532_EXPOSURE,value,2);  * 1c */
	setbrightness(gspca_dev);

	data = ADWIDTHL;	/* 0x20; */
	reg_w(dev, TV8532_ADWIDTH_L, &data, 1);	/* 0x0c */
	data = ADWIDTHH;
	reg_w(dev, TV8532_ADWIDTH_H, &data, 1);	/* 0x0d */

	/************************************************/
	data = TESTCOMP;	/* 0x72 compressed mode */
	reg_w(dev, TV8532_QUANT_COMP, &data, 1);	/* 0x28 */
	if (gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv) {
		/* 176x144 */
		data = QCIFLINE;	/* 0x84; // CIF | 4 packet */
		reg_w(dev, TV8532_MODE_PACKET, &data, 1);	/* 0x29 */
	} else {
		/* 352x288 */
		data = TESTLINE;	/* 0x84; // CIF | 4 packet */
		reg_w(dev, TV8532_MODE_PACKET, &data, 1);	/* 0x29 */
	}
	/************************************************/
	data = TESTCLK;		/* 0x48; //0x08; */
	reg_w(dev, TV8532_SETCLK, &data, 1);	/* 0x2c */
	data = TESTPTL;		/* 0x38; */
	reg_w(dev, TV8532_POINT_L, &data, 1);	/* 0x2d */
	data = TESTPTH;		/* 0x04; */
	reg_w(dev, TV8532_POINT_H, &data, 1);	/* 0x2e */
	data = TESTPTBL;	/* 0x04; */
	reg_w(dev, TV8532_POINTB_L, &data, 1);	/* 0x2f */
	data = TESTPTBH;	/* 0x04; */
	reg_w(dev, TV8532_POINTB_H, &data, 1);	/* 0x30 */
	data = TV8532_CMD_UPDATE;
	reg_w(dev, TV8532_PART_CTRL, &data, 1);	/* 0x00<-0x84 */
	/************************************************/
	data = 0x01;
	reg_w(dev, TV8532_UDP_UPDATE, &data, 1);	/* 0x31 */
	msleep(200);
	data = 0x00;
	reg_w(dev, TV8532_UDP_UPDATE, &data, 1);	/* 0x31 */
	/************************************************/
	tv_8532_setReg(gspca_dev);
	/************************************************/
	data = 0x0b;
	reg_w(dev, TV8532_GPIO_OE, &data, 1);
	/************************************************/
	tv_8532_setReg(gspca_dev);
	/************************************************/
	tv_8532_PollReg(gspca_dev);
	data = 0x00;
	reg_w(dev, TV8532_UDP_UPDATE, &data, 1);	/* 0x31 */
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	struct usb_device *dev = gspca_dev->dev;
	__u8 data;

	data = 0x0b;
	reg_w(dev, TV8532_GPIO_OE, &data, 1);
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
#define DVNM(name) .driver_info = (kernel_ulong_t) name
static const __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE(0x046d, 0x0920), DVNM("QC Express")},
	{USB_DEVICE(0x046d, 0x0921), DVNM("Labtec Webcam")},
	{USB_DEVICE(0x0545, 0x808b), DVNM("Veo Stingray")},
	{USB_DEVICE(0x0545, 0x8333), DVNM("Veo Stingray")},
	{USB_DEVICE(0x0923, 0x010f), DVNM("ICM532 cams")},
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
