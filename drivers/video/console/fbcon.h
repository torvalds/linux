/*
 *  linux/drivers/video/console/fbcon.h -- Low level frame buffer based console driver
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef _VIDEO_FBCON_H
#define _VIDEO_FBCON_H

#include <linux/config.h>
#include <linux/types.h>
#include <linux/vt_buffer.h>
#include <linux/vt_kern.h>

#include <asm/io.h>

#define FBCON_FLAGS_INIT         1
#define FBCON_FLAGS_CURSOR_TIMER 2

   /*
    *    This is the interface between the low-level console driver and the
    *    low-level frame buffer device
    */

struct display {
    /* Filled in by the frame buffer device */
    u_short inverse;                /* != 0 text black on white as default */
    /* Filled in by the low-level console driver */
    const u_char *fontdata;
    int userfont;                   /* != 0 if fontdata kmalloc()ed */
    u_short scrollmode;             /* Scroll Method */
    short yscroll;                  /* Hardware scrolling */
    int vrows;                      /* number of virtual rows */
    int cursor_shape;
    u32 xres_virtual;
    u32 yres_virtual;
    u32 height;
    u32 width;
    u32 bits_per_pixel;
    u32 grayscale;
    u32 nonstd;
    u32 accel_flags;
    u32 rotate;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
    struct fb_videomode *mode;
};

struct fbcon_ops {
	void (*bmove)(struct vc_data *vc, struct fb_info *info, int sy,
		      int sx, int dy, int dx, int height, int width);
	void (*clear)(struct vc_data *vc, struct fb_info *info, int sy,
		      int sx, int height, int width);
	void (*putcs)(struct vc_data *vc, struct fb_info *info,
		      const unsigned short *s, int count, int yy, int xx,
		      int fg, int bg);
	void (*clear_margins)(struct vc_data *vc, struct fb_info *info,
			      int bottom_only);
	void (*cursor)(struct vc_data *vc, struct fb_info *info,
		       struct display *p, int mode, int softback_lines, int fg, int bg);

	struct timer_list cursor_timer; /* Cursor timer */
	struct fb_cursor cursor_state;
        int    currcon;	                /* Current VC. */
	int    cursor_flash;
	int    cursor_reset;
	int    blank_state;
	int    graphics;
	int    flags;
	char  *cursor_data;
};
    /*
     *  Attribute Decoding
     */

/* Color */
#define attr_fgcol(fgshift,s)    \
	(((s) >> (fgshift)) & 0x0f)
#define attr_bgcol(bgshift,s)    \
	(((s) >> (bgshift)) & 0x0f)
#define	attr_bgcol_ec(bgshift,vc) \
	((vc) ? (((vc)->vc_video_erase_char >> (bgshift)) & 0x0f) : 0)
#define attr_fgcol_ec(fgshift,vc) \
	((vc) ? (((vc)->vc_video_erase_char >> (fgshift)) & 0x0f) : 0)

/* Monochrome */
#define attr_bold(s) \
	((s) & 0x200)
#define attr_reverse(s) \
	((s) & 0x800)
#define attr_underline(s) \
	((s) & 0x400)
#define attr_blink(s) \
	((s) & 0x8000)
	
/* Font */
#define REFCOUNT(fd)	(((int *)(fd))[-1])
#define FNTSIZE(fd)	(((int *)(fd))[-2])
#define FNTCHARCNT(fd)	(((int *)(fd))[-3])
#define FNTSUM(fd)	(((int *)(fd))[-4])
#define FONT_EXTRA_WORDS 4

    /*
     *  Scroll Method
     */
     
/* There are several methods fbcon can use to move text around the screen:
 *
 *                     Operation   Pan    Wrap
 *---------------------------------------------
 * SCROLL_MOVE         copyarea    No     No
 * SCROLL_PAN_MOVE     copyarea    Yes    No
 * SCROLL_WRAP_MOVE    copyarea    No     Yes
 * SCROLL_REDRAW       imageblit   No     No
 * SCROLL_PAN_REDRAW   imageblit   Yes    No
 * SCROLL_WRAP_REDRAW  imageblit   No     Yes
 *
 * (SCROLL_WRAP_REDRAW is not implemented yet)
 *
 * In general, fbcon will choose the best scrolling
 * method based on the rule below:
 *
 * Pan/Wrap > accel imageblit > accel copyarea >
 * soft imageblit > (soft copyarea)
 *
 * Exception to the rule: Pan + accel copyarea is
 * preferred over Pan + accel imageblit.
 *
 * The above is typical for PCI/AGP cards. Unless
 * overridden, fbcon will never use soft copyarea.
 *
 * If you need to override the above rule, set the
 * appropriate flags in fb_info->flags.  For example,
 * to prefer copyarea over imageblit, set
 * FBINFO_READS_FAST.
 *
 * Other notes:
 * + use the hardware engine to move the text
 *    (hw-accelerated copyarea() and fillrect())
 * + use hardware-supported panning on a large virtual screen
 * + amifb can not only pan, but also wrap the display by N lines
 *    (i.e. visible line i = physical line (i+N) % yres).
 * + read what's already rendered on the screen and
 *     write it in a different place (this is cfb_copyarea())
 * + re-render the text to the screen
 *
 * Whether to use wrapping or panning can only be figured out at
 * runtime (when we know whether our font height is a multiple
 * of the pan/wrap step)
 *
 */

#define SCROLL_MOVE	   0x001
#define SCROLL_PAN_MOVE	   0x002
#define SCROLL_WRAP_MOVE   0x003
#define SCROLL_REDRAW	   0x004
#define SCROLL_PAN_REDRAW  0x005

#ifdef CONFIG_FB_TILEBLITTING
extern void fbcon_set_tileops(struct vc_data *vc, struct fb_info *info,
			      struct display *p, struct fbcon_ops *ops);
#endif
extern void fbcon_set_bitops(struct fbcon_ops *ops);

#endif /* _VIDEO_FBCON_H */
