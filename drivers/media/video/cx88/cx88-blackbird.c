/*
 *
 *  Support for a cx23416 mpeg encoder via cx2388x host port.
 *  "blackbird" reference design.
 *
 *    (c) 2004 Jelle Foks <jelle@foks.8m.com>
 *    (c) 2004 Gerd Knorr <kraxel@bytesex.org>
 *
 *    (c) 2005-2006 Mauro Carvalho Chehab <mchehab@infradead.org>
 *        - video_ioctl2 conversion
 *
 *  Includes parts from the ivtv driver( http://ivtv.sourceforge.net/),
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <media/v4l2-common.h>
#include <media/cx2341x.h>

#include "cx88.h"

MODULE_DESCRIPTION("driver for cx2388x/cx23416 based mpeg encoder cards");
MODULE_AUTHOR("Jelle Foks <jelle@foks.8m.com>, Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

static unsigned int mpegbufs = 32;
module_param(mpegbufs,int,0644);
MODULE_PARM_DESC(mpegbufs,"number of mpeg buffers, range 2-32");

static unsigned int debug = 0;
module_param(debug,int,0644);
MODULE_PARM_DESC(debug,"enable debug messages [blackbird]");

#define dprintk(level,fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG "%s/2-bb: " fmt, dev->core->name , ## arg)


/* ------------------------------------------------------------------ */

#define OLD_BLACKBIRD_FIRM_IMAGE_SIZE 262144
#define     BLACKBIRD_FIRM_IMAGE_SIZE 376836

/* defines below are from ivtv-driver.h */

#define IVTV_CMD_HW_BLOCKS_RST 0xFFFFFFFF

/* Firmware API commands */
#define IVTV_API_STD_TIMEOUT 500

enum blackbird_capture_type {
	BLACKBIRD_MPEG_CAPTURE,
	BLACKBIRD_RAW_CAPTURE,
	BLACKBIRD_RAW_PASSTHRU_CAPTURE
};
enum blackbird_capture_bits {
	BLACKBIRD_RAW_BITS_NONE             = 0x00,
	BLACKBIRD_RAW_BITS_YUV_CAPTURE      = 0x01,
	BLACKBIRD_RAW_BITS_PCM_CAPTURE      = 0x02,
	BLACKBIRD_RAW_BITS_VBI_CAPTURE      = 0x04,
	BLACKBIRD_RAW_BITS_PASSTHRU_CAPTURE = 0x08,
	BLACKBIRD_RAW_BITS_TO_HOST_CAPTURE  = 0x10
};
enum blackbird_capture_end {
	BLACKBIRD_END_AT_GOP, /* stop at the end of gop, generate irq */
	BLACKBIRD_END_NOW, /* stop immediately, no irq */
};
enum blackbird_framerate {
	BLACKBIRD_FRAMERATE_NTSC_30, /* NTSC: 30fps */
	BLACKBIRD_FRAMERATE_PAL_25   /* PAL: 25fps */
};
enum blackbird_stream_port {
	BLACKBIRD_OUTPUT_PORT_MEMORY,
	BLACKBIRD_OUTPUT_PORT_STREAMING,
	BLACKBIRD_OUTPUT_PORT_SERIAL
};
enum blackbird_data_xfer_status {
	BLACKBIRD_MORE_BUFFERS_FOLLOW,
	BLACKBIRD_LAST_BUFFER,
};
enum blackbird_picture_mask {
	BLACKBIRD_PICTURE_MASK_NONE,
	BLACKBIRD_PICTURE_MASK_I_FRAMES,
	BLACKBIRD_PICTURE_MASK_I_P_FRAMES = 0x3,
	BLACKBIRD_PICTURE_MASK_ALL_FRAMES = 0x7,
};
enum blackbird_vbi_mode_bits {
	BLACKBIRD_VBI_BITS_SLICED,
	BLACKBIRD_VBI_BITS_RAW,
};
enum blackbird_vbi_insertion_bits {
	BLACKBIRD_VBI_BITS_INSERT_IN_XTENSION_USR_DATA,
	BLACKBIRD_VBI_BITS_INSERT_IN_PRIVATE_PACKETS = 0x1 << 1,
	BLACKBIRD_VBI_BITS_SEPARATE_STREAM = 0x2 << 1,
	BLACKBIRD_VBI_BITS_SEPARATE_STREAM_USR_DATA = 0x4 << 1,
	BLACKBIRD_VBI_BITS_SEPARATE_STREAM_PRV_DATA = 0x5 << 1,
};
enum blackbird_dma_unit {
	BLACKBIRD_DMA_BYTES,
	BLACKBIRD_DMA_FRAMES,
};
enum blackbird_dma_transfer_status_bits {
	BLACKBIRD_DMA_TRANSFER_BITS_DONE = 0x01,
	BLACKBIRD_DMA_TRANSFER_BITS_ERROR = 0x04,
	BLACKBIRD_DMA_TRANSFER_BITS_LL_ERROR = 0x10,
};
enum blackbird_pause {
	BLACKBIRD_PAUSE_ENCODING,
	BLACKBIRD_RESUME_ENCODING,
};
enum blackbird_copyright {
	BLACKBIRD_COPYRIGHT_OFF,
	BLACKBIRD_COPYRIGHT_ON,
};
enum blackbird_notification_type {
	BLACKBIRD_NOTIFICATION_REFRESH,
};
enum blackbird_notification_status {
	BLACKBIRD_NOTIFICATION_OFF,
	BLACKBIRD_NOTIFICATION_ON,
};
enum blackbird_notification_mailbox {
	BLACKBIRD_NOTIFICATION_NO_MAILBOX = -1,
};
enum blackbird_field1_lines {
	BLACKBIRD_FIELD1_SAA7114 = 0x00EF, /* 239 */
	BLACKBIRD_FIELD1_SAA7115 = 0x00F0, /* 240 */
	BLACKBIRD_FIELD1_MICRONAS = 0x0105, /* 261 */
};
enum blackbird_field2_lines {
	BLACKBIRD_FIELD2_SAA7114 = 0x00EF, /* 239 */
	BLACKBIRD_FIELD2_SAA7115 = 0x00F0, /* 240 */
	BLACKBIRD_FIELD2_MICRONAS = 0x0106, /* 262 */
};
enum blackbird_custom_data_type {
	BLACKBIRD_CUSTOM_EXTENSION_USR_DATA,
	BLACKBIRD_CUSTOM_PRIVATE_PACKET,
};
enum blackbird_mute {
	BLACKBIRD_UNMUTE,
	BLACKBIRD_MUTE,
};
enum blackbird_mute_video_mask {
	BLACKBIRD_MUTE_VIDEO_V_MASK = 0x0000FF00,
	BLACKBIRD_MUTE_VIDEO_U_MASK = 0x00FF0000,
	BLACKBIRD_MUTE_VIDEO_Y_MASK = 0xFF000000,
};
enum blackbird_mute_video_shift {
	BLACKBIRD_MUTE_VIDEO_V_SHIFT = 8,
	BLACKBIRD_MUTE_VIDEO_U_SHIFT = 16,
	BLACKBIRD_MUTE_VIDEO_Y_SHIFT = 24,
};

/* Registers */
#define IVTV_REG_ENC_SDRAM_REFRESH (0x07F8 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_ENC_SDRAM_PRECHARGE (0x07FC /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_SPU (0x9050 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_HW_BLOCKS (0x9054 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_VPU (0x9058 /*| IVTV_REG_OFFSET*/)
#define IVTV_REG_APU (0xA064 /*| IVTV_REG_OFFSET*/)

/* ------------------------------------------------------------------ */

static void host_setup(struct cx88_core *core)
{
	/* toggle reset of the host */
	cx_write(MO_GPHST_SOFT_RST, 1);
	udelay(100);
	cx_write(MO_GPHST_SOFT_RST, 0);
	udelay(100);

	/* host port setup */
	cx_write(MO_GPHST_WSC, 0x44444444U);
	cx_write(MO_GPHST_XFR, 0);
	cx_write(MO_GPHST_WDTH, 15);
	cx_write(MO_GPHST_HDSHK, 0);
	cx_write(MO_GPHST_MUX16, 0x44448888U);
	cx_write(MO_GPHST_MODE, 0);
}

/* ------------------------------------------------------------------ */

#define P1_MDATA0 0x390000
#define P1_MDATA1 0x390001
#define P1_MDATA2 0x390002
#define P1_MDATA3 0x390003
#define P1_MADDR2 0x390004
#define P1_MADDR1 0x390005
#define P1_MADDR0 0x390006
#define P1_RDATA0 0x390008
#define P1_RDATA1 0x390009
#define P1_RDATA2 0x39000A
#define P1_RDATA3 0x39000B
#define P1_RADDR0 0x39000C
#define P1_RADDR1 0x39000D
#define P1_RRDWR  0x39000E

static int wait_ready_gpio0_bit1(struct cx88_core *core, u32 state)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1);
	u32 gpio0,need;

	need = state ? 2 : 0;
	for (;;) {
		gpio0 = cx_read(MO_GP0_IO) & 2;
		if (need == gpio0)
			return 0;
		if (time_after(jiffies,timeout))
			return -1;
		udelay(1);
	}
}

static int memory_write(struct cx88_core *core, u32 address, u32 value)
{
	/* Warning: address is dword address (4 bytes) */
	cx_writeb(P1_MDATA0, (unsigned int)value);
	cx_writeb(P1_MDATA1, (unsigned int)(value >> 8));
	cx_writeb(P1_MDATA2, (unsigned int)(value >> 16));
	cx_writeb(P1_MDATA3, (unsigned int)(value >> 24));
	cx_writeb(P1_MADDR2, (unsigned int)(address >> 16) | 0x40);
	cx_writeb(P1_MADDR1, (unsigned int)(address >> 8));
	cx_writeb(P1_MADDR0, (unsigned int)address);
	cx_read(P1_MDATA0);
	cx_read(P1_MADDR0);

	return wait_ready_gpio0_bit1(core,1);
}

static int memory_read(struct cx88_core *core, u32 address, u32 *value)
{
	int retval;
	u32 val;

	/* Warning: address is dword address (4 bytes) */
	cx_writeb(P1_MADDR2, (unsigned int)(address >> 16) & ~0xC0);
	cx_writeb(P1_MADDR1, (unsigned int)(address >> 8));
	cx_writeb(P1_MADDR0, (unsigned int)address);
	cx_read(P1_MADDR0);

	retval = wait_ready_gpio0_bit1(core,1);

	cx_writeb(P1_MDATA3, 0);
	val     = (unsigned char)cx_read(P1_MDATA3) << 24;
	cx_writeb(P1_MDATA2, 0);
	val    |= (unsigned char)cx_read(P1_MDATA2) << 16;
	cx_writeb(P1_MDATA1, 0);
	val    |= (unsigned char)cx_read(P1_MDATA1) << 8;
	cx_writeb(P1_MDATA0, 0);
	val    |= (unsigned char)cx_read(P1_MDATA0);

	*value  = val;
	return retval;
}

static int register_write(struct cx88_core *core, u32 address, u32 value)
{
	cx_writeb(P1_RDATA0, (unsigned int)value);
	cx_writeb(P1_RDATA1, (unsigned int)(value >> 8));
	cx_writeb(P1_RDATA2, (unsigned int)(value >> 16));
	cx_writeb(P1_RDATA3, (unsigned int)(value >> 24));
	cx_writeb(P1_RADDR0, (unsigned int)address);
	cx_writeb(P1_RADDR1, (unsigned int)(address >> 8));
	cx_writeb(P1_RRDWR, 1);
	cx_read(P1_RDATA0);
	cx_read(P1_RADDR0);

	return wait_ready_gpio0_bit1(core,1);
}


static int register_read(struct cx88_core *core, u32 address, u32 *value)
{
	int retval;
	u32 val;

	cx_writeb(P1_RADDR0, (unsigned int)address);
	cx_writeb(P1_RADDR1, (unsigned int)(address >> 8));
	cx_writeb(P1_RRDWR, 0);
	cx_read(P1_RADDR0);

	retval  = wait_ready_gpio0_bit1(core,1);
	val     = (unsigned char)cx_read(P1_RDATA0);
	val    |= (unsigned char)cx_read(P1_RDATA1) << 8;
	val    |= (unsigned char)cx_read(P1_RDATA2) << 16;
	val    |= (unsigned char)cx_read(P1_RDATA3) << 24;

	*value  = val;
	return retval;
}

/* ------------------------------------------------------------------ */

static int blackbird_mbox_func(void *priv, int command, int in, int out, u32 data[CX2341X_MBOX_MAX_DATA])
{
	struct cx8802_dev *dev = priv;
	unsigned long timeout;
	u32 value, flag, retval;
	int i;

	dprintk(1,"%s: 0x%X\n", __FUNCTION__, command);

	/* this may not be 100% safe if we can't read any memory location
	   without side effects */
	memory_read(dev->core, dev->mailbox - 4, &value);
	if (value != 0x12345678) {
		dprintk(0, "Firmware and/or mailbox pointer not initialized or corrupted\n");
		return -1;
	}

	memory_read(dev->core, dev->mailbox, &flag);
	if (flag) {
		dprintk(0, "ERROR: Mailbox appears to be in use (%x)\n", flag);
		return -1;
	}

	flag |= 1; /* tell 'em we're working on it */
	memory_write(dev->core, dev->mailbox, flag);

	/* write command + args + fill remaining with zeros */
	memory_write(dev->core, dev->mailbox + 1, command); /* command code */
	memory_write(dev->core, dev->mailbox + 3, IVTV_API_STD_TIMEOUT); /* timeout */
	for (i = 0; i < in; i++) {
		memory_write(dev->core, dev->mailbox + 4 + i, data[i]);
		dprintk(1, "API Input %d = %d\n", i, data[i]);
	}
	for (; i < CX2341X_MBOX_MAX_DATA; i++)
		memory_write(dev->core, dev->mailbox + 4 + i, 0);

	flag |= 3; /* tell 'em we're done writing */
	memory_write(dev->core, dev->mailbox, flag);

	/* wait for firmware to handle the API command */
	timeout = jiffies + msecs_to_jiffies(10);
	for (;;) {
		memory_read(dev->core, dev->mailbox, &flag);
		if (0 != (flag & 4))
			break;
		if (time_after(jiffies,timeout)) {
			dprintk(0, "ERROR: API Mailbox timeout\n");
			return -1;
		}
		udelay(10);
	}

	/* read output values */
	for (i = 0; i < out; i++) {
		memory_read(dev->core, dev->mailbox + 4 + i, data + i);
		dprintk(1, "API Output %d = %d\n", i, data[i]);
	}

	memory_read(dev->core, dev->mailbox + 2, &retval);
	dprintk(1, "API result = %d\n",retval);

	flag = 0;
	memory_write(dev->core, dev->mailbox, flag);
	return retval;
}
/* ------------------------------------------------------------------ */

/* We don't need to call the API often, so using just one mailbox will probably suffice */
static int blackbird_api_cmd(struct cx8802_dev *dev, u32 command,
			     u32 inputcnt, u32 outputcnt, ...)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	va_list vargs;
	int i, err;

	va_start(vargs, outputcnt);

	for (i = 0; i < inputcnt; i++) {
		data[i] = va_arg(vargs, int);
	}
	err = blackbird_mbox_func(dev, command, inputcnt, outputcnt, data);
	for (i = 0; i < outputcnt; i++) {
		int *vptr = va_arg(vargs, int *);
		*vptr = data[i];
	}
	va_end(vargs);
	return err;
}

static int blackbird_find_mailbox(struct cx8802_dev *dev)
{
	u32 signature[4]={0x12345678, 0x34567812, 0x56781234, 0x78123456};
	int signaturecnt=0;
	u32 value;
	int i;

	for (i = 0; i < dev->fw_size; i++) {
		memory_read(dev->core, i, &value);
		if (value == signature[signaturecnt])
			signaturecnt++;
		else
			signaturecnt = 0;
		if (4 == signaturecnt) {
			dprintk(1, "Mailbox signature found\n");
			return i+1;
		}
	}
	dprintk(0, "Mailbox signature values not found!\n");
	return -1;
}

static int blackbird_load_firmware(struct cx8802_dev *dev)
{
	static const unsigned char magic[8] = {
		0xa7, 0x0d, 0x00, 0x00, 0x66, 0xbb, 0x55, 0xaa
	};
	const struct firmware *firmware;
	int i, retval = 0;
	u32 value = 0;
	u32 checksum = 0;
	u32 *dataptr;

	retval  = register_write(dev->core, IVTV_REG_VPU, 0xFFFFFFED);
	retval |= register_write(dev->core, IVTV_REG_HW_BLOCKS, IVTV_CMD_HW_BLOCKS_RST);
	retval |= register_write(dev->core, IVTV_REG_ENC_SDRAM_REFRESH, 0x80000640);
	retval |= register_write(dev->core, IVTV_REG_ENC_SDRAM_PRECHARGE, 0x1A);
	msleep(1);
	retval |= register_write(dev->core, IVTV_REG_APU, 0);

	if (retval < 0)
		dprintk(0, "Error with register_write\n");

	retval = request_firmware(&firmware, CX2341X_FIRM_ENC_FILENAME,
				  &dev->pci->dev);


	if (retval != 0) {
		dprintk(0, "ERROR: Hotplug firmware request failed (%s).\n",
			CX2341X_FIRM_ENC_FILENAME);
		dprintk(0, "Please fix your hotplug setup, the board will "
			"not work without firmware loaded!\n");
		return -1;
	}

	if ((firmware->size != BLACKBIRD_FIRM_IMAGE_SIZE) &&
	    (firmware->size != OLD_BLACKBIRD_FIRM_IMAGE_SIZE)) {
		dprintk(0, "ERROR: Firmware size mismatch (have %zd, expected %d or %d)\n",
			firmware->size, BLACKBIRD_FIRM_IMAGE_SIZE,
			OLD_BLACKBIRD_FIRM_IMAGE_SIZE);
		release_firmware(firmware);
		return -1;
	}
	dev->fw_size = firmware->size;

	if (0 != memcmp(firmware->data, magic, 8)) {
		dprintk(0, "ERROR: Firmware magic mismatch, wrong file?\n");
		release_firmware(firmware);
		return -1;
	}

	/* transfer to the chip */
	dprintk(1,"Loading firmware ...\n");
	dataptr = (u32*)firmware->data;
	for (i = 0; i < (firmware->size >> 2); i++) {
		value = *dataptr;
		checksum += ~value;
		memory_write(dev->core, i, value);
		dataptr++;
	}

	/* read back to verify with the checksum */
	for (i--; i >= 0; i--) {
		memory_read(dev->core, i, &value);
		checksum -= ~value;
	}
	if (checksum) {
		dprintk(0, "ERROR: Firmware load failed (checksum mismatch).\n");
		release_firmware(firmware);
		return -1;
	}
	release_firmware(firmware);
	dprintk(0, "Firmware upload successful.\n");

	retval |= register_write(dev->core, IVTV_REG_HW_BLOCKS, IVTV_CMD_HW_BLOCKS_RST);
	retval |= register_read(dev->core, IVTV_REG_SPU, &value);
	retval |= register_write(dev->core, IVTV_REG_SPU, value & 0xFFFFFFFE);
	msleep(1);

	retval |= register_read(dev->core, IVTV_REG_VPU, &value);
	retval |= register_write(dev->core, IVTV_REG_VPU, value & 0xFFFFFFE8);

	if (retval < 0)
		dprintk(0, "Error with register_write\n");
	return 0;
}

/**
 Settings used by the windows tv app for PVR2000:
=================================================================================================================
Profile | Codec | Resolution | CBR/VBR | Video Qlty   | V. Bitrate | Frmrate | Audio Codec | A. Bitrate | A. Mode
-----------------------------------------------------------------------------------------------------------------
MPEG-1  | MPEG1 | 352x288PAL | (CBR)   | 1000:Optimal | 2000 Kbps  | 25fps   | MPG1 Layer2 | 224kbps    | Stereo
MPEG-2  | MPEG2 | 720x576PAL | VBR     | 600 :Good    | 4000 Kbps  | 25fps   | MPG1 Layer2 | 224kbps    | Stereo
VCD     | MPEG1 | 352x288PAL | (CBR)   | 1000:Optimal | 1150 Kbps  | 25fps   | MPG1 Layer2 | 224kbps    | Stereo
DVD     | MPEG2 | 720x576PAL | VBR     | 600 :Good    | 6000 Kbps  | 25fps   | MPG1 Layer2 | 224kbps    | Stereo
DB* DVD | MPEG2 | 720x576PAL | CBR     | 600 :Good    | 6000 Kbps  | 25fps   | MPG1 Layer2 | 224kbps    | Stereo
=================================================================================================================
*DB: "DirectBurn"
*/

static void blackbird_codec_settings(struct cx8802_dev *dev)
{
	/* assign frame size */
	blackbird_api_cmd(dev, CX2341X_ENC_SET_FRAME_SIZE, 2, 0,
				dev->height, dev->width);

	dev->params.width = dev->width;
	dev->params.height = dev->height;
	dev->params.is_50hz = (dev->core->tvnorm & V4L2_STD_625_50) != 0;

	cx2341x_update(dev, blackbird_mbox_func, NULL, &dev->params);
}

static struct v4l2_mpeg_compression default_mpeg_params = {
	.st_type          = V4L2_MPEG_PS_2,
	.st_bitrate       = {
		.mode     = V4L2_BITRATE_CBR,
		.min      = 0,
		.target   = 0,
		.max      = 0
	},
	.ts_pid_pmt       = 16,
	.ts_pid_audio     = 260,
	.ts_pid_video     = 256,
	.ts_pid_pcr       = 259,
	.ps_size          = 0,
	.au_type          = V4L2_MPEG_AU_2_II,
	.au_bitrate       = {
		.mode     = V4L2_BITRATE_CBR,
		.min      = 224,
		.target   = 224,
		.max      = 224
	},
	.au_sample_rate    = 48000,
	.au_pesid          = 0,
	.vi_type           = V4L2_MPEG_VI_2,
	.vi_aspect_ratio   = V4L2_MPEG_ASPECT_4_3,
	.vi_bitrate        = {
		.mode      = V4L2_BITRATE_CBR,
		.min       = 4000,
		.target    = 4500,
		.max       = 6000
	},
	.vi_frame_rate     = 25,
	.vi_frames_per_gop = 12,
	.vi_bframes_count  = 2,
	.vi_pesid          = 0,
	.closed_gops       = 1,
	.pulldown          = 0
};

static int blackbird_initialize_codec(struct cx8802_dev *dev)
{
	struct cx88_core *core = dev->core;
	int version;
	int retval;

	dprintk(1,"Initialize codec\n");
	retval = blackbird_api_cmd(dev, CX2341X_ENC_PING_FW, 0, 0); /* ping */
	if (retval < 0) {
		/* ping was not successful, reset and upload firmware */
		cx_write(MO_SRST_IO, 0); /* SYS_RSTO=0 */
		msleep(1);
		cx_write(MO_SRST_IO, 1); /* SYS_RSTO=1 */
		msleep(1);
		retval = blackbird_load_firmware(dev);
		if (retval < 0)
			return retval;

		dev->mailbox = blackbird_find_mailbox(dev);
		if (dev->mailbox < 0)
			return -1;

		retval = blackbird_api_cmd(dev, CX2341X_ENC_PING_FW, 0, 0); /* ping */
		if (retval < 0) {
			dprintk(0, "ERROR: Firmware ping failed!\n");
			return -1;
		}

		retval = blackbird_api_cmd(dev, CX2341X_ENC_GET_VERSION, 0, 1, &version);
		if (retval < 0) {
			dprintk(0, "ERROR: Firmware get encoder version failed!\n");
			return -1;
		}
		dprintk(0, "Firmware version is 0x%08x\n", version);
	}
	msleep(1);

	cx_write(MO_PINMUX_IO, 0x88); /* 656-8bit IO and enable MPEG parallel IO */
	cx_clear(MO_INPUT_FORMAT, 0x100); /* chroma subcarrier lock to normal? */
	cx_write(MO_VBOS_CONTROL, 0x84A00); /* no 656 mode, 8-bit pixels, disable VBI */
	cx_clear(MO_OUTPUT_FORMAT, 0x0008); /* Normal Y-limits to let the mpeg encoder sync */

	blackbird_codec_settings(dev);
	msleep(1);

	/* blackbird_api_cmd(dev, IVTV_API_ASSIGN_NUM_VSYNC_LINES, 4, 0, 0xef, 0xef);
	   blackbird_api_cmd(dev, IVTV_API_ASSIGN_NUM_VSYNC_LINES, 4, 0, 0xf0, 0xf0);
	   blackbird_api_cmd(dev, IVTV_API_ASSIGN_NUM_VSYNC_LINES, 4, 0, 0x180, 0x180); */
	blackbird_api_cmd(dev, CX2341X_ENC_SET_NUM_VSYNC_LINES, 2, 0,
			BLACKBIRD_FIELD1_SAA7115,
			BLACKBIRD_FIELD2_SAA7115
		);

	/* blackbird_api_cmd(dev, IVTV_API_ASSIGN_PLACEHOLDER, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0); */
	blackbird_api_cmd(dev, CX2341X_ENC_SET_PLACEHOLDER, 12, 0,
			BLACKBIRD_CUSTOM_EXTENSION_USR_DATA,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

	/* initialize the video input */
	blackbird_api_cmd(dev, CX2341X_ENC_INITIALIZE_INPUT, 0, 0);

	msleep(1);

	blackbird_api_cmd(dev, CX2341X_ENC_MUTE_VIDEO, 1, 0, BLACKBIRD_UNMUTE);
	msleep(1);
	blackbird_api_cmd(dev, CX2341X_ENC_MUTE_AUDIO, 1, 0, BLACKBIRD_UNMUTE);
	msleep(1);

	/* start capturing to the host interface */
	/* blackbird_api_cmd(dev, CX2341X_ENC_START_CAPTURE, 2, 0, 0, 0x13); */
	blackbird_api_cmd(dev, CX2341X_ENC_START_CAPTURE, 2, 0,
			BLACKBIRD_MPEG_CAPTURE,
			BLACKBIRD_RAW_BITS_NONE
		);
	msleep(10);

	blackbird_api_cmd(dev, CX2341X_ENC_REFRESH_INPUT, 0,0);
	return 0;
}

/* ------------------------------------------------------------------ */

static int bb_buf_setup(struct videobuf_queue *q,
			unsigned int *count, unsigned int *size)
{
	struct cx8802_fh *fh = q->priv_data;

	fh->dev->ts_packet_size  = 188 * 4; /* was: 512 */
	fh->dev->ts_packet_count = mpegbufs; /* was: 100 */

	*size = fh->dev->ts_packet_size * fh->dev->ts_packet_count;
	*count = fh->dev->ts_packet_count;
	return 0;
}

static int
bb_buf_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
	       enum v4l2_field field)
{
	struct cx8802_fh *fh = q->priv_data;
	return cx8802_buf_prepare(q, fh->dev, (struct cx88_buffer*)vb, field);
}

static void
bb_buf_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct cx8802_fh *fh = q->priv_data;
	cx8802_buf_queue(fh->dev, (struct cx88_buffer*)vb);
}

static void
bb_buf_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	cx88_free_buffer(q, (struct cx88_buffer*)vb);
}

static struct videobuf_queue_ops blackbird_qops = {
	.buf_setup    = bb_buf_setup,
	.buf_prepare  = bb_buf_prepare,
	.buf_queue    = bb_buf_queue,
	.buf_release  = bb_buf_release,
};

/* ------------------------------------------------------------------ */

static const u32 *ctrl_classes[] = {
	cx88_user_ctrls,
	cx2341x_mpeg_ctrls,
	NULL
};

static int blackbird_queryctrl(struct cx8802_dev *dev, struct v4l2_queryctrl *qctrl)
{
	qctrl->id = v4l2_ctrl_next(ctrl_classes, qctrl->id);
	if (qctrl->id == 0)
		return -EINVAL;

	/* Standard V4L2 controls */
	if (cx8800_ctrl_query(qctrl) == 0)
		return 0;

	/* MPEG V4L2 controls */
	if (cx2341x_ctrl_query(&dev->params, qctrl))
		qctrl->flags |= V4L2_CTRL_FLAG_DISABLED;
	return 0;
}

/* ------------------------------------------------------------------ */
/* IOCTL Handlers                                                     */

static int vidioc_querymenu (struct file *file, void *priv,
				struct v4l2_querymenu *qmenu)
{
	struct cx8802_dev *dev  = ((struct cx8802_fh *)priv)->dev;
	struct v4l2_queryctrl qctrl;

	qctrl.id = qmenu->id;
	blackbird_queryctrl(dev, &qctrl);
	return v4l2_ctrl_query_menu(qmenu, &qctrl, cx2341x_ctrl_get_menu(qmenu->id));
}

static int vidioc_querycap (struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
	struct cx8802_dev *dev  = ((struct cx8802_fh *)priv)->dev;
	struct cx88_core  *core = dev->core;

	strcpy(cap->driver, "cx88_blackbird");
	strlcpy(cap->card, cx88_boards[core->board].name,sizeof(cap->card));
	sprintf(cap->bus_info,"PCI:%s",pci_name(dev->pci));
	cap->version = CX88_VERSION_CODE;
	cap->capabilities =
		V4L2_CAP_VIDEO_CAPTURE |
		V4L2_CAP_READWRITE     |
		V4L2_CAP_STREAMING;
	if (UNSET != core->tuner_type)
		cap->capabilities |= V4L2_CAP_TUNER;
	return 0;
}

static int vidioc_enum_fmt_cap (struct file *file, void  *priv,
					struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
		return -EINVAL;

	strlcpy(f->description, "MPEG", sizeof(f->description));
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f->pixelformat = V4L2_PIX_FMT_MPEG;
	return 0;
}

static int vidioc_g_fmt_cap (struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct cx8802_fh  *fh   = priv;
	struct cx8802_dev *dev  = fh->dev;

	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage    = dev->ts_packet_size * dev->ts_packet_count; /* 188 * 4 * 1024; */
	f->fmt.pix.colorspace   = 0;
	f->fmt.pix.width        = dev->width;
	f->fmt.pix.height       = dev->height;
	f->fmt.pix.field        = fh->mpegq.field;
	dprintk(0,"VIDIOC_G_FMT: w: %d, h: %d, f: %d\n",
		dev->width, dev->height, fh->mpegq.field );
	return 0;
}

static int vidioc_try_fmt_cap (struct file *file, void *priv,
			struct v4l2_format *f)
{
	struct cx8802_fh  *fh   = priv;
	struct cx8802_dev *dev  = fh->dev;

	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage    = dev->ts_packet_size * dev->ts_packet_count; /* 188 * 4 * 1024; */;
	f->fmt.pix.colorspace   = 0;
	dprintk(0,"VIDIOC_TRY_FMT: w: %d, h: %d, f: %d\n",
		dev->width, dev->height, fh->mpegq.field );
	return 0;
}

static int vidioc_s_fmt_cap (struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct cx8802_fh  *fh   = priv;
	struct cx8802_dev *dev  = fh->dev;
	struct cx88_core  *core = dev->core;

	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage    = dev->ts_packet_size * dev->ts_packet_count; /* 188 * 4 * 1024; */;
	f->fmt.pix.colorspace   = 0;
	dev->width              = f->fmt.pix.width;
	dev->height             = f->fmt.pix.height;
	fh->mpegq.field         = f->fmt.pix.field;
	cx88_set_scale(core, f->fmt.pix.width, f->fmt.pix.height, f->fmt.pix.field);
	blackbird_api_cmd(dev, CX2341X_ENC_SET_FRAME_SIZE, 2, 0,
				f->fmt.pix.height, f->fmt.pix.width);
	dprintk(0,"VIDIOC_S_FMT: w: %d, h: %d, f: %d\n",
		f->fmt.pix.width, f->fmt.pix.height, f->fmt.pix.field );
	return 0;
}

static int vidioc_reqbufs (struct file *file, void *priv, struct v4l2_requestbuffers *p)
{
	struct cx8802_fh  *fh   = priv;
	return (videobuf_reqbufs(&fh->mpegq, p));
}

static int vidioc_querybuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cx8802_fh  *fh   = priv;
	return (videobuf_querybuf(&fh->mpegq, p));
}

static int vidioc_qbuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cx8802_fh  *fh   = priv;
	return (videobuf_qbuf(&fh->mpegq, p));
}

static int vidioc_dqbuf (struct file *file, void *priv, struct v4l2_buffer *p)
{
	struct cx8802_fh  *fh   = priv;
	return (videobuf_dqbuf(&fh->mpegq, p,
				file->f_flags & O_NONBLOCK));
}

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct cx8802_fh  *fh   = priv;
	return videobuf_streamon(&fh->mpegq);
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
	struct cx8802_fh  *fh   = priv;
	return videobuf_streamoff(&fh->mpegq);
}

static int vidioc_g_mpegcomp (struct file *file, void *fh,
			      struct v4l2_mpeg_compression *f)
{
	printk(KERN_WARNING "VIDIOC_G_MPEGCOMP is obsolete. "
				"Replace with VIDIOC_G_EXT_CTRLS!");
	memcpy(f,&default_mpeg_params,sizeof(*f));
	return 0;
}

static int vidioc_s_mpegcomp (struct file *file, void *fh,
			      struct v4l2_mpeg_compression *f)
{
	printk(KERN_WARNING "VIDIOC_S_MPEGCOMP is obsolete. "
				"Replace with VIDIOC_S_EXT_CTRLS!");
	return 0;
}

static int vidioc_g_ext_ctrls (struct file *file, void *priv,
			       struct v4l2_ext_controls *f)
{
	struct cx8802_dev *dev  = ((struct cx8802_fh *)priv)->dev;

	if (f->ctrl_class != V4L2_CTRL_CLASS_MPEG)
		return -EINVAL;
	return cx2341x_ext_ctrls(&dev->params, f, VIDIOC_G_EXT_CTRLS);
}

static int vidioc_s_ext_ctrls (struct file *file, void *priv,
			       struct v4l2_ext_controls *f)
{
	struct cx8802_dev *dev  = ((struct cx8802_fh *)priv)->dev;
	struct cx2341x_mpeg_params p;
	int err;

	if (f->ctrl_class != V4L2_CTRL_CLASS_MPEG)
		return -EINVAL;
	p = dev->params;
	err = cx2341x_ext_ctrls(&p, f, VIDIOC_S_EXT_CTRLS);
	if (!err) {
		err = cx2341x_update(dev, blackbird_mbox_func, &dev->params, &p);
		dev->params = p;
	}
	return err;
}

static int vidioc_try_ext_ctrls (struct file *file, void *priv,
			       struct v4l2_ext_controls *f)
{
	struct cx8802_dev *dev  = ((struct cx8802_fh *)priv)->dev;
	struct cx2341x_mpeg_params p;
	int err;

	if (f->ctrl_class != V4L2_CTRL_CLASS_MPEG)
		return -EINVAL;
	p = dev->params;
	err = cx2341x_ext_ctrls(&p, f, VIDIOC_TRY_EXT_CTRLS);

	return err;
}

static int vidioc_s_frequency (struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct cx8802_fh  *fh   = priv;
	struct cx8802_dev *dev  = fh->dev;
	struct cx88_core  *core = dev->core;

	blackbird_api_cmd(fh->dev, CX2341X_ENC_STOP_CAPTURE, 3, 0,
				BLACKBIRD_END_NOW,
				BLACKBIRD_MPEG_CAPTURE,
				BLACKBIRD_RAW_BITS_NONE);
	cx88_set_freq (core,f);
	blackbird_initialize_codec(dev);
	cx88_set_scale(dev->core, dev->width, dev->height,
			fh->mpegq.field);
	return 0;
}

static int vidioc_log_status (struct file *file, void *priv)
{
	struct cx8802_dev *dev  = ((struct cx8802_fh *)priv)->dev;
	struct cx88_core  *core = dev->core;
	char name[32 + 2];

	snprintf(name, sizeof(name), "%s/2", core->name);
	printk("%s/2: ============  START LOG STATUS  ============\n",
		core->name);
	cx88_call_i2c_clients(core, VIDIOC_LOG_STATUS, NULL);
	cx2341x_log_status(&dev->params, name);
	printk("%s/2: =============  END LOG STATUS  =============\n",
		core->name);
	return 0;
}

static int vidioc_queryctrl (struct file *file, void *priv,
				struct v4l2_queryctrl *qctrl)
{
	struct cx8802_dev *dev  = ((struct cx8802_fh *)priv)->dev;

	if (blackbird_queryctrl(dev, qctrl) == 0)
		return 0;

	qctrl->id = v4l2_ctrl_next(ctrl_classes, qctrl->id);
	if (unlikely(qctrl->id == 0))
		return -EINVAL;
	return cx8800_ctrl_query(qctrl);
}

static int vidioc_enum_input (struct file *file, void *priv,
				struct v4l2_input *i)
{
	struct cx88_core  *core = ((struct cx8802_fh *)priv)->dev->core;
	return cx88_enum_input (core,i);
}

static int vidioc_g_ctrl (struct file *file, void *priv,
				struct v4l2_control *ctl)
{
	struct cx88_core  *core = ((struct cx8802_fh *)priv)->dev->core;
	return
		cx88_get_control(core,ctl);
}

static int vidioc_s_ctrl (struct file *file, void *priv,
				struct v4l2_control *ctl)
{
	struct cx88_core  *core = ((struct cx8802_fh *)priv)->dev->core;
	return
		cx88_set_control(core,ctl);
}

static int vidioc_g_frequency (struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct cx8802_fh  *fh   = priv;
	struct cx88_core  *core = fh->dev->core;

	if (unlikely(UNSET == core->tuner_type))
		return -EINVAL;

	f->type = V4L2_TUNER_ANALOG_TV;
	f->frequency = core->freq;
	cx88_call_i2c_clients(core,VIDIOC_G_FREQUENCY,f);

	return 0;
}

static int vidioc_g_input (struct file *file, void *priv, unsigned int *i)
{
	struct cx88_core  *core = ((struct cx8802_fh *)priv)->dev->core;

	*i = core->input;
	return 0;
}

static int vidioc_s_input (struct file *file, void *priv, unsigned int i)
{
	struct cx88_core  *core = ((struct cx8802_fh *)priv)->dev->core;

	if (i >= 4)
		return -EINVAL;

	mutex_lock(&core->lock);
	cx88_newstation(core);
	cx88_video_mux(core,i);
	mutex_unlock(&core->lock);
	return 0;
}

static int vidioc_g_tuner (struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct cx88_core  *core = ((struct cx8802_fh *)priv)->dev->core;
	u32 reg;

	if (unlikely(UNSET == core->tuner_type))
		return -EINVAL;
	if (0 != t->index)
		return -EINVAL;

	strcpy(t->name, "Television");
	t->type       = V4L2_TUNER_ANALOG_TV;
	t->capability = V4L2_TUNER_CAP_NORM;
	t->rangehigh  = 0xffffffffUL;

	cx88_get_stereo(core ,t);
	reg = cx_read(MO_DEVICE_STATUS);
	t->signal = (reg & (1<<5)) ? 0xffff : 0x0000;
	return 0;
}

static int vidioc_s_tuner (struct file *file, void *priv,
				struct v4l2_tuner *t)
{
	struct cx88_core  *core = ((struct cx8802_fh *)priv)->dev->core;

	if (UNSET == core->tuner_type)
		return -EINVAL;
	if (0 != t->index)
		return -EINVAL;

	cx88_set_stereo(core, t->audmode, 1);
	return 0;
}

static int vidioc_s_std (struct file *file, void *priv, v4l2_std_id *id)
{
	struct cx88_core  *core = ((struct cx8802_fh *)priv)->dev->core;

	mutex_lock(&core->lock);
	cx88_set_tvnorm(core,*id);
	mutex_unlock(&core->lock);
	return 0;
}

/* FIXME: cx88_ioctl_hook not implemented */

static int mpeg_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct cx8802_dev *dev = NULL;
	struct cx8802_fh *fh;
	struct cx8802_driver *drv = NULL;
	int err;

       dev = cx8802_get_device(inode);

	dprintk( 1, "%s\n", __FUNCTION__);

	if (dev == NULL)
		return -ENODEV;

	/* Make sure we can acquire the hardware */
	drv = cx8802_get_driver(dev, CX88_MPEG_BLACKBIRD);
	if (drv) {
		err = drv->request_acquire(drv);
		if(err != 0) {
			dprintk(1,"%s: Unable to acquire hardware, %d\n", __FUNCTION__, err);
			return err;
		}
	}

	if (blackbird_initialize_codec(dev) < 0) {
		if (drv)
			drv->request_release(drv);
		return -EINVAL;
	}
	dprintk(1,"open minor=%d\n",minor);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh),GFP_KERNEL);
	if (NULL == fh) {
		if (drv)
			drv->request_release(drv);
		return -ENOMEM;
	}
	file->private_data = fh;
	fh->dev      = dev;

	videobuf_queue_init(&fh->mpegq, &blackbird_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_INTERLACED,
			    sizeof(struct cx88_buffer),
			    fh);

	/* FIXME: locking against other video device */
	cx88_set_scale(dev->core, dev->width, dev->height,
			fh->mpegq.field);

	return 0;
}

static int mpeg_release(struct inode *inode, struct file *file)
{
	struct cx8802_fh  *fh  = file->private_data;
	struct cx8802_dev *dev = NULL;
	struct cx8802_driver *drv = NULL;

	/* blackbird_api_cmd(fh->dev, CX2341X_ENC_STOP_CAPTURE, 3, 0, BLACKBIRD_END_NOW, 0, 0x13); */
	blackbird_api_cmd(fh->dev, CX2341X_ENC_STOP_CAPTURE, 3, 0,
			BLACKBIRD_END_NOW,
			BLACKBIRD_MPEG_CAPTURE,
			BLACKBIRD_RAW_BITS_NONE
		);

	cx8802_cancel_buffers(fh->dev);
	/* stop mpeg capture */
	if (fh->mpegq.streaming)
		videobuf_streamoff(&fh->mpegq);
	if (fh->mpegq.reading)
		videobuf_read_stop(&fh->mpegq);

	videobuf_mmap_free(&fh->mpegq);
	file->private_data = NULL;
	kfree(fh);

	/* Make sure we release the hardware */
	dev = cx8802_get_device(inode);
	if (dev == NULL)
		return -ENODEV;

	drv = cx8802_get_driver(dev, CX88_MPEG_BLACKBIRD);
	if (drv)
		drv->request_release(drv);

	return 0;
}

static ssize_t
mpeg_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
	struct cx8802_fh *fh = file->private_data;

	return videobuf_read_stream(&fh->mpegq, data, count, ppos, 0,
				    file->f_flags & O_NONBLOCK);
}

static unsigned int
mpeg_poll(struct file *file, struct poll_table_struct *wait)
{
	struct cx8802_fh *fh = file->private_data;

	return videobuf_poll_stream(file, &fh->mpegq, wait);
}

static int
mpeg_mmap(struct file *file, struct vm_area_struct * vma)
{
	struct cx8802_fh *fh = file->private_data;

	return videobuf_mmap_mapper(&fh->mpegq, vma);
}

static const struct file_operations mpeg_fops =
{
	.owner	       = THIS_MODULE,
	.open	       = mpeg_open,
	.release       = mpeg_release,
	.read	       = mpeg_read,
	.poll          = mpeg_poll,
	.mmap	       = mpeg_mmap,
	.ioctl	       = video_ioctl2,
	.llseek        = no_llseek,
};

static struct video_device cx8802_mpeg_template =
{
	.name                 = "cx8802",
	.type                 = VID_TYPE_CAPTURE|VID_TYPE_TUNER|VID_TYPE_SCALES|VID_TYPE_MPEG_ENCODER,
	.fops                 = &mpeg_fops,
	.minor                = -1,
	.vidioc_querymenu     = vidioc_querymenu,
	.vidioc_querycap      = vidioc_querycap,
	.vidioc_enum_fmt_cap  = vidioc_enum_fmt_cap,
	.vidioc_g_fmt_cap     = vidioc_g_fmt_cap,
	.vidioc_try_fmt_cap   = vidioc_try_fmt_cap,
	.vidioc_s_fmt_cap     = vidioc_s_fmt_cap,
	.vidioc_reqbufs       = vidioc_reqbufs,
	.vidioc_querybuf      = vidioc_querybuf,
	.vidioc_qbuf          = vidioc_qbuf,
	.vidioc_dqbuf         = vidioc_dqbuf,
	.vidioc_streamon      = vidioc_streamon,
	.vidioc_streamoff     = vidioc_streamoff,
	.vidioc_g_mpegcomp    = vidioc_g_mpegcomp,
	.vidioc_s_mpegcomp    = vidioc_s_mpegcomp,
	.vidioc_g_ext_ctrls   = vidioc_g_ext_ctrls,
	.vidioc_s_ext_ctrls   = vidioc_s_ext_ctrls,
	.vidioc_try_ext_ctrls = vidioc_try_ext_ctrls,
	.vidioc_s_frequency   = vidioc_s_frequency,
	.vidioc_log_status    = vidioc_log_status,
	.vidioc_queryctrl     = vidioc_queryctrl,
	.vidioc_enum_input    = vidioc_enum_input,
	.vidioc_g_ctrl        = vidioc_g_ctrl,
	.vidioc_s_ctrl        = vidioc_s_ctrl,
	.vidioc_g_frequency   = vidioc_g_frequency,
	.vidioc_g_input       = vidioc_g_input,
	.vidioc_s_input       = vidioc_s_input,
	.vidioc_g_tuner       = vidioc_g_tuner,
	.vidioc_s_tuner       = vidioc_s_tuner,
	.vidioc_s_std         = vidioc_s_std,
	.tvnorms              = CX88_NORMS,
       .current_norm         = V4L2_STD_NTSC_M,
};

/* ------------------------------------------------------------------ */

/* The CX8802 MPEG API will call this when we can use the hardware */
static int cx8802_blackbird_advise_acquire(struct cx8802_driver *drv)
{
	struct cx88_core *core = drv->core;
	int err = 0;

	switch (core->board) {
	case CX88_BOARD_HAUPPAUGE_HVR1300:
		/* By default, core setup will leave the cx22702 out of reset, on the bus.
		 * We left the hardware on power up with the cx22702 active.
		 * We're being given access to re-arrange the GPIOs.
		 * Take the bus off the cx22702 and put the cx23416 on it.
		 */
		cx_clear(MO_GP0_IO, 0x00000080); /* cx22702 in reset */
		cx_set(MO_GP0_IO,   0x00000004); /* Disable the cx22702 */
		break;
	default:
		err = -ENODEV;
	}
	return err;
}

/* The CX8802 MPEG API will call this when we need to release the hardware */
static int cx8802_blackbird_advise_release(struct cx8802_driver *drv)
{
	struct cx88_core *core = drv->core;
	int err = 0;

	switch (core->board) {
	case CX88_BOARD_HAUPPAUGE_HVR1300:
		/* Exit leaving the cx23416 on the bus */
		break;
	default:
		err = -ENODEV;
	}
	return err;
}

static void blackbird_unregister_video(struct cx8802_dev *dev)
{
	if (dev->mpeg_dev) {
		if (-1 != dev->mpeg_dev->minor)
			video_unregister_device(dev->mpeg_dev);
		else
			video_device_release(dev->mpeg_dev);
		dev->mpeg_dev = NULL;
	}
}

static int blackbird_register_video(struct cx8802_dev *dev)
{
	int err;

	dev->mpeg_dev = cx88_vdev_init(dev->core,dev->pci,
				       &cx8802_mpeg_template,"mpeg");
	err = video_register_device(dev->mpeg_dev,VFL_TYPE_GRABBER, -1);
	if (err < 0) {
		printk(KERN_INFO "%s/2: can't register mpeg device\n",
		       dev->core->name);
		return err;
	}
	printk(KERN_INFO "%s/2: registered device video%d [mpeg]\n",
	       dev->core->name,dev->mpeg_dev->minor & 0x1f);
	return 0;
}

/* ----------------------------------------------------------- */

static int cx8802_blackbird_probe(struct cx8802_driver *drv)
{
	struct cx88_core *core = drv->core;
	struct cx8802_dev *dev = core->dvbdev;
	int err;

	dprintk( 1, "%s\n", __FUNCTION__);
	dprintk( 1, " ->being probed by Card=%d Name=%s, PCI %02x:%02x\n",
		core->board,
		core->name,
		core->pci_bus,
		core->pci_slot);

	err = -ENODEV;
	if (!(cx88_boards[core->board].mpeg & CX88_MPEG_BLACKBIRD))
		goto fail_core;

	dev->width = 720;
	dev->height = 576;
	cx2341x_fill_defaults(&dev->params);
	dev->params.port = CX2341X_PORT_STREAMING;

	cx8802_mpeg_template.current_norm = core->tvnorm;

	if (core->tvnorm & V4L2_STD_525_60) {
		dev->height = 480;
	} else {
		dev->height = 576;
	}

	/* blackbird stuff */
	printk("%s/2: cx23416 based mpeg encoder (blackbird reference design)\n",
	       core->name);
	host_setup(dev->core);

	blackbird_register_video(dev);

	/* initial device configuration: needed ? */
	mutex_lock(&dev->core->lock);
//	init_controls(core);
	cx88_set_tvnorm(core,core->tvnorm);
	cx88_video_mux(core,0);
	mutex_unlock(&dev->core->lock);

	return 0;

 fail_core:
	return err;
}

static int cx8802_blackbird_remove(struct cx8802_driver *drv)
{
	/* blackbird */
	blackbird_unregister_video(drv->core->dvbdev);

	return 0;
}

static struct cx8802_driver cx8802_blackbird_driver = {
	.type_id	= CX88_MPEG_BLACKBIRD,
	.hw_access	= CX8802_DRVCTL_SHARED,
	.probe		= cx8802_blackbird_probe,
	.remove		= cx8802_blackbird_remove,
	.advise_acquire	= cx8802_blackbird_advise_acquire,
	.advise_release	= cx8802_blackbird_advise_release,
};

static int blackbird_init(void)
{
	printk(KERN_INFO "cx2388x blackbird driver version %d.%d.%d loaded\n",
	       (CX88_VERSION_CODE >> 16) & 0xff,
	       (CX88_VERSION_CODE >>  8) & 0xff,
	       CX88_VERSION_CODE & 0xff);
#ifdef SNAPSHOT
	printk(KERN_INFO "cx2388x: snapshot date %04d-%02d-%02d\n",
	       SNAPSHOT/10000, (SNAPSHOT/100)%100, SNAPSHOT%100);
#endif
	return cx8802_register_driver(&cx8802_blackbird_driver);
}

static void blackbird_fini(void)
{
	cx8802_unregister_driver(&cx8802_blackbird_driver);
}

module_init(blackbird_init);
module_exit(blackbird_fini);

module_param_named(video_debug,cx8802_mpeg_template.debug, int, 0644);
MODULE_PARM_DESC(debug,"enable debug messages [video]");

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 * kate: eol "unix"; indent-width 3; remove-trailing-space on; replace-trailing-space-save on; tab-width 8; replace-tabs off; space-indent off; mixed-indent off
 */
