/*
 * tvp5150 - Texas Instruments TVP5150A(M) video decoder driver
 *
 * Copyright (c) 2005 Mauro Carvalho Chehab (mchehab@brturbo.com.br)
 * This code is placed under the terms of the GNU General Public License
 */

#include <linux/i2c.h>
#include <linux/videodev.h>
#include <linux/delay.h>
#include <linux/video_decoder.h>
#include <media/v4l2-common.h>

#include "tvp5150_reg.h"

MODULE_DESCRIPTION("Texas Instruments TVP5150A video decoder driver");	/* standard i2c insmod options */
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_LICENSE("GPL");

static unsigned short normal_i2c[] = {
	0xb8 >> 1,
	0xba >> 1,
	I2C_CLIENT_END
};

I2C_CLIENT_INSMOD;

static int debug = 0;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

#define tvp5150_info(fmt, arg...) do { \
	printk(KERN_INFO "%s %d-%04x: " fmt, c->driver->driver.name, \
	       i2c_adapter_id(c->adapter), c->addr , ## arg); } while (0)
#define tvp5150_dbg(num, fmt, arg...) \
	do { \
		if (debug >= num) \
			printk(KERN_DEBUG "%s debug %d-%04x: " fmt,\
				c->driver->driver.name, \
				i2c_adapter_id(c->adapter), \
				c->addr , ## arg); } while (0)

/* supported controls */
static struct v4l2_queryctrl tvp5150_qctrl[] = {
	{
		.id = V4L2_CID_BRIGHTNESS,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Brightness",
		.minimum = 0,
		.maximum = 255,
		.step = 1,
		.default_value = 0,
		.flags = 0,
	}, {
		.id = V4L2_CID_CONTRAST,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Contrast",
		.minimum = 0,
		.maximum = 255,
		.step = 0x1,
		.default_value = 0x10,
		.flags = 0,
	}, {
		 .id = V4L2_CID_SATURATION,
		 .type = V4L2_CTRL_TYPE_INTEGER,
		 .name = "Saturation",
		 .minimum = 0,
		 .maximum = 255,
		 .step = 0x1,
		 .default_value = 0x10,
		 .flags = 0,
	}, {
		.id = V4L2_CID_HUE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Hue",
		.minimum = -128,
		.maximum = 127,
		.step = 0x1,
		.default_value = 0x10,
		.flags = 0,
	}
};

struct tvp5150 {
	struct i2c_client *client;

	int norm;
	int input;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;
};

static int tvp5150_read(struct i2c_client *c, unsigned char addr)
{
	unsigned char buffer[1];
	int rc;

	buffer[0] = addr;
	if (1 != (rc = i2c_master_send(c, buffer, 1)))
		tvp5150_dbg(0, "i2c i/o error: rc == %d (should be 1)\n", rc);

	msleep(10);

	if (1 != (rc = i2c_master_recv(c, buffer, 1)))
		tvp5150_dbg(0, "i2c i/o error: rc == %d (should be 1)\n", rc);

	tvp5150_dbg(2, "tvp5150: read 0x%02x = 0x%02x\n", addr, buffer[0]);

	return (buffer[0]);
}

static inline void tvp5150_write(struct i2c_client *c, unsigned char addr,
				 unsigned char value)
{
	unsigned char buffer[2];
	int rc;

	buffer[0] = addr;
	buffer[1] = value;
	tvp5150_dbg(2, "tvp5150: writing 0x%02x 0x%02x\n", buffer[0], buffer[1]);
	if (2 != (rc = i2c_master_send(c, buffer, 2)))
		tvp5150_dbg(0, "i2c i/o error: rc == %d (should be 2)\n", rc);
}

static void dump_reg(struct i2c_client *c)
{
	printk("tvp5150: Video input source selection #1 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VD_IN_SRC_SEL_1));
	printk("tvp5150: Analog channel controls = 0x%02x\n",
	       tvp5150_read(c, TVP5150_ANAL_CHL_CTL));
	printk("tvp5150: Operation mode controls = 0x%02x\n",
	       tvp5150_read(c, TVP5150_OP_MODE_CTL));
	printk("tvp5150: Miscellaneous controls = 0x%02x\n",
	       tvp5150_read(c, TVP5150_MISC_CTL));
	printk("tvp5150: Autoswitch mask: TVP5150A / TVP5150AM = 0x%02x\n",
	       tvp5150_read(c, TVP5150_AUTOSW_MSK));
	printk("tvp5150: Color killer threshold control = 0x%02x\n",
	       tvp5150_read(c, TVP5150_COLOR_KIL_THSH_CTL));
	printk("tvp5150: Luminance processing control #1 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LUMA_PROC_CTL_1));
	printk("tvp5150: Luminance processing control #2 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LUMA_PROC_CTL_2));
	printk("tvp5150: Brightness control = 0x%02x\n",
	       tvp5150_read(c, TVP5150_BRIGHT_CTL));
	printk("tvp5150: Color saturation control = 0x%02x\n",
	       tvp5150_read(c, TVP5150_SATURATION_CTL));
	printk("tvp5150: Hue control = 0x%02x\n",
	       tvp5150_read(c, TVP5150_HUE_CTL));
	printk("tvp5150: Contrast control = 0x%02x\n",
	       tvp5150_read(c, TVP5150_CONTRAST_CTL));
	printk("tvp5150: Outputs and data rates select = 0x%02x\n",
	       tvp5150_read(c, TVP5150_DATA_RATE_SEL));
	printk("tvp5150: Luminance processing control #3 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LUMA_PROC_CTL_3));
	printk("tvp5150: Configuration shared pins = 0x%02x\n",
	       tvp5150_read(c, TVP5150_CONF_SHARED_PIN));
	printk("tvp5150: Active video cropping start MSB = 0x%02x\n",
	       tvp5150_read(c, TVP5150_ACT_VD_CROP_ST_MSB));
	printk("tvp5150: Active video cropping start LSB = 0x%02x\n",
	       tvp5150_read(c, TVP5150_ACT_VD_CROP_ST_LSB));
	printk("tvp5150: Active video cropping stop MSB = 0x%02x\n",
	       tvp5150_read(c, TVP5150_ACT_VD_CROP_STP_MSB));
	printk("tvp5150: Active video cropping stop LSB = 0x%02x\n",
	       tvp5150_read(c, TVP5150_ACT_VD_CROP_STP_LSB));
	printk("tvp5150: Genlock/RTC = 0x%02x\n",
	       tvp5150_read(c, TVP5150_GENLOCK));
	printk("tvp5150: Horizontal sync start = 0x%02x\n",
	       tvp5150_read(c, TVP5150_HORIZ_SYNC_START));
	printk("tvp5150: Vertical blanking start = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VERT_BLANKING_START));
	printk("tvp5150: Vertical blanking stop = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VERT_BLANKING_STOP));
	printk("tvp5150: Chrominance processing control #1 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_CHROMA_PROC_CTL_1));
	printk("tvp5150: Chrominance processing control #2 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_CHROMA_PROC_CTL_2));
	printk("tvp5150: Interrupt reset register B = 0x%02x\n",
	       tvp5150_read(c, TVP5150_INT_RESET_REG_B));
	printk("tvp5150: Interrupt enable register B = 0x%02x\n",
	       tvp5150_read(c, TVP5150_INT_ENABLE_REG_B));
	printk("tvp5150: Interrupt configuration register B = 0x%02x\n",
	       tvp5150_read(c, TVP5150_INTT_CONFIG_REG_B));
	printk("tvp5150: Video standard = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VIDEO_STD));
	printk("tvp5150: Cb gain factor = 0x%02x\n",
	       tvp5150_read(c, TVP5150_CB_GAIN_FACT));
	printk("tvp5150: Cr gain factor = 0x%02x\n",
	       tvp5150_read(c, TVP5150_CR_GAIN_FACTOR));
	printk("tvp5150: Macrovision on counter = 0x%02x\n",
	       tvp5150_read(c, TVP5150_MACROVISION_ON_CTR));
	printk("tvp5150: Macrovision off counter = 0x%02x\n",
	       tvp5150_read(c, TVP5150_MACROVISION_OFF_CTR));
	printk("tvp5150: revision select (TVP5150AM1 only) = 0x%02x\n",
	       tvp5150_read(c, TVP5150_REV_SELECT));
	printk("tvp5150: MSB of device ID = 0x%02x\n",
	       tvp5150_read(c, TVP5150_MSB_DEV_ID));
	printk("tvp5150: LSB of device ID = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LSB_DEV_ID));
	printk("tvp5150: ROM major version = 0x%02x\n",
	       tvp5150_read(c, TVP5150_ROM_MAJOR_VER));
	printk("tvp5150: ROM minor version = 0x%02x\n",
	       tvp5150_read(c, TVP5150_ROM_MINOR_VER));
	printk("tvp5150: Vertical line count MSB = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VERT_LN_COUNT_MSB));
	printk("tvp5150: Vertical line count LSB = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VERT_LN_COUNT_LSB));
	printk("tvp5150: Interrupt status register B = 0x%02x\n",
	       tvp5150_read(c, TVP5150_INT_STATUS_REG_B));
	printk("tvp5150: Interrupt active register B = 0x%02x\n",
	       tvp5150_read(c, TVP5150_INT_ACTIVE_REG_B));
	printk("tvp5150: Status register #1 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_STATUS_REG_1));
	printk("tvp5150: Status register #2 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_STATUS_REG_2));
	printk("tvp5150: Status register #3 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_STATUS_REG_3));
	printk("tvp5150: Status register #4 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_STATUS_REG_4));
	printk("tvp5150: Status register #5 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_STATUS_REG_5));
	printk("tvp5150: Closed caption data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_CC_DATA_REG1));
	printk("tvp5150: Closed caption data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_CC_DATA_REG2));
	printk("tvp5150: Closed caption data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_CC_DATA_REG3));
	printk("tvp5150: Closed caption data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_CC_DATA_REG4));
	printk("tvp5150: WSS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_WSS_DATA_REG1));
	printk("tvp5150: WSS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_WSS_DATA_REG2));
	printk("tvp5150: WSS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_WSS_DATA_REG3));
	printk("tvp5150: WSS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_WSS_DATA_REG4));
	printk("tvp5150: WSS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_WSS_DATA_REG5));
	printk("tvp5150: WSS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_WSS_DATA_REG6));
	printk("tvp5150: VPS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VPS_DATA_REG1));
	printk("tvp5150: VPS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VPS_DATA_REG2));
	printk("tvp5150: VPS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VPS_DATA_REG3));
	printk("tvp5150: VPS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VPS_DATA_REG4));
	printk("tvp5150: VPS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VPS_DATA_REG5));
	printk("tvp5150: VPS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VPS_DATA_REG6));
	printk("tvp5150: VPS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VPS_DATA_REG7));
	printk("tvp5150: VPS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VPS_DATA_REG8));
	printk("tvp5150: VPS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VPS_DATA_REG9));
	printk("tvp5150: VPS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VPS_DATA_REG10));
	printk("tvp5150: VPS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VPS_DATA_REG11));
	printk("tvp5150: VPS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VPS_DATA_REG12));
	printk("tvp5150: VPS data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VPS_DATA_REG13));
	printk("tvp5150: VITC data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VITC_DATA_REG1));
	printk("tvp5150: VITC data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VITC_DATA_REG2));
	printk("tvp5150: VITC data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VITC_DATA_REG3));
	printk("tvp5150: VITC data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VITC_DATA_REG4));
	printk("tvp5150: VITC data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VITC_DATA_REG5));
	printk("tvp5150: VITC data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VITC_DATA_REG6));
	printk("tvp5150: VITC data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VITC_DATA_REG7));
	printk("tvp5150: VITC data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VITC_DATA_REG8));
	printk("tvp5150: VITC data registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VITC_DATA_REG9));
	printk("tvp5150: VBI FIFO read data = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VBI_FIFO_READ_DATA));
	printk("tvp5150: Teletext filter 1 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_TELETEXT_FIL_1_1));
	printk("tvp5150: Teletext filter 1 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_TELETEXT_FIL_1_2));
	printk("tvp5150: Teletext filter 1 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_TELETEXT_FIL_1_3));
	printk("tvp5150: Teletext filter 1 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_TELETEXT_FIL_1_4));
	printk("tvp5150: Teletext filter 1 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_TELETEXT_FIL_1_5));
	printk("tvp5150: Teletext filter 2 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_TELETEXT_FIL_2_1));
	printk("tvp5150: Teletext filter 2 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_TELETEXT_FIL_2_2));
	printk("tvp5150: Teletext filter 2 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_TELETEXT_FIL_2_3));
	printk("tvp5150: Teletext filter 2 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_TELETEXT_FIL_2_4));
	printk("tvp5150: Teletext filter 2 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_TELETEXT_FIL_2_5));
	printk("tvp5150: Teletext filter enable = 0x%02x\n",
	       tvp5150_read(c, TVP5150_TELETEXT_FIL_ENA));
	printk("tvp5150: Interrupt status register A = 0x%02x\n",
	       tvp5150_read(c, TVP5150_INT_STATUS_REG_A));
	printk("tvp5150: Interrupt enable register A = 0x%02x\n",
	       tvp5150_read(c, TVP5150_INT_ENABLE_REG_A));
	printk("tvp5150: Interrupt configuration = 0x%02x\n",
	       tvp5150_read(c, TVP5150_INT_CONF));
	printk("tvp5150: VDP configuration RAM data = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VDP_CONF_RAM_DATA));
	printk("tvp5150: Configuration RAM address low byte = 0x%02x\n",
	       tvp5150_read(c, TVP5150_CONF_RAM_ADDR_LOW));
	printk("tvp5150: Configuration RAM address high byte = 0x%02x\n",
	       tvp5150_read(c, TVP5150_CONF_RAM_ADDR_HIGH));
	printk("tvp5150: VDP status register = 0x%02x\n",
	       tvp5150_read(c, TVP5150_VDP_STATUS_REG));
	printk("tvp5150: FIFO word count = 0x%02x\n",
	       tvp5150_read(c, TVP5150_FIFO_WORD_COUNT));
	printk("tvp5150: FIFO interrupt threshold = 0x%02x\n",
	       tvp5150_read(c, TVP5150_FIFO_INT_THRESHOLD));
	printk("tvp5150: FIFO reset = 0x%02x\n",
	       tvp5150_read(c, TVP5150_FIFO_RESET));
	printk("tvp5150: Line number interrupt = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_NUMBER_INT));
	printk("tvp5150: Pixel alignment register low byte = 0x%02x\n",
	       tvp5150_read(c, TVP5150_PIX_ALIGN_REG_LOW));
	printk("tvp5150: Pixel alignment register high byte = 0x%02x\n",
	       tvp5150_read(c, TVP5150_PIX_ALIGN_REG_HIGH));
	printk("tvp5150: FIFO output control = 0x%02x\n",
	       tvp5150_read(c, TVP5150_FIFO_OUT_CTRL));
	printk("tvp5150: Full field enable 1 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_FULL_FIELD_ENA_1));
	printk("tvp5150: Full field enable 2 = 0x%02x\n",
	       tvp5150_read(c, TVP5150_FULL_FIELD_ENA_2));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_1));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_2));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_3));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_4));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_5));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_6));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_7));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_8));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_9));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_10));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_11));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_12));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_13));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_14));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_15));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_16));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_17));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_18));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_19));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_20));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_21));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_22));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_23));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_24));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_25));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_27));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_28));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_29));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_30));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_31));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_32));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_33));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_34));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_35));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_36));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_37));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_38));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_39));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_40));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_41));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_42));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_43));
	printk("tvp5150: Line mode registers = 0x%02x\n",
	       tvp5150_read(c, TVP5150_LINE_MODE_REG_44));
	printk("tvp5150: Full field mode register = 0x%02x\n",
	       tvp5150_read(c, TVP5150_FULL_FIELD_MODE_REG));
}

/****************************************************************************
			Basic functions
 ****************************************************************************/
enum tvp5150_input {
	TVP5150_ANALOG_CH0 = 0,
	TVP5150_SVIDEO = 1,
	TVP5150_ANALOG_CH1 = 2,
	TVP5150_BLACK_SCREEN = 8
};

static inline void tvp5150_selmux(struct i2c_client *c,
				  enum tvp5150_input input)
{
	int opmode=0;

	struct tvp5150 *decoder = i2c_get_clientdata(c);

	if (!decoder->enable)
		input |= TVP5150_BLACK_SCREEN;

	switch (input) {
	case TVP5150_ANALOG_CH0:
	case TVP5150_ANALOG_CH1:
		opmode=0x30;		/* TV Mode */
		break;
	default:
		opmode=0;		/* Auto Mode */
		break;
	}

	tvp5150_write(c, TVP5150_OP_MODE_CTL, opmode);
	tvp5150_write(c, TVP5150_VD_IN_SRC_SEL_1, input);
};

struct i2c_reg_value {
	unsigned char reg;
	unsigned char value;
};

/* Default values as sugested at TVP5150AM1 datasheet */
static const struct i2c_reg_value tvp5150_init_default[] = {
	{ /* 0x00 */
		TVP5150_VD_IN_SRC_SEL_1,0x00
	},
	{ /* 0x01 */
		TVP5150_ANAL_CHL_CTL,0x15
	},
	{ /* 0x02 */
		TVP5150_OP_MODE_CTL,0x00
	},
	{ /* 0x03 */
		TVP5150_MISC_CTL,0x01
	},
	{ /* 0x06 */
		TVP5150_COLOR_KIL_THSH_CTL,0x10
	},
	{ /* 0x07 */
		TVP5150_LUMA_PROC_CTL_1,0x60
	},
	{ /* 0x08 */
		TVP5150_LUMA_PROC_CTL_2,0x00
	},
	{ /* 0x09 */
		TVP5150_BRIGHT_CTL,0x80
	},
	{ /* 0x0a */
		TVP5150_SATURATION_CTL,0x80
	},
	{ /* 0x0b */
		TVP5150_HUE_CTL,0x00
	},
	{ /* 0x0c */
		TVP5150_CONTRAST_CTL,0x80
	},
	{ /* 0x0d */
		TVP5150_DATA_RATE_SEL,0x47
	},
	{ /* 0x0e */
		TVP5150_LUMA_PROC_CTL_3,0x00
	},
	{ /* 0x0f */
		TVP5150_CONF_SHARED_PIN,0x08
	},
	{ /* 0x11 */
		TVP5150_ACT_VD_CROP_ST_MSB,0x00
	},
	{ /* 0x12 */
		TVP5150_ACT_VD_CROP_ST_LSB,0x00
	},
	{ /* 0x13 */
		TVP5150_ACT_VD_CROP_STP_MSB,0x00
	},
	{ /* 0x14 */
		TVP5150_ACT_VD_CROP_STP_LSB,0x00
	},
	{ /* 0x15 */
		TVP5150_GENLOCK,0x01
	},
	{ /* 0x16 */
		TVP5150_HORIZ_SYNC_START,0x80
	},
	{ /* 0x18 */
		TVP5150_VERT_BLANKING_START,0x00
	},
	{ /* 0x19 */
		TVP5150_VERT_BLANKING_STOP,0x00
	},
	{ /* 0x1a */
		TVP5150_CHROMA_PROC_CTL_1,0x0c
	},
	{ /* 0x1b */
		TVP5150_CHROMA_PROC_CTL_2,0x14
	},
	{ /* 0x1c */
		TVP5150_INT_RESET_REG_B,0x00
	},
	{ /* 0x1d */
		TVP5150_INT_ENABLE_REG_B,0x00
	},
	{ /* 0x1e */
		TVP5150_INTT_CONFIG_REG_B,0x00
	},
	{ /* 0x28 */
		TVP5150_VIDEO_STD,0x00
	},
	{ /* 0x2e */
		TVP5150_MACROVISION_ON_CTR,0x0f
	},
	{ /* 0x2f */
		TVP5150_MACROVISION_OFF_CTR,0x01
	},
	{ /* 0xbb */
		TVP5150_TELETEXT_FIL_ENA,0x00
	},
	{ /* 0xc0 */
		TVP5150_INT_STATUS_REG_A,0x00
	},
	{ /* 0xc1 */
		TVP5150_INT_ENABLE_REG_A,0x00
	},
	{ /* 0xc2 */
		TVP5150_INT_CONF,0x04
	},
	{ /* 0xc8 */
		TVP5150_FIFO_INT_THRESHOLD,0x80
	},
	{ /* 0xc9 */
		TVP5150_FIFO_RESET,0x00
	},
	{ /* 0xca */
		TVP5150_LINE_NUMBER_INT,0x00
	},
	{ /* 0xcb */
		TVP5150_PIX_ALIGN_REG_LOW,0x4e
	},
	{ /* 0xcc */
		TVP5150_PIX_ALIGN_REG_HIGH,0x00
	},
	{ /* 0xcd */
		TVP5150_FIFO_OUT_CTRL,0x01
	},
	{ /* 0xcf */
		TVP5150_FULL_FIELD_ENA_1,0x00
	},
	{ /* 0xd0 */
		TVP5150_FULL_FIELD_ENA_2,0x00
	},
	{ /* 0xfc */
		TVP5150_FULL_FIELD_MODE_REG,0x7f
	},
	{ /* end of data */
		0xff,0xff
	}
};

/* Default values as sugested at TVP5150AM1 datasheet */
static const struct i2c_reg_value tvp5150_init_enable[] = {
	{
		TVP5150_CONF_SHARED_PIN, 2
	},{	/* Automatic offset and AGC enabled */
		TVP5150_ANAL_CHL_CTL, 0x15
	},{	/* Activate YCrCb output 0x9 or 0xd ? */
		TVP5150_MISC_CTL, 0x6f
	},{	/* Activates video std autodetection for all standards */
		TVP5150_AUTOSW_MSK, 0x0
	},{	/* Default format: 0x47. For 4:2:2: 0x40 */
		TVP5150_DATA_RATE_SEL, 0x47
	},{
		TVP5150_CHROMA_PROC_CTL_1, 0x0c
	},{
		TVP5150_CHROMA_PROC_CTL_2, 0x54
	},{	/* Non documented, but initialized on WinTV USB2 */
		0x27, 0x20
	},{
		0xff,0xff
	}
};

struct i2c_vbi_ram_value {
	u16 reg;
	unsigned char values[26];
};

static struct i2c_vbi_ram_value vbi_ram_default[] =
{
	{0x010, /* WST SECAM 6 */
		{ 0xaa, 0xaa, 0xff, 0xff , 0xe7, 0x2e, 0x20, 0x26, 0xe6, 0xb4, 0x0e, 0x0, 0x0, 0x0, 0x10, 0x0 }
	},
	{0x030, /* WST PAL B 6 */
		{ 0xaa, 0xaa, 0xff, 0xff , 0x27, 0x2e, 0x20, 0x2b, 0xa6, 0x72, 0x10, 0x0, 0x0, 0x0, 0x10, 0x0 }
	},
	{0x050, /* WST PAL C 6 */
		{ 0xaa, 0xaa, 0xff, 0xff , 0xe7, 0x2e, 0x20, 0x22, 0xa6, 0x98, 0x0d, 0x0, 0x0, 0x0, 0x10, 0x0 }
	},
	{0x070, /* WST NTSC 6 */
		{ 0xaa, 0xaa, 0xff, 0xff , 0x27, 0x2e, 0x20, 0x23, 0x69, 0x93, 0x0d, 0x0, 0x0, 0x0, 0x10, 0x0 }
	},
	{0x090, /* NABTS, NTSC 6 */
		{ 0xaa, 0xaa, 0xff, 0xff , 0xe7, 0x2e, 0x20, 0x22, 0x69, 0x93, 0x0d, 0x0, 0x0, 0x0, 0x15, 0x0 }
	},
	{0x0b0, /* NABTS, NTSC-J 6 */
		{ 0xaa, 0xaa, 0xff, 0xff , 0xa7, 0x2e, 0x20, 0x23, 0x69, 0x93, 0x0d, 0x0, 0x0, 0x0, 0x10, 0x0 }
	},
	{0x0d0, /* CC, PAL/SECAM 6 */
		{ 0xaa, 0x2a, 0xff, 0x3f , 0x04, 0x51, 0x6e, 0x02, 0xa6, 0x7b, 0x09, 0x0, 0x0, 0x0, 0x27, 0x0 }
	},
	{0x0f0, /* CC, NTSC 6 */
		{ 0xaa, 0x2a, 0xff, 0x3f , 0x04, 0x51, 0x6e, 0x02, 0x69, 0x8c, 0x09, 0x0, 0x0, 0x0, 0x27, 0x0 }
	},
	{0x110, /* WSS, PAL/SECAM 6 */
		{ 0x5b, 0x55, 0xc5, 0xff , 0x0, 0x71, 0x6e, 0x42, 0xa6, 0xcd, 0x0f, 0x0, 0x0, 0x0, 0x3a, 0x0 }
	},
	{0x130, /* WSS, NTSC C */
		{ 0x38, 0x00, 0x3f, 0x00 , 0x0, 0x71, 0x6e, 0x43, 0x69, 0x7c, 0x08, 0x0, 0x0, 0x0, 0x39, 0x0 }
	},
	{0x150, /* VITC, PAL/SECAM 6 */
		{ 0x0, 0x0, 0x0, 0x0 , 0x0, 0x8f, 0x6d, 0x49, 0xa6, 0x85, 0x08, 0x0, 0x0, 0x0, 0x4c, 0x0 }
	},
	{0x170, /* VITC, NTSC 6 */
		{ 0x0, 0x0, 0x0, 0x0 , 0x0, 0x8f, 0x6d, 0x49, 0x69, 0x94, 0x08, 0x0, 0x0, 0x0, 0x4c, 0x0 }
	},
	{ (u16)-1 }
};

static int tvp5150_write_inittab(struct i2c_client *c,
				 const struct i2c_reg_value *regs)
{
	while (regs->reg != 0xff) {
		tvp5150_write(c, regs->reg, regs->value);
		regs++;
	}
	return 0;
}

static int tvp5150_vdp_init(struct i2c_client *c,
				 const struct i2c_vbi_ram_value *regs)
{
	unsigned int i;

	/* Disable Full Field */
	tvp5150_write(c, TVP5150_FULL_FIELD_ENA_1, 0);

	/* Before programming, Line mode should be at 0xff */
	for (i=TVP5150_FULL_FIELD_ENA_2; i<=TVP5150_LINE_MODE_REG_44; i++)
		tvp5150_write(c, i, 0xff);

	/* Load Ram Table */
	while (regs->reg != (u16)-1 ) {
		tvp5150_write(c, TVP5150_CONF_RAM_ADDR_HIGH,regs->reg>>8);
		tvp5150_write(c, TVP5150_CONF_RAM_ADDR_LOW,regs->reg);

		for (i=0;i<16;i++)
			tvp5150_write(c, TVP5150_VDP_CONF_RAM_DATA,regs->values[i]);

		regs++;
	}
	return 0;
}

static int tvp5150_set_std(struct i2c_client *c, v4l2_std_id std)
{
	struct tvp5150 *decoder = i2c_get_clientdata(c);
	int fmt=0;

	decoder->norm=std;

	/* First tests should be against specific std */

	if (std == V4L2_STD_ALL) {
		fmt=0;	/* Autodetect mode */
	} else if (std & V4L2_STD_NTSC_443) {
		fmt=0xa;
	} else if (std & V4L2_STD_PAL_M) {
		fmt=0x6;
	} else if (std & (V4L2_STD_PAL_N| V4L2_STD_PAL_Nc)) {
		fmt=0x8;
	} else {
		/* Then, test against generic ones */
		if (std & V4L2_STD_NTSC) {
			fmt=0x2;
		} else if (std & V4L2_STD_PAL) {
			fmt=0x4;
		} else if (std & V4L2_STD_SECAM) {
			fmt=0xc;
		}
	}

	tvp5150_dbg(1,"Set video std register to %d.\n",fmt);
	tvp5150_write(c, TVP5150_VIDEO_STD, fmt);

	return 0;
}

static inline void tvp5150_reset(struct i2c_client *c)
{
	u8 msb_id, lsb_id, msb_rom, lsb_rom;
	struct tvp5150 *decoder = i2c_get_clientdata(c);

	msb_id=tvp5150_read(c,TVP5150_MSB_DEV_ID);
	lsb_id=tvp5150_read(c,TVP5150_LSB_DEV_ID);
	msb_rom=tvp5150_read(c,TVP5150_ROM_MAJOR_VER);
	lsb_rom=tvp5150_read(c,TVP5150_ROM_MINOR_VER);

	if ((msb_rom==4)&&(lsb_rom==0)) { /* Is TVP5150AM1 */
		tvp5150_info("tvp%02x%02xam1 detected.\n",msb_id, lsb_id);

		/* ITU-T BT.656.4 timing */
		tvp5150_write(c,TVP5150_REV_SELECT,0);
	} else {
		if ((msb_rom==3)||(lsb_rom==0x21)) { /* Is TVP5150A */
			tvp5150_info("tvp%02x%02xa detected.\n",msb_id, lsb_id);
		} else {
			tvp5150_info("*** unknown tvp%02x%02x chip detected.\n",msb_id,lsb_id);
			tvp5150_info("*** Rom ver is %d.%d\n",msb_rom,lsb_rom);
		}
	}

	/* Initializes TVP5150 to its default values */
	tvp5150_write_inittab(c, tvp5150_init_default);

	/* Initializes VDP registers */
	tvp5150_vdp_init(c, vbi_ram_default);

	/* Selects decoder input */
	tvp5150_selmux(c, decoder->input);

	/* Initializes TVP5150 to stream enabled values */
	tvp5150_write_inittab(c, tvp5150_init_enable);

	/* Initialize image preferences */
	tvp5150_write(c, TVP5150_BRIGHT_CTL, decoder->bright >> 8);
	tvp5150_write(c, TVP5150_CONTRAST_CTL, decoder->contrast >> 8);
	tvp5150_write(c, TVP5150_SATURATION_CTL, decoder->contrast >> 8);
	tvp5150_write(c, TVP5150_HUE_CTL, (decoder->hue - 32768) >> 8);

	tvp5150_set_std(c, decoder->norm);
};

static int tvp5150_get_ctrl(struct i2c_client *c, struct v4l2_control *ctrl)
{
/*	struct tvp5150 *decoder = i2c_get_clientdata(c); */

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = tvp5150_read(c, TVP5150_BRIGHT_CTL);
		return 0;
	case V4L2_CID_CONTRAST:
		ctrl->value = tvp5150_read(c, TVP5150_CONTRAST_CTL);
		return 0;
	case V4L2_CID_SATURATION:
		ctrl->value = tvp5150_read(c, TVP5150_SATURATION_CTL);
		return 0;
	case V4L2_CID_HUE:
		ctrl->value = tvp5150_read(c, TVP5150_HUE_CTL);
		return 0;
	}
	return -EINVAL;
}

static int tvp5150_set_ctrl(struct i2c_client *c, struct v4l2_control *ctrl)
{
/*	struct tvp5150 *decoder = i2c_get_clientdata(c); */

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		tvp5150_write(c, TVP5150_BRIGHT_CTL, ctrl->value);
		return 0;
	case V4L2_CID_CONTRAST:
		tvp5150_write(c, TVP5150_CONTRAST_CTL, ctrl->value);
		return 0;
	case V4L2_CID_SATURATION:
		tvp5150_write(c, TVP5150_SATURATION_CTL, ctrl->value);
		return 0;
	case V4L2_CID_HUE:
		tvp5150_write(c, TVP5150_HUE_CTL, ctrl->value);
		return 0;
	}
	return -EINVAL;
}

/****************************************************************************
			I2C Command
 ****************************************************************************/
static int tvp5150_command(struct i2c_client *c,
			   unsigned int cmd, void *arg)
{
	struct tvp5150 *decoder = i2c_get_clientdata(c);

	switch (cmd) {

	case 0:
	case VIDIOC_INT_RESET:
	case DECODER_INIT:
		tvp5150_reset(c);
		break;
	case VIDIOC_S_STD:
		if (decoder->norm == *(v4l2_std_id *)arg)
			break;
		return tvp5150_set_std(c, *(v4l2_std_id *)arg);
	case VIDIOC_G_STD:
		*(v4l2_std_id *)arg = decoder->norm;
		break;

#ifdef CONFIG_VIDEO_ADV_DEBUG
	case VIDIOC_INT_G_REGISTER:
	{
		struct v4l2_register *reg = arg;

		if (reg->i2c_id != I2C_DRIVERID_TVP5150)
			return -EINVAL;
		reg->val = tvp5150_read(c, reg->reg & 0xff);
		break;
	}

	case VIDIOC_INT_S_REGISTER:
	{
		struct v4l2_register *reg = arg;

		if (reg->i2c_id != I2C_DRIVERID_TVP5150)
			return -EINVAL;
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		tvp5150_write(c, reg->reg & 0xff, reg->val & 0xff);
		break;
	}
#endif

	case DECODER_DUMP:
		dump_reg(c);
		break;

	case DECODER_GET_CAPABILITIES:
		{
			struct video_decoder_capability *cap = arg;

			cap->flags = VIDEO_DECODER_PAL |
			    VIDEO_DECODER_NTSC |
			    VIDEO_DECODER_SECAM |
			    VIDEO_DECODER_AUTO | VIDEO_DECODER_CCIR;
			cap->inputs = 3;
			cap->outputs = 1;
			break;
		}
	case DECODER_GET_STATUS:
		{
			int *iarg = arg;
			int status;
			int res=0;
			status = tvp5150_read(c, 0x88);
			if(status&0x08){
				res |= DECODER_STATUS_COLOR;
			}
			if(status&0x04 && status&0x02){
				res |= DECODER_STATUS_GOOD;
			}
			*iarg=res;
			break;
		}

	case DECODER_SET_GPIO:
		break;

	case DECODER_SET_VBI_BYPASS:
		break;

	case DECODER_SET_NORM:
		{
			int *iarg = arg;

			switch (*iarg) {

			case VIDEO_MODE_NTSC:
				break;

			case VIDEO_MODE_PAL:
				break;

			case VIDEO_MODE_SECAM:
				break;

			case VIDEO_MODE_AUTO:
				break;

			default:
				return -EINVAL;

			}
			decoder->norm = *iarg;
			break;
		}
	case DECODER_SET_INPUT:
		{
			int *iarg = arg;
			if (*iarg < 0 || *iarg > 3) {
				return -EINVAL;
			}

			decoder->input = *iarg;
			tvp5150_selmux(c, decoder->input);

			break;
		}
	case DECODER_SET_OUTPUT:
		{
			int *iarg = arg;

			/* not much choice of outputs */
			if (*iarg != 0) {
				return -EINVAL;
			}
			break;
		}
	case DECODER_ENABLE_OUTPUT:
		{
			int *iarg = arg;

			decoder->enable = (*iarg != 0);

			tvp5150_selmux(c, decoder->input);

			break;
		}
	case VIDIOC_QUERYCTRL:
		{
			struct v4l2_queryctrl *qc = arg;
			int i;

			tvp5150_dbg(1, "VIDIOC_QUERYCTRL called\n");

			for (i = 0; i < ARRAY_SIZE(tvp5150_qctrl); i++)
				if (qc->id && qc->id == tvp5150_qctrl[i].id) {
					memcpy(qc, &(tvp5150_qctrl[i]),
					       sizeof(*qc));
					return 0;
				}

			return -EINVAL;
		}
	case VIDIOC_G_CTRL:
		{
			struct v4l2_control *ctrl = arg;
			tvp5150_dbg(1, "VIDIOC_G_CTRL called\n");

			return tvp5150_get_ctrl(c, ctrl);
		}
	case VIDIOC_S_CTRL:
		{
			struct v4l2_control *ctrl = arg;
			u8 i, n;
			n = sizeof(tvp5150_qctrl) / sizeof(tvp5150_qctrl[0]);
			for (i = 0; i < n; i++)
				if (ctrl->id == tvp5150_qctrl[i].id) {
					if (ctrl->value <
					    tvp5150_qctrl[i].minimum
					    || ctrl->value >
					    tvp5150_qctrl[i].maximum)
						return -ERANGE;
					tvp5150_dbg(1,
						"VIDIOC_S_CTRL: id=%d, value=%d\n",
						ctrl->id, ctrl->value);
					return tvp5150_set_ctrl(c, ctrl);
				}
			return -EINVAL;
		}

	case DECODER_SET_PICTURE:
		{
			struct video_picture *pic = arg;
			if (decoder->bright != pic->brightness) {
				/* We want 0 to 255 we get 0-65535 */
				decoder->bright = pic->brightness;
				tvp5150_write(c, TVP5150_BRIGHT_CTL,
					      decoder->bright >> 8);
			}
			if (decoder->contrast != pic->contrast) {
				/* We want 0 to 255 we get 0-65535 */
				decoder->contrast = pic->contrast;
				tvp5150_write(c, TVP5150_CONTRAST_CTL,
					      decoder->contrast >> 8);
			}
			if (decoder->sat != pic->colour) {
				/* We want 0 to 255 we get 0-65535 */
				decoder->sat = pic->colour;
				tvp5150_write(c, TVP5150_SATURATION_CTL,
					      decoder->contrast >> 8);
			}
			if (decoder->hue != pic->hue) {
				/* We want -128 to 127 we get 0-65535 */
				decoder->hue = pic->hue;
				tvp5150_write(c, TVP5150_HUE_CTL,
					      (decoder->hue - 32768) >> 8);
			}
			break;
		}
	default:
		return -EINVAL;
	}

	return 0;
}

/****************************************************************************
			I2C Client & Driver
 ****************************************************************************/
static struct i2c_driver driver;

static struct i2c_client client_template = {
	.name = "(unset)",
	.driver = &driver,
};

static int tvp5150_detect_client(struct i2c_adapter *adapter,
				 int address, int kind)
{
	struct i2c_client *c;
	struct tvp5150 *core;
	int rv;

	if (debug)
		printk( KERN_INFO
		"tvp5150.c: detecting tvp5150 client on address 0x%x\n",
		address << 1);

	client_template.adapter = adapter;
	client_template.addr = address;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality
	    (adapter,
	     I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return 0;

	c = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (c == 0)
		return -ENOMEM;
	memcpy(c, &client_template, sizeof(struct i2c_client));

	core = kzalloc(sizeof(struct tvp5150), GFP_KERNEL);
	if (core == 0) {
		kfree(c);
		return -ENOMEM;
	}
	i2c_set_clientdata(c, core);

	rv = i2c_attach_client(c);

	core->norm = V4L2_STD_ALL;
	core->input = 2;
	core->enable = 1;
	core->bright = 32768;
	core->contrast = 32768;
	core->hue = 32768;
	core->sat = 32768;

	if (rv) {
		kfree(c);
		kfree(core);
		return rv;
	}

	if (debug > 1)
		dump_reg(c);
	return 0;
}

static int tvp5150_attach_adapter(struct i2c_adapter *adapter)
{
	if (debug)
		printk( KERN_INFO
		"tvp5150.c: starting probe for adapter %s (0x%x)\n",
		adapter->name, adapter->id);
	return i2c_probe(adapter, &addr_data, &tvp5150_detect_client);
}

static int tvp5150_detach_client(struct i2c_client *c)
{
	struct tvp5150 *decoder = i2c_get_clientdata(c);
	int err;

	tvp5150_dbg(1,
		"tvp5150.c: removing tvp5150 adapter on address 0x%x\n",
		c->addr << 1);

	err = i2c_detach_client(c);
	if (err) {
		return err;
	}

	kfree(decoder);
	kfree(c);

	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver driver = {
	.driver = {
		.name = "tvp5150",
	},
	.id = I2C_DRIVERID_TVP5150,

	.attach_adapter = tvp5150_attach_adapter,
	.detach_client = tvp5150_detach_client,

	.command = tvp5150_command,
};

static int __init tvp5150_init(void)
{
	return i2c_add_driver(&driver);
}

static void __exit tvp5150_exit(void)
{
	i2c_del_driver(&driver);
}

module_init(tvp5150_init);
module_exit(tvp5150_exit);
