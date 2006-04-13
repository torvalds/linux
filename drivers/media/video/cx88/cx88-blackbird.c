/*
 *
 *  Support for a cx23416 mpeg encoder via cx2388x host port.
 *  "blackbird" reference design.
 *
 *    (c) 2004 Jelle Foks <jelle@foks.8m.com>
 *    (c) 2004 Gerd Knorr <kraxel@bytesex.org>
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

#include "cx88.h"
#include <media/v4l2-common.h>

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

static LIST_HEAD(cx8802_devlist);

/* ------------------------------------------------------------------ */

#define BLACKBIRD_FIRM_ENC_FILENAME "blackbird-fw-enc.bin"
#define BLACKBIRD_FIRM_IMAGE_SIZE 256*1024

/* defines below are from ivtv-driver.h */

#define IVTV_CMD_HW_BLOCKS_RST 0xFFFFFFFF

/* Firmware API commands */
#define IVTV_API_STD_TIMEOUT 500

#define BLACKBIRD_API_PING               0x80
#define BLACKBIRD_API_BEGIN_CAPTURE      0x81
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
#define BLACKBIRD_API_END_CAPTURE        0x82
enum blackbird_capture_end {
	BLACKBIRD_END_AT_GOP, /* stop at the end of gop, generate irq */
	BLACKBIRD_END_NOW, /* stop immediately, no irq */
};
#define BLACKBIRD_API_SET_AUDIO_ID       0x89
#define BLACKBIRD_API_SET_VIDEO_ID       0x8B
#define BLACKBIRD_API_SET_PCR_ID         0x8D
#define BLACKBIRD_API_SET_FRAMERATE      0x8F
enum blackbird_framerate {
	BLACKBIRD_FRAMERATE_NTSC_30, /* NTSC: 30fps */
	BLACKBIRD_FRAMERATE_PAL_25   /* PAL: 25fps */
};
#define BLACKBIRD_API_SET_RESOLUTION     0x91
#define BLACKBIRD_API_SET_VIDEO_BITRATE  0x95
enum blackbird_video_bitrate_type {
	BLACKBIRD_VIDEO_VBR,
	BLACKBIRD_VIDEO_CBR
};
#define BLACKBIRD_PEAK_RATE_DIVISOR 400
enum blackbird_mux_rate {
	BLACKBIRD_MUX_RATE_DEFAULT,
	 /* dvd mux rate: multiply by 400 to get the actual rate */
	BLACKBIRD_MUX_RATE_DVD = 25200
};
#define BLACKBIRD_API_SET_GOP_STRUCTURE  0x97
#define BLACKBIRD_API_SET_ASPECT_RATIO   0x99
enum blackbird_aspect_ratio {
	BLACKBIRD_ASPECT_RATIO_FORBIDDEN,
	BLACKBIRD_ASPECT_RATIO_1_1_SQUARE,
	BLACKBIRD_ASPECT_RATIO_4_3,
	BLACKBIRD_ASPECT_RATIO_16_9,
	BLACKBIRD_ASPECT_RATIO_221_100,
	BLACKBIRD_ASPECT_RATIO_RESERVED
};
#define BLACKBIRD_API_SET_DNR_MODE       0x9B
enum blackbird_dnr_bits {
	BLACKBIRD_DNR_BITS_MANUAL,
	BLACKBIRD_DNR_BITS_AUTO_SPATIAL,
	BLACKBIRD_DNR_BITS_AUTO_TEMPORAL,
	BLACKBIRD_DNR_BITS_AUTO
};
enum blackbird_median_filter {
	BLACKBIRD_MEDIAN_FILTER_DISABLED,
	BLACKBIRD_MEDIAN_FILTER_HORIZONTAL,
	BLACKBIRD_MEDIAN_FILTER_VERTICAL,
	BLACKBIRD_MEDIAN_FILTER_HV,
	BLACKBIRD_MEDIAN_FILTER_DIAGONAL
};
#define BLACKBIRD_API_SET_MANUAL_DNR     0x9D
#define BLACKBIRD_API_SET_DNR_MEDIAN     0x9F
#define BLACKBIRD_API_SET_SPATIAL_FILTER 0xA1
enum blackbird_spatial_filter_luma {
	BLACKBIRD_SPATIAL_FILTER_LUMA_DISABLED,
	BLACKBIRD_SPATIAL_FILTER_LUMA_1D_HORIZ,
	BLACKBIRD_SPATIAL_FILTER_LUMA_1D_VERT,
	BLACKBIRD_SPATIAL_FILTER_LUMA_2D_HV, /* separable, default */
	BLACKBIRD_SPATIAL_FILTER_LUMA_2D_SYMM /* symmetric non-separable */
};
enum blackbird_spatial_filter_chroma {
	BLACKBIRD_SPATIAL_FILTER_CHROMA_DISABLED,
	BLACKBIRD_SPATIAL_FILTER_CHROMA_1D_HORIZ /* default */
};
#define BLACKBIRD_API_SET_3_2_PULLDOWN   0xB1
enum blackbird_pulldown {
	BLACKBIRD_3_2_PULLDOWN_DISABLED,
	BLACKBIRD_3_2_PULLDOWN_ENABLED
};
#define BLACKBIRD_API_SET_VBI_LINE_NO    0xB7
enum blackbird_vbi_line_bits {
	BLACKBIRD_VBI_LINE_BITS_TOP_FIELD,
	BLACKBIRD_VBI_LINE_BITS_BOT_FIELD = (1 << 31),
	BLACKBIRD_VBI_LINE_BITS_ALL_LINES = 0xFFFFFFFF
};
enum blackbird_vbi_line {
	BLACKBIRD_VBI_LINE_DISABLED,
	BLACKBIRD_VBI_LINE_ENABLED
};
enum blackbird_vbi_slicing {
	BLACKBIRD_VBI_SLICING_NONE,
	BLACKBIRD_VBI_SLICING_CLOSED_CAPTION
};
#define BLACKBIRD_API_SET_STREAM_TYPE    0xB9
enum blackbird_stream_type {
	BLACKBIRD_STREAM_PROGRAM,
	BLACKBIRD_STREAM_TRANSPORT,
	BLACKBIRD_STREAM_MPEG1,
	BLACKBIRD_STREAM_PES_AV,
	BLACKBIRD_STREAM_UNKNOWN4,
	BLACKBIRD_STREAM_PES_VIDEO,
	BLACKBIRD_STREAM_UNKNOWN6,
	BLACKBIRD_STREAM_PES_AUDIO,
	BLACKBIRD_STREAM_UNKNOWN8,
	BLACKBIRD_STREAM_UNKNOWN9, /* audio/pcm ? */
	BLACKBIRD_STREAM_DVD,
	BLACKBIRD_STREAM_VCD,
	BLACKBIRD_STREAM_UNKNOWN12 /* svcd/xvcd ? */
};
#define BLACKBIRD_API_SET_OUTPUT_PORT    0xBB
enum blackbird_stream_port {
	BLACKBIRD_OUTPUT_PORT_MEMORY,
	BLACKBIRD_OUTPUT_PORT_STREAMING,
	BLACKBIRD_OUTPUT_PORT_SERIAL
};
#define BLACKBIRD_API_SET_AUDIO_PARAMS   0xBD
enum blackbird_audio_bits_sample_rate {
	BLACKBIRD_AUDIO_BITS_44100HZ,
	BLACKBIRD_AUDIO_BITS_48000HZ,
	BLACKBIRD_AUDIO_BITS_32000HZ,
	BLACKBIRD_AUDIO_BITS_RESERVED_HZ,
};
enum blackbird_audio_bits_encoding {
	BLACKBIRD_AUDIO_BITS_LAYER_1 = 0x1 << 2,
	BLACKBIRD_AUDIO_BITS_LAYER_2 = 0x2 << 2,
};
enum blackbird_audio_bits_bitrate_layer_1 {
	BLACKBIRD_AUDIO_BITS_LAYER_1_FREE_FORMAT,
	BLACKBIRD_AUDIO_BITS_LAYER_1_32  = 0x01 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_1_64  = 0x02 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_1_96  = 0x03 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_1_128 = 0x04 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_1_160 = 0x05 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_1_192 = 0x06 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_1_224 = 0x07 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_1_256 = 0x08 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_1_288 = 0x09 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_1_320 = 0x0A << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_1_352 = 0x0B << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_1_384 = 0x0C << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_1_416 = 0x0D << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_1_448 = 0x0E << 4,
};
enum blackbird_audio_bits_bitrate_layer_2 {
	BLACKBIRD_AUDIO_BITS_LAYER_2_FREE_FORMAT,
	BLACKBIRD_AUDIO_BITS_LAYER_2_32  = 0x01 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_2_48  = 0x02 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_2_56  = 0x03 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_2_64  = 0x04 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_2_80  = 0x05 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_2_96  = 0x06 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_2_112 = 0x07 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_2_128 = 0x08 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_2_160 = 0x09 << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_2_192 = 0x0A << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_2_224 = 0x0B << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_2_256 = 0x0C << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_2_320 = 0x0D << 4,
	BLACKBIRD_AUDIO_BITS_LAYER_2_384 = 0x0E << 4,
};
enum blackbird_audio_bits_mode {
	BLACKBIRD_AUDIO_BITS_STEREO,
	BLACKBIRD_AUDIO_BITS_JOINT_STEREO = 0x1 << 8,
	BLACKBIRD_AUDIO_BITS_DUAL         = 0x2 << 8,
	BLACKBIRD_AUDIO_BITS_MONO         = 0x3 << 8,
};
enum blackbird_audio_bits_mode_extension {
	BLACKBIRD_AUDIO_BITS_BOUND_4,
	BLACKBIRD_AUDIO_BITS_BOUND_8  = 0x1 << 10,
	BLACKBIRD_AUDIO_BITS_BOUND_12 = 0x2 << 10,
	BLACKBIRD_AUDIO_BITS_BOUND_16 = 0x3 << 10,
};
enum blackbird_audio_bits_emphasis {
	BLACKBIRD_AUDIO_BITS_EMPHASIS_NONE,
	BLACKBIRD_AUDIO_BITS_EMPHASIS_50_15     = 0x1 << 12,
	BLACKBIRD_AUDIO_BITS_EMPHASIS_RESERVED  = 0x2 << 12,
	BLACKBIRD_AUDIO_BITS_EMPHASIS_CCITT_J17 = 0x3 << 12,
};
enum blackbird_audio_bits_crc {
	BLACKBIRD_AUDIO_BITS_CRC_OFF,
	BLACKBIRD_AUDIO_BITS_CRC_ON = 0x1 << 14,
};
enum blackbird_audio_bits_copyright {
	BLACKBIRD_AUDIO_BITS_COPYRIGHT_OFF,
	BLACKBIRD_AUDIO_BITS_COPYRIGHT_ON = 0x1 << 15,
};
enum blackbird_audio_bits_original {
	BLACKBIRD_AUDIO_BITS_COPY,
	BLACKBIRD_AUDIO_BITS_ORIGINAL = 0x1 << 16,
};
#define BLACKBIRD_API_HALT               0xC3
#define BLACKBIRD_API_GET_VERSION        0xC4
#define BLACKBIRD_API_SET_GOP_CLOSURE    0xC5
enum blackbird_gop_closure {
	BLACKBIRD_GOP_CLOSURE_OFF,
	BLACKBIRD_GOP_CLOSURE_ON,
};
#define BLACKBIRD_API_DATA_XFER_STATUS   0xC6
enum blackbird_data_xfer_status {
	BLACKBIRD_MORE_BUFFERS_FOLLOW,
	BLACKBIRD_LAST_BUFFER,
};
#define BLACKBIRD_API_PROGRAM_INDEX_INFO 0xC7
enum blackbird_picture_mask {
	BLACKBIRD_PICTURE_MASK_NONE,
	BLACKBIRD_PICTURE_MASK_I_FRAMES,
	BLACKBIRD_PICTURE_MASK_I_P_FRAMES = 0x3,
	BLACKBIRD_PICTURE_MASK_ALL_FRAMES = 0x7,
};
#define BLACKBIRD_API_SET_VBI_PARAMS     0xC8
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
#define BLACKBIRD_API_SET_DMA_BLOCK_SIZE 0xC9
enum blackbird_dma_unit {
	BLACKBIRD_DMA_BYTES,
	BLACKBIRD_DMA_FRAMES,
};
#define BLACKBIRD_API_DMA_TRANSFER_INFO  0xCA
#define BLACKBIRD_API_DMA_TRANSFER_STAT  0xCB
enum blackbird_dma_transfer_status_bits {
	BLACKBIRD_DMA_TRANSFER_BITS_DONE = 0x01,
	BLACKBIRD_DMA_TRANSFER_BITS_ERROR = 0x04,
	BLACKBIRD_DMA_TRANSFER_BITS_LL_ERROR = 0x10,
};
#define BLACKBIRD_API_SET_DMA2HOST_ADDR  0xCC
#define BLACKBIRD_API_INIT_VIDEO_INPUT   0xCD
#define BLACKBIRD_API_SET_FRAMESKIP      0xD0
#define BLACKBIRD_API_PAUSE              0xD2
enum blackbird_pause {
	BLACKBIRD_PAUSE_ENCODING,
	BLACKBIRD_RESUME_ENCODING,
};
#define BLACKBIRD_API_REFRESH_INPUT      0xD3
#define BLACKBIRD_API_SET_COPYRIGHT      0xD4
enum blackbird_copyright {
	BLACKBIRD_COPYRIGHT_OFF,
	BLACKBIRD_COPYRIGHT_ON,
};
#define BLACKBIRD_API_SET_NOTIFICATION   0xD5
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
#define BLACKBIRD_API_SET_CAPTURE_LINES  0xD6
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
#define BLACKBIRD_API_SET_CUSTOM_DATA    0xD7
enum blackbird_custom_data_type {
	BLACKBIRD_CUSTOM_EXTENSION_USR_DATA,
	BLACKBIRD_CUSTOM_PRIVATE_PACKET,
};
#define BLACKBIRD_API_MUTE_VIDEO         0xD9
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
#define BLACKBIRD_API_MUTE_AUDIO         0xDA

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

/* We don't need to call the API often, so using just one mailbox will probably suffice */
static int blackbird_api_cmd(struct cx8802_dev *dev, u32 command,
			     u32 inputcnt, u32 outputcnt, ...)
{
	unsigned long timeout;
	u32 value, flag, retval;
	int i;
	va_list args;
	va_start(args, outputcnt);

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
	for (i = 0; i < inputcnt ; i++) {
		value = va_arg(args, int);
		memory_write(dev->core, dev->mailbox + 4 + i, value);
		dprintk(1, "API Input %d = %d\n", i, value);
	}
	for (; i < 16 ; i++)
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
	for (i = 0; i < outputcnt ; i++) {
		int *vptr = va_arg(args, int *);
		memory_read(dev->core, dev->mailbox + 4 + i, vptr);
		dprintk(1, "API Output %d = %d\n", i, *vptr);
	}
	va_end(args);

	memory_read(dev->core, dev->mailbox + 2, &retval);
	dprintk(1, "API result = %d\n",retval);

	flag = 0;
	memory_write(dev->core, dev->mailbox, flag);
	return retval;
}


static int blackbird_find_mailbox(struct cx8802_dev *dev)
{
	u32 signature[4]={0x12345678, 0x34567812, 0x56781234, 0x78123456};
	int signaturecnt=0;
	u32 value;
	int i;

	for (i = 0; i < BLACKBIRD_FIRM_IMAGE_SIZE; i++) {
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

	retval = request_firmware(&firmware, BLACKBIRD_FIRM_ENC_FILENAME,
				  &dev->pci->dev);


	if (retval != 0) {
		dprintk(0, "ERROR: Hotplug firmware request failed (%s).\n",
			BLACKBIRD_FIRM_ENC_FILENAME);
		dprintk(0, "Please fix your hotplug setup, the board will "
			"not work without firmware loaded!\n");
		return -1;
	}

	if (firmware->size != BLACKBIRD_FIRM_IMAGE_SIZE) {
		dprintk(0, "ERROR: Firmware size mismatch (have %zd, expected %d)\n",
			firmware->size, BLACKBIRD_FIRM_IMAGE_SIZE);
		return -1;
	}

	if (0 != memcmp(firmware->data, magic, 8)) {
		dprintk(0, "ERROR: Firmware magic mismatch, wrong file?\n");
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

static struct blackbird_dnr default_dnr_params = {
	.mode     = BLACKBIRD_DNR_BITS_MANUAL,
	.type     = BLACKBIRD_MEDIAN_FILTER_DISABLED,
	.spatial  = 0,
	.temporal = 0
};
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
	.au_sample_rate    = 44100,
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
	.vi_frames_per_gop = 15,
	.vi_bframes_count  = 2,
	.vi_pesid          = 0,
	.closed_gops       = 0,
	.pulldown          = 0
};

static enum blackbird_stream_type mpeg_stream_types[] = {
	[V4L2_MPEG_SS_1]   = BLACKBIRD_STREAM_MPEG1,
	[V4L2_MPEG_PS_2]   = BLACKBIRD_STREAM_PROGRAM,
	[V4L2_MPEG_TS_2]   = BLACKBIRD_STREAM_TRANSPORT,
	[V4L2_MPEG_PS_DVD] = BLACKBIRD_STREAM_DVD,
};
static enum blackbird_aspect_ratio mpeg_stream_ratios[] = {
	[V4L2_MPEG_ASPECT_SQUARE] = BLACKBIRD_ASPECT_RATIO_1_1_SQUARE,
	[V4L2_MPEG_ASPECT_4_3]    = BLACKBIRD_ASPECT_RATIO_4_3,
	[V4L2_MPEG_ASPECT_16_9]   = BLACKBIRD_ASPECT_RATIO_16_9,
	[V4L2_MPEG_ASPECT_1_221]  = BLACKBIRD_ASPECT_RATIO_221_100,
};
static enum blackbird_video_bitrate_type mpeg_video_bitrates[] = {
	[V4L2_BITRATE_NONE] = BLACKBIRD_VIDEO_CBR,
	[V4L2_BITRATE_CBR]  = BLACKBIRD_VIDEO_CBR,
	[V4L2_BITRATE_VBR]  = BLACKBIRD_VIDEO_VBR,
};
/* find the best layer I/II bitrate to fit a given numeric value */
struct bitrate_bits {
	u32 bits; /* layer bits for the best fit */
	u32 rate; /* actual numeric value for the layer best fit */
};
struct bitrate_approximation {
	u32                 target;   /* numeric value of the rate we want */
	struct bitrate_bits layer[2];
};
static struct bitrate_approximation mpeg_audio_bitrates[] = {
	/* target  layer[0].bits           layer[0].rate       layer[1].bits           layer[1].rate */
	{   0, { {                                0,   0, }, {                                0,   0, }, }, },
	{  32, { { BLACKBIRD_AUDIO_BITS_LAYER_1_32 ,  32, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_32 ,  32, }, }, },
	{  48, { { BLACKBIRD_AUDIO_BITS_LAYER_1_64 ,  64, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_48 ,  48, }, }, },
	{  56, { { BLACKBIRD_AUDIO_BITS_LAYER_1_64 ,  64, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_56 ,  56, }, }, },
	{  64, { { BLACKBIRD_AUDIO_BITS_LAYER_1_64 ,  64, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_64 ,  64, }, }, },
	{  80, { { BLACKBIRD_AUDIO_BITS_LAYER_1_96 ,  96, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_80 ,  80, }, }, },
	{  96, { { BLACKBIRD_AUDIO_BITS_LAYER_1_96 ,  96, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_96 ,  96, }, }, },
	{ 112, { { BLACKBIRD_AUDIO_BITS_LAYER_1_128, 128, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_112, 112, }, }, },
	{ 128, { { BLACKBIRD_AUDIO_BITS_LAYER_1_128, 128, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_128, 128, }, }, },
	{ 160, { { BLACKBIRD_AUDIO_BITS_LAYER_1_160, 160, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_160, 160, }, }, },
	{ 192, { { BLACKBIRD_AUDIO_BITS_LAYER_1_192, 192, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_192, 192, }, }, },
	{ 224, { { BLACKBIRD_AUDIO_BITS_LAYER_1_224, 224, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_224, 224, }, }, },
	{ 256, { { BLACKBIRD_AUDIO_BITS_LAYER_1_256, 256, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_256, 256, }, }, },
	{ 288, { { BLACKBIRD_AUDIO_BITS_LAYER_1_288, 288, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_320, 320, }, }, },
	{ 320, { { BLACKBIRD_AUDIO_BITS_LAYER_1_320, 320, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_320, 320, }, }, },
	{ 352, { { BLACKBIRD_AUDIO_BITS_LAYER_1_352, 352, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_384, 384, }, }, },
	{ 384, { { BLACKBIRD_AUDIO_BITS_LAYER_1_384, 384, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_384, 384, }, }, },
	{ 416, { { BLACKBIRD_AUDIO_BITS_LAYER_1_416, 416, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_384, 384, }, }, },
	{ 448, { { BLACKBIRD_AUDIO_BITS_LAYER_1_448, 448, }, { BLACKBIRD_AUDIO_BITS_LAYER_2_384, 384, }, }, },
};
static const int BITRATES_SIZE = ARRAY_SIZE(mpeg_audio_bitrates);

static void blackbird_set_default_params(struct cx8802_dev *dev)
{
	struct v4l2_mpeg_compression *params = &dev->params;
	u32 au_params;

	/* assign stream type */
	if( params->st_type >= ARRAY_SIZE(mpeg_stream_types) )
		params->st_type = V4L2_MPEG_PS_2;
	if( params->st_type == V4L2_MPEG_SS_1 )
		params->vi_type = V4L2_MPEG_VI_1;
	else
		params->vi_type = V4L2_MPEG_VI_2;
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_STREAM_TYPE, 1, 0, mpeg_stream_types[params->st_type]);

	/* assign framerate */
	if( params->vi_frame_rate <= 25 )
	{
		params->vi_frame_rate = 25;
		blackbird_api_cmd(dev, BLACKBIRD_API_SET_FRAMERATE, 1, 0, BLACKBIRD_FRAMERATE_PAL_25);
	}
	else
	{
		params->vi_frame_rate = 30;
		blackbird_api_cmd(dev, BLACKBIRD_API_SET_FRAMERATE, 1, 0, BLACKBIRD_FRAMERATE_NTSC_30);
	}

	/* assign aspect ratio */
	if( params->vi_aspect_ratio >= ARRAY_SIZE(mpeg_stream_ratios) )
		params->vi_aspect_ratio = V4L2_MPEG_ASPECT_4_3;
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_ASPECT_RATIO, 1, 0, mpeg_stream_ratios[params->vi_aspect_ratio]);

	/* assign gop properties */
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_GOP_STRUCTURE, 2, 0, params->vi_frames_per_gop, params->vi_bframes_count+1);

	/* assign gop closure */
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_GOP_CLOSURE, 1, 0, params->closed_gops);

	/* assign 3 2 pulldown */
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_3_2_PULLDOWN, 1, 0, params->pulldown);

	/* make sure the params are within bounds */
	if( params->st_bitrate.mode >= ARRAY_SIZE(mpeg_video_bitrates) )
		params->vi_bitrate.mode = V4L2_BITRATE_NONE;
	if( params->vi_bitrate.mode >= ARRAY_SIZE(mpeg_video_bitrates) )
		params->vi_bitrate.mode = V4L2_BITRATE_NONE;
	if( params->au_bitrate.mode >= ARRAY_SIZE(mpeg_video_bitrates) )
		params->au_bitrate.mode = V4L2_BITRATE_NONE;

	/* assign audio properties */
	/* note: it's not necessary to set the samplerate, the mpeg encoder seems to autodetect/adjust */
	au_params = BLACKBIRD_AUDIO_BITS_STEREO |
			/* BLACKBIRD_AUDIO_BITS_BOUND_4 | */
			BLACKBIRD_AUDIO_BITS_EMPHASIS_NONE |
			BLACKBIRD_AUDIO_BITS_CRC_OFF |
			BLACKBIRD_AUDIO_BITS_COPYRIGHT_OFF |
			BLACKBIRD_AUDIO_BITS_COPY |
			0;
	if( params->au_sample_rate <= 32000 )
	{
		params->au_sample_rate = 32000;
		au_params |= BLACKBIRD_AUDIO_BITS_32000HZ;
	}
	else if( params->au_sample_rate <= 44100 )
	{
		params->au_sample_rate = 44100;
		au_params |= BLACKBIRD_AUDIO_BITS_44100HZ;
	}
	else
	{
		params->au_sample_rate = 48000;
		au_params |= BLACKBIRD_AUDIO_BITS_48000HZ;
	}
	if( params->au_type == V4L2_MPEG_AU_2_I )
	{
		au_params |= BLACKBIRD_AUDIO_BITS_LAYER_1;
	}
	else
	{
		/* TODO: try to handle the other formats more gracefully */
		params->au_type = V4L2_MPEG_AU_2_II;
		au_params |= BLACKBIRD_AUDIO_BITS_LAYER_2;
	}
	if( params->au_bitrate.mode )
	{
		int layer;

		if( params->au_bitrate.mode == V4L2_BITRATE_CBR )
			params->au_bitrate.max = params->vi_bitrate.target;
		else
			params->au_bitrate.target = params->vi_bitrate.max;

		layer = params->au_type;
		if( params->au_bitrate.target == 0 )
		{
			/* TODO: use the minimum possible bitrate instead of 0 ? */
			au_params |= 0;
		}
		else if( params->au_bitrate.target >=
			 mpeg_audio_bitrates[BITRATES_SIZE-1].layer[layer].rate )
		{
			/* clamp the bitrate to the max supported by the standard */
			params->au_bitrate.target = mpeg_audio_bitrates[BITRATES_SIZE-1].layer[layer].rate;
			params->au_bitrate.max = params->au_bitrate.target;
			au_params |= mpeg_audio_bitrates[BITRATES_SIZE-1].layer[layer].bits;
		}
		else
		{
			/* round up to the nearest supported bitrate */
			int i;
			for(i = 1; i < BITRATES_SIZE; i++)
			{
				if( params->au_bitrate.target > mpeg_audio_bitrates[i-1].layer[layer].rate &&
				    params->au_bitrate.target <= mpeg_audio_bitrates[i].layer[layer].rate )
				{
					params->au_bitrate.target = mpeg_audio_bitrates[i].layer[layer].rate;
					params->au_bitrate.max = params->au_bitrate.target;
					au_params |= mpeg_audio_bitrates[i].layer[layer].bits;
					break;
				}
			}
		}
	}
	else
	{
		/* TODO: ??? */
		params->au_bitrate.target = params->au_bitrate.max = 0;
		au_params |= 0;
	}
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_AUDIO_PARAMS, 1, 0, au_params );

	/* assign bitrates */
	if( params->vi_bitrate.mode )
	{
		/* bitrate is set, let's figure out the cbr/vbr mess */
		if( params->vi_bitrate.max < params->vi_bitrate.target )
		{
			if( params->vi_bitrate.mode == V4L2_BITRATE_CBR )
				params->vi_bitrate.max = params->vi_bitrate.target;
			else
				params->vi_bitrate.target = params->vi_bitrate.max;
		}
	}
	else
	{
		if( params->st_bitrate.max < params->st_bitrate.target )
		{
			if( params->st_bitrate.mode == V4L2_BITRATE_VBR )
				params->st_bitrate.target = params->st_bitrate.max;
			else
				params->st_bitrate.max = params->st_bitrate.target;
		}
		/* calculate vi_bitrate = st_bitrate - au_bitrate */
		params->vi_bitrate.max = params->st_bitrate.max - params->au_bitrate.max;
		params->vi_bitrate.target = params->st_bitrate.target - params->au_bitrate.target;
	}
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_VIDEO_BITRATE, 4, 0,
				mpeg_video_bitrates[params->vi_bitrate.mode],
				params->vi_bitrate.target * 1000, /* kbps -> bps */
				params->vi_bitrate.max * 1000 / BLACKBIRD_PEAK_RATE_DIVISOR, /* peak/400 */
				BLACKBIRD_MUX_RATE_DEFAULT /*, 0x70*/); /* encoding buffer, ckennedy */

	/* TODO: implement the stream ID stuff:
		ts_pid_pmt, ts_pid_audio, ts_pid_video, ts_pid_pcr,
		ps_size, au_pesid, vi_pesid
	*/
}
#define CHECK_PARAM( name ) ( dev->params.name != params->name )
#define IF_PARAM( name ) if( CHECK_PARAM( name ) )
#define UPDATE_PARAM( name ) dev->params.name = params->name
void blackbird_set_params(struct cx8802_dev *dev, struct v4l2_mpeg_compression *params)
{
	u32 au_params;

	/* assign stream type */
	if( params->st_type >= ARRAY_SIZE(mpeg_stream_types) )
		params->st_type = V4L2_MPEG_PS_2;
	if( params->st_type == V4L2_MPEG_SS_1 )
		params->vi_type = V4L2_MPEG_VI_1;
	else
		params->vi_type = V4L2_MPEG_VI_2;
	if( CHECK_PARAM( st_type ) || CHECK_PARAM( vi_type ) )
	{
		UPDATE_PARAM( st_type );
		UPDATE_PARAM( vi_type );
		blackbird_api_cmd(dev, BLACKBIRD_API_SET_STREAM_TYPE, 1, 0, mpeg_stream_types[params->st_type]);
	}

	/* assign framerate */
	if( params->vi_frame_rate <= 25 )
		params->vi_frame_rate = 25;
	else
		params->vi_frame_rate = 30;
	IF_PARAM( vi_frame_rate )
	{
		UPDATE_PARAM( vi_frame_rate );
		if( params->vi_frame_rate == 25 )
			blackbird_api_cmd(dev, BLACKBIRD_API_SET_FRAMERATE, 1, 0, BLACKBIRD_FRAMERATE_PAL_25);
		else
			blackbird_api_cmd(dev, BLACKBIRD_API_SET_FRAMERATE, 1, 0, BLACKBIRD_FRAMERATE_NTSC_30);
	}

	/* assign aspect ratio */
	if( params->vi_aspect_ratio >= ARRAY_SIZE(mpeg_stream_ratios) )
		params->vi_aspect_ratio = V4L2_MPEG_ASPECT_4_3;
	IF_PARAM( vi_aspect_ratio )
	{
		UPDATE_PARAM( vi_aspect_ratio );
		blackbird_api_cmd(dev, BLACKBIRD_API_SET_ASPECT_RATIO, 1, 0, mpeg_stream_ratios[params->vi_aspect_ratio]);
	}

	/* assign gop properties */
	if( CHECK_PARAM( vi_frames_per_gop ) || CHECK_PARAM( vi_bframes_count ) )
	{
		UPDATE_PARAM( vi_frames_per_gop );
		UPDATE_PARAM( vi_bframes_count );
		blackbird_api_cmd(dev, BLACKBIRD_API_SET_GOP_STRUCTURE, 2, 0, params->vi_frames_per_gop, params->vi_bframes_count+1);
	}

	/* assign gop closure */
	IF_PARAM( closed_gops )
	{
		UPDATE_PARAM( closed_gops );
		blackbird_api_cmd(dev, BLACKBIRD_API_SET_GOP_CLOSURE, 1, 0, params->closed_gops);
	}

	/* assign 3 2 pulldown */
	IF_PARAM( pulldown )
	{
		UPDATE_PARAM( pulldown );
		blackbird_api_cmd(dev, BLACKBIRD_API_SET_3_2_PULLDOWN, 1, 0, params->pulldown);
	}

	/* make sure the params are within bounds */
	if( params->st_bitrate.mode >= ARRAY_SIZE(mpeg_video_bitrates) )
		params->vi_bitrate.mode = V4L2_BITRATE_NONE;
	if( params->vi_bitrate.mode >= ARRAY_SIZE(mpeg_video_bitrates) )
		params->vi_bitrate.mode = V4L2_BITRATE_NONE;
	if( params->au_bitrate.mode >= ARRAY_SIZE(mpeg_video_bitrates) )
		params->au_bitrate.mode = V4L2_BITRATE_NONE;

	/* assign audio properties */
	/* note: it's not necessary to set the samplerate, the mpeg encoder seems to autodetect/adjust */
	au_params = BLACKBIRD_AUDIO_BITS_STEREO |
			/* BLACKBIRD_AUDIO_BITS_BOUND_4 | */
	BLACKBIRD_AUDIO_BITS_EMPHASIS_NONE |
		BLACKBIRD_AUDIO_BITS_CRC_OFF |
		BLACKBIRD_AUDIO_BITS_COPYRIGHT_OFF |
		BLACKBIRD_AUDIO_BITS_COPY |
		0;
	if( params->au_sample_rate < 32000 )
	{
		params->au_sample_rate = 32000;
		au_params |= BLACKBIRD_AUDIO_BITS_32000HZ;
	}
	else if( params->au_sample_rate < 44100 )
	{
		params->au_sample_rate = 44100;
		au_params |= BLACKBIRD_AUDIO_BITS_44100HZ;
	}
	else
	{
		params->au_sample_rate = 48000;
		au_params |= BLACKBIRD_AUDIO_BITS_48000HZ;
	}
	if( params->au_type == V4L2_MPEG_AU_2_I )
	{
		au_params |= BLACKBIRD_AUDIO_BITS_LAYER_1;
	}
	else
	{
		/* TODO: try to handle the other formats more gracefully */
		params->au_type = V4L2_MPEG_AU_2_II;
		au_params |= BLACKBIRD_AUDIO_BITS_LAYER_2;
	}
	if( params->au_bitrate.mode )
	{
		int layer;

		if( params->au_bitrate.mode == V4L2_BITRATE_CBR )
			params->au_bitrate.max = params->vi_bitrate.target;
		else
			params->au_bitrate.target = params->vi_bitrate.max;

		layer = params->au_type;
		if( params->au_bitrate.target == 0 )
		{
			/* TODO: use the minimum possible bitrate instead of 0 ? */
			au_params |= 0;
		}
		else if( params->au_bitrate.target >=
			 mpeg_audio_bitrates[BITRATES_SIZE-1].layer[layer].rate )
		{
			/* clamp the bitrate to the max supported by the standard */
			params->au_bitrate.target = mpeg_audio_bitrates[BITRATES_SIZE-1].layer[layer].rate;
			params->au_bitrate.max = params->au_bitrate.target;
			au_params |= mpeg_audio_bitrates[BITRATES_SIZE-1].layer[layer].bits;
		}
		else
		{
			/* round up to the nearest supported bitrate */
			int i;
			for(i = 1; i < BITRATES_SIZE; i++)
			{
				if( params->au_bitrate.target > mpeg_audio_bitrates[i-1].layer[layer].rate &&
				    params->au_bitrate.target <= mpeg_audio_bitrates[i].layer[layer].rate )
				{
					params->au_bitrate.target = mpeg_audio_bitrates[i].layer[layer].rate;
					params->au_bitrate.max = params->au_bitrate.target;
					au_params |= mpeg_audio_bitrates[i].layer[layer].bits;
					break;
				}
			}
		}
	}
	else
	{
		/* TODO: ??? */
		params->au_bitrate.target = params->au_bitrate.max = 0;
		au_params |= 0;
	}
	if( CHECK_PARAM( au_type ) || CHECK_PARAM( au_sample_rate )
		|| CHECK_PARAM( au_bitrate.mode ) || CHECK_PARAM( au_bitrate.max )
		|| CHECK_PARAM( au_bitrate.target )
	)
	{
		UPDATE_PARAM( au_type );
		UPDATE_PARAM( au_sample_rate );
		UPDATE_PARAM( au_bitrate );
		blackbird_api_cmd(dev, BLACKBIRD_API_SET_AUDIO_PARAMS, 1, 0, au_params );
	}

	/* assign bitrates */
	if( params->vi_bitrate.mode )
	{
		/* bitrate is set, let's figure out the cbr/vbr mess */
		if( params->vi_bitrate.max < params->vi_bitrate.target )
		{
			if( params->vi_bitrate.mode == V4L2_BITRATE_CBR )
				params->vi_bitrate.max = params->vi_bitrate.target;
			else
				params->vi_bitrate.target = params->vi_bitrate.max;
		}
	}
	else
	{
		if( params->st_bitrate.max < params->st_bitrate.target )
		{
			if( params->st_bitrate.mode == V4L2_BITRATE_VBR )
				params->st_bitrate.target = params->st_bitrate.max;
			else
				params->st_bitrate.max = params->st_bitrate.target;
		}
		/* calculate vi_bitrate = st_bitrate - au_bitrate */
		params->vi_bitrate.max = params->st_bitrate.max - params->au_bitrate.max;
		params->vi_bitrate.target = params->st_bitrate.target - params->au_bitrate.target;
	}
	UPDATE_PARAM( st_bitrate );
	if( CHECK_PARAM( vi_bitrate.mode ) || CHECK_PARAM( vi_bitrate.max )
		|| CHECK_PARAM( vi_bitrate.target )
	)
	{
		UPDATE_PARAM( vi_bitrate );
		blackbird_api_cmd(dev, BLACKBIRD_API_SET_VIDEO_BITRATE, 4, 0,
				mpeg_video_bitrates[params->vi_bitrate.mode],
				params->vi_bitrate.target * 1000, /* kbps -> bps */
				params->vi_bitrate.max * 1000 / BLACKBIRD_PEAK_RATE_DIVISOR, /* peak/400 */
				BLACKBIRD_MUX_RATE_DEFAULT /*, 0x70*/); /* encoding buffer, ckennedy */
	}

	/* TODO: implement the stream ID stuff:
		ts_pid_pmt, ts_pid_audio, ts_pid_video, ts_pid_pcr,
		ps_size, au_pesid, vi_pesid
	*/
	UPDATE_PARAM( ts_pid_pmt );
	UPDATE_PARAM( ts_pid_audio );
	UPDATE_PARAM( ts_pid_video );
	UPDATE_PARAM( ts_pid_pcr );
	UPDATE_PARAM( ps_size );
	UPDATE_PARAM( au_pesid );
	UPDATE_PARAM( vi_pesid );
}

static void blackbird_set_default_dnr_params(struct cx8802_dev *dev)
{
	/* assign dnr filter mode */
	if( dev->dnr_params.mode > BLACKBIRD_DNR_BITS_AUTO )
		dev->dnr_params.mode = BLACKBIRD_DNR_BITS_MANUAL;
	if( dev->dnr_params.type > BLACKBIRD_MEDIAN_FILTER_DIAGONAL )
		dev->dnr_params.type = BLACKBIRD_MEDIAN_FILTER_DISABLED;
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_DNR_MODE, 2, 0,
				dev->dnr_params.mode,
				dev->dnr_params.type
			);

	/* assign dnr filter props*/
	if( dev->dnr_params.spatial > 15 )
		dev->dnr_params.spatial = 15;
	if( dev->dnr_params.temporal > 31 )
		dev->dnr_params.temporal = 31;
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_MANUAL_DNR, 2, 0,
				dev->dnr_params.spatial,
				dev->dnr_params.temporal
			);
}
#define CHECK_DNR_PARAM( name ) ( dev->dnr_params.name != dnr_params->name )
#define UPDATE_DNR_PARAM( name ) dev->dnr_params.name = dnr_params->name
void blackbird_set_dnr_params(struct cx8802_dev *dev, struct blackbird_dnr* dnr_params)
{
	/* assign dnr filter mode */
	/* clamp values */
	if( dnr_params->mode > BLACKBIRD_DNR_BITS_AUTO )
		dnr_params->mode = BLACKBIRD_DNR_BITS_MANUAL;
	if( dnr_params->type > BLACKBIRD_MEDIAN_FILTER_DIAGONAL )
		dnr_params->type = BLACKBIRD_MEDIAN_FILTER_DISABLED;
	/* check if the params actually changed */
	if( CHECK_DNR_PARAM( mode ) || CHECK_DNR_PARAM( type ) )
	{
		UPDATE_DNR_PARAM( mode );
		UPDATE_DNR_PARAM( type );
		blackbird_api_cmd(dev, BLACKBIRD_API_SET_DNR_MODE, 2, 0, dnr_params->mode, dnr_params->type);
	}

	/* assign dnr filter props*/
	if( dnr_params->spatial > 15 )
		dnr_params->spatial = 15;
	if( dnr_params->temporal > 31 )
		dnr_params->temporal = 31;
	if( CHECK_DNR_PARAM( spatial ) || CHECK_DNR_PARAM( temporal ) )
	{
		UPDATE_DNR_PARAM( spatial );
		UPDATE_DNR_PARAM( temporal );
		blackbird_api_cmd(dev, BLACKBIRD_API_SET_MANUAL_DNR, 2, 0, dnr_params->spatial, dnr_params->temporal);
	}
}

static void blackbird_codec_settings(struct cx8802_dev *dev)
{

	/* assign output port */
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_OUTPUT_PORT, 1, 0, BLACKBIRD_OUTPUT_PORT_STREAMING); /* Host */

	/* assign frame size */
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_RESOLUTION, 2, 0,
				dev->height, dev->width);

	/* assign coring levels (luma_h, luma_l, chroma_h, chroma_l) */
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_DNR_MEDIAN, 4, 0, 0, 255, 0, 255);

	/* assign spatial filter type: luma_t: horiz_only, chroma_t: horiz_only */
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_SPATIAL_FILTER, 2, 0,
				BLACKBIRD_SPATIAL_FILTER_LUMA_1D_HORIZ,
				BLACKBIRD_SPATIAL_FILTER_CHROMA_1D_HORIZ
			);

	/* assign frame drop rate */
	/* blackbird_api_cmd(dev, IVTV_API_ASSIGN_FRAME_DROP_RATE, 1, 0, 0); */

	blackbird_set_default_params(dev);
	blackbird_set_default_dnr_params(dev);
}

static int blackbird_initialize_codec(struct cx8802_dev *dev)
{
	struct cx88_core *core = dev->core;
	int version;
	int retval;

	dprintk(1,"Initialize codec\n");
	retval = blackbird_api_cmd(dev, BLACKBIRD_API_PING, 0, 0); /* ping */
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

		retval = blackbird_api_cmd(dev, BLACKBIRD_API_PING, 0, 0); /* ping */
		if (retval < 0) {
			dprintk(0, "ERROR: Firmware ping failed!\n");
			return -1;
		}

		retval = blackbird_api_cmd(dev, BLACKBIRD_API_GET_VERSION, 0, 1, &version);
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
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_CAPTURE_LINES, 2, 0,
			BLACKBIRD_FIELD1_SAA7115,
			BLACKBIRD_FIELD2_SAA7115
		);

	/* blackbird_api_cmd(dev, IVTV_API_ASSIGN_PLACEHOLDER, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0); */
	blackbird_api_cmd(dev, BLACKBIRD_API_SET_CUSTOM_DATA, 12, 0,
			BLACKBIRD_CUSTOM_EXTENSION_USR_DATA,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

	/* initialize the video input */
	blackbird_api_cmd(dev, BLACKBIRD_API_INIT_VIDEO_INPUT, 0, 0);

	msleep(1);

	blackbird_api_cmd(dev, BLACKBIRD_API_MUTE_VIDEO, 1, 0, BLACKBIRD_UNMUTE);
	msleep(1);
	blackbird_api_cmd(dev, BLACKBIRD_API_MUTE_AUDIO, 1, 0, BLACKBIRD_UNMUTE);
	msleep(1);

	/* start capturing to the host interface */
	/* blackbird_api_cmd(dev, BLACKBIRD_API_BEGIN_CAPTURE, 2, 0, 0, 0x13); */
	blackbird_api_cmd(dev, BLACKBIRD_API_BEGIN_CAPTURE, 2, 0,
			BLACKBIRD_MPEG_CAPTURE,
			BLACKBIRD_RAW_BITS_NONE
		);
	msleep(10);

	blackbird_api_cmd(dev, BLACKBIRD_API_REFRESH_INPUT, 0,0);
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

static int mpeg_do_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, void *arg)
{
	struct cx8802_fh  *fh  = file->private_data;
	struct cx8802_dev *dev = fh->dev;
	struct cx88_core  *core = dev->core;

	if (debug > 1)
		v4l_print_ioctl(core->name,cmd);

	switch (cmd) {

	/* --- capabilities ------------------------------------------ */
	case VIDIOC_QUERYCAP:
	{
		struct v4l2_capability *cap = arg;

		memset(cap,0,sizeof(*cap));
		strcpy(cap->driver, "cx88_blackbird");
		strlcpy(cap->card, cx88_boards[core->board].name,sizeof(cap->card));
		sprintf(cap->bus_info,"PCI:%s",pci_name(dev->pci));
		cap->version = CX88_VERSION_CODE;
		cap->capabilities =
			V4L2_CAP_VIDEO_CAPTURE |
			V4L2_CAP_READWRITE     |
			V4L2_CAP_STREAMING     |
			0;
		if (UNSET != core->tuner_type)
			cap->capabilities |= V4L2_CAP_TUNER;

		return 0;
	}

	/* --- capture ioctls ---------------------------------------- */
	case VIDIOC_ENUM_FMT:
	{
		struct v4l2_fmtdesc *f = arg;
		int index;

		index = f->index;
		if (index != 0)
			return -EINVAL;

		memset(f,0,sizeof(*f));
		f->index = index;
		strlcpy(f->description, "MPEG", sizeof(f->description));
		f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		f->pixelformat = V4L2_PIX_FMT_MPEG;
		return 0;
	}
	case VIDIOC_G_FMT:
	{
		struct v4l2_format *f = arg;

		memset(f,0,sizeof(*f));
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
	case VIDIOC_TRY_FMT:
	{
		struct v4l2_format *f = arg;

		f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
		f->fmt.pix.bytesperline = 0;
		f->fmt.pix.sizeimage    = dev->ts_packet_size * dev->ts_packet_count; /* 188 * 4 * 1024; */;
		f->fmt.pix.colorspace   = 0;
		dprintk(0,"VIDIOC_TRY_FMT: w: %d, h: %d, f: %d\n",
			dev->width, dev->height, fh->mpegq.field );
		return 0;
	}
	case VIDIOC_S_FMT:
	{
		struct v4l2_format *f = arg;

		f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
		f->fmt.pix.bytesperline = 0;
		f->fmt.pix.sizeimage    = dev->ts_packet_size * dev->ts_packet_count; /* 188 * 4 * 1024; */;
		f->fmt.pix.colorspace   = 0;
		dprintk(0,"VIDIOC_S_FMT: w: %d, h: %d, f: %d\n",
			f->fmt.pix.width, f->fmt.pix.height, f->fmt.pix.field );
		return 0;
	}

	/* --- streaming capture ------------------------------------- */
	case VIDIOC_REQBUFS:
		return videobuf_reqbufs(&fh->mpegq, arg);

	case VIDIOC_QUERYBUF:
		return videobuf_querybuf(&fh->mpegq, arg);

	case VIDIOC_QBUF:
		return videobuf_qbuf(&fh->mpegq, arg);

	case VIDIOC_DQBUF:
		return videobuf_dqbuf(&fh->mpegq, arg,
				      file->f_flags & O_NONBLOCK);

	case VIDIOC_STREAMON:
		return videobuf_streamon(&fh->mpegq);

	case VIDIOC_STREAMOFF:
		return videobuf_streamoff(&fh->mpegq);

	/* --- mpeg compression -------------------------------------- */
	case VIDIOC_G_MPEGCOMP:
	{
		struct v4l2_mpeg_compression *f = arg;

		memcpy(f,&dev->params,sizeof(*f));
		return 0;
	}
	case VIDIOC_S_MPEGCOMP:
	{
		struct v4l2_mpeg_compression *f = arg;

		blackbird_set_params(dev, f);
		return 0;
	}

	default:
		return cx88_do_ioctl( inode, file, 0, dev->core, cmd, arg, cx88_ioctl_hook );
	}
	return 0;
}

int (*cx88_ioctl_hook)(struct inode *inode, struct file *file,
			unsigned int cmd, void *arg);
unsigned int (*cx88_ioctl_translator)(unsigned int cmd);

static unsigned int mpeg_translate_ioctl(unsigned int cmd)
{
	return cmd;
}

static int mpeg_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	cmd = cx88_ioctl_translator( cmd );
	return video_usercopy(inode, file, cmd, arg, cx88_ioctl_hook);
}

static int mpeg_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct cx8802_dev *h,*dev = NULL;
	struct cx8802_fh *fh;
	struct list_head *list;

	list_for_each(list,&cx8802_devlist) {
		h = list_entry(list, struct cx8802_dev, devlist);
		if (h->mpeg_dev->minor == minor)
			dev = h;
	}
	if (NULL == dev)
		return -ENODEV;

	if (blackbird_initialize_codec(dev) < 0)
		return -EINVAL;
	dprintk(1,"open minor=%d\n",minor);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh),GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;
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

	/* blackbird_api_cmd(fh->dev, BLACKBIRD_API_END_CAPTURE, 3, 0, BLACKBIRD_END_NOW, 0, 0x13); */
	blackbird_api_cmd(fh->dev, BLACKBIRD_API_END_CAPTURE, 3, 0,
			BLACKBIRD_END_NOW,
			BLACKBIRD_MPEG_CAPTURE,
			BLACKBIRD_RAW_BITS_NONE
		);

	/* stop mpeg capture */
	if (fh->mpegq.streaming)
		videobuf_streamoff(&fh->mpegq);
	if (fh->mpegq.reading)
		videobuf_read_stop(&fh->mpegq);

	videobuf_mmap_free(&fh->mpegq);
	file->private_data = NULL;
	kfree(fh);
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

static struct file_operations mpeg_fops =
{
	.owner	       = THIS_MODULE,
	.open	       = mpeg_open,
	.release       = mpeg_release,
	.read	       = mpeg_read,
	.poll          = mpeg_poll,
	.mmap	       = mpeg_mmap,
	.ioctl	       = mpeg_ioctl,
	.llseek        = no_llseek,
};

static struct video_device cx8802_mpeg_template =
{
	.name          = "cx8802",
	.type          = VID_TYPE_CAPTURE|VID_TYPE_TUNER|VID_TYPE_SCALES|VID_TYPE_MPEG_ENCODER,
	.hardware      = 0,
	.fops          = &mpeg_fops,
	.minor         = -1,
};

/* ------------------------------------------------------------------ */

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

static int __devinit blackbird_probe(struct pci_dev *pci_dev,
				     const struct pci_device_id *pci_id)
{
	struct cx8802_dev *dev;
	struct cx88_core  *core;
	int err;

	/* general setup */
	core = cx88_core_get(pci_dev);
	if (NULL == core)
		return -EINVAL;

	err = -ENODEV;
	if (!cx88_boards[core->board].blackbird)
		goto fail_core;

	err = -ENOMEM;
	dev = kzalloc(sizeof(*dev),GFP_KERNEL);
	if (NULL == dev)
		goto fail_core;
	dev->pci = pci_dev;
	dev->core = core;
	dev->width = 720;
	dev->height = 576;
	memcpy(&dev->params,&default_mpeg_params,sizeof(default_mpeg_params));
	memcpy(&dev->dnr_params,&default_dnr_params,sizeof(default_dnr_params));

	if (core->tuner_formats & V4L2_STD_525_60) {
		dev->height = 480;
		dev->params.vi_frame_rate = 30;
	} else {
		dev->height = 576;
		dev->params.vi_frame_rate = 25;
	}

	err = cx8802_init_common(dev);
	if (0 != err)
		goto fail_free;

	/* blackbird stuff */
	printk("%s/2: cx23416 based mpeg encoder (blackbird reference design)\n",
	       core->name);
	host_setup(dev->core);

	list_add_tail(&dev->devlist,&cx8802_devlist);
	blackbird_register_video(dev);

	/* initial device configuration: needed ? */

	return 0;

 fail_free:
	kfree(dev);
 fail_core:
	cx88_core_put(core,pci_dev);
	return err;
}

static void __devexit blackbird_remove(struct pci_dev *pci_dev)
{
	struct cx8802_dev *dev = pci_get_drvdata(pci_dev);

	/* blackbird */
	blackbird_unregister_video(dev);
	list_del(&dev->devlist);

	/* common */
	cx8802_fini_common(dev);
	cx88_core_put(dev->core,dev->pci);
	kfree(dev);
}

static struct pci_device_id cx8802_pci_tbl[] = {
	{
		.vendor       = 0x14f1,
		.device       = 0x8802,
		.subvendor    = PCI_ANY_ID,
		.subdevice    = PCI_ANY_ID,
	},{
		/* --- end of list --- */
	}
};
MODULE_DEVICE_TABLE(pci, cx8802_pci_tbl);

static struct pci_driver blackbird_pci_driver = {
	.name     = "cx88-blackbird",
	.id_table = cx8802_pci_tbl,
	.probe    = blackbird_probe,
	.remove   = __devexit_p(blackbird_remove),
	.suspend  = cx8802_suspend_common,
	.resume   = cx8802_resume_common,
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
	cx88_ioctl_hook = mpeg_do_ioctl;
	cx88_ioctl_translator = mpeg_translate_ioctl;
	return pci_register_driver(&blackbird_pci_driver);
}

static void blackbird_fini(void)
{
	pci_unregister_driver(&blackbird_pci_driver);
}

module_init(blackbird_init);
module_exit(blackbird_fini);

EXPORT_SYMBOL(cx88_ioctl_hook);
EXPORT_SYMBOL(cx88_ioctl_translator);
EXPORT_SYMBOL(blackbird_set_params);
EXPORT_SYMBOL(blackbird_set_dnr_params);

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 * kate: eol "unix"; indent-width 3; remove-trailing-space on; replace-trailing-space-save on; tab-width 8; replace-tabs off; space-indent off; mixed-indent off
 */
