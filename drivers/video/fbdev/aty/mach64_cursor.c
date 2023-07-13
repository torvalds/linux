// SPDX-License-Identifier: GPL-2.0
/*
 *  ATI Mach64 CT/VT/GT/LT Cursor Support
 */

#include <linux/fb.h>
#include <linux/init.h>
#include <linux/string.h>
#include "../core/fb_draw.h"

#include <asm/io.h>

#ifdef __sparc__
#include <asm/fbio.h>
#endif

#include <video/mach64.h>
#include "atyfb.h"

/*
 * The hardware cursor definition requires 2 bits per pixel. The
 * Cursor size reguardless of the visible cursor size is 64 pixels
 * by 64 lines. The total memory required to define the cursor is
 * 16 bytes / line for 64 lines or 1024 bytes of data. The data
 * must be in a contigiuos format. The 2 bit cursor code values are
 * as follows:
 *
 *	00 - pixel colour = CURSOR_CLR_0
 *	01 - pixel colour = CURSOR_CLR_1
 *	10 - pixel colour = transparent (current display pixel)
 *	11 - pixel colour = 1's complement of current display pixel
 *
 *	Cursor Offset        64 pixels		 Actual Displayed Area
 *            \_________________________/
 *	      |			|	|	|
 *	      |<--------------->|	|	|
 *	      | CURS_HORZ_OFFSET|	|	|
 *	      |			|_______|	|  64 Lines
 *	      |			   ^	|	|
 *	      |			   |	|	|
 *	      |		CURS_VERT_OFFSET|	|
 *	      |			   |	|	|
 *	      |____________________|____|	|
 *
 *
 * The Screen position of the top left corner of the displayed
 * cursor is specificed by CURS_HORZ_VERT_POSN. Care must be taken
 * when the cursor hot spot is not the top left corner and the
 * physical cursor position becomes negative. It will be displayed
 * if either the horizontal or vertical cursor position is negative
 *
 * If x becomes negative the cursor manager must adjust the CURS_HORZ_OFFSET
 * to a larger number and saturate CUR_HORZ_POSN to zero.
 *
 * if Y becomes negative, CUR_VERT_OFFSET must be adjusted to a larger number,
 * CUR_OFFSET must be adjusted to a point to the appropriate line in the cursor
 * definitation and CUR_VERT_POSN must be saturated to zero.
 */

    /*
     *  Hardware Cursor support.
     */
static const u8 cursor_bits_lookup[16] = {
	0x00, 0x40, 0x10, 0x50, 0x04, 0x44, 0x14, 0x54,
	0x01, 0x41, 0x11, 0x51, 0x05, 0x45, 0x15, 0x55
};

static int atyfb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	u16 xoff, yoff;
	int x, y, h;

#ifdef __sparc__
	if (par->mmaped)
		return -EPERM;
#endif
	if (par->asleep)
		return -EPERM;

	wait_for_fifo(1, par);
	if (cursor->enable)
		aty_st_le32(GEN_TEST_CNTL, aty_ld_le32(GEN_TEST_CNTL, par)
			    | HWCURSOR_ENABLE, par);
	else
		aty_st_le32(GEN_TEST_CNTL, aty_ld_le32(GEN_TEST_CNTL, par)
				& ~HWCURSOR_ENABLE, par);

	/* set position */
	if (cursor->set & FB_CUR_SETPOS) {
		x = cursor->image.dx - cursor->hot.x - info->var.xoffset;
		if (x < 0) {
			xoff = -x;
			x = 0;
		} else {
			xoff = 0;
		}

		y = cursor->image.dy - cursor->hot.y - info->var.yoffset;
		if (y < 0) {
			yoff = -y;
			y = 0;
		} else {
			yoff = 0;
		}

		h = cursor->image.height;

		/*
		 * In doublescan mode, the cursor location
		 * and heigh also needs to be doubled.
		 */
                if (par->crtc.gen_cntl & CRTC_DBL_SCAN_EN) {
			y<<=1;
			h<<=1;
		}
		wait_for_fifo(3, par);
		aty_st_le32(CUR_OFFSET, (info->fix.smem_len >> 3) + (yoff << 1), par);
		aty_st_le32(CUR_HORZ_VERT_OFF,
			    ((u32) (64 - h + yoff) << 16) | xoff, par);
		aty_st_le32(CUR_HORZ_VERT_POSN, ((u32) y << 16) | x, par);
	}

	/* Set color map */
	if (cursor->set & FB_CUR_SETCMAP) {
		u32 fg_idx, bg_idx, fg, bg;

		fg_idx = cursor->image.fg_color;
		bg_idx = cursor->image.bg_color;

		fg = ((info->cmap.red[fg_idx] & 0xff) << 24) |
		     ((info->cmap.green[fg_idx] & 0xff) << 16) |
		     ((info->cmap.blue[fg_idx] & 0xff) << 8) | 0xff;

		bg = ((info->cmap.red[bg_idx] & 0xff) << 24) |
		     ((info->cmap.green[bg_idx] & 0xff) << 16) |
		     ((info->cmap.blue[bg_idx] & 0xff) << 8);

		wait_for_fifo(2, par);
		aty_st_le32(CUR_CLR0, bg, par);
		aty_st_le32(CUR_CLR1, fg, par);
	}

	if (cursor->set & (FB_CUR_SETSHAPE | FB_CUR_SETIMAGE)) {
	    u8 *src = (u8 *)cursor->image.data;
	    u8 *msk = (u8 *)cursor->mask;
	    u8 __iomem *dst = (u8 __iomem *)info->sprite.addr;
	    unsigned int width = (cursor->image.width + 7) >> 3;
	    unsigned int height = cursor->image.height;
	    unsigned int align = info->sprite.scan_align;

	    unsigned int i, j, offset;
	    u8 m, b;

	    // Clear cursor image with 1010101010...
	    fb_memset_io(dst, 0xaa, 1024);

	    offset = align - width*2;

	    for (i = 0; i < height; i++) {
		for (j = 0; j < width; j++) {
			u16 l = 0xaaaa;
			b = *src++;
			m = *msk++;
			switch (cursor->rop) {
			case ROP_XOR:
			    // Upper 4 bits of mask data
			    l = cursor_bits_lookup[(b ^ m) >> 4] |
			    // Lower 4 bits of mask
				    (cursor_bits_lookup[(b ^ m) & 0x0f] << 8);
			    break;
			case ROP_COPY:
			    // Upper 4 bits of mask data
			    l = cursor_bits_lookup[(b & m) >> 4] |
			    // Lower 4 bits of mask
				    (cursor_bits_lookup[(b & m) & 0x0f] << 8);
			    break;
			}
			/*
			 * If cursor size is not a multiple of 8 characters
			 * we must pad it with transparent pattern (0xaaaa).
			 */
			if ((j + 1) * 8 > cursor->image.width) {
				l = comp(l, 0xaaaa,
				    (1 << ((cursor->image.width & 7) * 2)) - 1);
			}
			fb_writeb(l & 0xff, dst++);
			fb_writeb(l >> 8, dst++);
		}
		dst += offset;
	    }
	}

	return 0;
}

int aty_init_cursor(struct fb_info *info, struct fb_ops *atyfb_ops)
{
	unsigned long addr;

	info->fix.smem_len -= PAGE_SIZE;

#ifdef __sparc__
	addr = (unsigned long) info->screen_base - 0x800000 + info->fix.smem_len;
	info->sprite.addr = (u8 *) addr;
#else
#ifdef __BIG_ENDIAN
	addr = info->fix.smem_start - 0x800000 + info->fix.smem_len;
	info->sprite.addr = (u8 *) ioremap(addr, 1024);
#else
	addr = (unsigned long) info->screen_base + info->fix.smem_len;
	info->sprite.addr = (u8 *) addr;
#endif
#endif
	if (!info->sprite.addr)
		return -ENXIO;
	info->sprite.size = PAGE_SIZE;
	info->sprite.scan_align = 16;	/* Scratch pad 64 bytes wide */
	info->sprite.buf_align = 16; 	/* and 64 lines tall. */
	info->sprite.flags = FB_PIXMAP_IO;

	atyfb_ops->fb_cursor = atyfb_cursor;

	return 0;
}

