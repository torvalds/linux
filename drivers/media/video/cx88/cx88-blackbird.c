/*
 * $Id: cx88-blackbird.c,v 1.26 2005/03/07 15:58:05 kraxel Exp $
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

MODULE_DESCRIPTION("driver for cx2388x/cx23416 based mpeg encoder cards");
MODULE_AUTHOR("Jelle Foks <jelle@foks.8m.com>");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

static unsigned int mpegbufs = 8;
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

/*Firmware API commands*/
#define IVTV_API_ENC_PING_FW 0x00000080
#define IVTV_API_ENC_GETVER 0x000000C4
#define IVTV_API_ENC_HALT_FW 0x000000C3
#define IVTV_API_STD_TIMEOUT 0x00010000 /*units??*/
//#define IVTV_API_ASSIGN_PGM_INDEX_INFO 0x000000c7
#define IVTV_API_ASSIGN_STREAM_TYPE 0x000000b9
#define IVTV_API_ASSIGN_OUTPUT_PORT 0x000000bb
#define IVTV_API_ASSIGN_FRAMERATE 0x0000008f
#define IVTV_API_ASSIGN_FRAME_SIZE 0x00000091
#define IVTV_API_ASSIGN_ASPECT_RATIO 0x00000099
#define IVTV_API_ASSIGN_BITRATES 0x00000095
#define IVTV_API_ASSIGN_GOP_PROPERTIES 0x00000097
#define IVTV_API_ASSIGN_3_2_PULLDOWN 0x000000b1
#define IVTV_API_ASSIGN_GOP_CLOSURE 0x000000c5
#define IVTV_API_ASSIGN_AUDIO_PROPERTIES 0x000000bd
#define IVTV_API_ASSIGN_DNR_FILTER_MODE 0x0000009b
#define IVTV_API_ASSIGN_DNR_FILTER_PROPS 0x0000009d
#define IVTV_API_ASSIGN_CORING_LEVELS 0x0000009f
#define IVTV_API_ASSIGN_SPATIAL_FILTER_TYPE 0x000000a1
#define IVTV_API_ASSIGN_FRAME_DROP_RATE 0x000000d0
#define IVTV_API_ASSIGN_PLACEHOLDER 0x000000d8
#define IVTV_API_MUTE_VIDEO 0x000000d9
#define IVTV_API_MUTE_AUDIO 0x000000da
#define IVTV_API_INITIALIZE_INPUT 0x000000cd
#define IVTV_API_REFRESH_INPUT 0x000000d3
#define IVTV_API_ASSIGN_NUM_VSYNC_LINES 0x000000d6
#define IVTV_API_BEGIN_CAPTURE 0x00000081
//#define IVTV_API_PAUSE_ENCODER 0x000000d2
//#define IVTV_API_EVENT_NOTIFICATION 0x000000d5
#define IVTV_API_END_CAPTURE 0x00000082

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

static void blackbird_codec_settings(struct cx8802_dev *dev)
{
	int bitrate_mode = 1;
	int bitrate = 7500000;
	int bitrate_peak = 7500000;

	/* assign stream type */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_STREAM_TYPE, 1, 0, 0); /* program stream */
        //blackbird_api_cmd(dev, IVTV_API_ASSIGN_STREAM_TYPE, 1, 0, 2); /* MPEG1 stream */
        //blackbird_api_cmd(dev, IVTV_API_ASSIGN_STREAM_TYPE, 1, 0, 3); /* PES A/V */
        //blackbird_api_cmd(dev, IVTV_API_ASSIGN_STREAM_TYPE, 1, 0, 10); /* DVD stream */

        /* assign output port */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_OUTPUT_PORT, 1, 0, 1); /* 1 = Host */

        /* assign framerate */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_FRAMERATE, 1, 0, 0);

        /* assign frame size */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_FRAME_SIZE, 2, 0,
			  dev->height, dev->width);

        /* assign aspect ratio */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_ASPECT_RATIO, 1, 0, 2);

        /* assign bitrates */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_BITRATES, 5, 0,
			 bitrate_mode,         /* mode */
			 bitrate,              /* bps */
			 bitrate_peak / 400,   /* peak/400 */
			 0, 0x70);             /* encoding buffer, ckennedy */

        /* assign gop properties */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_GOP_PROPERTIES, 2, 0, 15, 3);
        //blackbird_api_cmd(dev, IVTV_API_ASSIGN_GOP_PROPERTIES, 2, 0, 2, 1);

        /* assign 3 2 pulldown */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_3_2_PULLDOWN, 1, 0, 0);

        /* note: it's not necessary to set the samplerate, the mpeg encoder seems to autodetect/adjust */
	blackbird_api_cmd(dev, IVTV_API_ASSIGN_AUDIO_PROPERTIES, 1, 0, (2<<2) | (8<<4));

	/* assign gop closure */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_GOP_CLOSURE, 1, 0, 0);

        /* assign audio properties */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_AUDIO_PROPERTIES, 1, 0, 0 | (2 << 2) | (14 << 4));

        /* assign dnr filter mode */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_DNR_FILTER_MODE, 2, 0, 0, 0);

        /* assign dnr filter props*/
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_DNR_FILTER_PROPS, 2, 0, 0, 0);

        /* assign coring levels (luma_h, luma_l, chroma_h, chroma_l) */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_CORING_LEVELS, 4, 0, 0, 255, 0, 255);

	/* assign spatial filter type: luma_t: 1 = horiz_only, chroma_t: 1 = horiz_only */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_SPATIAL_FILTER_TYPE, 2, 0, 1, 1);

        /* assign frame drop rate */
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_FRAME_DROP_RATE, 1, 0, 0);
}

static int blackbird_initialize_codec(struct cx8802_dev *dev)
{
	struct cx88_core *core = dev->core;
	int version;
	int retval;

	dprintk(1,"Initialize codec\n");
	retval = blackbird_api_cmd(dev, IVTV_API_ENC_PING_FW, 0, 0); /* ping */
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

		retval = blackbird_api_cmd(dev, IVTV_API_ENC_PING_FW, 0, 0); /* ping */
		if (retval < 0) {
			dprintk(0, "ERROR: Firmware ping failed!\n");
			return -1;
		}

		retval = blackbird_api_cmd(dev, IVTV_API_ENC_GETVER, 0, 1, &version);
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

#if 0 /* FIXME */
	set_scale(dev, 720, 480, V4L2_FIELD_INTERLACED);
#endif
	blackbird_codec_settings(dev);
	msleep(1);

	//blackbird_api_cmd(dev, IVTV_API_ASSIGN_NUM_VSYNC_LINES, 4, 0, 0xef, 0xef);
	blackbird_api_cmd(dev, IVTV_API_ASSIGN_NUM_VSYNC_LINES, 4, 0, 0xf0, 0xf0);
	//blackbird_api_cmd(dev, IVTV_API_ASSIGN_NUM_VSYNC_LINES, 4, 0, 0x180, 0x180);
        blackbird_api_cmd(dev, IVTV_API_ASSIGN_PLACEHOLDER, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

	blackbird_api_cmd(dev, IVTV_API_INITIALIZE_INPUT, 0, 0); /* initialize the video input */

	msleep(1);

        blackbird_api_cmd(dev, IVTV_API_MUTE_VIDEO, 1, 0, 0);
	msleep(1);
        blackbird_api_cmd(dev, IVTV_API_MUTE_AUDIO, 1, 0, 0);
	msleep(1);

	blackbird_api_cmd(dev, IVTV_API_BEGIN_CAPTURE, 2, 0, 0, 0x13); /* start capturing to the host interface */
	//blackbird_api_cmd(dev, IVTV_API_BEGIN_CAPTURE, 2, 0, 0, 0); /* start capturing to the host interface */
	msleep(1);

	blackbird_api_cmd(dev, IVTV_API_REFRESH_INPUT, 0,0);
	return 0;
}

/* ------------------------------------------------------------------ */

static int bb_buf_setup(struct videobuf_queue *q,
			unsigned int *count, unsigned int *size)
{
	struct cx8802_fh *fh = q->priv_data;

	fh->dev->ts_packet_size  = 512;
	fh->dev->ts_packet_count = 100;

	*size = fh->dev->ts_packet_size * fh->dev->ts_packet_count;
	if (0 == *count)
		*count = mpegbufs;
	if (*count < 2)
		*count = 2;
	if (*count > 32)
		*count = 32;
	return 0;
}

static int
bb_buf_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
	       enum v4l2_field field)
{
	struct cx8802_fh *fh = q->priv_data;
	return cx8802_buf_prepare(fh->dev, (struct cx88_buffer*)vb);
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
	struct cx8802_fh *fh = q->priv_data;
	cx88_free_buffer(fh->dev->pci, (struct cx88_buffer*)vb);
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

	if (debug > 1)
		cx88_print_ioctl(dev->core->name,cmd);

	switch (cmd) {

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
		strlcpy(f->description, "MPEG TS", sizeof(f->description));
		f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		f->pixelformat = V4L2_PIX_FMT_MPEG;
		return 0;
	}
	case VIDIOC_G_FMT:
	case VIDIOC_S_FMT:
	case VIDIOC_TRY_FMT:
	{
		/* FIXME -- quick'n'dirty for exactly one size ... */
		struct v4l2_format *f = arg;

		memset(f,0,sizeof(*f));
		f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		f->fmt.pix.width        = dev->width;
		f->fmt.pix.height       = dev->height;
		f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
		f->fmt.pix.sizeimage    = 1024 * 512 /* FIXME: BUFFER_SIZE */;
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

	default:
		return -EINVAL;
	}
	return 0;
}

static int mpeg_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, mpeg_do_ioctl);
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
	fh = kmalloc(sizeof(*fh),GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;
	memset(fh,0,sizeof(*fh));
	file->private_data = fh;
	fh->dev      = dev;

	/* FIXME: locking against other video device */
	cx88_set_scale(dev->core, dev->width, dev->height,
		       V4L2_FIELD_INTERLACED);

	videobuf_queue_init(&fh->mpegq, &blackbird_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_TOP,
			    sizeof(struct cx88_buffer),
			    fh);
	return 0;
}

static int mpeg_release(struct inode *inode, struct file *file)
{
	struct cx8802_fh  *fh  = file->private_data;

	blackbird_api_cmd(fh->dev, IVTV_API_END_CAPTURE, 3, 0, 1, 0, 0x13);

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
	dev = kmalloc(sizeof(*dev),GFP_KERNEL);
	if (NULL == dev)
		goto fail_core;
	memset(dev,0,sizeof(*dev));
	dev->pci = pci_dev;
	dev->core = core;
	dev->width = 720;
	dev->height = 480;

	err = cx8802_init_common(dev);
	if (0 != err)
		goto fail_free;

	/* blackbird stuff */
	printk("%s/2: cx23416 based mpeg encoder (blackbird reference design)\n",
	       core->name);
	host_setup(dev->core);

	list_add_tail(&dev->devlist,&cx8802_devlist);
	blackbird_register_video(dev);
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
	return pci_register_driver(&blackbird_pci_driver);
}

static void blackbird_fini(void)
{
	pci_unregister_driver(&blackbird_pci_driver);
}

module_init(blackbird_init);
module_exit(blackbird_fini);

/* ----------------------------------------------------------- */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
