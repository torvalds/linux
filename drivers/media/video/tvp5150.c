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

#define dprintk(num, format, args...) \
	do { \
		if (debug >= num) \
			printk(format , ##args); \
	} while (0)

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

static inline int tvp5150_read(struct i2c_client *c, unsigned char addr)
{
	unsigned char buffer[1];
	int rc;

	buffer[0] = addr;
	if (1 != (rc = i2c_master_send(c, buffer, 1)))
		dprintk(0, "i2c i/o error: rc == %d (should be 1)\n", rc);

	msleep(10);

	if (1 != (rc = i2c_master_recv(c, buffer, 1)))
		dprintk(0, "i2c i/o error: rc == %d (should be 1)\n", rc);

	return (buffer[0]);
}

static inline void tvp5150_write(struct i2c_client *c, unsigned char addr,
				 unsigned char value)
{
	unsigned char buffer[2];
	int rc;
/*	struct tvp5150 *core = i2c_get_clientdata(c); */

	buffer[0] = addr;
	buffer[1] = value;
	dprintk(1, "tvp5150: writing 0x%02x 0x%02x\n", buffer[0], buffer[1]);
	if (2 != (rc = i2c_master_send(c, buffer, 2)))
		dprintk(0, "i2c i/o error: rc == %d (should be 2)\n", rc);
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
	struct tvp5150 *decoder = i2c_get_clientdata(c);

	if (!decoder->enable)
		input |= TVP5150_BLACK_SCREEN;

	tvp5150_write(c, TVP5150_VD_IN_SRC_SEL_1, input);
};

static inline void tvp5150_reset(struct i2c_client *c)
{
	struct tvp5150 *decoder = i2c_get_clientdata(c);

	tvp5150_write(c, TVP5150_CONF_SHARED_PIN, 2);

	/* Automatic offset and AGC enabled */
	tvp5150_write(c, TVP5150_ANAL_CHL_CTL, 0x15);

	/* Normal Operation */
//      tvp5150_write(c, TVP5150_OP_MODE_CTL, 0x00);

	/* Activate YCrCb output 0x9 or 0xd ? */
	tvp5150_write(c, TVP5150_MISC_CTL, 0x6f);

	/* Activates video std autodetection for all standards */
	tvp5150_write(c, TVP5150_AUTOSW_MSK, 0x0);

	/* Default format: 0x47, 4:2:2: 0x40 */
	tvp5150_write(c, TVP5150_DATA_RATE_SEL, 0x47);

	tvp5150_selmux(c, decoder->input);

	tvp5150_write(c, TVP5150_CHROMA_PROC_CTL_1, 0x0c);
	tvp5150_write(c, TVP5150_CHROMA_PROC_CTL_2, 0x54);

	tvp5150_write(c, 0x27, 0x20);	/* ?????????? */

	tvp5150_write(c, TVP5150_VIDEO_STD, 0x0);	/* Auto switch */

	tvp5150_write(c, TVP5150_BRIGHT_CTL, decoder->bright >> 8);
	tvp5150_write(c, TVP5150_CONTRAST_CTL, decoder->contrast >> 8);
	tvp5150_write(c, TVP5150_SATURATION_CTL, decoder->contrast >> 8);
	tvp5150_write(c, TVP5150_HUE_CTL, (decoder->hue - 32768) >> 8);
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
	default:
		return -EINVAL;
	}
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
	default:
		return -EINVAL;
	}
}

/****************************************************************************
			I2C Command
 ****************************************************************************/
static int tvp5150_command(struct i2c_client *client,
			   unsigned int cmd, void *arg)
{
	struct tvp5150 *decoder = i2c_get_clientdata(client);

	switch (cmd) {

	case 0:
	case DECODER_INIT:
		tvp5150_reset(client);
		break;

	case DECODER_DUMP:
		dump_reg(client);
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
			tvp5150_selmux(client, decoder->input);

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

			tvp5150_selmux(client, decoder->input);

			break;
		}
	case VIDIOC_QUERYCTRL:
		{
			struct v4l2_queryctrl *qc = arg;
			u8 i, n;

			dprintk(1, KERN_DEBUG "VIDIOC_QUERYCTRL");

			n = sizeof(tvp5150_qctrl) / sizeof(tvp5150_qctrl[0]);
			for (i = 0; i < n; i++)
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
			dprintk(1, KERN_DEBUG "VIDIOC_G_CTRL");

			return tvp5150_get_ctrl(client, ctrl);
		}
	case VIDIOC_S_CTRL_OLD:	/* ??? */
	case VIDIOC_S_CTRL:
		{
			struct v4l2_control *ctrl = arg;
			u8 i, n;
			dprintk(1, KERN_DEBUG "VIDIOC_S_CTRL");
			n = sizeof(tvp5150_qctrl) / sizeof(tvp5150_qctrl[0]);
			for (i = 0; i < n; i++)
				if (ctrl->id == tvp5150_qctrl[i].id) {
					if (ctrl->value <
					    tvp5150_qctrl[i].minimum
					    || ctrl->value >
					    tvp5150_qctrl[i].maximum)
						return -ERANGE;
					dprintk(1,
						KERN_DEBUG
						"VIDIOC_S_CTRL: id=%d, value=%d",
						ctrl->id, ctrl->value);
					return tvp5150_set_ctrl(client, ctrl);
				}
			return -EINVAL;
		}

	case DECODER_SET_PICTURE:
		{
			struct video_picture *pic = arg;
			if (decoder->bright != pic->brightness) {
				/* We want 0 to 255 we get 0-65535 */
				decoder->bright = pic->brightness;
				tvp5150_write(client, TVP5150_BRIGHT_CTL,
					      decoder->bright >> 8);
			}
			if (decoder->contrast != pic->contrast) {
				/* We want 0 to 255 we get 0-65535 */
				decoder->contrast = pic->contrast;
				tvp5150_write(client, TVP5150_CONTRAST_CTL,
					      decoder->contrast >> 8);
			}
			if (decoder->sat != pic->colour) {
				/* We want 0 to 255 we get 0-65535 */
				decoder->sat = pic->colour;
				tvp5150_write(client, TVP5150_SATURATION_CTL,
					      decoder->contrast >> 8);
			}
			if (decoder->hue != pic->hue) {
				/* We want -128 to 127 we get 0-65535 */
				decoder->hue = pic->hue;
				tvp5150_write(client, TVP5150_HUE_CTL,
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
	.flags = I2C_CLIENT_ALLOW_USE,
	.driver = &driver,
};

static int tvp5150_detect_client(struct i2c_adapter *adapter,
				 int address, int kind)
{
	struct i2c_client *client;
	struct tvp5150 *core;
	int rv;

	dprintk(1,
		KERN_INFO
		"tvp5150.c: detecting tvp5150 client on address 0x%x\n",
		address << 1);

	client_template.adapter = adapter;
	client_template.addr = address;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality
	    (adapter,
	     I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return 0;

	client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (client == 0)
		return -ENOMEM;
	memcpy(client, &client_template, sizeof(struct i2c_client));

	core = kmalloc(sizeof(struct tvp5150), GFP_KERNEL);
	if (core == 0) {
		kfree(client);
		return -ENOMEM;
	}
	memset(core, 0, sizeof(struct tvp5150));
	i2c_set_clientdata(client, core);

	rv = i2c_attach_client(client);

	core->norm = VIDEO_MODE_AUTO;
	core->input = 2;
	core->enable = 1;
	core->bright = 32768;
	core->contrast = 32768;
	core->hue = 32768;
	core->sat = 32768;

	if (rv) {
		kfree(client);
		kfree(core);
		return rv;
	}

	if (debug > 1)
		dump_reg(client);

	return 0;
}

static int tvp5150_attach_adapter(struct i2c_adapter *adapter)
{
	dprintk(1,
		KERN_INFO
		"tvp5150.c: starting probe for adapter %s (0x%x)\n",
		adapter->name, adapter->id);
	return i2c_probe(adapter, &addr_data, &tvp5150_detect_client);
}

static int tvp5150_detach_client(struct i2c_client *client)
{
	struct tvp5150 *decoder = i2c_get_clientdata(client);
	int err;

	err = i2c_detach_client(client);
	if (err) {
		return err;
	}

	kfree(decoder);
	kfree(client);

	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver driver = {
	.owner = THIS_MODULE,
	.name = "tvp5150",

	/* FIXME */
	.id = I2C_DRIVERID_SAA7110,
	.flags = I2C_DF_NOTIFY,

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
