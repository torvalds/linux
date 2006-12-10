/*
 * usbvision-core.c - driver for NT100x USB video capture devices
 *
 *
 * Copyright (c) 1999-2005 Joerg Heckenbach <joerg@heckenbach-aw.de>
 *                         Dwaine Garden <dwainegarden@rogers.com>
 *
 * This module is part of usbvision driver project.
 * Updates to driver completed by Dwaine P. Garden
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/utsname.h>
#include <linux/highmem.h>
#include <linux/smp_lock.h>
#include <linux/videodev.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <linux/videodev2.h>
#include <linux/video_decoder.h>
#include <linux/i2c.h>

#include <media/saa7115.h>
#include <media/v4l2-common.h>
#include <media/tuner.h>
#include <media/audiochip.h>

#include <linux/moduleparam.h>
#include <linux/workqueue.h>

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#include "usbvision.h"

static unsigned int core_debug = 0;
module_param(core_debug,int,0644);
MODULE_PARM_DESC(core_debug,"enable debug messages [core]");

static unsigned int force_testpattern = 0;
module_param(force_testpattern,int,0644);
MODULE_PARM_DESC(force_testpattern,"enable test pattern display [core]");

static int adjustCompression = 1;			// Set the compression to be adaptive
module_param(adjustCompression, int, 0444);
MODULE_PARM_DESC(adjustCompression, " Set the ADPCM compression for the device.  Default: 1 (On)");

static int SwitchSVideoInput = 0;			// To help people with Black and White output with using s-video input.  Some cables and input device are wired differently.
module_param(SwitchSVideoInput, int, 0444);
MODULE_PARM_DESC(SwitchSVideoInput, " Set the S-Video input.  Some cables and input device are wired differently. Default: 0 (Off)");

#define	ENABLE_HEXDUMP	0	/* Enable if you need it */


#ifdef USBVISION_DEBUG
	#define PDEBUG(level, fmt, args...) \
		if (core_debug & (level)) info("[%s:%d] " fmt, __PRETTY_FUNCTION__, __LINE__ , ## args)
#else
	#define PDEBUG(level, fmt, args...) do {} while(0)
#endif

#define DBG_HEADER	1<<0
#define DBG_IRQ		1<<1
#define DBG_ISOC	1<<2
#define DBG_PARSE	1<<3
#define DBG_SCRATCH	1<<4
#define DBG_FUNC	1<<5

static const int max_imgwidth = MAX_FRAME_WIDTH;
static const int max_imgheight = MAX_FRAME_HEIGHT;
static const int min_imgwidth = MIN_FRAME_WIDTH;
static const int min_imgheight = MIN_FRAME_HEIGHT;

/* The value of 'scratch_buf_size' affects quality of the picture
 * in many ways. Shorter buffers may cause loss of data when client
 * is too slow. Larger buffers are memory-consuming and take longer
 * to work with. This setting can be adjusted, but the default value
 * should be OK for most desktop users.
 */
#define DEFAULT_SCRATCH_BUF_SIZE	(0x20000)		// 128kB memory scratch buffer
static const int scratch_buf_size = DEFAULT_SCRATCH_BUF_SIZE;

// Function prototypes
static int usbvision_request_intra (struct usb_usbvision *usbvision);
static int usbvision_unrequest_intra (struct usb_usbvision *usbvision);
static int usbvision_adjust_compression (struct usb_usbvision *usbvision);
static int usbvision_measure_bandwidth (struct usb_usbvision *usbvision);

/*******************************/
/* Memory management functions */
/*******************************/

/*
 * Here we want the physical address of the memory.
 * This is used when initializing the contents of the area.
 */

void *usbvision_rvmalloc(unsigned long size)
{
	void *mem;
	unsigned long adr;

	size = PAGE_ALIGN(size);
	mem = vmalloc_32(size);
	if (!mem)
		return NULL;

	memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	adr = (unsigned long) mem;
	while (size > 0) {
		SetPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	return mem;
}

void usbvision_rvfree(void *mem, unsigned long size)
{
	unsigned long adr;

	if (!mem)
		return;

	size = PAGE_ALIGN(size);

	adr = (unsigned long) mem;
	while ((long) size > 0) {
		ClearPageReserved(vmalloc_to_page((void *)adr));
		adr += PAGE_SIZE;
		size -= PAGE_SIZE;
	}

	vfree(mem);
}



#if ENABLE_HEXDUMP
static void usbvision_hexdump(const unsigned char *data, int len)
{
	char tmp[80];
	int i, k;

	for (i = k = 0; len > 0; i++, len--) {
		if (i > 0 && (i % 16 == 0)) {
			printk("%s\n", tmp);
			k = 0;
		}
		k += sprintf(&tmp[k], "%02x ", data[i]);
	}
	if (k > 0)
		printk("%s\n", tmp);
}
#endif

/********************************
 * scratch ring buffer handling
 ********************************/
int scratch_len(struct usb_usbvision *usbvision)    /*This returns the amount of data actually in the buffer */
{
	int len = usbvision->scratch_write_ptr - usbvision->scratch_read_ptr;
	if (len < 0) {
		len += scratch_buf_size;
	}
	PDEBUG(DBG_SCRATCH, "scratch_len() = %d\n", len);

	return len;
}


/* This returns the free space left in the buffer */
int scratch_free(struct usb_usbvision *usbvision)
{
	int free = usbvision->scratch_read_ptr - usbvision->scratch_write_ptr;
	if (free <= 0) {
		free += scratch_buf_size;
	}
	if (free) {
		free -= 1;							/* at least one byte in the buffer must */
										/* left blank, otherwise there is no chance to differ between full and empty */
	}
	PDEBUG(DBG_SCRATCH, "return %d\n", free);

	return free;
}


/* This puts data into the buffer */
int scratch_put(struct usb_usbvision *usbvision, unsigned char *data, int len)
{
	int len_part;

	if (usbvision->scratch_write_ptr + len < scratch_buf_size) {
		memcpy(usbvision->scratch + usbvision->scratch_write_ptr, data, len);
		usbvision->scratch_write_ptr += len;
	}
	else {
		len_part = scratch_buf_size - usbvision->scratch_write_ptr;
		memcpy(usbvision->scratch + usbvision->scratch_write_ptr, data, len_part);
		if (len == len_part) {
			usbvision->scratch_write_ptr = 0;			/* just set write_ptr to zero */
		}
		else {
			memcpy(usbvision->scratch, data + len_part, len - len_part);
			usbvision->scratch_write_ptr = len - len_part;
		}
	}

	PDEBUG(DBG_SCRATCH, "len=%d, new write_ptr=%d\n", len, usbvision->scratch_write_ptr);

	return len;
}

/* This marks the write_ptr as position of new frame header */
void scratch_mark_header(struct usb_usbvision *usbvision)
{
	PDEBUG(DBG_SCRATCH, "header at write_ptr=%d\n", usbvision->scratch_headermarker_write_ptr);

	usbvision->scratch_headermarker[usbvision->scratch_headermarker_write_ptr] =
				usbvision->scratch_write_ptr;
	usbvision->scratch_headermarker_write_ptr += 1;
	usbvision->scratch_headermarker_write_ptr %= USBVISION_NUM_HEADERMARKER;
}

/* This gets data from the buffer at the given "ptr" position */
int scratch_get_extra(struct usb_usbvision *usbvision, unsigned char *data, int *ptr, int len)
{
	int len_part;
	if (*ptr + len < scratch_buf_size) {
		memcpy(data, usbvision->scratch + *ptr, len);
		*ptr += len;
	}
	else {
		len_part = scratch_buf_size - *ptr;
		memcpy(data, usbvision->scratch + *ptr, len_part);
		if (len == len_part) {
			*ptr = 0;							/* just set the y_ptr to zero */
		}
		else {
			memcpy(data + len_part, usbvision->scratch, len - len_part);
			*ptr = len - len_part;
		}
	}

	PDEBUG(DBG_SCRATCH, "len=%d, new ptr=%d\n", len, *ptr);

	return len;
}


/* This sets the scratch extra read pointer */
void scratch_set_extra_ptr(struct usb_usbvision *usbvision, int *ptr, int len)
{
	*ptr = (usbvision->scratch_read_ptr + len)%scratch_buf_size;

	PDEBUG(DBG_SCRATCH, "ptr=%d\n", *ptr);
}


/*This increments the scratch extra read pointer */
void scratch_inc_extra_ptr(int *ptr, int len)
{
	*ptr = (*ptr + len) % scratch_buf_size;

	PDEBUG(DBG_SCRATCH, "ptr=%d\n", *ptr);
}


/* This gets data from the buffer */
int scratch_get(struct usb_usbvision *usbvision, unsigned char *data, int len)
{
	int len_part;
	if (usbvision->scratch_read_ptr + len < scratch_buf_size) {
		memcpy(data, usbvision->scratch + usbvision->scratch_read_ptr, len);
		usbvision->scratch_read_ptr += len;
	}
	else {
		len_part = scratch_buf_size - usbvision->scratch_read_ptr;
		memcpy(data, usbvision->scratch + usbvision->scratch_read_ptr, len_part);
		if (len == len_part) {
			usbvision->scratch_read_ptr = 0;				/* just set the read_ptr to zero */
		}
		else {
			memcpy(data + len_part, usbvision->scratch, len - len_part);
			usbvision->scratch_read_ptr = len - len_part;
		}
	}

	PDEBUG(DBG_SCRATCH, "len=%d, new read_ptr=%d\n", len, usbvision->scratch_read_ptr);

	return len;
}


/* This sets read pointer to next header and returns it */
int scratch_get_header(struct usb_usbvision *usbvision,struct usbvision_frame_header *header)
{
	int errCode = 0;

	PDEBUG(DBG_SCRATCH, "from read_ptr=%d", usbvision->scratch_headermarker_read_ptr);

	while (usbvision->scratch_headermarker_write_ptr -
		usbvision->scratch_headermarker_read_ptr != 0) {
		usbvision->scratch_read_ptr =
			usbvision->scratch_headermarker[usbvision->scratch_headermarker_read_ptr];
		usbvision->scratch_headermarker_read_ptr += 1;
		usbvision->scratch_headermarker_read_ptr %= USBVISION_NUM_HEADERMARKER;
		scratch_get(usbvision, (unsigned char *)header, USBVISION_HEADER_LENGTH);
		if ((header->magic_1 == USBVISION_MAGIC_1)
			 && (header->magic_2 == USBVISION_MAGIC_2)
			 && (header->headerLength == USBVISION_HEADER_LENGTH)) {
			errCode = USBVISION_HEADER_LENGTH;
			header->frameWidth  = header->frameWidthLo  + (header->frameWidthHi << 8);
			header->frameHeight = header->frameHeightLo + (header->frameHeightHi << 8);
			break;
		}
	}

	return errCode;
}


/*This removes len bytes of old data from the buffer */
void scratch_rm_old(struct usb_usbvision *usbvision, int len)
{

	usbvision->scratch_read_ptr += len;
	usbvision->scratch_read_ptr %= scratch_buf_size;
	PDEBUG(DBG_SCRATCH, "read_ptr is now %d\n", usbvision->scratch_read_ptr);
}


/*This resets the buffer - kills all data in it too */
void scratch_reset(struct usb_usbvision *usbvision)
{
	PDEBUG(DBG_SCRATCH, "\n");

	usbvision->scratch_read_ptr = 0;
	usbvision->scratch_write_ptr = 0;
	usbvision->scratch_headermarker_read_ptr = 0;
	usbvision->scratch_headermarker_write_ptr = 0;
	usbvision->isocstate = IsocState_NoFrame;
}

int usbvision_scratch_alloc(struct usb_usbvision *usbvision)
{
	usbvision->scratch = vmalloc(scratch_buf_size);
	scratch_reset(usbvision);
	if(usbvision->scratch == NULL) {
		err("%s: unable to allocate %d bytes for scratch",
		    __FUNCTION__, scratch_buf_size);
		return -ENOMEM;
	}
	return 0;
}

void usbvision_scratch_free(struct usb_usbvision *usbvision)
{
	if (usbvision->scratch != NULL) {
		vfree(usbvision->scratch);
		usbvision->scratch = NULL;
	}
}

/*
 * usbvision_testpattern()
 *
 * Procedure forms a test pattern (yellow grid on blue background).
 *
 * Parameters:
 * fullframe:   if TRUE then entire frame is filled, otherwise the procedure
 *		continues from the current scanline.
 * pmode	0: fill the frame with solid blue color (like on VCR or TV)
 *		1: Draw a colored grid
 *
 */
void usbvision_testpattern(struct usb_usbvision *usbvision, int fullframe,
			int pmode)
{
	static const char proc[] = "usbvision_testpattern";
	struct usbvision_frame *frame;
	unsigned char *f;
	int num_cell = 0;
	int scan_length = 0;
	static int num_pass = 0;

	if (usbvision == NULL) {
		printk(KERN_ERR "%s: usbvision == NULL\n", proc);
		return;
	}
	if (usbvision->curFrame == NULL) {
		printk(KERN_ERR "%s: usbvision->curFrame is NULL.\n", proc);
		return;
	}

	/* Grab the current frame */
	frame = usbvision->curFrame;

	/* Optionally start at the beginning */
	if (fullframe) {
		frame->curline = 0;
		frame->scanlength = 0;
	}

	/* Form every scan line */
	for (; frame->curline < frame->frmheight; frame->curline++) {
		int i;

		f = frame->data + (usbvision->curwidth * 3 * frame->curline);
		for (i = 0; i < usbvision->curwidth; i++) {
			unsigned char cb = 0x80;
			unsigned char cg = 0;
			unsigned char cr = 0;

			if (pmode == 1) {
				if (frame->curline % 32 == 0)
					cb = 0, cg = cr = 0xFF;
				else if (i % 32 == 0) {
					if (frame->curline % 32 == 1)
						num_cell++;
					cb = 0, cg = cr = 0xFF;
				} else {
					cb =
					    ((num_cell * 7) +
					     num_pass) & 0xFF;
					cg =
					    ((num_cell * 5) +
					     num_pass * 2) & 0xFF;
					cr =
					    ((num_cell * 3) +
					     num_pass * 3) & 0xFF;
				}
			} else {
				/* Just the blue screen */
			}

			*f++ = cb;
			*f++ = cg;
			*f++ = cr;
			scan_length += 3;
		}
	}

	frame->grabstate = FrameState_Done;
	frame->scanlength += scan_length;
	++num_pass;

}

/*
 * usbvision_decompress_alloc()
 *
 * allocates intermediate buffer for decompression
 */
int usbvision_decompress_alloc(struct usb_usbvision *usbvision)
{
	int IFB_size = MAX_FRAME_WIDTH * MAX_FRAME_HEIGHT * 3 / 2;
	usbvision->IntraFrameBuffer = vmalloc(IFB_size);
	if (usbvision->IntraFrameBuffer == NULL) {
		err("%s: unable to allocate %d for compr. frame buffer", __FUNCTION__, IFB_size);
		return -ENOMEM;
	}
	return 0;
}

/*
 * usbvision_decompress_free()
 *
 * frees intermediate buffer for decompression
 */
void usbvision_decompress_free(struct usb_usbvision *usbvision)
{
	if (usbvision->IntraFrameBuffer != NULL) {
		vfree(usbvision->IntraFrameBuffer);
		usbvision->IntraFrameBuffer = NULL;
	}
}

/************************************************************
 * Here comes the data parsing stuff that is run as interrupt
 ************************************************************/
/*
 * usbvision_find_header()
 *
 * Locate one of supported header markers in the scratch buffer.
 */
static enum ParseState usbvision_find_header(struct usb_usbvision *usbvision)
{
	struct usbvision_frame *frame;
	int foundHeader = 0;

	frame = usbvision->curFrame;

	while (scratch_get_header(usbvision, &frame->isocHeader) == USBVISION_HEADER_LENGTH) {
		// found header in scratch
		PDEBUG(DBG_HEADER, "found header: 0x%02x%02x %d %d %d %d %#x 0x%02x %u %u",
				frame->isocHeader.magic_2,
				frame->isocHeader.magic_1,
				frame->isocHeader.headerLength,
				frame->isocHeader.frameNum,
				frame->isocHeader.framePhase,
				frame->isocHeader.frameLatency,
				frame->isocHeader.dataFormat,
				frame->isocHeader.formatParam,
				frame->isocHeader.frameWidth,
				frame->isocHeader.frameHeight);

		if (usbvision->requestIntra) {
			if (frame->isocHeader.formatParam & 0x80) {
				foundHeader = 1;
				usbvision->lastIsocFrameNum = -1; // do not check for lost frames this time
				usbvision_unrequest_intra(usbvision);
				break;
			}
		}
		else {
			foundHeader = 1;
			break;
		}
	}

	if (foundHeader) {
		frame->frmwidth = frame->isocHeader.frameWidth * usbvision->stretch_width;
		frame->frmheight = frame->isocHeader.frameHeight * usbvision->stretch_height;
		frame->v4l2_linesize = (frame->frmwidth * frame->v4l2_format.depth)>> 3;
	}
	else { // no header found
		PDEBUG(DBG_HEADER, "skipping scratch data, no header");
		scratch_reset(usbvision);
		return ParseState_EndParse;
	}

	// found header
	if (frame->isocHeader.dataFormat==ISOC_MODE_COMPRESS) {
		//check isocHeader.frameNum for lost frames
		if (usbvision->lastIsocFrameNum >= 0) {
			if (((usbvision->lastIsocFrameNum + 1) % 32) != frame->isocHeader.frameNum) {
				// unexpected frame drop: need to request new intra frame
				PDEBUG(DBG_HEADER, "Lost frame before %d on USB", frame->isocHeader.frameNum);
				usbvision_request_intra(usbvision);
				return ParseState_NextFrame;
			}
		}
		usbvision->lastIsocFrameNum = frame->isocHeader.frameNum;
	}
	usbvision->header_count++;
	frame->scanstate = ScanState_Lines;
	frame->curline = 0;

	if (force_testpattern) {
		usbvision_testpattern(usbvision, 1, 1);
		return ParseState_NextFrame;
	}
	return ParseState_Continue;
}

static enum ParseState usbvision_parse_lines_422(struct usb_usbvision *usbvision,
					   long *pcopylen)
{
	volatile struct usbvision_frame *frame;
	unsigned char *f;
	int len;
	int i;
	unsigned char yuyv[4]={180, 128, 10, 128}; // YUV components
	unsigned char rv, gv, bv;	// RGB components
	int clipmask_index, bytes_per_pixel;
	int stretch_bytes, clipmask_add;

	frame  = usbvision->curFrame;
	f = frame->data + (frame->v4l2_linesize * frame->curline);

	/* Make sure there's enough data for the entire line */
	len = (frame->isocHeader.frameWidth * 2)+5;
	if (scratch_len(usbvision) < len) {
		PDEBUG(DBG_PARSE, "out of data in line %d, need %u.\n", frame->curline, len);
		return ParseState_Out;
	}

	if ((frame->curline + 1) >= frame->frmheight) {
		return ParseState_NextFrame;
	}

	bytes_per_pixel = frame->v4l2_format.bytes_per_pixel;
	stretch_bytes = (usbvision->stretch_width - 1) * bytes_per_pixel;
	clipmask_index = frame->curline * MAX_FRAME_WIDTH;
	clipmask_add = usbvision->stretch_width;

	for (i = 0; i < frame->frmwidth; i+=(2 * usbvision->stretch_width)) {

		scratch_get(usbvision, &yuyv[0], 4);

		if (frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
			*f++ = yuyv[0]; // Y
			*f++ = yuyv[3]; // U
		}
		else {

			YUV_TO_RGB_BY_THE_BOOK(yuyv[0], yuyv[1], yuyv[3], rv, gv, bv);
			switch (frame->v4l2_format.format) {
				case V4L2_PIX_FMT_RGB565:
					*f++ = (0x1F & (bv >> 3)) | (0xE0 & (gv << 3));
					*f++ = (0x07 & (gv >> 5)) | (0xF8 &  rv);
					break;
				case V4L2_PIX_FMT_RGB24:
					*f++ = bv;
					*f++ = gv;
					*f++ = rv;
					break;
				case V4L2_PIX_FMT_RGB32:
					*f++ = bv;
					*f++ = gv;
					*f++ = rv;
					f++;
					break;
				case V4L2_PIX_FMT_RGB555:
					*f++ = (0x1F & (bv >> 3)) | (0xE0 & (gv << 2));
					*f++ = (0x03 & (gv >> 6)) | (0x7C & (rv >> 1));
					break;
			}
		}
		clipmask_index += clipmask_add;
		f += stretch_bytes;

		if (frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
			*f++ = yuyv[2]; // Y
			*f++ = yuyv[1]; // V
		}
		else {

			YUV_TO_RGB_BY_THE_BOOK(yuyv[2], yuyv[1], yuyv[3], rv, gv, bv);
			switch (frame->v4l2_format.format) {
				case V4L2_PIX_FMT_RGB565:
					*f++ = (0x1F & (bv >> 3)) | (0xE0 & (gv << 3));
					*f++ = (0x07 & (gv >> 5)) | (0xF8 &  rv);
					break;
				case V4L2_PIX_FMT_RGB24:
					*f++ = bv;
					*f++ = gv;
					*f++ = rv;
					break;
				case V4L2_PIX_FMT_RGB32:
					*f++ = bv;
					*f++ = gv;
					*f++ = rv;
					f++;
					break;
				case V4L2_PIX_FMT_RGB555:
					*f++ = (0x1F & (bv >> 3)) | (0xE0 & (gv << 2));
					*f++ = (0x03 & (gv >> 6)) | (0x7C & (rv >> 1));
					break;
			}
		}
		clipmask_index += clipmask_add;
		f += stretch_bytes;
	}

	frame->curline += usbvision->stretch_height;
	*pcopylen += frame->v4l2_linesize * usbvision->stretch_height;

	if (frame->curline >= frame->frmheight) {
		return ParseState_NextFrame;
	}
	else {
		return ParseState_Continue;
	}
}

/* The decompression routine  */
static int usbvision_decompress(struct usb_usbvision *usbvision,unsigned char *Compressed,
								unsigned char *Decompressed, int *StartPos,
								int *BlockTypeStartPos, int Len)
{
	int RestPixel, Idx, MaxPos, Pos, ExtraPos, BlockLen, BlockTypePos, BlockTypeLen;
	unsigned char BlockByte, BlockCode, BlockType, BlockTypeByte, Integrator;

	Integrator = 0;
	Pos = *StartPos;
	BlockTypePos = *BlockTypeStartPos;
	MaxPos = 396; //Pos + Len;
	ExtraPos = Pos;
	BlockLen = 0;
	BlockByte = 0;
	BlockCode = 0;
	BlockType = 0;
	BlockTypeByte = 0;
	BlockTypeLen = 0;
	RestPixel = Len;

	for (Idx = 0; Idx < Len; Idx++) {

		if (BlockLen == 0) {
			if (BlockTypeLen==0) {
				BlockTypeByte = Compressed[BlockTypePos];
				BlockTypePos++;
				BlockTypeLen = 4;
			}
			BlockType = (BlockTypeByte & 0xC0) >> 6;

			//statistic:
			usbvision->ComprBlockTypes[BlockType]++;

			Pos = ExtraPos;
			if (BlockType == 0) {
				if(RestPixel >= 24) {
					Idx += 23;
					RestPixel -= 24;
					Integrator = Decompressed[Idx];
				} else {
					Idx += RestPixel - 1;
					RestPixel = 0;
				}
			} else {
				BlockCode = Compressed[Pos];
				Pos++;
				if (RestPixel >= 24) {
					BlockLen  = 24;
				} else {
					BlockLen = RestPixel;
				}
				RestPixel -= BlockLen;
				ExtraPos = Pos + (BlockLen / 4);
			}
			BlockTypeByte <<= 2;
			BlockTypeLen -= 1;
		}
		if (BlockLen > 0) {
			if ((BlockLen%4) == 0) {
				BlockByte = Compressed[Pos];
				Pos++;
			}
			if (BlockType == 1) { //inter Block
				Integrator = Decompressed[Idx];
			}
			switch (BlockByte & 0xC0) {
				case 0x03<<6:
					Integrator += Compressed[ExtraPos];
					ExtraPos++;
					break;
				case 0x02<<6:
					Integrator += BlockCode;
					break;
				case 0x00:
					Integrator -= BlockCode;
					break;
			}
			Decompressed[Idx] = Integrator;
			BlockByte <<= 2;
			BlockLen -= 1;
		}
	}
	*StartPos = ExtraPos;
	*BlockTypeStartPos = BlockTypePos;
	return Idx;
}


/*
 * usbvision_parse_compress()
 *
 * Parse compressed frame from the scratch buffer, put
 * decoded RGB value into the current frame buffer and add the written
 * number of bytes (RGB) to the *pcopylen.
 *
 */
static enum ParseState usbvision_parse_compress(struct usb_usbvision *usbvision,
					   long *pcopylen)
{
#define USBVISION_STRIP_MAGIC		0x5A
#define USBVISION_STRIP_LEN_MAX		400
#define USBVISION_STRIP_HEADER_LEN	3

	struct usbvision_frame *frame;
	unsigned char *f,*u = NULL ,*v = NULL;
	unsigned char StripData[USBVISION_STRIP_LEN_MAX];
	unsigned char StripHeader[USBVISION_STRIP_HEADER_LEN];
	int Idx, IdxEnd, StripLen, StripPtr, StartBlockPos, BlockPos, BlockTypePos;
	int clipmask_index, bytes_per_pixel, rc;
	int imageSize;
	unsigned char rv, gv, bv;
	static unsigned char *Y, *U, *V;

	frame  = usbvision->curFrame;
	imageSize = frame->frmwidth * frame->frmheight;
	if ( (frame->v4l2_format.format == V4L2_PIX_FMT_YUV422P) ||
	     (frame->v4l2_format.format == V4L2_PIX_FMT_YVU420) ) {       // this is a planar format
		//... v4l2_linesize not used here.
		f = frame->data + (frame->width * frame->curline);
	} else
		f = frame->data + (frame->v4l2_linesize * frame->curline);

	if (frame->v4l2_format.format == V4L2_PIX_FMT_YUYV){ //initialise u and v pointers
		// get base of u and b planes add halfoffset

		u = frame->data
			+ imageSize
			+ (frame->frmwidth >>1) * frame->curline ;
		v = u + (imageSize >>1 );

	} else if (frame->v4l2_format.format == V4L2_PIX_FMT_YVU420){

		v = frame->data + imageSize + ((frame->curline* (frame->width))>>2) ;
		u = v + (imageSize >>2) ;
	}

	if (frame->curline == 0) {
		usbvision_adjust_compression(usbvision);
	}

	if (scratch_len(usbvision) < USBVISION_STRIP_HEADER_LEN) {
		return ParseState_Out;
	}

	//get strip header without changing the scratch_read_ptr
	scratch_set_extra_ptr(usbvision, &StripPtr, 0);
	scratch_get_extra(usbvision, &StripHeader[0], &StripPtr,
				USBVISION_STRIP_HEADER_LEN);

	if (StripHeader[0] != USBVISION_STRIP_MAGIC) {
		// wrong strip magic
		usbvision->stripMagicErrors++;
		return ParseState_NextFrame;
	}

	if (frame->curline != (int)StripHeader[2]) {
		//line number missmatch error
		usbvision->stripLineNumberErrors++;
	}

	StripLen = 2 * (unsigned int)StripHeader[1];
	if (StripLen > USBVISION_STRIP_LEN_MAX) {
		// strip overrun
		// I think this never happens
		usbvision_request_intra(usbvision);
	}

	if (scratch_len(usbvision) < StripLen) {
		//there is not enough data for the strip
		return ParseState_Out;
	}

	if (usbvision->IntraFrameBuffer) {
		Y = usbvision->IntraFrameBuffer + frame->frmwidth * frame->curline;
		U = usbvision->IntraFrameBuffer + imageSize + (frame->frmwidth / 2) * (frame->curline / 2);
		V = usbvision->IntraFrameBuffer + imageSize / 4 * 5 + (frame->frmwidth / 2) * (frame->curline / 2);
	}
	else {
		return ParseState_NextFrame;
	}

	bytes_per_pixel = frame->v4l2_format.bytes_per_pixel;
	clipmask_index = frame->curline * MAX_FRAME_WIDTH;

	scratch_get(usbvision, StripData, StripLen);

	IdxEnd = frame->frmwidth;
	BlockTypePos = USBVISION_STRIP_HEADER_LEN;
	StartBlockPos = BlockTypePos + (IdxEnd - 1) / 96 + (IdxEnd / 2 - 1) / 96 + 2;
	BlockPos = StartBlockPos;

	usbvision->BlockPos = BlockPos;

	if ((rc = usbvision_decompress(usbvision, StripData, Y, &BlockPos, &BlockTypePos, IdxEnd)) != IdxEnd) {
		//return ParseState_Continue;
	}
	if (StripLen > usbvision->maxStripLen) {
		usbvision->maxStripLen = StripLen;
	}

	if (frame->curline%2) {
		if ((rc = usbvision_decompress(usbvision, StripData, V, &BlockPos, &BlockTypePos, IdxEnd/2)) != IdxEnd/2) {
		//return ParseState_Continue;
		}
	}
	else {
		if ((rc = usbvision_decompress(usbvision, StripData, U, &BlockPos, &BlockTypePos, IdxEnd/2)) != IdxEnd/2) {
			//return ParseState_Continue;
		}
	}

	if (BlockPos > usbvision->comprBlockPos) {
		usbvision->comprBlockPos = BlockPos;
	}
	if (BlockPos > StripLen) {
		usbvision->stripLenErrors++;
	}

	for (Idx = 0; Idx < IdxEnd; Idx++) {
		if(frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
			*f++ = Y[Idx];
			*f++ = Idx & 0x01 ? U[Idx/2] : V[Idx/2];
		}
		else if(frame->v4l2_format.format == V4L2_PIX_FMT_YUV422P) {
			*f++ = Y[Idx];
			if ( Idx & 0x01)
				*u++ = U[Idx>>1] ;
			else
				*v++ = V[Idx>>1];
		}
		else if (frame->v4l2_format.format == V4L2_PIX_FMT_YVU420) {
			*f++ = Y [Idx];
			if ( !((  Idx & 0x01  ) | (  frame->curline & 0x01  )) ){

/* 				 only need do this for 1 in 4 pixels */
/* 				 intraframe buffer is YUV420 format */

				*u++ = U[Idx >>1];
				*v++ = V[Idx >>1];
			}

		}
		else {
			YUV_TO_RGB_BY_THE_BOOK(Y[Idx], U[Idx/2], V[Idx/2], rv, gv, bv);
			switch (frame->v4l2_format.format) {
				case V4L2_PIX_FMT_GREY:
					*f++ = Y[Idx];
					break;
				case V4L2_PIX_FMT_RGB555:
					*f++ = (0x1F & (bv >> 3)) | (0xE0 & (gv << 2));
					*f++ = (0x03 & (gv >> 6)) | (0x7C & (rv >> 1));
					break;
				case V4L2_PIX_FMT_RGB565:
					*f++ = (0x1F & (bv >> 3)) | (0xE0 & (gv << 3));
					*f++ = (0x07 & (gv >> 5)) | (0xF8 &  rv);
					break;
				case V4L2_PIX_FMT_RGB24:
					*f++ = bv;
					*f++ = gv;
					*f++ = rv;
					break;
				case V4L2_PIX_FMT_RGB32:
					*f++ = bv;
					*f++ = gv;
					*f++ = rv;
					f++;
					break;
			}
		}
		clipmask_index++;
	}
	/* Deal with non-integer no. of bytes for YUV420P */
	if (frame->v4l2_format.format != V4L2_PIX_FMT_YVU420 )
		*pcopylen += frame->v4l2_linesize;
	else
		*pcopylen += frame->curline & 0x01 ? frame->v4l2_linesize : frame->v4l2_linesize << 1;

	frame->curline += 1;

	if (frame->curline >= frame->frmheight) {
		return ParseState_NextFrame;
	}
	else {
		return ParseState_Continue;
	}

}


/*
 * usbvision_parse_lines_420()
 *
 * Parse two lines from the scratch buffer, put
 * decoded RGB value into the current frame buffer and add the written
 * number of bytes (RGB) to the *pcopylen.
 *
 */
static enum ParseState usbvision_parse_lines_420(struct usb_usbvision *usbvision,
					   long *pcopylen)
{
	struct usbvision_frame *frame;
	unsigned char *f_even = NULL, *f_odd = NULL;
	unsigned int pixel_per_line, block;
	int pixel, block_split;
	int y_ptr, u_ptr, v_ptr, y_odd_offset;
	const int   y_block_size = 128;
	const int  uv_block_size = 64;
	const int sub_block_size = 32;
	const int y_step[] = { 0, 0, 0, 2 },  y_step_size = 4;
	const int uv_step[]= { 0, 0, 0, 4 }, uv_step_size = 4;
	unsigned char y[2], u, v;	/* YUV components */
	int y_, u_, v_, vb, uvg, ur;
	int r_, g_, b_;			/* RGB components */
	unsigned char g;
	int clipmask_even_index, clipmask_odd_index, bytes_per_pixel;
	int clipmask_add, stretch_bytes;

	frame  = usbvision->curFrame;
	f_even = frame->data + (frame->v4l2_linesize * frame->curline);
	f_odd  = f_even + frame->v4l2_linesize * usbvision->stretch_height;

	/* Make sure there's enough data for the entire line */
	/* In this mode usbvision transfer 3 bytes for every 2 pixels */
	/* I need two lines to decode the color */
	bytes_per_pixel = frame->v4l2_format.bytes_per_pixel;
	stretch_bytes = (usbvision->stretch_width - 1) * bytes_per_pixel;
	clipmask_even_index = frame->curline * MAX_FRAME_WIDTH;
	clipmask_odd_index  = clipmask_even_index + MAX_FRAME_WIDTH;
	clipmask_add = usbvision->stretch_width;
	pixel_per_line = frame->isocHeader.frameWidth;

	if (scratch_len(usbvision) < (int)pixel_per_line * 3) {
		//printk(KERN_DEBUG "out of data, need %d\n", len);
		return ParseState_Out;
	}

	if ((frame->curline + 1) >= frame->frmheight) {
		return ParseState_NextFrame;
	}

	block_split = (pixel_per_line%y_block_size) ? 1 : 0;	//are some blocks splitted into different lines?

	y_odd_offset = (pixel_per_line / y_block_size) * (y_block_size + uv_block_size)
			+ block_split * uv_block_size;

	scratch_set_extra_ptr(usbvision, &y_ptr, y_odd_offset);
	scratch_set_extra_ptr(usbvision, &u_ptr, y_block_size);
	scratch_set_extra_ptr(usbvision, &v_ptr, y_odd_offset
			+ (4 - block_split) * sub_block_size);

	for (block = 0; block < (pixel_per_line / sub_block_size);
	     block++) {


		for (pixel = 0; pixel < sub_block_size; pixel +=2) {
			scratch_get(usbvision, &y[0], 2);
			scratch_get_extra(usbvision, &u, &u_ptr, 1);
			scratch_get_extra(usbvision, &v, &v_ptr, 1);

			//I don't use the YUV_TO_RGB macro for better performance
			v_ = v - 128;
			u_ = u - 128;
			vb =              132252 * v_;
			uvg= -53281 * u_ - 25625 * v_;
			ur = 104595 * u_;

			if(frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
				*f_even++ = y[0];
				*f_even++ = v;
			}
			else {
				y_ = 76284 * (y[0] - 16);

				b_ = (y_ + vb) >> 16;
				g_ = (y_ + uvg)>> 16;
				r_ = (y_ + ur) >> 16;

				switch (frame->v4l2_format.format) {
					case V4L2_PIX_FMT_RGB565:
						g = LIMIT_RGB(g_);
						*f_even++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 3));
						*f_even++ = (0x07 & (          g   >> 5)) | (0xF8 & LIMIT_RGB(r_));
						break;
					case V4L2_PIX_FMT_RGB24:
						*f_even++ = LIMIT_RGB(b_);
						*f_even++ = LIMIT_RGB(g_);
						*f_even++ = LIMIT_RGB(r_);
						break;
					case V4L2_PIX_FMT_RGB32:
						*f_even++ = LIMIT_RGB(b_);
						*f_even++ = LIMIT_RGB(g_);
						*f_even++ = LIMIT_RGB(r_);
						f_even++;
						break;
					case V4L2_PIX_FMT_RGB555:
						g = LIMIT_RGB(g_);
						*f_even++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 2));
						*f_even++ = (0x03 & (          g   >> 6)) |
							    (0x7C & (LIMIT_RGB(r_) >> 1));
						break;
				}
			}
			clipmask_even_index += clipmask_add;
			f_even += stretch_bytes;

			if(frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
				*f_even++ = y[1];
				*f_even++ = u;
			}
			else {
				y_ = 76284 * (y[1] - 16);

				b_ = (y_ + vb) >> 16;
				g_ = (y_ + uvg)>> 16;
				r_ = (y_ + ur) >> 16;

				switch (frame->v4l2_format.format) {
					case V4L2_PIX_FMT_RGB565:
						g = LIMIT_RGB(g_);
						*f_even++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 3));
						*f_even++ = (0x07 & (          g   >> 5)) | (0xF8 & LIMIT_RGB(r_));
						break;
					case V4L2_PIX_FMT_RGB24:
						*f_even++ = LIMIT_RGB(b_);
						*f_even++ = LIMIT_RGB(g_);
						*f_even++ = LIMIT_RGB(r_);
						break;
					case V4L2_PIX_FMT_RGB32:
						*f_even++ = LIMIT_RGB(b_);
						*f_even++ = LIMIT_RGB(g_);
						*f_even++ = LIMIT_RGB(r_);
						f_even++;
						break;
					case V4L2_PIX_FMT_RGB555:
						g = LIMIT_RGB(g_);
						*f_even++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 2));
						*f_even++ = (0x03 & (          g   >> 6)) |
							    (0x7C & (LIMIT_RGB(r_) >> 1));
						break;
				}
			}
			clipmask_even_index += clipmask_add;
			f_even += stretch_bytes;

			scratch_get_extra(usbvision, &y[0], &y_ptr, 2);

			if(frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
				*f_odd++ = y[0];
				*f_odd++ = v;
			}
			else {
				y_ = 76284 * (y[0] - 16);

				b_ = (y_ + vb) >> 16;
				g_ = (y_ + uvg)>> 16;
				r_ = (y_ + ur) >> 16;

				switch (frame->v4l2_format.format) {
					case V4L2_PIX_FMT_RGB565:
						g = LIMIT_RGB(g_);
						*f_odd++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 3));
						*f_odd++ = (0x07 & (          g   >> 5)) | (0xF8 & LIMIT_RGB(r_));
						break;
					case V4L2_PIX_FMT_RGB24:
						*f_odd++ = LIMIT_RGB(b_);
						*f_odd++ = LIMIT_RGB(g_);
						*f_odd++ = LIMIT_RGB(r_);
						break;
					case V4L2_PIX_FMT_RGB32:
						*f_odd++ = LIMIT_RGB(b_);
						*f_odd++ = LIMIT_RGB(g_);
						*f_odd++ = LIMIT_RGB(r_);
						f_odd++;
						break;
					case V4L2_PIX_FMT_RGB555:
						g = LIMIT_RGB(g_);
						*f_odd++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 2));
						*f_odd++ = (0x03 & (          g   >> 6)) |
							   (0x7C & (LIMIT_RGB(r_) >> 1));
						break;
				}
			}
			clipmask_odd_index += clipmask_add;
			f_odd += stretch_bytes;

			if(frame->v4l2_format.format == V4L2_PIX_FMT_YUYV) {
				*f_odd++ = y[1];
				*f_odd++ = u;
			}
			else {
				y_ = 76284 * (y[1] - 16);

				b_ = (y_ + vb) >> 16;
				g_ = (y_ + uvg)>> 16;
				r_ = (y_ + ur) >> 16;

				switch (frame->v4l2_format.format) {
					case V4L2_PIX_FMT_RGB565:
						g = LIMIT_RGB(g_);
						*f_odd++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 3));
						*f_odd++ = (0x07 & (          g   >> 5)) | (0xF8 & LIMIT_RGB(r_));
						break;
					case V4L2_PIX_FMT_RGB24:
						*f_odd++ = LIMIT_RGB(b_);
						*f_odd++ = LIMIT_RGB(g_);
						*f_odd++ = LIMIT_RGB(r_);
						break;
					case V4L2_PIX_FMT_RGB32:
						*f_odd++ = LIMIT_RGB(b_);
						*f_odd++ = LIMIT_RGB(g_);
						*f_odd++ = LIMIT_RGB(r_);
						f_odd++;
						break;
					case V4L2_PIX_FMT_RGB555:
						g = LIMIT_RGB(g_);
						*f_odd++ = (0x1F & (LIMIT_RGB(b_) >> 3)) | (0xE0 & (g << 2));
						*f_odd++ = (0x03 & (          g   >> 6)) |
							   (0x7C & (LIMIT_RGB(r_) >> 1));
						break;
				}
			}
			clipmask_odd_index += clipmask_add;
			f_odd += stretch_bytes;
		}

		scratch_rm_old(usbvision,y_step[block % y_step_size] * sub_block_size);
		scratch_inc_extra_ptr(&y_ptr, y_step[(block + 2 * block_split) % y_step_size]
				* sub_block_size);
		scratch_inc_extra_ptr(&u_ptr, uv_step[block % uv_step_size]
				* sub_block_size);
		scratch_inc_extra_ptr(&v_ptr, uv_step[(block + 2 * block_split) % uv_step_size]
				* sub_block_size);
	}

	scratch_rm_old(usbvision, pixel_per_line * 3 / 2
			+ block_split * sub_block_size);

	frame->curline += 2 * usbvision->stretch_height;
	*pcopylen += frame->v4l2_linesize * 2 * usbvision->stretch_height;

	if (frame->curline >= frame->frmheight)
		return ParseState_NextFrame;
	else
		return ParseState_Continue;
}

/*
 * usbvision_parse_data()
 *
 * Generic routine to parse the scratch buffer. It employs either
 * usbvision_find_header() or usbvision_parse_lines() to do most
 * of work.
 *
 */
static void usbvision_parse_data(struct usb_usbvision *usbvision)
{
	struct usbvision_frame *frame;
	enum ParseState newstate;
	long copylen = 0;
	unsigned long lock_flags;

	frame = usbvision->curFrame;

	PDEBUG(DBG_PARSE, "parsing len=%d\n", scratch_len(usbvision));

	while (1) {

		newstate = ParseState_Out;
		if (scratch_len(usbvision)) {
			if (frame->scanstate == ScanState_Scanning) {
				newstate = usbvision_find_header(usbvision);
			}
			else if (frame->scanstate == ScanState_Lines) {
				if (usbvision->isocMode == ISOC_MODE_YUV420) {
					newstate = usbvision_parse_lines_420(usbvision, &copylen);
				}
				else if (usbvision->isocMode == ISOC_MODE_YUV422) {
					newstate = usbvision_parse_lines_422(usbvision, &copylen);
				}
				else if (usbvision->isocMode == ISOC_MODE_COMPRESS) {
					newstate = usbvision_parse_compress(usbvision, &copylen);
				}

			}
		}
		if (newstate == ParseState_Continue) {
			continue;
		}
		else if ((newstate == ParseState_NextFrame) || (newstate == ParseState_Out)) {
			break;
		}
		else {
			return;	/* ParseState_EndParse */
		}
	}

	if (newstate == ParseState_NextFrame) {
		frame->grabstate = FrameState_Done;
		do_gettimeofday(&(frame->timestamp));
		frame->sequence = usbvision->frame_num;

		spin_lock_irqsave(&usbvision->queue_lock, lock_flags);
		list_move_tail(&(frame->frame), &usbvision->outqueue);
		usbvision->curFrame = NULL;
		spin_unlock_irqrestore(&usbvision->queue_lock, lock_flags);

		usbvision->frame_num++;

		/* This will cause the process to request another frame. */
		if (waitqueue_active(&usbvision->wait_frame)) {
			PDEBUG(DBG_PARSE, "Wake up !");
			wake_up_interruptible(&usbvision->wait_frame);
		}
	}
	else
		frame->grabstate = FrameState_Grabbing;


	/* Update the frame's uncompressed length. */
	frame->scanlength += copylen;
}


/*
 * Make all of the blocks of data contiguous
 */
static int usbvision_compress_isochronous(struct usb_usbvision *usbvision,
					  struct urb *urb)
{
	unsigned char *packet_data;
	int i, totlen = 0;

	for (i = 0; i < urb->number_of_packets; i++) {
		int packet_len = urb->iso_frame_desc[i].actual_length;
		int packet_stat = urb->iso_frame_desc[i].status;

		packet_data = urb->transfer_buffer + urb->iso_frame_desc[i].offset;

		/* Detect and ignore errored packets */
		if (packet_stat) {	// packet_stat != 0 ?????????????
			PDEBUG(DBG_ISOC, "data error: [%d] len=%d, status=%X", i, packet_len, packet_stat);
			usbvision->isocErrCount++;
			continue;
		}

		/* Detect and ignore empty packets */
		if (packet_len < 0) {
			PDEBUG(DBG_ISOC, "error packet [%d]", i);
			usbvision->isocSkipCount++;
			continue;
		}
		else if (packet_len == 0) {	/* Frame end ????? */
			PDEBUG(DBG_ISOC, "null packet [%d]", i);
			usbvision->isocstate=IsocState_NoFrame;
			usbvision->isocSkipCount++;
			continue;
		}
		else if (packet_len > usbvision->isocPacketSize) {
			PDEBUG(DBG_ISOC, "packet[%d] > isocPacketSize", i);
			usbvision->isocSkipCount++;
			continue;
		}

		PDEBUG(DBG_ISOC, "packet ok [%d] len=%d", i, packet_len);

		if (usbvision->isocstate==IsocState_NoFrame) { //new frame begins
			usbvision->isocstate=IsocState_InFrame;
			scratch_mark_header(usbvision);
			usbvision_measure_bandwidth(usbvision);
			PDEBUG(DBG_ISOC, "packet with header");
		}

		/*
		 * If usbvision continues to feed us with data but there is no
		 * consumption (if, for example, V4L client fell asleep) we
		 * may overflow the buffer. We have to move old data over to
		 * free room for new data. This is bad for old data. If we
		 * just drop new data then it's bad for new data... choose
		 * your favorite evil here.
		 */
		if (scratch_free(usbvision) < packet_len) {

			usbvision->scratch_ovf_count++;
			PDEBUG(DBG_ISOC, "scratch buf overflow! scr_len: %d, n: %d",
			       scratch_len(usbvision), packet_len);
			scratch_rm_old(usbvision, packet_len - scratch_free(usbvision));
		}

		/* Now we know that there is enough room in scratch buffer */
		scratch_put(usbvision, packet_data, packet_len);
		totlen += packet_len;
		usbvision->isocDataCount += packet_len;
		usbvision->isocPacketCount++;
	}
#if ENABLE_HEXDUMP
	if (totlen > 0) {
		static int foo = 0;
		if (foo < 1) {
			printk(KERN_DEBUG "+%d.\n", usbvision->scratchlen);
			usbvision_hexdump(data0, (totlen > 64) ? 64 : totlen);
			++foo;
		}
	}
#endif
 return totlen;
}

static void usbvision_isocIrq(struct urb *urb)
{
	int errCode = 0;
	int len;
	struct usb_usbvision *usbvision = urb->context;
	int i;
	unsigned long startTime = jiffies;
	struct usbvision_frame **f;

	/* We don't want to do anything if we are about to be removed! */
	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return;

	f = &usbvision->curFrame;

	/* Manage streaming interruption */
	if (usbvision->streaming == Stream_Interrupt) {
		usbvision->streaming = Stream_Idle;
		if ((*f)) {
			(*f)->grabstate = FrameState_Ready;
			(*f)->scanstate = ScanState_Scanning;
		}
		PDEBUG(DBG_IRQ, "stream interrupted");
		wake_up_interruptible(&usbvision->wait_stream);
	}

	/* Copy the data received into our scratch buffer */
	len = usbvision_compress_isochronous(usbvision, urb);

	usbvision->isocUrbCount++;
	usbvision->urb_length = len;

	if (usbvision->streaming == Stream_On) {

		/* If we collected enough data let's parse! */
		if (scratch_len(usbvision) > USBVISION_HEADER_LENGTH) {	/* 12 == header_length */
			/*If we don't have a frame we're current working on, complain */
			if(!list_empty(&(usbvision->inqueue))) {
				if (!(*f)) {
					(*f) = list_entry(usbvision->inqueue.next,struct usbvision_frame, frame);
				}
				usbvision_parse_data(usbvision);
			}
			else {
				PDEBUG(DBG_IRQ, "received data, but no one needs it");
				scratch_reset(usbvision);
			}
		}
	}
	else {
		PDEBUG(DBG_IRQ, "received data, but no one needs it");
		scratch_reset(usbvision);
	}

	usbvision->timeInIrq += jiffies - startTime;

	for (i = 0; i < USBVISION_URB_FRAMES; i++) {
		urb->iso_frame_desc[i].status = 0;
		urb->iso_frame_desc[i].actual_length = 0;
	}

	urb->status = 0;
	urb->dev = usbvision->dev;
	errCode = usb_submit_urb (urb, GFP_ATOMIC);

	/* Disable this warning.  By design of the driver. */
	//	if(errCode) {
	//		err("%s: usb_submit_urb failed: error %d", __FUNCTION__, errCode);
	//	}

	return;
}

/*************************************/
/* Low level usbvision access functions */
/*************************************/

/*
 * usbvision_read_reg()
 *
 * return  < 0 -> Error
 *        >= 0 -> Data
 */

int usbvision_read_reg(struct usb_usbvision *usbvision, unsigned char reg)
{
	int errCode = 0;
	unsigned char buffer[1];

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return -1;

	errCode = usb_control_msg(usbvision->dev, usb_rcvctrlpipe(usbvision->dev, 1),
				USBVISION_OP_CODE,
				USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
				0, (__u16) reg, buffer, 1, HZ);

	if (errCode < 0) {
		err("%s: failed: error %d", __FUNCTION__, errCode);
		return errCode;
	}
	return buffer[0];
}

/*
 * usbvision_write_reg()
 *
 * return 1 -> Reg written
 *        0 -> usbvision is not yet ready
 *       -1 -> Something went wrong
 */

int usbvision_write_reg(struct usb_usbvision *usbvision, unsigned char reg,
			    unsigned char value)
{
	int errCode = 0;

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return 0;

	errCode = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
				USBVISION_OP_CODE,
				USB_DIR_OUT | USB_TYPE_VENDOR |
				USB_RECIP_ENDPOINT, 0, (__u16) reg, &value, 1, HZ);

	if (errCode < 0) {
		err("%s: failed: error %d", __FUNCTION__, errCode);
	}
	return errCode;
}


static void usbvision_ctrlUrb_complete(struct urb *urb)
{
	struct usb_usbvision *usbvision = (struct usb_usbvision *)urb->context;

	PDEBUG(DBG_IRQ, "");
	usbvision->ctrlUrbBusy = 0;
	if (waitqueue_active(&usbvision->ctrlUrb_wq)) {
		wake_up_interruptible(&usbvision->ctrlUrb_wq);
	}
}


static int usbvision_write_reg_irq(struct usb_usbvision *usbvision,int address,
									unsigned char *data, int len)
{
	int errCode = 0;

	PDEBUG(DBG_IRQ, "");
	if (len > 8) {
		return -EFAULT;
	}
//	down(&usbvision->ctrlUrbLock);
	if (usbvision->ctrlUrbBusy) {
//		up(&usbvision->ctrlUrbLock);
		return -EBUSY;
	}
	usbvision->ctrlUrbBusy = 1;
//	up(&usbvision->ctrlUrbLock);

	usbvision->ctrlUrbSetup.bRequestType = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT;
	usbvision->ctrlUrbSetup.bRequest     = USBVISION_OP_CODE;
	usbvision->ctrlUrbSetup.wValue       = 0;
	usbvision->ctrlUrbSetup.wIndex       = cpu_to_le16(address);
	usbvision->ctrlUrbSetup.wLength      = cpu_to_le16(len);
	usb_fill_control_urb (usbvision->ctrlUrb, usbvision->dev,
							usb_sndctrlpipe(usbvision->dev, 1),
							(unsigned char *)&usbvision->ctrlUrbSetup,
							(void *)usbvision->ctrlUrbBuffer, len,
							usbvision_ctrlUrb_complete,
							(void *)usbvision);

	memcpy(usbvision->ctrlUrbBuffer, data, len);

	errCode = usb_submit_urb(usbvision->ctrlUrb, GFP_ATOMIC);
	if (errCode < 0) {
		// error in usb_submit_urb()
		usbvision->ctrlUrbBusy = 0;
	}
	PDEBUG(DBG_IRQ, "submit %d byte: error %d", len, errCode);
	return errCode;
}


static int usbvision_init_compression(struct usb_usbvision *usbvision)
{
	int errCode = 0;

	usbvision->lastIsocFrameNum = -1;
	usbvision->isocDataCount = 0;
	usbvision->isocPacketCount = 0;
	usbvision->isocSkipCount = 0;
	usbvision->comprLevel = 50;
	usbvision->lastComprLevel = -1;
	usbvision->isocUrbCount = 0;
	usbvision->requestIntra = 1;
	usbvision->isocMeasureBandwidthCount = 0;

	return errCode;
}

/* this function measures the used bandwidth since last call
 * return:    0 : no error
 * sets usedBandwidth to 1-100 : 1-100% of full bandwidth resp. to isocPacketSize
 */
static int usbvision_measure_bandwidth (struct usb_usbvision *usbvision)
{
	int errCode = 0;

	if (usbvision->isocMeasureBandwidthCount < 2) { // this gives an average bandwidth of 3 frames
		usbvision->isocMeasureBandwidthCount++;
		return errCode;
	}
	if ((usbvision->isocPacketSize > 0) && (usbvision->isocPacketCount > 0)) {
		usbvision->usedBandwidth = usbvision->isocDataCount /
					(usbvision->isocPacketCount + usbvision->isocSkipCount) *
					100 / usbvision->isocPacketSize;
	}
	usbvision->isocMeasureBandwidthCount = 0;
	usbvision->isocDataCount = 0;
	usbvision->isocPacketCount = 0;
	usbvision->isocSkipCount = 0;
	return errCode;
}

static int usbvision_adjust_compression (struct usb_usbvision *usbvision)
{
	int errCode = 0;
	unsigned char buffer[6];

	PDEBUG(DBG_IRQ, "");
	if ((adjustCompression) && (usbvision->usedBandwidth > 0)) {
		usbvision->comprLevel += (usbvision->usedBandwidth - 90) / 2;
		RESTRICT_TO_RANGE(usbvision->comprLevel, 0, 100);
		if (usbvision->comprLevel != usbvision->lastComprLevel) {
			int distorsion;
			if (usbvision->bridgeType == BRIDGE_NT1004 || usbvision->bridgeType == BRIDGE_NT1005) {
				buffer[0] = (unsigned char)(4 + 16 * usbvision->comprLevel / 100);	// PCM Threshold 1
				buffer[1] = (unsigned char)(4 + 8 * usbvision->comprLevel / 100);	// PCM Threshold 2
				distorsion = 7 + 248 * usbvision->comprLevel / 100;
				buffer[2] = (unsigned char)(distorsion & 0xFF);				// Average distorsion Threshold (inter)
				buffer[3] = (unsigned char)(distorsion & 0xFF);				// Average distorsion Threshold (intra)
				distorsion = 1 + 42 * usbvision->comprLevel / 100;
				buffer[4] = (unsigned char)(distorsion & 0xFF);				// Maximum distorsion Threshold (inter)
				buffer[5] = (unsigned char)(distorsion & 0xFF);				// Maximum distorsion Threshold (intra)
			}
			else { //BRIDGE_NT1003
				buffer[0] = (unsigned char)(4 + 16 * usbvision->comprLevel / 100);	// PCM threshold 1
				buffer[1] = (unsigned char)(4 + 8 * usbvision->comprLevel / 100);	// PCM threshold 2
				distorsion = 2 + 253 * usbvision->comprLevel / 100;
				buffer[2] = (unsigned char)(distorsion & 0xFF);				// distorsion threshold bit0-7
				buffer[3] = 0; 	//(unsigned char)((distorsion >> 8) & 0x0F);		// distorsion threshold bit 8-11
				distorsion = 0 + 43 * usbvision->comprLevel / 100;
				buffer[4] = (unsigned char)(distorsion & 0xFF);				// maximum distorsion bit0-7
				buffer[5] = 0; //(unsigned char)((distorsion >> 8) & 0x01);		// maximum distorsion bit 8
			}
			errCode = usbvision_write_reg_irq(usbvision, USBVISION_PCM_THR1, buffer, 6);
			if (errCode == 0){
				PDEBUG(DBG_IRQ, "new compr params %#02x %#02x %#02x %#02x %#02x %#02x", buffer[0],
								buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
				usbvision->lastComprLevel = usbvision->comprLevel;
			}
		}
	}
	return errCode;
}

static int usbvision_request_intra (struct usb_usbvision *usbvision)
{
	int errCode = 0;
	unsigned char buffer[1];

	PDEBUG(DBG_IRQ, "");
	usbvision->requestIntra = 1;
	buffer[0] = 1;
	usbvision_write_reg_irq(usbvision, USBVISION_FORCE_INTRA, buffer, 1);
	return errCode;
}

static int usbvision_unrequest_intra (struct usb_usbvision *usbvision)
{
	int errCode = 0;
	unsigned char buffer[1];

	PDEBUG(DBG_IRQ, "");
	usbvision->requestIntra = 0;
	buffer[0] = 0;
	usbvision_write_reg_irq(usbvision, USBVISION_FORCE_INTRA, buffer, 1);
	return errCode;
}

/*******************************
 * usbvision utility functions
 *******************************/

int usbvision_power_off(struct usb_usbvision *usbvision)
{
	int errCode = 0;

	PDEBUG(DBG_FUNC, "");

	errCode = usbvision_write_reg(usbvision, USBVISION_PWR_REG, USBVISION_SSPND_EN);
	if (errCode == 1) {
		usbvision->power = 0;
	}
	PDEBUG(DBG_FUNC, "%s: errCode %d", (errCode!=1)?"ERROR":"power is off", errCode);
	return errCode;
}

/*
 * usbvision_set_video_format()
 *
 */
static int usbvision_set_video_format(struct usb_usbvision *usbvision, int format)
{
	static const char proc[] = "usbvision_set_video_format";
	int rc;
	unsigned char value[2];

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return 0;

	PDEBUG(DBG_FUNC, "isocMode %#02x", format);

	if ((format != ISOC_MODE_YUV422)
	    && (format != ISOC_MODE_YUV420)
	    && (format != ISOC_MODE_COMPRESS)) {
		printk(KERN_ERR "usbvision: unknown video format %02x, using default YUV420",
		       format);
		format = ISOC_MODE_YUV420;
	}
	value[0] = 0x0A;  //TODO: See the effect of the filter
	value[1] = format;
	rc = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
			     USBVISION_OP_CODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR |
			     USB_RECIP_ENDPOINT, 0,
			     (__u16) USBVISION_FILT_CONT, value, 2, HZ);

	if (rc < 0) {
		printk(KERN_ERR "%s: ERROR=%d. USBVISION stopped - "
		       "reconnect or reload driver.\n", proc, rc);
	}
	usbvision->isocMode = format;
	return rc;
}

/*
 * usbvision_set_output()
 *
 */

int usbvision_set_output(struct usb_usbvision *usbvision, int width,
			 int height)
{
	int errCode = 0;
	int UsbWidth, UsbHeight;
	unsigned int frameRate=0, frameDrop=0;
	unsigned char value[4];

	if (!USBVISION_IS_OPERATIONAL(usbvision)) {
		return 0;
	}

	if (width > MAX_USB_WIDTH) {
		UsbWidth = width / 2;
		usbvision->stretch_width = 2;
	}
	else {
		UsbWidth = width;
		usbvision->stretch_width = 1;
	}

	if (height > MAX_USB_HEIGHT) {
		UsbHeight = height / 2;
		usbvision->stretch_height = 2;
	}
	else {
		UsbHeight = height;
		usbvision->stretch_height = 1;
	}

	RESTRICT_TO_RANGE(UsbWidth, MIN_FRAME_WIDTH, MAX_USB_WIDTH);
	UsbWidth &= ~(MIN_FRAME_WIDTH-1);
	RESTRICT_TO_RANGE(UsbHeight, MIN_FRAME_HEIGHT, MAX_USB_HEIGHT);
	UsbHeight &= ~(1);

	PDEBUG(DBG_FUNC, "usb %dx%d; screen %dx%d; stretch %dx%d",
						UsbWidth, UsbHeight, width, height,
						usbvision->stretch_width, usbvision->stretch_height);

	/* I'll not rewrite the same values */
	if ((UsbWidth != usbvision->curwidth) || (UsbHeight != usbvision->curheight)) {
		value[0] = UsbWidth & 0xff;		//LSB
		value[1] = (UsbWidth >> 8) & 0x03;	//MSB
		value[2] = UsbHeight & 0xff;		//LSB
		value[3] = (UsbHeight >> 8) & 0x03;	//MSB

		errCode = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
			     USBVISION_OP_CODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT,
				 0, (__u16) USBVISION_LXSIZE_O, value, 4, HZ);

		if (errCode < 0) {
			err("%s failed: error %d", __FUNCTION__, errCode);
			return errCode;
		}
		usbvision->curwidth = usbvision->stretch_width * UsbWidth;
		usbvision->curheight = usbvision->stretch_height * UsbHeight;
	}

	if (usbvision->isocMode == ISOC_MODE_YUV422) {
		frameRate = (usbvision->isocPacketSize * 1000) / (UsbWidth * UsbHeight * 2);
	}
	else if (usbvision->isocMode == ISOC_MODE_YUV420) {
		frameRate = (usbvision->isocPacketSize * 1000) / ((UsbWidth * UsbHeight * 12) / 8);
	}
	else {
		frameRate = FRAMERATE_MAX;
	}

	if (usbvision->tvnorm->id & V4L2_STD_625_50) {
		frameDrop = frameRate * 32 / 25 - 1;
	}
	else if (usbvision->tvnorm->id & V4L2_STD_525_60) {
		frameDrop = frameRate * 32 / 30 - 1;
	}

	RESTRICT_TO_RANGE(frameDrop, FRAMERATE_MIN, FRAMERATE_MAX);

	PDEBUG(DBG_FUNC, "frameRate %d fps, frameDrop %d", frameRate, frameDrop);

	frameDrop = FRAMERATE_MAX; 	// We can allow the maximum here, because dropping is controlled

	/* frameDrop = 7; => framePhase = 1, 5, 9, 13, 17, 21, 25, 0, 4, 8, ...
		=> frameSkip = 4;
		=> frameRate = (7 + 1) * 25 / 32 = 200 / 32 = 6.25;

	   frameDrop = 9; => framePhase = 1, 5, 8, 11, 14, 17, 21, 24, 27, 1, 4, 8, ...
	    => frameSkip = 4, 3, 3, 3, 3, 4, 3, 3, 3, 3, 4, ...
		=> frameRate = (9 + 1) * 25 / 32 = 250 / 32 = 7.8125;
	*/
	errCode = usbvision_write_reg(usbvision, USBVISION_FRM_RATE, frameDrop);
	return errCode;
}


/*
 * usbvision_frames_alloc
 * allocate the maximum frames this driver can manage
 */
int usbvision_frames_alloc(struct usb_usbvision *usbvision)
{
	int i;

	/* Allocate memory for the frame buffers */
	usbvision->max_frame_size = MAX_FRAME_SIZE;
	usbvision->fbuf_size = USBVISION_NUMFRAMES * usbvision->max_frame_size;
	usbvision->fbuf = usbvision_rvmalloc(usbvision->fbuf_size);

	if(usbvision->fbuf == NULL) {
		err("%s: unable to allocate %d bytes for fbuf ",
		    __FUNCTION__, usbvision->fbuf_size);
		return -ENOMEM;
	}
	spin_lock_init(&usbvision->queue_lock);
	init_waitqueue_head(&usbvision->wait_frame);
	init_waitqueue_head(&usbvision->wait_stream);

	/* Allocate all buffers */
	for (i = 0; i < USBVISION_NUMFRAMES; i++) {
		usbvision->frame[i].index = i;
		usbvision->frame[i].grabstate = FrameState_Unused;
		usbvision->frame[i].data = usbvision->fbuf +
			i * usbvision->max_frame_size;
		/*
		 * Set default sizes for read operation.
		 */
		usbvision->stretch_width = 1;
		usbvision->stretch_height = 1;
		usbvision->frame[i].width = usbvision->curwidth;
		usbvision->frame[i].height = usbvision->curheight;
		usbvision->frame[i].bytes_read = 0;
	}
	return 0;
}

/*
 * usbvision_frames_free
 * frees memory allocated for the frames
 */
void usbvision_frames_free(struct usb_usbvision *usbvision)
{
	/* Have to free all that memory */
	if (usbvision->fbuf != NULL) {
		usbvision_rvfree(usbvision->fbuf, usbvision->fbuf_size);
		usbvision->fbuf = NULL;
	}
}
/*
 * usbvision_empty_framequeues()
 * prepare queues for incoming and outgoing frames
 */
void usbvision_empty_framequeues(struct usb_usbvision *usbvision)
{
	u32 i;

	INIT_LIST_HEAD(&(usbvision->inqueue));
	INIT_LIST_HEAD(&(usbvision->outqueue));

	for (i = 0; i < USBVISION_NUMFRAMES; i++) {
		usbvision->frame[i].grabstate = FrameState_Unused;
		usbvision->frame[i].bytes_read = 0;
	}
}

/*
 * usbvision_stream_interrupt()
 * stops streaming
 */
int usbvision_stream_interrupt(struct usb_usbvision *usbvision)
{
	int ret = 0;

	/* stop reading from the device */

	usbvision->streaming = Stream_Interrupt;
	ret = wait_event_timeout(usbvision->wait_stream,
				 (usbvision->streaming == Stream_Idle),
				 msecs_to_jiffies(USBVISION_NUMSBUF*USBVISION_URB_FRAMES));
	return ret;
}

/*
 * usbvision_set_compress_params()
 *
 */

static int usbvision_set_compress_params(struct usb_usbvision *usbvision)
{
	static const char proc[] = "usbvision_set_compresion_params: ";
	int rc;
	unsigned char value[6];

	value[0] = 0x0F;    // Intra-Compression cycle
	value[1] = 0x01;    // Reg.45 one line per strip
	value[2] = 0x00;    // Reg.46 Force intra mode on all new frames
	value[3] = 0x00;    // Reg.47 FORCE_UP <- 0 normal operation (not force)
	value[4] = 0xA2;    // Reg.48 BUF_THR I'm not sure if this does something in not compressed mode.
	value[5] = 0x00;    // Reg.49 DVI_YUV This has nothing to do with compression

	//catched values for NT1004
	// value[0] = 0xFF; // Never apply intra mode automatically
	// value[1] = 0xF1; // Use full frame height for virtual strip width; One line per strip
	// value[2] = 0x01; // Force intra mode on all new frames
	// value[3] = 0x00; // Strip size 400 Bytes; do not force up
	// value[4] = 0xA2; //
	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return 0;

	rc = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
			     USBVISION_OP_CODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR |
			     USB_RECIP_ENDPOINT, 0,
			     (__u16) USBVISION_INTRA_CYC, value, 5, HZ);

	if (rc < 0) {
		printk(KERN_ERR "%sERROR=%d. USBVISION stopped - "
		       "reconnect or reload driver.\n", proc, rc);
		return rc;
	}

	if (usbvision->bridgeType == BRIDGE_NT1004) {
		value[0] =  20; // PCM Threshold 1
		value[1] =  12; // PCM Threshold 2
		value[2] = 255; // Distorsion Threshold inter
		value[3] = 255; // Distorsion Threshold intra
		value[4] =  43; // Max Distorsion inter
		value[5] =  43; // Max Distorsion intra
	}
	else {
		value[0] =  20; // PCM Threshold 1
		value[1] =  12; // PCM Threshold 2
		value[2] = 255; // Distorsion Threshold d7-d0
		value[3] =   0; // Distorsion Threshold d11-d8
		value[4] =  43; // Max Distorsion d7-d0
		value[5] =   0; // Max Distorsion d8
	}

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return 0;

	rc = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
			     USBVISION_OP_CODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR |
			     USB_RECIP_ENDPOINT, 0,
			     (__u16) USBVISION_PCM_THR1, value, 6, HZ);

	if (rc < 0) {
		printk(KERN_ERR "%sERROR=%d. USBVISION stopped - "
		       "reconnect or reload driver.\n", proc, rc);
		return rc;
	}


	return rc;
}


/*
 * usbvision_set_input()
 *
 * Set the input (saa711x, ...) size x y and other misc input params
 * I've no idea if this parameters are right
 *
 */
int usbvision_set_input(struct usb_usbvision *usbvision)
{
	static const char proc[] = "usbvision_set_input: ";
	int rc;
	unsigned char value[8];
	unsigned char dvi_yuv_value;

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return 0;

	/* Set input format expected from decoder*/
	if (usbvision_device_data[usbvision->DevModel].Vin_Reg1 >= 0) {
		value[0] = usbvision_device_data[usbvision->DevModel].Vin_Reg1 & 0xff;
	} else if(usbvision_device_data[usbvision->DevModel].Codec == CODEC_SAA7113) {
		/* SAA7113 uses 8 bit output */
		value[0] = USBVISION_8_422_SYNC;
	} else {
		/* I'm sure only about d2-d0 [010] 16 bit 4:2:2 usin sync pulses
		 * as that is how saa7111 is configured */
		value[0] = USBVISION_16_422_SYNC;
		/* | USBVISION_VSNC_POL | USBVISION_VCLK_POL);*/
	}

	rc = usbvision_write_reg(usbvision, USBVISION_VIN_REG1, value[0]);
	if (rc < 0) {
		printk(KERN_ERR "%sERROR=%d. USBVISION stopped - "
		       "reconnect or reload driver.\n", proc, rc);
		return rc;
	}


	if (usbvision->tvnorm->id & V4L2_STD_PAL) {
		value[0] = 0xC0;
		value[1] = 0x02;	//0x02C0 -> 704 Input video line length
		value[2] = 0x20;
		value[3] = 0x01;	//0x0120 -> 288 Input video n. of lines
		value[4] = 0x60;
		value[5] = 0x00;	//0x0060 -> 96 Input video h offset
		value[6] = 0x16;
		value[7] = 0x00;	//0x0016 -> 22 Input video v offset
	} else if (usbvision->tvnorm->id & V4L2_STD_SECAM) {
		value[0] = 0xC0;
		value[1] = 0x02;	//0x02C0 -> 704 Input video line length
		value[2] = 0x20;
		value[3] = 0x01;	//0x0120 -> 288 Input video n. of lines
		value[4] = 0x01;
		value[5] = 0x00;	//0x0001 -> 01 Input video h offset
		value[6] = 0x01;
		value[7] = 0x00;	//0x0001 -> 01 Input video v offset
	} else {	/* V4L2_STD_NTSC */
		value[0] = 0xD0;
		value[1] = 0x02;	//0x02D0 -> 720 Input video line length
		value[2] = 0xF0;
		value[3] = 0x00;	//0x00F0 -> 240 Input video number of lines
		value[4] = 0x50;
		value[5] = 0x00;	//0x0050 -> 80 Input video h offset
		value[6] = 0x10;
		value[7] = 0x00;	//0x0010 -> 16 Input video v offset
	}

	if (usbvision_device_data[usbvision->DevModel].X_Offset >= 0) {
		value[4]=usbvision_device_data[usbvision->DevModel].X_Offset & 0xff;
		value[5]=(usbvision_device_data[usbvision->DevModel].X_Offset & 0x0300) >> 8;
	}

	if (usbvision_device_data[usbvision->DevModel].Y_Offset >= 0) {
		value[6]=usbvision_device_data[usbvision->DevModel].Y_Offset & 0xff;
		value[7]=(usbvision_device_data[usbvision->DevModel].Y_Offset & 0x0300) >> 8;
	}

	rc = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
			     USBVISION_OP_CODE,	/* USBVISION specific code */
			     USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_ENDPOINT, 0,
			     (__u16) USBVISION_LXSIZE_I, value, 8, HZ);
	if (rc < 0) {
		printk(KERN_ERR "%sERROR=%d. USBVISION stopped - "
		       "reconnect or reload driver.\n", proc, rc);
		return rc;
	}


	dvi_yuv_value = 0x00;	/* U comes after V, Ya comes after U/V, Yb comes after Yb */

	if(usbvision_device_data[usbvision->DevModel].Dvi_yuv >= 0){
		dvi_yuv_value = usbvision_device_data[usbvision->DevModel].Dvi_yuv & 0xff;
	}
	else if(usbvision_device_data[usbvision->DevModel].Codec == CODEC_SAA7113) {
	/* This changes as the fine sync control changes. Further investigation necessary */
		dvi_yuv_value = 0x06;
	}

	return (usbvision_write_reg(usbvision, USBVISION_DVI_YUV, dvi_yuv_value));
}


/*
 * usbvision_set_dram_settings()
 *
 * Set the buffer address needed by the usbvision dram to operate
 * This values has been taken with usbsnoop.
 *
 */

static int usbvision_set_dram_settings(struct usb_usbvision *usbvision)
{
	int rc;
	unsigned char value[8];

	if (usbvision->isocMode == ISOC_MODE_COMPRESS) {
		value[0] = 0x42;
		value[1] = 0x71;
		value[2] = 0xff;
		value[3] = 0x00;
		value[4] = 0x98;
		value[5] = 0xe0;
		value[6] = 0x71;
		value[7] = 0xff;
		// UR:  0x0E200-0x3FFFF = 204288 Words (1 Word = 2 Byte)
		// FDL: 0x00000-0x0E099 =  57498 Words
		// VDW: 0x0E3FF-0x3FFFF
	}
	else {
		value[0] = 0x42;
		value[1] = 0x00;
		value[2] = 0xff;
		value[3] = 0x00;
		value[4] = 0x00;
		value[5] = 0x00;
		value[6] = 0x00;
		value[7] = 0xff;
	}
	/* These are the values of the address of the video buffer,
	 * they have to be loaded into the USBVISION_DRM_PRM1-8
	 *
	 * Start address of video output buffer for read: 	drm_prm1-2 -> 0x00000
	 * End address of video output buffer for read: 	drm_prm1-3 -> 0x1ffff
	 * Start address of video frame delay buffer: 		drm_prm1-4 -> 0x20000
	 *    Only used in compressed mode
	 * End address of video frame delay buffer: 		drm_prm1-5-6 -> 0x3ffff
	 *    Only used in compressed mode
	 * Start address of video output buffer for write: 	drm_prm1-7 -> 0x00000
	 * End address of video output buffer for write: 	drm_prm1-8 -> 0x1ffff
	 */

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return 0;

	rc = usb_control_msg(usbvision->dev, usb_sndctrlpipe(usbvision->dev, 1),
			     USBVISION_OP_CODE,	/* USBVISION specific code */
			     USB_DIR_OUT | USB_TYPE_VENDOR |
			     USB_RECIP_ENDPOINT, 0,
			     (__u16) USBVISION_DRM_PRM1, value, 8, HZ);

	if (rc < 0) {
		err("%sERROR=%d", __FUNCTION__, rc);
		return rc;
	}

	/* Restart the video buffer logic */
	if ((rc = usbvision_write_reg(usbvision, USBVISION_DRM_CONT, USBVISION_RES_UR |
				   USBVISION_RES_FDL | USBVISION_RES_VDW)) < 0)
		return rc;
	rc = usbvision_write_reg(usbvision, USBVISION_DRM_CONT, 0x00);

	return rc;
}

/*
 * ()
 *
 * Power on the device, enables suspend-resume logic
 * &  reset the isoc End-Point
 *
 */

int usbvision_power_on(struct usb_usbvision *usbvision)
{
	int errCode = 0;

	PDEBUG(DBG_FUNC, "");

	usbvision_write_reg(usbvision, USBVISION_PWR_REG, USBVISION_SSPND_EN);
	usbvision_write_reg(usbvision, USBVISION_PWR_REG,
			 USBVISION_SSPND_EN | USBVISION_RES2);
	usbvision_write_reg(usbvision, USBVISION_PWR_REG,
			 USBVISION_SSPND_EN | USBVISION_PWR_VID);
	errCode = usbvision_write_reg(usbvision, USBVISION_PWR_REG,
						USBVISION_SSPND_EN | USBVISION_PWR_VID | USBVISION_RES2);
	if (errCode == 1) {
		usbvision->power = 1;
	}
	PDEBUG(DBG_FUNC, "%s: errCode %d", (errCode<0)?"ERROR":"power is on", errCode);
	return errCode;
}


/*
 * usbvision timer stuff
 */

// to call usbvision_power_off from task queue
static void call_usbvision_power_off(struct work_struct *work)
{
	struct usb_usbvision *usbvision = container_of(work, struct usb_usbvision, powerOffWork);

	PDEBUG(DBG_FUNC, "");
	down_interruptible(&usbvision->lock);
	if(usbvision->user == 0) {
		usbvision_i2c_usb_del_bus(&usbvision->i2c_adap);

		usbvision_power_off(usbvision);
		usbvision->initialized = 0;
	}
	up(&usbvision->lock);
}

static void usbvision_powerOffTimer(unsigned long data)
{
	struct usb_usbvision *usbvision = (void *) data;

	PDEBUG(DBG_FUNC, "");
	del_timer(&usbvision->powerOffTimer);
	INIT_WORK(&usbvision->powerOffWork, call_usbvision_power_off);
	(void) schedule_work(&usbvision->powerOffWork);

}

void usbvision_init_powerOffTimer(struct usb_usbvision *usbvision)
{
	init_timer(&usbvision->powerOffTimer);
	usbvision->powerOffTimer.data = (long) usbvision;
	usbvision->powerOffTimer.function = usbvision_powerOffTimer;
}

void usbvision_set_powerOffTimer(struct usb_usbvision *usbvision)
{
	mod_timer(&usbvision->powerOffTimer, jiffies + USBVISION_POWEROFF_TIME);
}

void usbvision_reset_powerOffTimer(struct usb_usbvision *usbvision)
{
	if (timer_pending(&usbvision->powerOffTimer)) {
		del_timer(&usbvision->powerOffTimer);
	}
}

/*
 * usbvision_begin_streaming()
 * Sure you have to put bit 7 to 0, if not incoming frames are droped, but no
 * idea about the rest
 */
int usbvision_begin_streaming(struct usb_usbvision *usbvision)
{
	int errCode = 0;

	if (usbvision->isocMode == ISOC_MODE_COMPRESS) {
		usbvision_init_compression(usbvision);
	}
	errCode = usbvision_write_reg(usbvision, USBVISION_VIN_REG2, USBVISION_NOHVALID |
										usbvision->Vin_Reg2_Preset);
	return errCode;
}

/*
 * usbvision_restart_isoc()
 * Not sure yet if touching here PWR_REG make loose the config
 */

int usbvision_restart_isoc(struct usb_usbvision *usbvision)
{
	int ret;

	if (
	    (ret =
	     usbvision_write_reg(usbvision, USBVISION_PWR_REG,
			      USBVISION_SSPND_EN | USBVISION_PWR_VID)) < 0)
		return ret;
	if (
	    (ret =
	     usbvision_write_reg(usbvision, USBVISION_PWR_REG,
			      USBVISION_SSPND_EN | USBVISION_PWR_VID |
			      USBVISION_RES2)) < 0)
		return ret;
	if (
	    (ret =
	     usbvision_write_reg(usbvision, USBVISION_VIN_REG2,
			      USBVISION_KEEP_BLANK | USBVISION_NOHVALID |
				  usbvision->Vin_Reg2_Preset)) < 0) return ret;

	/* TODO: schedule timeout */
	while ((usbvision_read_reg(usbvision, USBVISION_STATUS_REG) && 0x01) != 1);

	return 0;
}

int usbvision_audio_off(struct usb_usbvision *usbvision)
{
	if (usbvision_write_reg(usbvision, USBVISION_IOPIN_REG, USBVISION_AUDIO_MUTE) < 0) {
		printk(KERN_ERR "usbvision_audio_off: can't wirte reg\n");
		return -1;
	}
	usbvision->AudioMute = 0;
	usbvision->AudioChannel = USBVISION_AUDIO_MUTE;
	return 0;
}

int usbvision_set_audio(struct usb_usbvision *usbvision, int AudioChannel)
{
	if (!usbvision->AudioMute) {
		if (usbvision_write_reg(usbvision, USBVISION_IOPIN_REG, AudioChannel) < 0) {
			printk(KERN_ERR "usbvision_set_audio: can't write iopin register for audio switching\n");
			return -1;
		}
	}
	usbvision->AudioChannel = AudioChannel;
	return 0;
}

int usbvision_setup(struct usb_usbvision *usbvision,int format)
{
	usbvision_set_video_format(usbvision, format);
	usbvision_set_dram_settings(usbvision);
	usbvision_set_compress_params(usbvision);
	usbvision_set_input(usbvision);
	usbvision_set_output(usbvision, MAX_USB_WIDTH, MAX_USB_HEIGHT);
	usbvision_restart_isoc(usbvision);

	/* cosas del PCM */
	return USBVISION_IS_OPERATIONAL(usbvision);
}


int usbvision_sbuf_alloc(struct usb_usbvision *usbvision)
{
	int i, errCode = 0;
	const int sb_size = USBVISION_URB_FRAMES * USBVISION_MAX_ISOC_PACKET_SIZE;

	/* Clean pointers so we know if we allocated something */
	for (i = 0; i < USBVISION_NUMSBUF; i++)
		usbvision->sbuf[i].data = NULL;

	for (i = 0; i < USBVISION_NUMSBUF; i++) {
		usbvision->sbuf[i].data = kzalloc(sb_size, GFP_KERNEL);
		if (usbvision->sbuf[i].data == NULL) {
			err("%s: unable to allocate %d bytes for sbuf", __FUNCTION__, sb_size);
			errCode = -ENOMEM;
			break;
		}
	}
	return errCode;
}


void usbvision_sbuf_free(struct usb_usbvision *usbvision)
{
	int i;

	for (i = 0; i < USBVISION_NUMSBUF; i++) {
		if (usbvision->sbuf[i].data != NULL) {
			kfree(usbvision->sbuf[i].data);
			usbvision->sbuf[i].data = NULL;
		}
	}
}

/*
 * usbvision_init_isoc()
 *
 */
int usbvision_init_isoc(struct usb_usbvision *usbvision)
{
	struct usb_device *dev = usbvision->dev;
	int bufIdx, errCode, regValue;

	if (!USBVISION_IS_OPERATIONAL(usbvision))
		return -EFAULT;

	usbvision->curFrame = NULL;
	scratch_reset(usbvision);

	/* Alternate interface 1 is is the biggest frame size */
	errCode = usb_set_interface(dev, usbvision->iface, usbvision->ifaceAltActive);
	if (errCode < 0) {
		usbvision->last_error = errCode;
		return -EBUSY;
	}

	regValue = (16 - usbvision_read_reg(usbvision, USBVISION_ALTER_REG)) & 0x0F;
	usbvision->isocPacketSize = (regValue == 0) ? 0 : (regValue * 64) - 1;
	PDEBUG(DBG_ISOC, "ISO Packet Length:%d", usbvision->isocPacketSize);

	usbvision->usb_bandwidth = regValue >> 1;
	PDEBUG(DBG_ISOC, "USB Bandwidth Usage: %dMbit/Sec", usbvision->usb_bandwidth);



	/* We double buffer the Iso lists */

	for (bufIdx = 0; bufIdx < USBVISION_NUMSBUF; bufIdx++) {
		int j, k;
		struct urb *urb;

		urb = usb_alloc_urb(USBVISION_URB_FRAMES, GFP_KERNEL);
		if (urb == NULL) {
			err("%s: usb_alloc_urb() failed", __FUNCTION__);
			return -ENOMEM;
		}
		usbvision->sbuf[bufIdx].urb = urb;
		urb->dev = dev;
		urb->context = usbvision;
		urb->pipe = usb_rcvisocpipe(dev, usbvision->video_endp);
		urb->transfer_flags = URB_ISO_ASAP;
		urb->interval = 1;
		urb->transfer_buffer = usbvision->sbuf[bufIdx].data;
		urb->complete = usbvision_isocIrq;
		urb->number_of_packets = USBVISION_URB_FRAMES;
		urb->transfer_buffer_length =
		    usbvision->isocPacketSize * USBVISION_URB_FRAMES;
		for (j = k = 0; j < USBVISION_URB_FRAMES; j++,
		     k += usbvision->isocPacketSize) {
			urb->iso_frame_desc[j].offset = k;
			urb->iso_frame_desc[j].length = usbvision->isocPacketSize;
		}
	}


	/* Submit all URBs */
	for (bufIdx = 0; bufIdx < USBVISION_NUMSBUF; bufIdx++) {
			errCode = usb_submit_urb(usbvision->sbuf[bufIdx].urb, GFP_KERNEL);
		if (errCode) {
			err("%s: usb_submit_urb(%d) failed: error %d", __FUNCTION__, bufIdx, errCode);
		}
	}

	usbvision->streaming = Stream_Idle;
	PDEBUG(DBG_ISOC, "%s: streaming=1 usbvision->video_endp=$%02x", __FUNCTION__, usbvision->video_endp);
	return 0;
}

/*
 * usbvision_stop_isoc()
 *
 * This procedure stops streaming and deallocates URBs. Then it
 * activates zero-bandwidth alt. setting of the video interface.
 *
 */
void usbvision_stop_isoc(struct usb_usbvision *usbvision)
{
	int bufIdx, errCode, regValue;

	if ((usbvision->streaming == Stream_Off) || (usbvision->dev == NULL))
		return;

	/* Unschedule all of the iso td's */
	for (bufIdx = 0; bufIdx < USBVISION_NUMSBUF; bufIdx++) {
		usb_kill_urb(usbvision->sbuf[bufIdx].urb);
		usb_free_urb(usbvision->sbuf[bufIdx].urb);
		usbvision->sbuf[bufIdx].urb = NULL;
	}


	PDEBUG(DBG_ISOC, "%s: streaming=Stream_Off\n", __FUNCTION__);
	usbvision->streaming = Stream_Off;

	if (!usbvision->remove_pending) {

		/* Set packet size to 0 */
		errCode = usb_set_interface(usbvision->dev, usbvision->iface,
				      usbvision->ifaceAltInactive);
		if (errCode < 0) {
			err("%s: usb_set_interface() failed: error %d", __FUNCTION__, errCode);
			usbvision->last_error = errCode;
		}
		regValue = (16 - usbvision_read_reg(usbvision, USBVISION_ALTER_REG)) & 0x0F;
		usbvision->isocPacketSize = (regValue == 0) ? 0 : (regValue * 64) - 1;
		PDEBUG(DBG_ISOC, "ISO Packet Length:%d", usbvision->isocPacketSize);

		usbvision->usb_bandwidth = regValue >> 1;
		PDEBUG(DBG_ISOC, "USB Bandwidth Usage: %dMbit/Sec", usbvision->usb_bandwidth);
	}
}

int usbvision_muxsel(struct usb_usbvision *usbvision, int channel)
{
	int mode[4];
	int audio[]= {1, 0, 0, 0};
	struct v4l2_routing route;
	//channel 0 is TV with audiochannel 1 (tuner mono)
	//channel 1 is Composite with audio channel 0 (line in)
	//channel 2 is S-Video with audio channel 0 (line in)
	//channel 3 is additional video inputs to the device with audio channel 0 (line in)

	RESTRICT_TO_RANGE(channel, 0, usbvision->video_inputs);
	usbvision->ctl_input = channel;
	  route.input = SAA7115_COMPOSITE1;
	  call_i2c_clients(usbvision, VIDIOC_INT_S_VIDEO_ROUTING,&route);
	  call_i2c_clients(usbvision, VIDIOC_S_INPUT, &usbvision->ctl_input);

	// set the new channel
	// Regular USB TV Tuners -> channel: 0 = Television, 1 = Composite, 2 = S-Video
	// Four video input devices -> channel: 0 = Chan White, 1 = Chan Green, 2 = Chan Yellow, 3 = Chan Red

	switch (usbvision_device_data[usbvision->DevModel].Codec) {
		case CODEC_SAA7113:
			if (SwitchSVideoInput) { // To handle problems with S-Video Input for some devices.  Use SwitchSVideoInput parameter when loading the module.
				mode[2] = 1;
			}
			else {
				mode[2] = 7;
			}
			if (usbvision_device_data[usbvision->DevModel].VideoChannels == 4) {
				mode[0] = 0; mode[1] = 2; mode[3] = 3;  // Special for four input devices
			}
			else {
				mode[0] = 0; mode[1] = 2; //modes for regular saa7113 devices
			}
			break;
		case CODEC_SAA7111:
			mode[0] = 0; mode[1] = 1; mode[2] = 7; //modes for saa7111
			break;
		default:
			mode[0] = 0; mode[1] = 1; mode[2] = 7; //default modes
	}
	route.input = mode[channel];
	call_i2c_clients(usbvision, VIDIOC_INT_S_VIDEO_ROUTING,&route);
	usbvision->channel = channel;
	usbvision_set_audio(usbvision, audio[channel]);
	return 0;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
