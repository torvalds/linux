/*
 *  linux/drivers/video/console/sticon.c - console driver using HP's STI firmware
 *
 *	Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 *	Copyright (C) 2002 Helge Deller <deller@gmx.de>
 *
 *  Based on linux/drivers/video/vgacon.c and linux/drivers/video/fbcon.c,
 *  which were
 *
 *	Created 28 Sep 1997 by Geert Uytterhoeven
 *	Rewritten by Martin Mares <mj@ucw.cz>, July 1998
 *	Copyright (C) 1991, 1992  Linus Torvalds
 *			    1995  Jay Estabrook
 *	Copyright (C) 1995 Geert Uytterhoeven
 *	Copyright (C) 1993 Bjoern Brauel
 *			   Roman Hodek
 *	Copyright (C) 1993 Hamish Macdonald
 *			   Greg Harp
 *	Copyright (C) 1994 David Carter [carter@compsci.bristol.ac.uk]
 *
 *	      with work by William Rucklidge (wjr@cs.cornell.edu)
 *			   Geert Uytterhoeven
 *			   Jes Sorensen (jds@kom.auc.dk)
 *			   Martin Apel
 *	      with work by Guenther Kelleter
 *			   Martin Schaller
 *			   Andreas Schwab
 *			   Emmanuel Marty (core@ggi-project.org)
 *			   Jakub Jelinek (jj@ultra.linux.cz)
 *			   Martin Mares <mj@ucw.cz>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/errno.h>
#include <linux/vt_kern.h>
#include <linux/kd.h>
#include <linux/selection.h>
#include <linux/module.h>

#include <asm/io.h>

#include "../fbdev/sticore.h"

/* switching to graphics mode */
#define BLANK 0
static int vga_is_gfx;

/* this is the sti_struct used for this console */
static struct sti_struct *sticon_sti;

/* Software scrollback */
static unsigned long softback_buf, softback_curr;
static unsigned long softback_in;
static unsigned long /* softback_top, */ softback_end;
static int softback_lines;

/* software cursor */
static int cursor_drawn;
#define CURSOR_DRAW_DELAY		(1)
#define DEFAULT_CURSOR_BLINK_RATE	(20)

static int vbl_cursor_cnt;

static inline void cursor_undrawn(void)
{
    vbl_cursor_cnt = 0;
    cursor_drawn = 0;
}

static const char *sticon_startup(void)
{
    return "STI console";
}

static int sticon_set_palette(struct vc_data *c, const unsigned char *table)
{
    return -EINVAL;
}

static void sticon_putc(struct vc_data *conp, int c, int ypos, int xpos)
{
    int redraw_cursor = 0;

    if (vga_is_gfx || console_blanked)
	    return;

    if (conp->vc_mode != KD_TEXT)
    	    return;
#if 0
    if ((p->cursor_x == xpos) && (p->cursor_y == ypos)) {
	    cursor_undrawn();
	    redraw_cursor = 1;
    }
#endif

    sti_putc(sticon_sti, c, ypos, xpos);

    if (redraw_cursor)
	    vbl_cursor_cnt = CURSOR_DRAW_DELAY;
}

static void sticon_putcs(struct vc_data *conp, const unsigned short *s,
			 int count, int ypos, int xpos)
{
    int redraw_cursor = 0;

    if (vga_is_gfx || console_blanked)
	    return;

    if (conp->vc_mode != KD_TEXT)
    	    return;

#if 0
    if ((p->cursor_y == ypos) && (xpos <= p->cursor_x) &&
	(p->cursor_x < (xpos + count))) {
	    cursor_undrawn();
	    redraw_cursor = 1;
    }
#endif

    while (count--) {
	sti_putc(sticon_sti, scr_readw(s++), ypos, xpos++);
    }

    if (redraw_cursor)
	    vbl_cursor_cnt = CURSOR_DRAW_DELAY;
}

static void sticon_cursor(struct vc_data *conp, int mode)
{
    unsigned short car1;

    car1 = conp->vc_screenbuf[conp->vc_x + conp->vc_y * conp->vc_cols];
    switch (mode) {
    case CM_ERASE:
	sti_putc(sticon_sti, car1, conp->vc_y, conp->vc_x);
	break;
    case CM_MOVE:
    case CM_DRAW:
	switch (conp->vc_cursor_type & 0x0f) {
	case CUR_UNDERLINE:
	case CUR_LOWER_THIRD:
	case CUR_LOWER_HALF:
	case CUR_TWO_THIRDS:
	case CUR_BLOCK:
	    sti_putc(sticon_sti, (car1 & 255) + (0 << 8) + (7 << 11),
		     conp->vc_y, conp->vc_x);
	    break;
	}
	break;
    }
}

static int sticon_scroll(struct vc_data *conp, int t, int b, int dir, int count)
{
    struct sti_struct *sti = sticon_sti;

    if (vga_is_gfx)
        return 0;

    sticon_cursor(conp, CM_ERASE);

    switch (dir) {
    case SM_UP:
	sti_bmove(sti, t + count, 0, t, 0, b - t - count, conp->vc_cols);
	sti_clear(sti, b - count, 0, count, conp->vc_cols, conp->vc_video_erase_char);
	break;

    case SM_DOWN:
	sti_bmove(sti, t, 0, t + count, 0, b - t - count, conp->vc_cols);
	sti_clear(sti, t, 0, count, conp->vc_cols, conp->vc_video_erase_char);
	break;
    }

    return 0;
}

static void sticon_bmove(struct vc_data *conp, int sy, int sx, 
	int dy, int dx, int height, int width)
{
    if (!width || !height)
	    return;
#if 0
    if (((sy <= p->cursor_y) && (p->cursor_y < sy+height) &&
	(sx <= p->cursor_x) && (p->cursor_x < sx+width)) ||
	((dy <= p->cursor_y) && (p->cursor_y < dy+height) &&
	(dx <= p->cursor_x) && (p->cursor_x < dx+width)))
		sticon_cursor(p, CM_ERASE /*|CM_SOFTBACK*/);
#endif

    sti_bmove(sticon_sti, sy, sx, dy, dx, height, width);
}

static void sticon_init(struct vc_data *c, int init)
{
    struct sti_struct *sti = sticon_sti;
    int vc_cols, vc_rows;

    sti_set(sti, 0, 0, sti_onscreen_y(sti), sti_onscreen_x(sti), 0);
    vc_cols = sti_onscreen_x(sti) / sti->font_width;
    vc_rows = sti_onscreen_y(sti) / sti->font_height;
    c->vc_can_do_color = 1;
    
    if (init) {
	c->vc_cols = vc_cols;
	c->vc_rows = vc_rows;
    } else {
	/* vc_rows = (c->vc_rows > vc_rows) ? vc_rows : c->vc_rows; */
	/* vc_cols = (c->vc_cols > vc_cols) ? vc_cols : c->vc_cols; */
	vc_resize(c, vc_cols, vc_rows);
/*	vc_resize_con(vc_rows, vc_cols, c->vc_num); */
    }
}

static void sticon_deinit(struct vc_data *c)
{
}

static void sticon_clear(struct vc_data *conp, int sy, int sx, int height,
			 int width)
{
    if (!height || !width)
	return;

    sti_clear(sticon_sti, sy, sx, height, width, conp->vc_video_erase_char);
}

static int sticon_switch(struct vc_data *conp)
{
    return 1;	/* needs refreshing */
}

static int sticon_set_origin(struct vc_data *conp)
{
    return 0;
}

static int sticon_blank(struct vc_data *c, int blank, int mode_switch)
{
    if (blank == 0) {
	if (mode_switch)
	    vga_is_gfx = 0;
	return 1;
    }
    sticon_set_origin(c);
    sti_clear(sticon_sti, 0,0, c->vc_rows, c->vc_cols, BLANK);
    if (mode_switch)
	vga_is_gfx = 1;
    return 1;
}

static int sticon_scrolldelta(struct vc_data *conp, int lines)
{
    return 0;
}

static u16 *sticon_screen_pos(struct vc_data *conp, int offset)
{
    int line;
    unsigned long p;

    if (conp->vc_num != fg_console || !softback_lines)
    	return (u16 *)(conp->vc_origin + offset);
    line = offset / conp->vc_size_row;
    if (line >= softback_lines)
    	return (u16 *)(conp->vc_origin + offset - softback_lines * conp->vc_size_row);
    p = softback_curr + offset;
    if (p >= softback_end)
    	p += softback_buf - softback_end;
    return (u16 *)p;
}

static unsigned long sticon_getxy(struct vc_data *conp, unsigned long pos,
				  int *px, int *py)
{
    int x, y;
    unsigned long ret;
    if (pos >= conp->vc_origin && pos < conp->vc_scr_end) {
    	unsigned long offset = (pos - conp->vc_origin) / 2;
    	
    	x = offset % conp->vc_cols;
    	y = offset / conp->vc_cols;
    	if (conp->vc_num == fg_console)
    	    y += softback_lines;
    	ret = pos + (conp->vc_cols - x) * 2;
    } else if (conp->vc_num == fg_console && softback_lines) {
    	unsigned long offset = pos - softback_curr;
    	
    	if (pos < softback_curr)
    	    offset += softback_end - softback_buf;
    	offset /= 2;
    	x = offset % conp->vc_cols;
    	y = offset / conp->vc_cols;
	ret = pos + (conp->vc_cols - x) * 2;
	if (ret == softback_end)
	    ret = softback_buf;
	if (ret == softback_in)
	    ret = conp->vc_origin;
    } else {
    	/* Should not happen */
    	x = y = 0;
    	ret = conp->vc_origin;
    }
    if (px) *px = x;
    if (py) *py = y;
    return ret;
}

static u8 sticon_build_attr(struct vc_data *conp, u8 color, u8 intens,
			    u8 blink, u8 underline, u8 reverse, u8 italic)
{
    u8 attr = ((color & 0x70) >> 1) | ((color & 7));

    if (reverse) {
	color = ((color >> 3) & 0x7) | ((color & 0x7) << 3);
    }

    return attr;
}

static void sticon_invert_region(struct vc_data *conp, u16 *p, int count)
{
    int col = 1; /* vga_can_do_color; */

    while (count--) {
	u16 a = scr_readw(p);

	if (col)
		a = ((a) & 0x88ff) | (((a) & 0x7000) >> 4) | (((a) & 0x0700) << 4);
	else
		a = ((a & 0x0700) == 0x0100) ? 0x7000 : 0x7700;

	scr_writew(a, p++);
    }
}

static void sticon_save_screen(struct vc_data *conp)
{
}

static const struct consw sti_con = {
	.owner			= THIS_MODULE,
	.con_startup		= sticon_startup,
	.con_init		= sticon_init,
	.con_deinit		= sticon_deinit,
	.con_clear		= sticon_clear,
	.con_putc		= sticon_putc,
	.con_putcs		= sticon_putcs,
	.con_cursor		= sticon_cursor,
	.con_scroll		= sticon_scroll,
	.con_bmove		= sticon_bmove,
	.con_switch		= sticon_switch,
	.con_blank		= sticon_blank,
	.con_set_palette	= sticon_set_palette,
	.con_scrolldelta	= sticon_scrolldelta,
	.con_set_origin		= sticon_set_origin,
	.con_save_screen	= sticon_save_screen, 
	.con_build_attr		= sticon_build_attr,
	.con_invert_region	= sticon_invert_region, 
	.con_screen_pos		= sticon_screen_pos,
	.con_getxy		= sticon_getxy,
};



static int __init sticonsole_init(void)
{
    int err;
    /* already initialized ? */
    if (sticon_sti)
	 return 0;

    sticon_sti = sti_get_rom(0);
    if (!sticon_sti)
	return -ENODEV;

    if (conswitchp == &dummy_con) {
	printk(KERN_INFO "sticon: Initializing STI text console.\n");
	console_lock();
	err = do_take_over_console(&sti_con, 0, MAX_NR_CONSOLES - 1, 1);
	console_unlock();
	return err;
    }
    return 0;
}

module_init(sticonsole_init);
MODULE_LICENSE("GPL");
