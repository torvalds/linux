// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  Support for a cx23417 mpeg encoder via cx231xx host port.
 *
 *    (c) 2004 Jelle Foks <jelle@foks.us>
 *    (c) 2004 Gerd Knorr <kraxel@bytesex.org>
 *    (c) 2008 Steven Toth <stoth@linuxtv.org>
 *      - CX23885/7/8 support
 *
 *  Includes parts from the ivtv driver( http://ivtv.sourceforge.net/),
 */

#include "cx231xx.h"

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/drv-intf/cx2341x.h>
#include <media/tuner.h>

#define CX231xx_FIRM_IMAGE_SIZE 376836
#define CX231xx_FIRM_IMAGE_NAME "v4l-cx23885-enc.fw"

/* for polaris ITVC */
#define ITVC_WRITE_DIR          0x03FDFC00
#define ITVC_READ_DIR            0x0001FC00

#define  MCI_MEMORY_DATA_BYTE0          0x00
#define  MCI_MEMORY_DATA_BYTE1          0x08
#define  MCI_MEMORY_DATA_BYTE2          0x10
#define  MCI_MEMORY_DATA_BYTE3          0x18

#define  MCI_MEMORY_ADDRESS_BYTE2       0x20
#define  MCI_MEMORY_ADDRESS_BYTE1       0x28
#define  MCI_MEMORY_ADDRESS_BYTE0       0x30

#define  MCI_REGISTER_DATA_BYTE0        0x40
#define  MCI_REGISTER_DATA_BYTE1        0x48
#define  MCI_REGISTER_DATA_BYTE2        0x50
#define  MCI_REGISTER_DATA_BYTE3        0x58

#define  MCI_REGISTER_ADDRESS_BYTE0     0x60
#define  MCI_REGISTER_ADDRESS_BYTE1     0x68

#define  MCI_REGISTER_MODE              0x70

/* Read and write modes for polaris ITVC */
#define  MCI_MODE_REGISTER_READ         0x000
#define  MCI_MODE_REGISTER_WRITE        0x100
#define  MCI_MODE_MEMORY_READ           0x000
#define  MCI_MODE_MEMORY_WRITE          0x4000

static unsigned int mpeglines = 128;
module_param(mpeglines, int, 0644);
MODULE_PARM_DESC(mpeglines, "number of lines in an MPEG buffer, range 2-32");

static unsigned int mpeglinesize = 512;
module_param(mpeglinesize, int, 0644);
MODULE_PARM_DESC(mpeglinesize,
	"number of bytes in each line of an MPEG buffer, range 512-1024");

static unsigned int v4l_debug = 1;
module_param(v4l_debug, int, 0644);
MODULE_PARM_DESC(v4l_debug, "enable V4L debug messages");

#define dprintk(level, fmt, arg...)	\
	do {				\
		if (v4l_debug >= level) \
			printk(KERN_DEBUG pr_fmt(fmt), ## arg); \
	} while (0)

static struct cx231xx_tvnorm cx231xx_tvnorms[] = {
	{
		.name      = "NTSC-M",
		.id        = V4L2_STD_NTSC_M,
	}, {
		.name      = "NTSC-JP",
		.id        = V4L2_STD_NTSC_M_JP,
	}, {
		.name      = "PAL-BG",
		.id        = V4L2_STD_PAL_BG,
	}, {
		.name      = "PAL-DK",
		.id        = V4L2_STD_PAL_DK,
	}, {
		.name      = "PAL-I",
		.id        = V4L2_STD_PAL_I,
	}, {
		.name      = "PAL-M",
		.id        = V4L2_STD_PAL_M,
	}, {
		.name      = "PAL-N",
		.id        = V4L2_STD_PAL_N,
	}, {
		.name      = "PAL-Nc",
		.id        = V4L2_STD_PAL_Nc,
	}, {
		.name      = "PAL-60",
		.id        = V4L2_STD_PAL_60,
	}, {
		.name      = "SECAM-L",
		.id        = V4L2_STD_SECAM_L,
	}, {
		.name      = "SECAM-DK",
		.id        = V4L2_STD_SECAM_DK,
	}
};

/* ------------------------------------------------------------------ */

enum cx231xx_capture_type {
	CX231xx_MPEG_CAPTURE,
	CX231xx_RAW_CAPTURE,
	CX231xx_RAW_PASSTHRU_CAPTURE
};

enum cx231xx_capture_bits {
	CX231xx_RAW_BITS_NONE             = 0x00,
	CX231xx_RAW_BITS_YUV_CAPTURE      = 0x01,
	CX231xx_RAW_BITS_PCM_CAPTURE      = 0x02,
	CX231xx_RAW_BITS_VBI_CAPTURE      = 0x04,
	CX231xx_RAW_BITS_PASSTHRU_CAPTURE = 0x08,
	CX231xx_RAW_BITS_TO_HOST_CAPTURE  = 0x10
};

enum cx231xx_capture_end {
	CX231xx_END_AT_GOP, /* stop at the end of gop, generate irq */
	CX231xx_END_NOW, /* stop immediately, no irq */
};

enum cx231xx_framerate {
	CX231xx_FRAMERATE_NTSC_30, /* NTSC: 30fps */
	CX231xx_FRAMERATE_PAL_25   /* PAL: 25fps */
};

enum cx231xx_stream_port {
	CX231xx_OUTPUT_PORT_MEMORY,
	CX231xx_OUTPUT_PORT_STREAMING,
	CX231xx_OUTPUT_PORT_SERIAL
};

enum cx231xx_data_xfer_status {
	CX231xx_MORE_BUFFERS_FOLLOW,
	CX231xx_LAST_BUFFER,
};

enum cx231xx_picture_mask {
	CX231xx_PICTURE_MASK_NONE,
	CX231xx_PICTURE_MASK_I_FRAMES,
	CX231xx_PICTURE_MASK_I_P_FRAMES = 0x3,
	CX231xx_PICTURE_MASK_ALL_FRAMES = 0x7,
};

enum cx231xx_vbi_mode_bits {
	CX231xx_VBI_BITS_SLICED,
	CX231xx_VBI_BITS_RAW,
};

enum cx231xx_vbi_insertion_bits {
	CX231xx_VBI_BITS_INSERT_IN_XTENSION_USR_DATA,
	CX231xx_VBI_BITS_INSERT_IN_PRIVATE_PACKETS = 0x1 << 1,
	CX231xx_VBI_BITS_SEPARATE_STREAM = 0x2 << 1,
	CX231xx_VBI_BITS_SEPARATE_STREAM_USR_DATA = 0x4 << 1,
	CX231xx_VBI_BITS_SEPARATE_STREAM_PRV_DATA = 0x5 << 1,
};

enum cx231xx_dma_unit {
	CX231xx_DMA_BYTES,
	CX231xx_DMA_FRAMES,
};

enum cx231xx_dma_transfer_status_bits {
	CX231xx_DMA_TRANSFER_BITS_DONE = 0x01,
	CX231xx_DMA_TRANSFER_BITS_ERROR = 0x04,
	CX231xx_DMA_TRANSFER_BITS_LL_ERROR = 0x10,
};

enum cx231xx_pause {
	CX231xx_PAUSE_ENCODING,
	CX231xx_RESUME_ENCODING,
};

enum cx231xx_copyright {
	CX231xx_COPYRIGHT_OFF,
	CX231xx_COPYRIGHT_ON,
};

enum cx231xx_notification_type {
	CX231xx_NOTIFICATION_REFRESH,
};

enum cx231xx_notification_status {
	CX231xx_NOTIFICATION_OFF,
	CX231xx_NOTIFICATION_ON,
};

enum cx231xx_notification_mailbox {
	CX231xx_NOTIFICATION_NO_MAILBOX = -1,
};

enum cx231xx_field1_lines {
	CX231xx_FIELD1_SAA7114 = 0x00EF, /* 239 */
	CX231xx_FIELD1_SAA7115 = 0x00F0, /* 240 */
	CX231xx_FIELD1_MICRONAS = 0x0105, /* 261 */
};

enum cx231xx_field2_lines {
	CX231xx_FIELD2_SAA7114 = 0x00EF, /* 239 */
	CX231xx_FIELD2_SAA7115 = 0x00F0, /* 240 */
	CX231xx_FIELD2_MICRONAS = 0x0106, /* 262 */
};

enum cx231xx_custom_data_type {
	CX231xx_CUSTOM_EXTENSION_USR_DATA,
	CX231xx_CUSTOM_PRIVATE_PACKET,
};

enum cx231xx_mute {
	CX231xx_UNMUTE,
	CX231xx_MUTE,
};

enum cx231xx_mute_video_mask {
	CX231xx_MUTE_VIDEO_V_MASK = 0x0000FF00,
	CX231xx_MUTE_VIDEO_U_MASK = 0x00FF0000,
	CX231xx_MUTE_VIDEO_Y_MASK = 0xFF000000,
};

enum cx231xx_mute_video_shift {
	CX231xx_MUTE_VIDEO_V_SHIFT = 8,
	CX231xx_MUTE_VIDEO_U_SHIFT = 16,
	CX231xx_MUTE_VIDEO_Y_SHIFT = 24,
};

/* defines below are from ivtv-driver.h */
#define IVTV_CMD_HW_BLOCKS_RST 0xFFFFFFFF

/* Firmware API commands */
#define IVTV_API_STD_TIMEOUT 500

/* Registers */
/* IVTV_REG_OFFSET */
#define IVTV_REG_ENC_SDRAM_REFRESH (0x07F8)
#define IVTV_REG_ENC_SDRAM_PRECHARGE (0x07FC)
#define IVTV_REG_SPU (0x9050)
#define IVTV_REG_HW_BLOCKS (0x9054)
#define IVTV_REG_VPU (0x9058)
#define IVTV_REG_APU (0xA064)

/*
 * Bit definitions for MC417_RWD and MC417_OEN registers
 *
 * bits 31-16
 *+-----------+
 *| Reserved  |
 *|+-----------+
 *|  bit 15  bit 14  bit 13 bit 12  bit 11  bit 10  bit 9   bit 8
 *|+-------+-------+-------+-------+-------+-------+-------+-------+
 *|| MIWR# | MIRD# | MICS# |MIRDY# |MIADDR3|MIADDR2|MIADDR1|MIADDR0|
 *|+-------+-------+-------+-------+-------+-------+-------+-------+
 *| bit 7   bit 6   bit 5   bit 4   bit 3   bit 2   bit 1   bit 0
 *|+-------+-------+-------+-------+-------+-------+-------+-------+
 *||MIDATA7|MIDATA6|MIDATA5|MIDATA4|MIDATA3|MIDATA2|MIDATA1|MIDATA0|
 *|+-------+-------+-------+-------+-------+-------+-------+-------+
 */
#define MC417_MIWR	0x8000
#define MC417_MIRD	0x4000
#define MC417_MICS	0x2000
#define MC417_MIRDY	0x1000
#define MC417_MIADDR	0x0F00
#define MC417_MIDATA	0x00FF


/* Bit definitions for MC417_CTL register ****
 *bits 31-6   bits 5-4   bit 3    bits 2-1       Bit 0
 *+--------+-------------+--------+--------------+------------+
 *|Reserved|MC417_SPD_CTL|Reserved|MC417_GPIO_SEL|UART_GPIO_EN|
 *+--------+-------------+--------+--------------+------------+
 */
#define MC417_SPD_CTL(x)	(((x) << 4) & 0x00000030)
#define MC417_GPIO_SEL(x)	(((x) << 1) & 0x00000006)
#define MC417_UART_GPIO_EN	0x00000001

/* Values for speed control */
#define MC417_SPD_CTL_SLOW	0x1
#define MC417_SPD_CTL_MEDIUM	0x0
#define MC417_SPD_CTL_FAST	0x3     /* b'1x, but we use b'11 */

/* Values for GPIO select */
#define MC417_GPIO_SEL_GPIO3	0x3
#define MC417_GPIO_SEL_GPIO2	0x2
#define MC417_GPIO_SEL_GPIO1	0x1
#define MC417_GPIO_SEL_GPIO0	0x0


#define CX23417_GPIO_MASK 0xFC0003FF

static int set_itvc_reg(struct cx231xx *dev, u32 gpio_direction, u32 value)
{
	int status = 0;
	u32 _gpio_direction = 0;

	_gpio_direction = _gpio_direction & CX23417_GPIO_MASK;
	_gpio_direction = _gpio_direction | gpio_direction;
	status = cx231xx_send_gpio_cmd(dev, _gpio_direction,
			 (u8 *)&value, 4, 0, 0);
	return status;
}

static int get_itvc_reg(struct cx231xx *dev, u32 gpio_direction, u32 *val_ptr)
{
	int status = 0;
	u32 _gpio_direction = 0;

	_gpio_direction = _gpio_direction & CX23417_GPIO_MASK;
	_gpio_direction = _gpio_direction | gpio_direction;

	status = cx231xx_send_gpio_cmd(dev, _gpio_direction,
		 (u8 *)val_ptr, 4, 0, 1);
	return status;
}

static int wait_for_mci_complete(struct cx231xx *dev)
{
	u32 gpio;
	u32 gpio_direction = 0;
	u8 count = 0;
	get_itvc_reg(dev, gpio_direction, &gpio);

	while (!(gpio&0x020000)) {
		msleep(10);

		get_itvc_reg(dev, gpio_direction, &gpio);

		if (count++ > 100) {
			dprintk(3, "ERROR: Timeout - gpio=%x\n", gpio);
			return -EIO;
		}
	}
	return 0;
}

static int mc417_register_write(struct cx231xx *dev, u16 address, u32 value)
{
	u32 temp;
	int status = 0;

	temp = 0x82 | MCI_REGISTER_DATA_BYTE0 | ((value & 0x000000FF) << 8);
	temp = temp << 10;
	status = set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	if (status < 0)
		return status;
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*write data byte 1;*/
	temp = 0x82 | MCI_REGISTER_DATA_BYTE1 | (value & 0x0000FF00);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*write data byte 2;*/
	temp = 0x82 | MCI_REGISTER_DATA_BYTE2 | ((value & 0x00FF0000) >> 8);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*write data byte 3;*/
	temp = 0x82 | MCI_REGISTER_DATA_BYTE3 | ((value & 0xFF000000) >> 16);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*write address byte 0;*/
	temp = 0x82 | MCI_REGISTER_ADDRESS_BYTE0 | ((address & 0x000000FF) << 8);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*write address byte 1;*/
	temp = 0x82 | MCI_REGISTER_ADDRESS_BYTE1 | (address & 0x0000FF00);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*Write that the mode is write.*/
	temp = 0x82 | MCI_REGISTER_MODE | MCI_MODE_REGISTER_WRITE;
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	return wait_for_mci_complete(dev);
}

static int mc417_register_read(struct cx231xx *dev, u16 address, u32 *value)
{
	/*write address byte 0;*/
	u32 temp;
	u32 return_value = 0;
	int ret = 0;

	temp = 0x82 | MCI_REGISTER_ADDRESS_BYTE0 | ((address & 0x00FF) << 8);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | ((0x05) << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*write address byte 1;*/
	temp = 0x82 | MCI_REGISTER_ADDRESS_BYTE1 | (address & 0xFF00);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | ((0x05) << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*write that the mode is read;*/
	temp = 0x82 | MCI_REGISTER_MODE | MCI_MODE_REGISTER_READ;
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | ((0x05) << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*wait for the MIRDY line to be asserted ,
	signalling that the read is done;*/
	ret = wait_for_mci_complete(dev);

	/*switch the DATA- GPIO to input mode;*/

	/*Read data byte 0;*/
	temp = (0x82 | MCI_REGISTER_DATA_BYTE0) << 10;
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	temp = ((0x81 | MCI_REGISTER_DATA_BYTE0) << 10);
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	get_itvc_reg(dev, ITVC_READ_DIR, &temp);
	return_value |= ((temp & 0x03FC0000) >> 18);
	set_itvc_reg(dev, ITVC_READ_DIR, (0x87 << 10));

	/* Read data byte 1;*/
	temp = (0x82 | MCI_REGISTER_DATA_BYTE1) << 10;
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	temp = ((0x81 | MCI_REGISTER_DATA_BYTE1) << 10);
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	get_itvc_reg(dev, ITVC_READ_DIR, &temp);

	return_value |= ((temp & 0x03FC0000) >> 10);
	set_itvc_reg(dev, ITVC_READ_DIR, (0x87 << 10));

	/*Read data byte 2;*/
	temp = (0x82 | MCI_REGISTER_DATA_BYTE2) << 10;
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	temp = ((0x81 | MCI_REGISTER_DATA_BYTE2) << 10);
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	get_itvc_reg(dev, ITVC_READ_DIR, &temp);
	return_value |= ((temp & 0x03FC0000) >> 2);
	set_itvc_reg(dev, ITVC_READ_DIR, (0x87 << 10));

	/*Read data byte 3;*/
	temp = (0x82 | MCI_REGISTER_DATA_BYTE3) << 10;
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	temp = ((0x81 | MCI_REGISTER_DATA_BYTE3) << 10);
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	get_itvc_reg(dev, ITVC_READ_DIR, &temp);
	return_value |= ((temp & 0x03FC0000) << 6);
	set_itvc_reg(dev, ITVC_READ_DIR, (0x87 << 10));

	*value  = return_value;
	return ret;
}

static int mc417_memory_write(struct cx231xx *dev, u32 address, u32 value)
{
	/*write data byte 0;*/

	u32 temp;
	int ret = 0;

	temp = 0x82 | MCI_MEMORY_DATA_BYTE0 | ((value & 0x000000FF) << 8);
	temp = temp << 10;
	ret = set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	if (ret < 0)
		return ret;
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*write data byte 1;*/
	temp = 0x82 | MCI_MEMORY_DATA_BYTE1 | (value & 0x0000FF00);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*write data byte 2;*/
	temp = 0x82 | MCI_MEMORY_DATA_BYTE2 | ((value & 0x00FF0000) >> 8);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*write data byte 3;*/
	temp = 0x82 | MCI_MEMORY_DATA_BYTE3 | ((value & 0xFF000000) >> 16);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/* write address byte 2;*/
	temp = 0x82 | MCI_MEMORY_ADDRESS_BYTE2 | MCI_MODE_MEMORY_WRITE |
		((address & 0x003F0000) >> 8);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/* write address byte 1;*/
	temp = 0x82 | MCI_MEMORY_ADDRESS_BYTE1 | (address & 0xFF00);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/* write address byte 0;*/
	temp = 0x82 | MCI_MEMORY_ADDRESS_BYTE0 | ((address & 0x00FF) << 8);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*wait for MIRDY line;*/
	wait_for_mci_complete(dev);

	return 0;
}

static int mc417_memory_read(struct cx231xx *dev, u32 address, u32 *value)
{
	u32 temp = 0;
	u32 return_value = 0;
	int ret = 0;

	/*write address byte 2;*/
	temp = 0x82 | MCI_MEMORY_ADDRESS_BYTE2 | MCI_MODE_MEMORY_READ |
		((address & 0x003F0000) >> 8);
	temp = temp << 10;
	ret = set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	if (ret < 0)
		return ret;
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*write address byte 1*/
	temp = 0x82 | MCI_MEMORY_ADDRESS_BYTE1 | (address & 0xFF00);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*write address byte 0*/
	temp = 0x82 | MCI_MEMORY_ADDRESS_BYTE0 | ((address & 0x00FF) << 8);
	temp = temp << 10;
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);
	temp = temp | (0x05 << 10);
	set_itvc_reg(dev, ITVC_WRITE_DIR, temp);

	/*Wait for MIRDY line*/
	ret = wait_for_mci_complete(dev);


	/*Read data byte 3;*/
	temp = (0x82 | MCI_MEMORY_DATA_BYTE3) << 10;
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	temp = ((0x81 | MCI_MEMORY_DATA_BYTE3) << 10);
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	get_itvc_reg(dev, ITVC_READ_DIR, &temp);
	return_value |= ((temp & 0x03FC0000) << 6);
	set_itvc_reg(dev, ITVC_READ_DIR, (0x87 << 10));

	/*Read data byte 2;*/
	temp = (0x82 | MCI_MEMORY_DATA_BYTE2) << 10;
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	temp = ((0x81 | MCI_MEMORY_DATA_BYTE2) << 10);
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	get_itvc_reg(dev, ITVC_READ_DIR, &temp);
	return_value |= ((temp & 0x03FC0000) >> 2);
	set_itvc_reg(dev, ITVC_READ_DIR, (0x87 << 10));

	/* Read data byte 1;*/
	temp = (0x82 | MCI_MEMORY_DATA_BYTE1) << 10;
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	temp = ((0x81 | MCI_MEMORY_DATA_BYTE1) << 10);
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	get_itvc_reg(dev, ITVC_READ_DIR, &temp);
	return_value |= ((temp & 0x03FC0000) >> 10);
	set_itvc_reg(dev, ITVC_READ_DIR, (0x87 << 10));

	/*Read data byte 0;*/
	temp = (0x82 | MCI_MEMORY_DATA_BYTE0) << 10;
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	temp = ((0x81 | MCI_MEMORY_DATA_BYTE0) << 10);
	set_itvc_reg(dev, ITVC_READ_DIR, temp);
	get_itvc_reg(dev, ITVC_READ_DIR, &temp);
	return_value |= ((temp & 0x03FC0000) >> 18);
	set_itvc_reg(dev, ITVC_READ_DIR, (0x87 << 10));

	*value  = return_value;
	return ret;
}

/* ------------------------------------------------------------------ */

/* MPEG encoder API */
static char *cmd_to_str(int cmd)
{
	switch (cmd) {
	case CX2341X_ENC_PING_FW:
		return "PING_FW";
	case CX2341X_ENC_START_CAPTURE:
		return "START_CAPTURE";
	case CX2341X_ENC_STOP_CAPTURE:
		return "STOP_CAPTURE";
	case CX2341X_ENC_SET_AUDIO_ID:
		return "SET_AUDIO_ID";
	case CX2341X_ENC_SET_VIDEO_ID:
		return "SET_VIDEO_ID";
	case CX2341X_ENC_SET_PCR_ID:
		return "SET_PCR_PID";
	case CX2341X_ENC_SET_FRAME_RATE:
		return "SET_FRAME_RATE";
	case CX2341X_ENC_SET_FRAME_SIZE:
		return "SET_FRAME_SIZE";
	case CX2341X_ENC_SET_BIT_RATE:
		return "SET_BIT_RATE";
	case CX2341X_ENC_SET_GOP_PROPERTIES:
		return "SET_GOP_PROPERTIES";
	case CX2341X_ENC_SET_ASPECT_RATIO:
		return "SET_ASPECT_RATIO";
	case CX2341X_ENC_SET_DNR_FILTER_MODE:
		return "SET_DNR_FILTER_PROPS";
	case CX2341X_ENC_SET_DNR_FILTER_PROPS:
		return "SET_DNR_FILTER_PROPS";
	case CX2341X_ENC_SET_CORING_LEVELS:
		return "SET_CORING_LEVELS";
	case CX2341X_ENC_SET_SPATIAL_FILTER_TYPE:
		return "SET_SPATIAL_FILTER_TYPE";
	case CX2341X_ENC_SET_VBI_LINE:
		return "SET_VBI_LINE";
	case CX2341X_ENC_SET_STREAM_TYPE:
		return "SET_STREAM_TYPE";
	case CX2341X_ENC_SET_OUTPUT_PORT:
		return "SET_OUTPUT_PORT";
	case CX2341X_ENC_SET_AUDIO_PROPERTIES:
		return "SET_AUDIO_PROPERTIES";
	case CX2341X_ENC_HALT_FW:
		return "HALT_FW";
	case CX2341X_ENC_GET_VERSION:
		return "GET_VERSION";
	case CX2341X_ENC_SET_GOP_CLOSURE:
		return "SET_GOP_CLOSURE";
	case CX2341X_ENC_GET_SEQ_END:
		return "GET_SEQ_END";
	case CX2341X_ENC_SET_PGM_INDEX_INFO:
		return "SET_PGM_INDEX_INFO";
	case CX2341X_ENC_SET_VBI_CONFIG:
		return "SET_VBI_CONFIG";
	case CX2341X_ENC_SET_DMA_BLOCK_SIZE:
		return "SET_DMA_BLOCK_SIZE";
	case CX2341X_ENC_GET_PREV_DMA_INFO_MB_10:
		return "GET_PREV_DMA_INFO_MB_10";
	case CX2341X_ENC_GET_PREV_DMA_INFO_MB_9:
		return "GET_PREV_DMA_INFO_MB_9";
	case CX2341X_ENC_SCHED_DMA_TO_HOST:
		return "SCHED_DMA_TO_HOST";
	case CX2341X_ENC_INITIALIZE_INPUT:
		return "INITIALIZE_INPUT";
	case CX2341X_ENC_SET_FRAME_DROP_RATE:
		return "SET_FRAME_DROP_RATE";
	case CX2341X_ENC_PAUSE_ENCODER:
		return "PAUSE_ENCODER";
	case CX2341X_ENC_REFRESH_INPUT:
		return "REFRESH_INPUT";
	case CX2341X_ENC_SET_COPYRIGHT:
		return "SET_COPYRIGHT";
	case CX2341X_ENC_SET_EVENT_NOTIFICATION:
		return "SET_EVENT_NOTIFICATION";
	case CX2341X_ENC_SET_NUM_VSYNC_LINES:
		return "SET_NUM_VSYNC_LINES";
	case CX2341X_ENC_SET_PLACEHOLDER:
		return "SET_PLACEHOLDER";
	case CX2341X_ENC_MUTE_VIDEO:
		return "MUTE_VIDEO";
	case CX2341X_ENC_MUTE_AUDIO:
		return "MUTE_AUDIO";
	case CX2341X_ENC_MISC:
		return "MISC";
	default:
		return "UNKNOWN";
	}
}

static int cx231xx_mbox_func(void *priv, u32 command, int in, int out,
			     u32 data[CX2341X_MBOX_MAX_DATA])
{
	struct cx231xx *dev = priv;
	unsigned long timeout;
	u32 value, flag, retval = 0;
	int i;

	dprintk(3, "%s: command(0x%X) = %s\n", __func__, command,
		cmd_to_str(command));

	/* this may not be 100% safe if we can't read any memory location
	   without side effects */
	mc417_memory_read(dev, dev->cx23417_mailbox - 4, &value);
	if (value != 0x12345678) {
		dprintk(3, "Firmware and/or mailbox pointer not initialized or corrupted, signature = 0x%x, cmd = %s\n",
			value, cmd_to_str(command));
		return -EIO;
	}

	/* This read looks at 32 bits, but flag is only 8 bits.
	 * Seems we also bail if CMD or TIMEOUT bytes are set???
	 */
	mc417_memory_read(dev, dev->cx23417_mailbox, &flag);
	if (flag) {
		dprintk(3, "ERROR: Mailbox appears to be in use (%x), cmd = %s\n",
				flag, cmd_to_str(command));
		return -EBUSY;
	}

	flag |= 1; /* tell 'em we're working on it */
	mc417_memory_write(dev, dev->cx23417_mailbox, flag);

	/* write command + args + fill remaining with zeros */
	/* command code */
	mc417_memory_write(dev, dev->cx23417_mailbox + 1, command);
	mc417_memory_write(dev, dev->cx23417_mailbox + 3,
		IVTV_API_STD_TIMEOUT); /* timeout */
	for (i = 0; i < in; i++) {
		mc417_memory_write(dev, dev->cx23417_mailbox + 4 + i, data[i]);
		dprintk(3, "API Input %d = %d\n", i, data[i]);
	}
	for (; i < CX2341X_MBOX_MAX_DATA; i++)
		mc417_memory_write(dev, dev->cx23417_mailbox + 4 + i, 0);

	flag |= 3; /* tell 'em we're done writing */
	mc417_memory_write(dev, dev->cx23417_mailbox, flag);

	/* wait for firmware to handle the API command */
	timeout = jiffies + msecs_to_jiffies(10);
	for (;;) {
		mc417_memory_read(dev, dev->cx23417_mailbox, &flag);
		if (0 != (flag & 4))
			break;
		if (time_after(jiffies, timeout)) {
			dprintk(3, "ERROR: API Mailbox timeout\n");
			return -EIO;
		}
		udelay(10);
	}

	/* read output values */
	for (i = 0; i < out; i++) {
		mc417_memory_read(dev, dev->cx23417_mailbox + 4 + i, data + i);
		dprintk(3, "API Output %d = %d\n", i, data[i]);
	}

	mc417_memory_read(dev, dev->cx23417_mailbox + 2, &retval);
	dprintk(3, "API result = %d\n", retval);

	flag = 0;
	mc417_memory_write(dev, dev->cx23417_mailbox, flag);

	return 0;
}

/* We don't need to call the API often, so using just one
 * mailbox will probably suffice
 */
static int cx231xx_api_cmd(struct cx231xx *dev, u32 command,
		u32 inputcnt, u32 outputcnt, ...)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	va_list vargs;
	int i, err;

	dprintk(3, "%s() cmds = 0x%08x\n", __func__, command);

	va_start(vargs, outputcnt);
	for (i = 0; i < inputcnt; i++)
		data[i] = va_arg(vargs, int);

	err = cx231xx_mbox_func(dev, command, inputcnt, outputcnt, data);
	for (i = 0; i < outputcnt; i++) {
		int *vptr = va_arg(vargs, int *);
		*vptr = data[i];
	}
	va_end(vargs);

	return err;
}


static int cx231xx_find_mailbox(struct cx231xx *dev)
{
	u32 signature[4] = {
		0x12345678, 0x34567812, 0x56781234, 0x78123456
	};
	int signaturecnt = 0;
	u32 value;
	int i;
	int ret = 0;

	dprintk(2, "%s()\n", __func__);

	for (i = 0; i < 0x100; i++) {/*CX231xx_FIRM_IMAGE_SIZE*/
		ret = mc417_memory_read(dev, i, &value);
		if (ret < 0)
			return ret;
		if (value == signature[signaturecnt])
			signaturecnt++;
		else
			signaturecnt = 0;
		if (4 == signaturecnt) {
			dprintk(1, "Mailbox signature found at 0x%x\n", i + 1);
			return i + 1;
		}
	}
	dprintk(3, "Mailbox signature values not found!\n");
	return -EIO;
}

static void mci_write_memory_to_gpio(struct cx231xx *dev, u32 address, u32 value,
		u32 *p_fw_image)
{
	u32 temp = 0;
	int i = 0;

	temp = 0x82 | MCI_MEMORY_DATA_BYTE0 | ((value & 0x000000FF) << 8);
	temp = temp << 10;
	*p_fw_image = temp;
	p_fw_image++;
	temp = temp | (0x05 << 10);
	*p_fw_image = temp;
	p_fw_image++;

	/*write data byte 1;*/
	temp = 0x82 | MCI_MEMORY_DATA_BYTE1 | (value & 0x0000FF00);
	temp = temp << 10;
	*p_fw_image = temp;
	p_fw_image++;
	temp = temp | (0x05 << 10);
	*p_fw_image = temp;
	p_fw_image++;

	/*write data byte 2;*/
	temp = 0x82 | MCI_MEMORY_DATA_BYTE2 | ((value & 0x00FF0000) >> 8);
	temp = temp << 10;
	*p_fw_image = temp;
	p_fw_image++;
	temp = temp | (0x05 << 10);
	*p_fw_image = temp;
	p_fw_image++;

	/*write data byte 3;*/
	temp = 0x82 | MCI_MEMORY_DATA_BYTE3 | ((value & 0xFF000000) >> 16);
	temp = temp << 10;
	*p_fw_image = temp;
	p_fw_image++;
	temp = temp | (0x05 << 10);
	*p_fw_image = temp;
	p_fw_image++;

	/* write address byte 2;*/
	temp = 0x82 | MCI_MEMORY_ADDRESS_BYTE2 | MCI_MODE_MEMORY_WRITE |
		((address & 0x003F0000) >> 8);
	temp = temp << 10;
	*p_fw_image = temp;
	p_fw_image++;
	temp = temp | (0x05 << 10);
	*p_fw_image = temp;
	p_fw_image++;

	/* write address byte 1;*/
	temp = 0x82 | MCI_MEMORY_ADDRESS_BYTE1 | (address & 0xFF00);
	temp = temp << 10;
	*p_fw_image = temp;
	p_fw_image++;
	temp = temp | (0x05 << 10);
	*p_fw_image = temp;
	p_fw_image++;

	/* write address byte 0;*/
	temp = 0x82 | MCI_MEMORY_ADDRESS_BYTE0 | ((address & 0x00FF) << 8);
	temp = temp << 10;
	*p_fw_image = temp;
	p_fw_image++;
	temp = temp | (0x05 << 10);
	*p_fw_image = temp;
	p_fw_image++;

	for (i = 0; i < 6; i++) {
		*p_fw_image = 0xFFFFFFFF;
		p_fw_image++;
	}
}


static int cx231xx_load_firmware(struct cx231xx *dev)
{
	static const unsigned char magic[8] = {
		0xa7, 0x0d, 0x00, 0x00, 0x66, 0xbb, 0x55, 0xaa
	};
	const struct firmware *firmware;
	int i, retval = 0;
	u32 value = 0;
	u32 gpio_output = 0;
	/*u32 checksum = 0;*/
	/*u32 *dataptr;*/
	u32 transfer_size = 0;
	u32 fw_data = 0;
	u32 address = 0;
	/*u32 current_fw[800];*/
	u32 *p_current_fw, *p_fw;
	u32 *p_fw_data;
	int frame = 0;
	u8 *p_buffer;

	p_current_fw = vmalloc(1884180 * 4);
	p_fw = p_current_fw;
	if (p_current_fw == NULL) {
		dprintk(2, "FAIL!!!\n");
		return -ENOMEM;
	}

	p_buffer = vmalloc(EP5_BUF_SIZE);
	if (p_buffer == NULL) {
		dprintk(2, "FAIL!!!\n");
		vfree(p_current_fw);
		return -ENOMEM;
	}

	dprintk(2, "%s()\n", __func__);

	/* Save GPIO settings before reset of APU */
	retval |= mc417_memory_read(dev, 0x9020, &gpio_output);
	retval |= mc417_memory_read(dev, 0x900C, &value);

	retval  = mc417_register_write(dev,
		IVTV_REG_VPU, 0xFFFFFFED);
	retval |= mc417_register_write(dev,
		IVTV_REG_HW_BLOCKS, IVTV_CMD_HW_BLOCKS_RST);
	retval |= mc417_register_write(dev,
		IVTV_REG_ENC_SDRAM_REFRESH, 0x80000800);
	retval |= mc417_register_write(dev,
		IVTV_REG_ENC_SDRAM_PRECHARGE, 0x1A);
	retval |= mc417_register_write(dev,
		IVTV_REG_APU, 0);

	if (retval != 0) {
		dev_err(dev->dev,
			"%s: Error with mc417_register_write\n", __func__);
		vfree(p_current_fw);
		vfree(p_buffer);
		return retval;
	}

	retval = request_firmware(&firmware, CX231xx_FIRM_IMAGE_NAME,
				  dev->dev);

	if (retval != 0) {
		dev_err(dev->dev,
			"ERROR: Hotplug firmware request failed (%s).\n",
			CX231xx_FIRM_IMAGE_NAME);
		dev_err(dev->dev,
			"Please fix your hotplug setup, the board will not work without firmware loaded!\n");
		vfree(p_current_fw);
		vfree(p_buffer);
		return retval;
	}

	if (firmware->size != CX231xx_FIRM_IMAGE_SIZE) {
		dev_err(dev->dev,
			"ERROR: Firmware size mismatch (have %zd, expected %d)\n",
			firmware->size, CX231xx_FIRM_IMAGE_SIZE);
		release_firmware(firmware);
		vfree(p_current_fw);
		vfree(p_buffer);
		return -EINVAL;
	}

	if (0 != memcmp(firmware->data, magic, 8)) {
		dev_err(dev->dev,
			"ERROR: Firmware magic mismatch, wrong file?\n");
		release_firmware(firmware);
		vfree(p_current_fw);
		vfree(p_buffer);
		return -EINVAL;
	}

	initGPIO(dev);

	/* transfer to the chip */
	dprintk(2, "Loading firmware to GPIO...\n");
	p_fw_data = (u32 *)firmware->data;
	dprintk(2, "firmware->size=%zd\n", firmware->size);
	for (transfer_size = 0; transfer_size < firmware->size;
		 transfer_size += 4) {
		fw_data = *p_fw_data;

		mci_write_memory_to_gpio(dev, address, fw_data, p_current_fw);
		address = address + 1;
		p_current_fw += 20;
		p_fw_data += 1;
	}

	/*download the firmware by ep5-out*/

	for (frame = 0; frame < (int)(CX231xx_FIRM_IMAGE_SIZE*20/EP5_BUF_SIZE);
	     frame++) {
		for (i = 0; i < EP5_BUF_SIZE; i++) {
			*(p_buffer + i) = (u8)(*(p_fw + (frame * 128 * 8 + (i / 4))) & 0x000000FF);
			i++;
			*(p_buffer + i) = (u8)((*(p_fw + (frame * 128 * 8 + (i / 4))) & 0x0000FF00) >> 8);
			i++;
			*(p_buffer + i) = (u8)((*(p_fw + (frame * 128 * 8 + (i / 4))) & 0x00FF0000) >> 16);
			i++;
			*(p_buffer + i) = (u8)((*(p_fw + (frame * 128 * 8 + (i / 4))) & 0xFF000000) >> 24);
		}
		cx231xx_ep5_bulkout(dev, p_buffer, EP5_BUF_SIZE);
	}

	p_current_fw = p_fw;
	vfree(p_current_fw);
	p_current_fw = NULL;
	vfree(p_buffer);
	uninitGPIO(dev);
	release_firmware(firmware);
	dprintk(1, "Firmware upload successful.\n");

	retval |= mc417_register_write(dev, IVTV_REG_HW_BLOCKS,
		IVTV_CMD_HW_BLOCKS_RST);
	if (retval < 0) {
		dev_err(dev->dev,
			"%s: Error with mc417_register_write\n",
			__func__);
		return retval;
	}
	/* F/W power up disturbs the GPIOs, restore state */
	retval |= mc417_register_write(dev, 0x9020, gpio_output);
	retval |= mc417_register_write(dev, 0x900C, value);

	retval |= mc417_register_read(dev, IVTV_REG_VPU, &value);
	retval |= mc417_register_write(dev, IVTV_REG_VPU, value & 0xFFFFFFE8);

	if (retval < 0) {
		dev_err(dev->dev,
			"%s: Error with mc417_register_write\n",
			__func__);
		return retval;
	}
	return 0;
}

static void cx231xx_codec_settings(struct cx231xx *dev)
{
	dprintk(1, "%s()\n", __func__);

	/* assign frame size */
	cx231xx_api_cmd(dev, CX2341X_ENC_SET_FRAME_SIZE, 2, 0,
				dev->ts1.height, dev->ts1.width);

	dev->mpeg_ctrl_handler.width = dev->ts1.width;
	dev->mpeg_ctrl_handler.height = dev->ts1.height;

	cx2341x_handler_setup(&dev->mpeg_ctrl_handler);

	cx231xx_api_cmd(dev, CX2341X_ENC_MISC, 2, 0, 3, 1);
	cx231xx_api_cmd(dev, CX2341X_ENC_MISC, 2, 0, 4, 1);
}

static int cx231xx_initialize_codec(struct cx231xx *dev)
{
	int version;
	int retval;
	u32 i;
	u32 val = 0;

	dprintk(1, "%s()\n", __func__);
	cx231xx_disable656(dev);
	retval = cx231xx_api_cmd(dev, CX2341X_ENC_PING_FW, 0, 0); /* ping */
	if (retval < 0) {
		dprintk(2, "%s: PING OK\n", __func__);
		retval = cx231xx_load_firmware(dev);
		if (retval < 0) {
			dev_err(dev->dev,
				"%s: f/w load failed\n", __func__);
			return retval;
		}
		retval = cx231xx_find_mailbox(dev);
		if (retval < 0) {
			dev_err(dev->dev, "%s: mailbox < 0, error\n",
				__func__);
			return retval;
		}
		dev->cx23417_mailbox = retval;
		retval = cx231xx_api_cmd(dev, CX2341X_ENC_PING_FW, 0, 0);
		if (retval < 0) {
			dev_err(dev->dev,
				"ERROR: cx23417 firmware ping failed!\n");
			return retval;
		}
		retval = cx231xx_api_cmd(dev, CX2341X_ENC_GET_VERSION, 0, 1,
			&version);
		if (retval < 0) {
			dev_err(dev->dev,
				"ERROR: cx23417 firmware get encoder: version failed!\n");
			return retval;
		}
		dprintk(1, "cx23417 firmware version is 0x%08x\n", version);
		msleep(200);
	}

	for (i = 0; i < 1; i++) {
		retval = mc417_register_read(dev, 0x20f8, &val);
		dprintk(3, "***before enable656() VIM Capture Lines = %d ***\n",
				 val);
		if (retval < 0)
			return retval;
	}

	cx231xx_enable656(dev);

	/* stop mpeg capture */
	cx231xx_api_cmd(dev, CX2341X_ENC_STOP_CAPTURE, 3, 0, 1, 3, 4);

	cx231xx_codec_settings(dev);
	msleep(60);

/*	cx231xx_api_cmd(dev, CX2341X_ENC_SET_NUM_VSYNC_LINES, 2, 0,
		CX231xx_FIELD1_SAA7115, CX231xx_FIELD2_SAA7115);
	cx231xx_api_cmd(dev, CX2341X_ENC_SET_PLACEHOLDER, 12, 0,
		CX231xx_CUSTOM_EXTENSION_USR_DATA, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0);
*/

#if 0
	/* TODO */
	u32 data[7];

	/* Setup to capture VBI */
	data[0] = 0x0001BD00;
	data[1] = 1;          /* frames per interrupt */
	data[2] = 4;          /* total bufs */
	data[3] = 0x91559155; /* start codes */
	data[4] = 0x206080C0; /* stop codes */
	data[5] = 6;          /* lines */
	data[6] = 64;         /* BPL */

	cx231xx_api_cmd(dev, CX2341X_ENC_SET_VBI_CONFIG, 7, 0, data[0], data[1],
		data[2], data[3], data[4], data[5], data[6]);

	for (i = 2; i <= 24; i++) {
		int valid;

		valid = ((i >= 19) && (i <= 21));
		cx231xx_api_cmd(dev, CX2341X_ENC_SET_VBI_LINE, 5, 0, i,
				valid, 0 , 0, 0);
		cx231xx_api_cmd(dev, CX2341X_ENC_SET_VBI_LINE, 5, 0,
				i | 0x80000000, valid, 0, 0, 0);
	}
#endif
/*	cx231xx_api_cmd(dev, CX2341X_ENC_MUTE_AUDIO, 1, 0, CX231xx_UNMUTE);
	msleep(60);
*/
	/* initialize the video input */
	retval = cx231xx_api_cmd(dev, CX2341X_ENC_INITIALIZE_INPUT, 0, 0);
	if (retval < 0)
		return retval;
	msleep(60);

	/* Enable VIP style pixel invalidation so we work with scaled mode */
	mc417_memory_write(dev, 2120, 0x00000080);

	/* start capturing to the host interface */
	retval = cx231xx_api_cmd(dev, CX2341X_ENC_START_CAPTURE, 2, 0,
		CX231xx_MPEG_CAPTURE, CX231xx_RAW_BITS_NONE);
	if (retval < 0)
		return retval;
	msleep(10);

	for (i = 0; i < 1; i++) {
		mc417_register_read(dev, 0x20f8, &val);
		dprintk(3, "***VIM Capture Lines =%d ***\n", val);
	}

	return 0;
}

/* ------------------------------------------------------------------ */

static int queue_setup(struct vb2_queue *vq,
		       unsigned int *nbuffers, unsigned int *nplanes,
		       unsigned int sizes[], struct device *alloc_devs[])
{
	struct cx231xx *dev = vb2_get_drv_priv(vq);
	unsigned int size = mpeglinesize * mpeglines;

	dev->ts1.ts_packet_size  = mpeglinesize;
	dev->ts1.ts_packet_count = mpeglines;

	if (vq->num_buffers + *nbuffers < CX231XX_MIN_BUF)
		*nbuffers = CX231XX_MIN_BUF - vq->num_buffers;

	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;
	*nplanes = 1;
	sizes[0] = mpeglinesize * mpeglines;

	return 0;
}

static void buffer_copy(struct cx231xx *dev, char *data, int len, struct urb *urb,
		struct cx231xx_dmaqueue *dma_q)
{
	void *vbuf;
	struct cx231xx_buffer *buf;
	u32 tail_data = 0;
	char *p_data;

	if (dma_q->mpeg_buffer_done == 0) {
		if (list_empty(&dma_q->active))
			return;

		buf = list_entry(dma_q->active.next,
				struct cx231xx_buffer, list);
		dev->video_mode.isoc_ctl.buf = buf;
		dma_q->mpeg_buffer_done = 1;
	}
	/* Fill buffer */
	buf = dev->video_mode.isoc_ctl.buf;
	vbuf = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);

	if ((dma_q->mpeg_buffer_completed+len) <
			mpeglines*mpeglinesize) {
		if (dma_q->add_ps_package_head ==
				CX231XX_NEED_ADD_PS_PACKAGE_HEAD) {
			memcpy(vbuf+dma_q->mpeg_buffer_completed,
					dma_q->ps_head, 3);
			dma_q->mpeg_buffer_completed =
				dma_q->mpeg_buffer_completed + 3;
			dma_q->add_ps_package_head =
				CX231XX_NONEED_PS_PACKAGE_HEAD;
		}
		memcpy(vbuf+dma_q->mpeg_buffer_completed, data, len);
		dma_q->mpeg_buffer_completed =
			dma_q->mpeg_buffer_completed + len;
	} else {
		dma_q->mpeg_buffer_done = 0;

		tail_data =
			mpeglines*mpeglinesize - dma_q->mpeg_buffer_completed;
		memcpy(vbuf+dma_q->mpeg_buffer_completed,
				data, tail_data);

		buf->vb.vb2_buf.timestamp = ktime_get_ns();
		buf->vb.sequence = dma_q->sequence++;
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
		dma_q->mpeg_buffer_completed = 0;

		if (len - tail_data > 0) {
			p_data = data + tail_data;
			dma_q->left_data_count = len - tail_data;
			memcpy(dma_q->p_left_data,
					p_data, len - tail_data);
		}
	}
}

static void buffer_filled(char *data, int len, struct urb *urb,
		struct cx231xx_dmaqueue *dma_q)
{
	void *vbuf;
	struct cx231xx_buffer *buf;

	if (list_empty(&dma_q->active))
		return;

	buf = list_entry(dma_q->active.next, struct cx231xx_buffer, list);

	/* Fill buffer */
	vbuf = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
	memcpy(vbuf, data, len);
	buf->vb.sequence = dma_q->sequence++;
	buf->vb.vb2_buf.timestamp = ktime_get_ns();
	list_del(&buf->list);
	vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
}

static int cx231xx_isoc_copy(struct cx231xx *dev, struct urb *urb)
{
	struct cx231xx_dmaqueue *dma_q = urb->context;
	unsigned char *p_buffer;
	u32 buffer_size = 0;
	u32 i = 0;

	for (i = 0; i < urb->number_of_packets; i++) {
		if (dma_q->left_data_count > 0) {
			buffer_copy(dev, dma_q->p_left_data,
				    dma_q->left_data_count, urb, dma_q);
			dma_q->mpeg_buffer_completed = dma_q->left_data_count;
			dma_q->left_data_count = 0;
		}

		p_buffer = urb->transfer_buffer +
				urb->iso_frame_desc[i].offset;
		buffer_size = urb->iso_frame_desc[i].actual_length;

		if (buffer_size > 0)
			buffer_copy(dev, p_buffer, buffer_size, urb, dma_q);
	}

	return 0;
}

static int cx231xx_bulk_copy(struct cx231xx *dev, struct urb *urb)
{
	struct cx231xx_dmaqueue *dma_q = urb->context;
	unsigned char *p_buffer, *buffer;
	u32 buffer_size = 0;

	p_buffer = urb->transfer_buffer;
	buffer_size = urb->actual_length;

	buffer = kmalloc(buffer_size, GFP_ATOMIC);
	if (!buffer)
		return -ENOMEM;

	memcpy(buffer, dma_q->ps_head, 3);
	memcpy(buffer+3, p_buffer, buffer_size-3);
	memcpy(dma_q->ps_head, p_buffer+buffer_size-3, 3);

	p_buffer = buffer;
	buffer_filled(p_buffer, buffer_size, urb, dma_q);

	kfree(buffer);
	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct cx231xx_buffer *buf =
	    container_of(vb, struct cx231xx_buffer, vb.vb2_buf);
	struct cx231xx *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct cx231xx_dmaqueue *vidq = &dev->video_mode.vidq;
	unsigned long flags;

	spin_lock_irqsave(&dev->video_mode.slock, flags);
	list_add_tail(&buf->list, &vidq->active);
	spin_unlock_irqrestore(&dev->video_mode.slock, flags);
}

static void return_all_buffers(struct cx231xx *dev,
			       enum vb2_buffer_state state)
{
	struct cx231xx_dmaqueue *vidq = &dev->video_mode.vidq;
	struct cx231xx_buffer *buf, *node;
	unsigned long flags;

	spin_lock_irqsave(&dev->video_mode.slock, flags);
	list_for_each_entry_safe(buf, node, &vidq->active, list) {
		vb2_buffer_done(&buf->vb.vb2_buf, state);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&dev->video_mode.slock, flags);
}

static int start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct cx231xx *dev = vb2_get_drv_priv(vq);
	struct cx231xx_dmaqueue *vidq = &dev->video_mode.vidq;
	int ret = 0;

	vidq->sequence = 0;
	dev->mode_tv = 1;

	cx231xx_set_alt_setting(dev, INDEX_VANC, 1);
	cx231xx_set_gpio_value(dev, 2, 0);

	cx231xx_initialize_codec(dev);

	cx231xx_start_TS1(dev);

	cx231xx_set_alt_setting(dev, INDEX_TS1, 0);
	cx231xx_set_mode(dev, CX231XX_DIGITAL_MODE);
	if (dev->USE_ISO)
		ret = cx231xx_init_isoc(dev, CX231XX_NUM_PACKETS,
					CX231XX_NUM_BUFS,
					dev->ts1_mode.max_pkt_size,
					cx231xx_isoc_copy);
	else
		ret = cx231xx_init_bulk(dev, 320, 5,
					dev->ts1_mode.max_pkt_size,
					cx231xx_bulk_copy);
	if (ret)
		return_all_buffers(dev, VB2_BUF_STATE_QUEUED);

	call_all(dev, video, s_stream, 1);
	return ret;
}

static void stop_streaming(struct vb2_queue *vq)
{
	struct cx231xx *dev = vb2_get_drv_priv(vq);
	unsigned long flags;

	call_all(dev, video, s_stream, 0);

	cx231xx_stop_TS1(dev);

	/* do this before setting alternate! */
	if (dev->USE_ISO)
		cx231xx_uninit_isoc(dev);
	else
		cx231xx_uninit_bulk(dev);
	cx231xx_set_mode(dev, CX231XX_SUSPEND);

	cx231xx_api_cmd(dev, CX2341X_ENC_STOP_CAPTURE, 3, 0,
			CX231xx_END_NOW, CX231xx_MPEG_CAPTURE,
			CX231xx_RAW_BITS_NONE);

	spin_lock_irqsave(&dev->video_mode.slock, flags);
	if (dev->USE_ISO)
		dev->video_mode.isoc_ctl.buf = NULL;
	else
		dev->video_mode.bulk_ctl.buf = NULL;
	spin_unlock_irqrestore(&dev->video_mode.slock, flags);
	return_all_buffers(dev, VB2_BUF_STATE_ERROR);
}

static struct vb2_ops cx231xx_video_qops = {
	.queue_setup		= queue_setup,
	.buf_queue		= buffer_queue,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

/* ------------------------------------------------------------------ */

static int vidioc_g_pixelaspect(struct file *file, void *priv,
				int type, struct v4l2_fract *f)
{
	struct cx231xx *dev = video_drvdata(file);
	bool is_50hz = dev->encodernorm.id & V4L2_STD_625_50;

	if (type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	f->numerator = is_50hz ? 54 : 11;
	f->denominator = is_50hz ? 59 : 10;

	return 0;
}

static int vidioc_g_selection(struct file *file, void *priv,
			      struct v4l2_selection *s)
{
	struct cx231xx *dev = video_drvdata(file);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = dev->ts1.width;
		s->r.height = dev->ts1.height;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_g_std(struct file *file, void *fh0, v4l2_std_id *norm)
{
	struct cx231xx *dev = video_drvdata(file);

	*norm = dev->encodernorm.id;
	return 0;
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id id)
{
	struct cx231xx *dev = video_drvdata(file);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(cx231xx_tvnorms); i++)
		if (id & cx231xx_tvnorms[i].id)
			break;
	if (i == ARRAY_SIZE(cx231xx_tvnorms))
		return -EINVAL;
	dev->encodernorm = cx231xx_tvnorms[i];

	if (dev->encodernorm.id & 0xb000) {
		dprintk(3, "encodernorm set to NTSC\n");
		dev->norm = V4L2_STD_NTSC;
		dev->ts1.height = 480;
		cx2341x_handler_set_50hz(&dev->mpeg_ctrl_handler, false);
	} else {
		dprintk(3, "encodernorm set to PAL\n");
		dev->norm = V4L2_STD_PAL_B;
		dev->ts1.height = 576;
		cx2341x_handler_set_50hz(&dev->mpeg_ctrl_handler, true);
	}
	call_all(dev, video, s_std, dev->norm);
	/* do mode control overrides */
	cx231xx_do_mode_ctrl_overrides(dev);

	dprintk(3, "exit vidioc_s_std() i=0x%x\n", i);
	return 0;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
				struct v4l2_control *ctl)
{
	struct cx231xx *dev = video_drvdata(file);
	struct v4l2_subdev *sd;

	dprintk(3, "enter vidioc_s_ctrl()\n");
	/* Update the A/V core */
	v4l2_device_for_each_subdev(sd, &dev->v4l2_dev)
		v4l2_s_ctrl(NULL, sd->ctrl_handler, ctl);
	dprintk(3, "exit vidioc_s_ctrl()\n");
	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
		return -EINVAL;

	f->pixelformat = V4L2_PIX_FMT_MPEG;

	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cx231xx *dev = video_drvdata(file);

	dprintk(3, "enter vidioc_g_fmt_vid_cap()\n");
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage = mpeglines * mpeglinesize;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.width = dev->ts1.width;
	f->fmt.pix.height = dev->ts1.height;
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;
	dprintk(1, "VIDIOC_G_FMT: w: %d, h: %d\n",
		dev->ts1.width, dev->ts1.height);
	dprintk(3, "exit vidioc_g_fmt_vid_cap()\n");
	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct cx231xx *dev = video_drvdata(file);

	dprintk(3, "enter vidioc_try_fmt_vid_cap()\n");
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage = mpeglines * mpeglinesize;
	f->fmt.pix.field = V4L2_FIELD_INTERLACED;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SMPTE170M;
	dprintk(1, "VIDIOC_TRY_FMT: w: %d, h: %d\n",
		dev->ts1.width, dev->ts1.height);
	dprintk(3, "exit vidioc_try_fmt_vid_cap()\n");
	return 0;
}

static int vidioc_log_status(struct file *file, void *priv)
{
	struct cx231xx *dev = video_drvdata(file);

	call_all(dev, core, log_status);
	return v4l2_ctrl_log_status(file, priv);
}

static const struct v4l2_file_operations mpeg_fops = {
	.owner	       = THIS_MODULE,
	.open	       = v4l2_fh_open,
	.release       = vb2_fop_release,
	.read	       = vb2_fop_read,
	.poll          = vb2_fop_poll,
	.mmap	       = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
};

static const struct v4l2_ioctl_ops mpeg_ioctl_ops = {
	.vidioc_s_std		 = vidioc_s_std,
	.vidioc_g_std		 = vidioc_g_std,
	.vidioc_g_tuner          = cx231xx_g_tuner,
	.vidioc_s_tuner          = cx231xx_s_tuner,
	.vidioc_g_frequency      = cx231xx_g_frequency,
	.vidioc_s_frequency      = cx231xx_s_frequency,
	.vidioc_enum_input	 = cx231xx_enum_input,
	.vidioc_g_input		 = cx231xx_g_input,
	.vidioc_s_input		 = cx231xx_s_input,
	.vidioc_s_ctrl		 = vidioc_s_ctrl,
	.vidioc_g_pixelaspect	 = vidioc_g_pixelaspect,
	.vidioc_g_selection	 = vidioc_g_selection,
	.vidioc_querycap	 = cx231xx_querycap,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	 = vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	 = vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	 = vidioc_try_fmt_vid_cap,
	.vidioc_reqbufs		 = vb2_ioctl_reqbufs,
	.vidioc_querybuf	 = vb2_ioctl_querybuf,
	.vidioc_qbuf		 = vb2_ioctl_qbuf,
	.vidioc_dqbuf		 = vb2_ioctl_dqbuf,
	.vidioc_streamon	 = vb2_ioctl_streamon,
	.vidioc_streamoff	 = vb2_ioctl_streamoff,
	.vidioc_log_status	 = vidioc_log_status,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register	 = cx231xx_g_register,
	.vidioc_s_register	 = cx231xx_s_register,
#endif
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static struct video_device cx231xx_mpeg_template = {
	.name          = "cx231xx",
	.fops          = &mpeg_fops,
	.ioctl_ops     = &mpeg_ioctl_ops,
	.minor         = -1,
	.tvnorms       = V4L2_STD_ALL,
};

void cx231xx_417_unregister(struct cx231xx *dev)
{
	dprintk(1, "%s()\n", __func__);
	dprintk(3, "%s()\n", __func__);

	if (video_is_registered(&dev->v4l_device)) {
		video_unregister_device(&dev->v4l_device);
		v4l2_ctrl_handler_free(&dev->mpeg_ctrl_handler.hdl);
	}
}

static int cx231xx_s_video_encoding(struct cx2341x_handler *cxhdl, u32 val)
{
	struct cx231xx *dev = container_of(cxhdl, struct cx231xx, mpeg_ctrl_handler);
	int is_mpeg1 = val == V4L2_MPEG_VIDEO_ENCODING_MPEG_1;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
	};

	/* fix videodecoder resolution */
	format.format.width = cxhdl->width / (is_mpeg1 ? 2 : 1);
	format.format.height = cxhdl->height;
	format.format.code = MEDIA_BUS_FMT_FIXED;
	v4l2_subdev_call(dev->sd_cx25840, pad, set_fmt, NULL, &format);
	return 0;
}

static int cx231xx_s_audio_sampling_freq(struct cx2341x_handler *cxhdl, u32 idx)
{
	static const u32 freqs[3] = { 44100, 48000, 32000 };
	struct cx231xx *dev = container_of(cxhdl, struct cx231xx, mpeg_ctrl_handler);

	/* The audio clock of the digitizer must match the codec sample
	   rate otherwise you get some very strange effects. */
	if (idx < ARRAY_SIZE(freqs))
		call_all(dev, audio, s_clock_freq, freqs[idx]);
	return 0;
}

static const struct cx2341x_handler_ops cx231xx_ops = {
	/* needed for the video clock freq */
	.s_audio_sampling_freq = cx231xx_s_audio_sampling_freq,
	/* needed for setting up the video resolution */
	.s_video_encoding = cx231xx_s_video_encoding,
};

static void cx231xx_video_dev_init(
	struct cx231xx *dev,
	struct usb_device *usbdev,
	struct video_device *vfd,
	const struct video_device *template,
	const char *type)
{
	dprintk(1, "%s()\n", __func__);
	*vfd = *template;
	snprintf(vfd->name, sizeof(vfd->name), "%s %s (%s)", dev->name,
		type, cx231xx_boards[dev->model].name);

	vfd->v4l2_dev = &dev->v4l2_dev;
	vfd->lock = &dev->lock;
	vfd->release = video_device_release_empty;
	vfd->ctrl_handler = &dev->mpeg_ctrl_handler.hdl;
	video_set_drvdata(vfd, dev);
	if (dev->tuner_type == TUNER_ABSENT) {
		v4l2_disable_ioctl(vfd, VIDIOC_G_FREQUENCY);
		v4l2_disable_ioctl(vfd, VIDIOC_S_FREQUENCY);
		v4l2_disable_ioctl(vfd, VIDIOC_G_TUNER);
		v4l2_disable_ioctl(vfd, VIDIOC_S_TUNER);
	}
}

int cx231xx_417_register(struct cx231xx *dev)
{
	/* FIXME: Port1 hardcoded here */
	int err;
	struct cx231xx_tsport *tsport = &dev->ts1;
	struct vb2_queue *q;

	dprintk(1, "%s()\n", __func__);

	/* Set default TV standard */
	dev->encodernorm = cx231xx_tvnorms[0];

	if (dev->encodernorm.id & V4L2_STD_525_60)
		tsport->height = 480;
	else
		tsport->height = 576;

	tsport->width = 720;
	err = cx2341x_handler_init(&dev->mpeg_ctrl_handler, 50);
	if (err) {
		dprintk(3, "%s: can't init cx2341x controls\n", dev->name);
		return err;
	}
	dev->mpeg_ctrl_handler.func = cx231xx_mbox_func;
	dev->mpeg_ctrl_handler.priv = dev;
	dev->mpeg_ctrl_handler.ops = &cx231xx_ops;
	if (dev->sd_cx25840)
		v4l2_ctrl_add_handler(&dev->mpeg_ctrl_handler.hdl,
				dev->sd_cx25840->ctrl_handler, NULL, false);
	if (dev->mpeg_ctrl_handler.hdl.error) {
		err = dev->mpeg_ctrl_handler.hdl.error;
		dprintk(3, "%s: can't add cx25840 controls\n", dev->name);
		v4l2_ctrl_handler_free(&dev->mpeg_ctrl_handler.hdl);
		return err;
	}
	dev->norm = V4L2_STD_NTSC;

	dev->mpeg_ctrl_handler.port = CX2341X_PORT_SERIAL;
	cx2341x_handler_set_50hz(&dev->mpeg_ctrl_handler, false);

	/* Allocate and initialize V4L video device */
	cx231xx_video_dev_init(dev, dev->udev,
			&dev->v4l_device, &cx231xx_mpeg_template, "mpeg");
	q = &dev->mpegq;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_USERPTR | VB2_MMAP | VB2_DMABUF | VB2_READ;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct cx231xx_buffer);
	q->ops = &cx231xx_video_qops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed = 1;
	q->lock = &dev->lock;
	err = vb2_queue_init(q);
	if (err)
		return err;
	dev->v4l_device.queue = q;

	err = video_register_device(&dev->v4l_device,
		VFL_TYPE_VIDEO, -1);
	if (err < 0) {
		dprintk(3, "%s: can't register mpeg device\n", dev->name);
		v4l2_ctrl_handler_free(&dev->mpeg_ctrl_handler.hdl);
		return err;
	}

	dprintk(3, "%s: registered device video%d [mpeg]\n",
	       dev->name, dev->v4l_device.num);

	return 0;
}

MODULE_FIRMWARE(CX231xx_FIRM_IMAGE_NAME);
