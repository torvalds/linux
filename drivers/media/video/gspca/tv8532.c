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

	__u16 brightness;

	__u8 packet;
};

/* V4L2 controls supported by the driver */
static int sd_setbrightness(struct gspca_dev *gspca_dev, __s32 val);
static int sd_getbrightness(struct gspca_dev *gspca_dev, __s32 *val);

static struct ctrl sd_ctrls[] = {
	{
	 {
	  .id = V4L2_CID_BRIGHTNESS,
	  .type = V4L2_CTRL_TYPE_INTEGER,
	  .name = "Brightness",
	  .minimum = 1,
	  .maximum = 0x15f,	/* = 352 - 1 */
	  .step = 1,
#define BRIGHTNESS_DEF 0x14c
	  .default_value = BRIGHTNESS_DEF,
	  },
	 .set = sd_setbrightness,
	 .get = sd_getbrightness,
	 },
};

static const struct v4l2_pix_format sif_mode[] = {
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

/* TV-8532A (ICM532A) registers (LE) */
#define R00_PART_CONTROL 0x00
#define		LATENT_CHANGE	0x80
#define		EXPO_CHANGE	0x04
#define R01_TIMING_CONTROL_LOW 0x01
#define		CMD_EEprom_Open 0x30
#define		CMD_EEprom_Close 0x29
#define R03_TABLE_ADDR 0x03
#define R04_WTRAM_DATA_L 0x04
#define R05_WTRAM_DATA_M 0x05
#define R06_WTRAM_DATA_H 0x06
#define R07_TABLE_LEN	0x07
#define R08_RAM_WRITE_ACTION 0x08
#define R0C_AD_WIDTHL	0x0c
#define R0D_AD_WIDTHH	0x0d
#define R0E_AD_HEIGHTL	0x0e
#define R0F_AD_HEIGHTH	0x0f
#define R10_AD_COL_BEGINL 0x10
#define R11_AD_COL_BEGINH 0x11
#define		MIRROR		0x04	/* [10] */
#define R14_AD_ROW_BEGINL 0x14
#define R15_AD_ROWBEGINH  0x15
#define R1C_AD_EXPOSE_TIMEL 0x1c
#define R28_QUANT	0x28
#define R29_LINE	0x29
#define R2C_POLARITY	0x2c
#define R2D_POINT	0x2d
#define R2E_POINTH	0x2e
#define R2F_POINTB	0x2f
#define R30_POINTBH	0x30
#define R31_UPD		0x31
#define R2A_HIGH_BUDGET 0x2a
#define R2B_LOW_BUDGET	0x2b
#define R34_VID		0x34
#define R35_VIDH	0x35
#define R36_PID		0x36
#define R37_PIDH	0x37
#define R39_Test1	0x39		/* GPIO */
#define R3B_Test3	0x3B		/* GPIO */
#define R83_AD_IDH	0x83
#define R91_AD_SLOPEREG 0x91
#define R94_AD_BITCONTROL 0x94

static const u8 eeprom_data[][3] = {
/*	dataH dataM dataL */
	{0x01, 0x00, 0x01},
	{0x01, 0x80, 0x11},
	{0x05, 0x00, 0x14},
	{0x05, 0x00, 0x1c},
	{0x0d, 0x00, 0x1e},
	{0x05, 0x00, 0x1f},
	{0x05, 0x05, 0x19},
	{0x05, 0x01, 0x1b},
	{0x05, 0x09, 0x1e},
	{0x0d, 0x89, 0x2e},
	{0x05, 0x89, 0x2f},
	{0x05, 0x0d, 0xd9},
	{0x05, 0x09, 0xf1},
};

static int reg_r(struct gspca_dev *gspca_dev,
		 __u16 index)
{
	usb_control_msg(gspca_dev->dev,
			usb_rcvctrlpipe(gspca_dev->dev, 0),
			0x03,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,	/* value */
			index, gspca_dev->usb_buf, 1,
			500);
	return gspca_dev->usb_buf[0];
}

/* write 1 byte */
static void reg_w1(struct gspca_dev *gspca_dev,
		  __u16 index, __u8 value)
{
	gspca_dev->usb_buf[0] = value;
	usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			0x02,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,	/* value */
			index, gspca_dev->usb_buf, 1, 500);
}

/* write 2 bytes */
static void reg_w2(struct gspca_dev *gspca_dev,
		  u16 index, u16 value)
{
	gspca_dev->usb_buf[0] = value;
	gspca_dev->usb_buf[1] = value >> 8;
	usb_control_msg(gspca_dev->dev,
			usb_sndctrlpipe(gspca_dev->dev, 0),
			0x02,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0,	/* value */
			index, gspca_dev->usb_buf, 2, 500);
}

static void tv_8532WriteEEprom(struct gspca_dev *gspca_dev)
{
	int i;

	reg_w1(gspca_dev, R01_TIMING_CONTROL_LOW, CMD_EEprom_Open);
	for (i = 0; i < ARRAY_SIZE(eeprom_data); i++) {
		reg_w1(gspca_dev, R03_TABLE_ADDR, i);
		reg_w1(gspca_dev, R04_WTRAM_DATA_L, eeprom_data[i][2]);
		reg_w1(gspca_dev, R05_WTRAM_DATA_M, eeprom_data[i][1]);
		reg_w1(gspca_dev, R06_WTRAM_DATA_H, eeprom_data[i][0]);
		reg_w1(gspca_dev, R08_RAM_WRITE_ACTION, 0);
	}
	reg_w1(gspca_dev, R07_TABLE_LEN, i);
	reg_w1(gspca_dev, R01_TIMING_CONTROL_LOW, CMD_EEprom_Close);
	msleep(10);
}

/* this function is called at probe time */
static int sd_config(struct gspca_dev *gspca_dev,
		     const struct usb_device_id *id)
{
	struct sd *sd = (struct sd *) gspca_dev;
	struct cam *cam;

	cam = &gspca_dev->cam;
	cam->cam_mode = sif_mode;
	cam->nmodes = ARRAY_SIZE(sif_mode);

	sd->brightness = BRIGHTNESS_DEF;
	return 0;
}

static void tv_8532ReadRegisters(struct gspca_dev *gspca_dev)
{
	int i;
	static u8 reg_tb[] = {
		R0C_AD_WIDTHL,
		R0D_AD_WIDTHH,
		R28_QUANT,
		R29_LINE,
		R2C_POLARITY,
		R2D_POINT,
		R2E_POINTH,
		R2F_POINTB,
		R30_POINTBH,
		R2A_HIGH_BUDGET,
		R2B_LOW_BUDGET,
		R34_VID,
		R35_VIDH,
		R36_PID,
		R37_PIDH,
		R83_AD_IDH,
		R10_AD_COL_BEGINL,
		R11_AD_COL_BEGINH,
		R14_AD_ROW_BEGINL,
		R15_AD_ROWBEGINH,
		0
	};

	i = 0;
	do {
		reg_r(gspca_dev, reg_tb[i]);
		i++;
	} while (reg_tb[i] != 0);
}

static void tv_8532_setReg(struct gspca_dev *gspca_dev)
{
	reg_w1(gspca_dev, R10_AD_COL_BEGINL, 0x44);
						/* begin active line */
	reg_w1(gspca_dev, R11_AD_COL_BEGINH, 0x00);
						/* mirror and digital gain */
	reg_w1(gspca_dev, R00_PART_CONTROL, LATENT_CHANGE | EXPO_CHANGE);
						/* = 0x84 */

	reg_w1(gspca_dev, R3B_Test3, 0x0a);	/* Test0Sel = 10 */
	/******************************************************/
	reg_w1(gspca_dev, R0E_AD_HEIGHTL, 0x90);
	reg_w1(gspca_dev, R0F_AD_HEIGHTH, 0x01);
	reg_w2(gspca_dev, R1C_AD_EXPOSE_TIMEL, 0x018f);
	reg_w1(gspca_dev, R10_AD_COL_BEGINL, 0x44);
						/* begin active line */
	reg_w1(gspca_dev, R11_AD_COL_BEGINH, 0x00);
						/* mirror and digital gain */
	reg_w1(gspca_dev, R14_AD_ROW_BEGINL, 0x0a);

	reg_w1(gspca_dev, R91_AD_SLOPEREG, 0x00);
	reg_w1(gspca_dev, R94_AD_BITCONTROL, 0x02);

	reg_w1(gspca_dev, R01_TIMING_CONTROL_LOW, CMD_EEprom_Close);

	reg_w1(gspca_dev, R91_AD_SLOPEREG, 0x00);
	reg_w1(gspca_dev, R00_PART_CONTROL, LATENT_CHANGE | EXPO_CHANGE);
						/* = 0x84 */
}

static void tv_8532_PollReg(struct gspca_dev *gspca_dev)
{
	int i;

	/* strange polling from tgc */
	for (i = 0; i < 10; i++) {
		reg_w1(gspca_dev, R2C_POLARITY, 0x10);
		reg_w1(gspca_dev, R00_PART_CONTROL,
				LATENT_CHANGE | EXPO_CHANGE);
		reg_w1(gspca_dev, R31_UPD, 0x01);
	}
}

/* this function is called at probe and resume time */
static int sd_init(struct gspca_dev *gspca_dev)
{
	tv_8532WriteEEprom(gspca_dev);

	reg_w1(gspca_dev, R91_AD_SLOPEREG, 0x32);	/* slope begin 1,7V,
							 * slope rate 2 */
	reg_w1(gspca_dev, R94_AD_BITCONTROL, 0x00);
	tv_8532ReadRegisters(gspca_dev);
	reg_w1(gspca_dev, R3B_Test3, 0x0b);
	reg_w2(gspca_dev, R0E_AD_HEIGHTL, 0x0190);
	reg_w2(gspca_dev, R1C_AD_EXPOSE_TIMEL, 0x018f);
	reg_w1(gspca_dev, R0C_AD_WIDTHL, 0xe8);
	reg_w1(gspca_dev, R0D_AD_WIDTHH, 0x03);

	/*******************************************************************/
	reg_w1(gspca_dev, R28_QUANT, 0x90);
					/* no compress - fixed Q - quant 0 */
	reg_w1(gspca_dev, R29_LINE, 0x81);
					/* 0x84; // CIF | 4 packet 0x29 */

	/************************************************/
	reg_w1(gspca_dev, R2C_POLARITY, 0x10);
						/* 0x48; //0x08; 0x2c */
	reg_w1(gspca_dev, R2D_POINT, 0x14);
						/* 0x38; 0x2d */
	reg_w1(gspca_dev, R2E_POINTH, 0x01);
						/* 0x04; 0x2e */
	reg_w1(gspca_dev, R2F_POINTB, 0x12);
						/* 0x04; 0x2f */
	reg_w1(gspca_dev, R30_POINTBH, 0x01);
						/* 0x04; 0x30 */
	reg_w1(gspca_dev, R00_PART_CONTROL, LATENT_CHANGE | EXPO_CHANGE);
						/* 0x00<-0x84 */
	/*************************************************/
	reg_w1(gspca_dev, R31_UPD, 0x01);	/* update registers */
	msleep(200);
	reg_w1(gspca_dev, R31_UPD, 0x00);		/* end update */
	/*************************************************/
	tv_8532_setReg(gspca_dev);
	/*************************************************/
	reg_w1(gspca_dev, R3B_Test3, 0x0b);	/* Test0Sel = 11 = GPIO */
	/*************************************************/
	tv_8532_setReg(gspca_dev);
	/*************************************************/
	tv_8532_PollReg(gspca_dev);
	return 0;
}

static void setbrightness(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	reg_w2(gspca_dev, R1C_AD_EXPOSE_TIMEL, sd->brightness);
	reg_w1(gspca_dev, R00_PART_CONTROL, LATENT_CHANGE | EXPO_CHANGE);
						/* 0x84 */
}

/* -- start the camera -- */
static int sd_start(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	reg_w1(gspca_dev, R91_AD_SLOPEREG, 0x32);	/* slope begin 1,7V,
							 * slope rate 2 */
	reg_w1(gspca_dev, R94_AD_BITCONTROL, 0x00);
	tv_8532ReadRegisters(gspca_dev);
	reg_w1(gspca_dev, R3B_Test3, 0x0b);

	reg_w2(gspca_dev, R0E_AD_HEIGHTL, 0x0190);
	setbrightness(gspca_dev);

	reg_w1(gspca_dev, R0C_AD_WIDTHL, 0xe8);		/* 0x20; 0x0c */
	reg_w1(gspca_dev, R0D_AD_WIDTHH, 0x03);

	/************************************************/
	reg_w1(gspca_dev, R28_QUANT, 0x90);
					/* 0x72 compressed mode 0x28 */
	if (gspca_dev->cam.cam_mode[(int) gspca_dev->curr_mode].priv) {
		/* 176x144 */
		reg_w1(gspca_dev, R29_LINE, 0x41);
					/* CIF - 2 lines/packet */
	} else {
		/* 352x288 */
		reg_w1(gspca_dev, R29_LINE, 0x81);
					/* CIF - 2 lines/packet */
	}
	/************************************************/
	reg_w1(gspca_dev, R2C_POLARITY, 0x10);		/* slow clock */
	reg_w1(gspca_dev, R2D_POINT, 0x14);
	reg_w1(gspca_dev, R2E_POINTH, 0x01);
	reg_w1(gspca_dev, R2F_POINTB, 0x12);
	reg_w1(gspca_dev, R30_POINTBH, 0x01);
	reg_w1(gspca_dev, R00_PART_CONTROL, LATENT_CHANGE | EXPO_CHANGE);
	/************************************************/
	reg_w1(gspca_dev, R31_UPD, 0x01);	/* update registers */
	msleep(200);
	reg_w1(gspca_dev, R31_UPD, 0x00);		/* end update */
	/************************************************/
	tv_8532_setReg(gspca_dev);
	/************************************************/
	reg_w1(gspca_dev, R3B_Test3, 0x0b);	/* Test0Sel = 11 = GPIO */
	/************************************************/
	tv_8532_setReg(gspca_dev);
	/************************************************/
	tv_8532_PollReg(gspca_dev);
	reg_w1(gspca_dev, R31_UPD, 0x00);	/* end update */

	gspca_dev->empty_packet = 0;		/* check the empty packets */
	sd->packet = 0;				/* ignore the first packets */

	return 0;
}

static void sd_stopN(struct gspca_dev *gspca_dev)
{
	reg_w1(gspca_dev, R3B_Test3, 0x0b);	/* Test0Sel = 11 = GPIO */
}

static void sd_pkt_scan(struct gspca_dev *gspca_dev,
			struct gspca_frame *frame,	/* target */
			__u8 *data,			/* isoc packet */
			int len)			/* iso packet length */
{
	struct sd *sd = (struct sd *) gspca_dev;
	int packet_type0, packet_type1;

	packet_type0 = packet_type1 = INTER_PACKET;
	if (gspca_dev->empty_packet) {
		gspca_dev->empty_packet = 0;
		sd->packet = gspca_dev->height / 2;
		packet_type0 = FIRST_PACKET;
	} else if (sd->packet == 0)
		return;			/* 2 more lines in 352x288 ! */
	sd->packet--;
	if (sd->packet == 0)
		packet_type1 = LAST_PACKET;

	/* each packet contains:
	 * - header 2 bytes
	 * - RGRG line
	 * - 4 bytes
	 * - GBGB line
	 * - 4 bytes
	 */
	gspca_frame_add(gspca_dev, packet_type0,
			frame, data + 2, gspca_dev->width);
	gspca_frame_add(gspca_dev, packet_type1,
			frame, data + gspca_dev->width + 6, gspca_dev->width);
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

/* sub-driver description */
static const struct sd_desc sd_desc = {
	.name = MODULE_NAME,
	.ctrls = sd_ctrls,
	.nctrls = ARRAY_SIZE(sd_ctrls),
	.config = sd_config,
	.init = sd_init,
	.start = sd_start,
	.stopN = sd_stopN,
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
	int ret;
	ret = usb_register(&sd_driver);
	if (ret < 0)
		return ret;
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
