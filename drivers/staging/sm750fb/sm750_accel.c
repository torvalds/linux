// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/console.h>
#include <linux/platform_device.h>

#include "sm750.h"
#include "sm750_accel.h"
static inline void write_dpr(struct lynx_accel *accel, int offset, u32 reg_value)
{
	writel(reg_value, accel->dpr_base + offset);
}

static inline u32 read_dpr(struct lynx_accel *accel, int offset)
{
	return readl(accel->dpr_base + offset);
}

static inline void write_dpPort(struct lynx_accel *accel, u32 data)
{
	writel(data, accel->dp_port_base);
}

void sm750_hw_de_init(struct lynx_accel *accel)
{
	/* setup 2d engine registers */
	u32 reg, clr;

	write_dpr(accel, DE_MASKS, 0xFFFFFFFF);

	/* dpr1c */
	reg =  0x3;

	clr = DE_STRETCH_FORMAT_PATTERN_XY |
	      DE_STRETCH_FORMAT_PATTERN_Y_MASK |
	      DE_STRETCH_FORMAT_PATTERN_X_MASK |
	      DE_STRETCH_FORMAT_ADDRESSING_MASK |
	      DE_STRETCH_FORMAT_SOURCE_HEIGHT_MASK;

	/* DE_STRETCH bpp format need be initialized in setMode routine */
	write_dpr(accel, DE_STRETCH_FORMAT,
		  (read_dpr(accel, DE_STRETCH_FORMAT) & ~clr) | reg);

	/* disable clipping and transparent */
	write_dpr(accel, DE_CLIP_TL, 0); /* dpr2c */
	write_dpr(accel, DE_CLIP_BR, 0); /* dpr30 */

	write_dpr(accel, DE_COLOR_COMPARE_MASK, 0); /* dpr24 */
	write_dpr(accel, DE_COLOR_COMPARE, 0);

	clr = DE_CONTROL_TRANSPARENCY | DE_CONTROL_TRANSPARENCY_MATCH |
		DE_CONTROL_TRANSPARENCY_SELECT;

	/* dpr0c */
	write_dpr(accel, DE_CONTROL, read_dpr(accel, DE_CONTROL) & ~clr);
}

/*
 * set2dformat only be called from setmode functions
 * but if you need dual framebuffer driver,need call set2dformat
 * every time you use 2d function
 */

void sm750_hw_set2dformat(struct lynx_accel *accel, int fmt)
{
	u32 reg;

	/* fmt=0,1,2 for 8,16,32,bpp on sm718/750/502 */
	reg = read_dpr(accel, DE_STRETCH_FORMAT);
	reg &= ~DE_STRETCH_FORMAT_PIXEL_FORMAT_MASK;
	reg |= ((fmt << DE_STRETCH_FORMAT_PIXEL_FORMAT_SHIFT) &
		DE_STRETCH_FORMAT_PIXEL_FORMAT_MASK);
	write_dpr(accel, DE_STRETCH_FORMAT, reg);
}

int sm750_hw_fillrect(struct lynx_accel *accel,
		      u32 base, u32 pitch, u32 Bpp,
		      u32 x, u32 y, u32 width, u32 height,
		      u32 color, u32 rop)
{
	u32 deCtrl;

	if (accel->de_wait() != 0) {
		/*
		 * int time wait and always busy,seems hardware
		 * got something error
		 */
		pr_debug("De engine always busy\n");
		return -1;
	}

	write_dpr(accel, DE_WINDOW_DESTINATION_BASE, base); /* dpr40 */
	write_dpr(accel, DE_PITCH,
		  ((pitch / Bpp << DE_PITCH_DESTINATION_SHIFT) &
		   DE_PITCH_DESTINATION_MASK) |
		  (pitch / Bpp & DE_PITCH_SOURCE_MASK)); /* dpr10 */

	write_dpr(accel, DE_WINDOW_WIDTH,
		  ((pitch / Bpp << DE_WINDOW_WIDTH_DST_SHIFT) &
		   DE_WINDOW_WIDTH_DST_MASK) |
		   (pitch / Bpp & DE_WINDOW_WIDTH_SRC_MASK)); /* dpr44 */

	write_dpr(accel, DE_FOREGROUND, color); /* DPR14 */

	write_dpr(accel, DE_DESTINATION,
		  ((x << DE_DESTINATION_X_SHIFT) & DE_DESTINATION_X_MASK) |
		  (y & DE_DESTINATION_Y_MASK)); /* dpr4 */

	write_dpr(accel, DE_DIMENSION,
		  ((width << DE_DIMENSION_X_SHIFT) & DE_DIMENSION_X_MASK) |
		  (height & DE_DIMENSION_Y_ET_MASK)); /* dpr8 */

	deCtrl = DE_CONTROL_STATUS | DE_CONTROL_LAST_PIXEL |
		DE_CONTROL_COMMAND_RECTANGLE_FILL | DE_CONTROL_ROP_SELECT |
		(rop & DE_CONTROL_ROP_MASK); /* dpr0xc */

	write_dpr(accel, DE_CONTROL, deCtrl);
	return 0;
}

/**
 * sm750_hw_copyarea
 * @accel: Acceleration device data
 * @sBase: Address of source: offset in frame buffer
 * @sPitch: Pitch value of source surface in BYTE
 * @sx: Starting x coordinate of source surface
 * @sy: Starting y coordinate of source surface
 * @dBase: Address of destination: offset in frame buffer
 * @dPitch: Pitch value of destination surface in BYTE
 * @Bpp: Color depth of destination surface
 * @dx: Starting x coordinate of destination surface
 * @dy: Starting y coordinate of destination surface
 * @width: width of rectangle in pixel value
 * @height: height of rectangle in pixel value
 * @rop2: ROP value
 */
int sm750_hw_copyarea(struct lynx_accel *accel,
		      unsigned int sBase, unsigned int sPitch,
		      unsigned int sx, unsigned int sy,
		      unsigned int dBase, unsigned int dPitch,
		      unsigned int Bpp, unsigned int dx, unsigned int dy,
		      unsigned int width, unsigned int height,
		      unsigned int rop2)
{
	unsigned int nDirection, de_ctrl;

	nDirection = LEFT_TO_RIGHT;
	/* Direction of ROP2 operation: 1 = Left to Right, (-1) = Right to Left */
	de_ctrl = 0;

	/* If source and destination are the same surface, need to check for overlay cases */
	if (sBase == dBase && sPitch == dPitch) {
		/* Determine direction of operation */
		if (sy < dy) {
			/*  +----------+
			 *  |S         |
			 *  |   +----------+
			 *  |   |      |   |
			 *  |   |      |   |
			 *  +---|------+   |
			 *	|         D|
			 *	+----------+
			 */

			nDirection = BOTTOM_TO_TOP;
		} else if (sy > dy) {
			/*  +----------+
			 *  |D         |
			 *  |   +----------+
			 *  |   |      |   |
			 *  |   |      |   |
			 *  +---|------+   |
			 *	|         S|
			 *	+----------+
			 */

			nDirection = TOP_TO_BOTTOM;
		} else {
			/* sy == dy */

			if (sx <= dx) {
				/* +------+---+------+
				 * |S     |   |     D|
				 * |      |   |      |
				 * |      |   |      |
				 * |      |   |      |
				 * +------+---+------+
				 */

				nDirection = RIGHT_TO_LEFT;
			} else {
			/* sx > dx */

				/* +------+---+------+
				 * |D     |   |     S|
				 * |      |   |      |
				 * |      |   |      |
				 * |      |   |      |
				 * +------+---+------+
				 */

				nDirection = LEFT_TO_RIGHT;
			}
		}
	}

	if ((nDirection == BOTTOM_TO_TOP) || (nDirection == RIGHT_TO_LEFT)) {
		sx += width - 1;
		sy += height - 1;
		dx += width - 1;
		dy += height - 1;
	}

	/*
	 * Note:
	 * DE_FOREGROUND and DE_BACKGROUND are don't care.
	 * DE_COLOR_COMPARE and DE_COLOR_COMPARE_MAKS
	 * are set by set deSetTransparency().
	 */

	/*
	 * 2D Source Base.
	 * It is an address offset (128 bit aligned)
	 * from the beginning of frame buffer.
	 */
	write_dpr(accel, DE_WINDOW_SOURCE_BASE, sBase); /* dpr40 */

	/*
	 * 2D Destination Base.
	 * It is an address offset (128 bit aligned)
	 * from the beginning of frame buffer.
	 */
	write_dpr(accel, DE_WINDOW_DESTINATION_BASE, dBase); /* dpr44 */

	/*
	 * Program pitch (distance between the 1st points of two adjacent lines).
	 * Note that input pitch is BYTE value, but the 2D Pitch register uses
	 * pixel values. Need Byte to pixel conversion.
	 */
	write_dpr(accel, DE_PITCH,
		  ((dPitch / Bpp << DE_PITCH_DESTINATION_SHIFT) &
		   DE_PITCH_DESTINATION_MASK) |
		  (sPitch / Bpp & DE_PITCH_SOURCE_MASK)); /* dpr10 */

	/*
	 * Screen Window width in Pixels.
	 * 2D engine uses this value to calculate the linear address in frame buffer
	 * for a given point.
	 */
	write_dpr(accel, DE_WINDOW_WIDTH,
		  ((dPitch / Bpp << DE_WINDOW_WIDTH_DST_SHIFT) &
		   DE_WINDOW_WIDTH_DST_MASK) |
		  (sPitch / Bpp & DE_WINDOW_WIDTH_SRC_MASK)); /* dpr3c */

	if (accel->de_wait() != 0)
		return -1;

	write_dpr(accel, DE_SOURCE,
		  ((sx << DE_SOURCE_X_K1_SHIFT) & DE_SOURCE_X_K1_MASK) |
		  (sy & DE_SOURCE_Y_K2_MASK)); /* dpr0 */
	write_dpr(accel, DE_DESTINATION,
		  ((dx << DE_DESTINATION_X_SHIFT) & DE_DESTINATION_X_MASK) |
		  (dy & DE_DESTINATION_Y_MASK)); /* dpr04 */
	write_dpr(accel, DE_DIMENSION,
		  ((width << DE_DIMENSION_X_SHIFT) & DE_DIMENSION_X_MASK) |
		  (height & DE_DIMENSION_Y_ET_MASK)); /* dpr08 */

	de_ctrl = (rop2 & DE_CONTROL_ROP_MASK) | DE_CONTROL_ROP_SELECT |
		((nDirection == RIGHT_TO_LEFT) ? DE_CONTROL_DIRECTION : 0) |
		DE_CONTROL_COMMAND_BITBLT | DE_CONTROL_STATUS;
	write_dpr(accel, DE_CONTROL, de_ctrl); /* dpr0c */

	return 0;
}

static unsigned int deGetTransparency(struct lynx_accel *accel)
{
	unsigned int de_ctrl;

	de_ctrl = read_dpr(accel, DE_CONTROL);

	de_ctrl &= (DE_CONTROL_TRANSPARENCY_MATCH |
		    DE_CONTROL_TRANSPARENCY_SELECT | DE_CONTROL_TRANSPARENCY);

	return de_ctrl;
}

/**
 * sm750_hw_imageblit
 * @accel: Acceleration device data
 * @pSrcbuf: pointer to start of source buffer in system memory
 * @srcDelta: Pitch value (in bytes) of the source buffer, +ive means top down
 *	      and -ive mean button up
 * @startBit: Mono data can start at any bit in a byte, this value should be
 *	      0 to 7
 * @dBase: Address of destination: offset in frame buffer
 * @dPitch: Pitch value of destination surface in BYTE
 * @bytePerPixel: Color depth of destination surface
 * @dx: Starting x coordinate of destination surface
 * @dy: Starting y coordinate of destination surface
 * @width: width of rectangle in pixel value
 * @height: height of rectangle in pixel value
 * @fColor: Foreground color (corresponding to a 1 in the monochrome data
 * @bColor: Background color (corresponding to a 0 in the monochrome data
 * @rop2: ROP value
 */
int sm750_hw_imageblit(struct lynx_accel *accel, const char *pSrcbuf,
		       u32 srcDelta, u32 startBit, u32 dBase, u32 dPitch,
		       u32 bytePerPixel, u32 dx, u32 dy, u32 width,
		       u32 height, u32 fColor, u32 bColor, u32 rop2)
{
	unsigned int ulBytesPerScan;
	unsigned int ul4BytesPerScan;
	unsigned int ulBytesRemain;
	unsigned int de_ctrl = 0;
	unsigned char ajRemain[4];
	int i, j;

	startBit &= 7; /* Just make sure the start bit is within legal range */
	ulBytesPerScan = (width + startBit + 7) / 8;
	ul4BytesPerScan = ulBytesPerScan & ~3;
	ulBytesRemain = ulBytesPerScan & 3;

	if (accel->de_wait() != 0)
		return -1;

	/*
	 * 2D Source Base.
	 * Use 0 for HOST Blt.
	 */
	write_dpr(accel, DE_WINDOW_SOURCE_BASE, 0);

	/* 2D Destination Base.
	 * It is an address offset (128 bit aligned)
	 * from the beginning of frame buffer.
	 */
	write_dpr(accel, DE_WINDOW_DESTINATION_BASE, dBase);

	/*
	 * Program pitch (distance between the 1st points of two adjacent
	 * lines). Note that input pitch is BYTE value, but the 2D Pitch
	 * register uses pixel values. Need Byte to pixel conversion.
	 */
	write_dpr(accel, DE_PITCH,
		  ((dPitch / bytePerPixel << DE_PITCH_DESTINATION_SHIFT) &
		   DE_PITCH_DESTINATION_MASK) |
		  (dPitch / bytePerPixel & DE_PITCH_SOURCE_MASK)); /* dpr10 */

	/*
	 * Screen Window width in Pixels.
	 * 2D engine uses this value to calculate the linear address
	 * in frame buffer for a given point.
	 */
	write_dpr(accel, DE_WINDOW_WIDTH,
		  ((dPitch / bytePerPixel << DE_WINDOW_WIDTH_DST_SHIFT) &
		   DE_WINDOW_WIDTH_DST_MASK) |
		  (dPitch / bytePerPixel & DE_WINDOW_WIDTH_SRC_MASK));

	 /*
	  * Note: For 2D Source in Host Write, only X_K1_MONO field is needed,
	  * and Y_K2 field is not used.
	  * For mono bitmap, use startBit for X_K1.
	  */
	write_dpr(accel, DE_SOURCE,
		  (startBit << DE_SOURCE_X_K1_SHIFT) &
		  DE_SOURCE_X_K1_MONO_MASK); /* dpr00 */

	write_dpr(accel, DE_DESTINATION,
		  ((dx << DE_DESTINATION_X_SHIFT) & DE_DESTINATION_X_MASK) |
		  (dy & DE_DESTINATION_Y_MASK)); /* dpr04 */

	write_dpr(accel, DE_DIMENSION,
		  ((width << DE_DIMENSION_X_SHIFT) & DE_DIMENSION_X_MASK) |
		  (height & DE_DIMENSION_Y_ET_MASK)); /* dpr08 */

	write_dpr(accel, DE_FOREGROUND, fColor);
	write_dpr(accel, DE_BACKGROUND, bColor);

	de_ctrl = (rop2 & DE_CONTROL_ROP_MASK) |
		DE_CONTROL_ROP_SELECT | DE_CONTROL_COMMAND_HOST_WRITE |
		DE_CONTROL_HOST | DE_CONTROL_STATUS;

	write_dpr(accel, DE_CONTROL, de_ctrl | deGetTransparency(accel));

	/* Write MONO data (line by line) to 2D Engine data port */
	for (i = 0; i < height; i++) {
		/* For each line, send the data in chunks of 4 bytes */
		for (j = 0; j < (ul4BytesPerScan / 4); j++)
			write_dpPort(accel, *(unsigned int *)(pSrcbuf + (j * 4)));

		if (ulBytesRemain) {
			memcpy(ajRemain, pSrcbuf + ul4BytesPerScan,
			       ulBytesRemain);
			write_dpPort(accel, *(unsigned int *)ajRemain);
		}

		pSrcbuf += srcDelta;
	}

	return 0;
}

