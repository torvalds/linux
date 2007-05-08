/*
 *  linux/drivers/video/fbcon.c -- Low level frame buffer based console driver
 *
 *	Copyright (C) 1995 Geert Uytterhoeven
 *
 *
 *  This file is based on the original Amiga console driver (amicon.c):
 *
 *	Copyright (C) 1993 Hamish Macdonald
 *			   Greg Harp
 *	Copyright (C) 1994 David Carter [carter@compsci.bristol.ac.uk]
 *
 *	      with work by William Rucklidge (wjr@cs.cornell.edu)
 *			   Geert Uytterhoeven
 *			   Jes Sorensen (jds@kom.auc.dk)
 *			   Martin Apel
 *
 *  and on the original Atari console driver (atacon.c):
 *
 *	Copyright (C) 1993 Bjoern Brauel
 *			   Roman Hodek
 *
 *	      with work by Guenther Kelleter
 *			   Martin Schaller
 *			   Andreas Schwab
 *
 *  Hardware cursor support added by Emmanuel Marty (core@ggi-project.org)
 *  Smart redraw scrolling, arbitrary font width support, 512char font support
 *  and software scrollback added by 
 *                         Jakub Jelinek (jj@ultra.linux.cz)
 *
 *  Random hacking by Martin Mares <mj@ucw.cz>
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 *  The low level operations for the various display memory organizations are
 *  now in separate source files.
 *
 *  Currently the following organizations are supported:
 *
 *    o afb			Amiga bitplanes
 *    o cfb{2,4,8,16,24,32}	Packed pixels
 *    o ilbm			Amiga interleaved bitplanes
 *    o iplan2p[248]		Atari interleaved bitplanes
 *    o mfb			Monochrome
 *    o vga			VGA characters/attributes
 *
 *  To do:
 *
 *    - Implement 16 plane mode (iplan2p16)
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#undef FBCONDEBUG

#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/delay.h>	/* MSch: for IRQ probe */
#include <linux/console.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/font.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/crc32.h> /* For counting font checksums */
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#ifdef CONFIG_ATARI
#include <asm/atariints.h>
#endif
#ifdef CONFIG_MAC
#include <asm/macints.h>
#endif
#if defined(__mc68000__) || defined(CONFIG_APUS)
#include <asm/machdep.h>
#include <asm/setup.h>
#endif

#include "fbcon.h"

#ifdef FBCONDEBUG
#  define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif

enum {
	FBCON_LOGO_CANSHOW	= -1,	/* the logo can be shown */
	FBCON_LOGO_DRAW		= -2,	/* draw the logo to a console */
	FBCON_LOGO_DONTSHOW	= -3	/* do not show the logo */
};

static struct display fb_display[MAX_NR_CONSOLES];

static signed char con2fb_map[MAX_NR_CONSOLES];
static signed char con2fb_map_boot[MAX_NR_CONSOLES];
#ifndef MODULE
static int logo_height;
#endif
static int logo_lines;
/* logo_shown is an index to vc_cons when >= 0; otherwise follows FBCON_LOGO
   enums.  */
static int logo_shown = FBCON_LOGO_CANSHOW;
/* Software scrollback */
static int fbcon_softback_size = 32768;
static unsigned long softback_buf, softback_curr;
static unsigned long softback_in;
static unsigned long softback_top, softback_end;
static int softback_lines;
/* console mappings */
static int first_fb_vc;
static int last_fb_vc = MAX_NR_CONSOLES - 1;
static int fbcon_is_default = 1; 
static int fbcon_has_exited;

/* font data */
static char fontname[40];

/* current fb_info */
static int info_idx = -1;

/* console rotation */
static int rotate;
static int fbcon_has_sysfs;

static const struct consw fb_con;

#define CM_SOFTBACK	(8)

#define advance_row(p, delta) (unsigned short *)((unsigned long)(p) + (delta) * vc->vc_size_row)

static int fbcon_set_origin(struct vc_data *);

#define CURSOR_DRAW_DELAY		(1)

/* # VBL ints between cursor state changes */
#define ATARI_CURSOR_BLINK_RATE		(42)
#define MAC_CURSOR_BLINK_RATE		(32)
#define DEFAULT_CURSOR_BLINK_RATE	(20)

static int vbl_cursor_cnt;

#define divides(a, b)	((!(a) || (b)%(a)) ? 0 : 1)

/*
 *  Interface used by the world
 */

static const char *fbcon_startup(void);
static void fbcon_init(struct vc_data *vc, int init);
static void fbcon_deinit(struct vc_data *vc);
static void fbcon_clear(struct vc_data *vc, int sy, int sx, int height,
			int width);
static void fbcon_putc(struct vc_data *vc, int c, int ypos, int xpos);
static void fbcon_putcs(struct vc_data *vc, const unsigned short *s,
			int count, int ypos, int xpos);
static void fbcon_clear_margins(struct vc_data *vc, int bottom_only);
static void fbcon_cursor(struct vc_data *vc, int mode);
static int fbcon_scroll(struct vc_data *vc, int t, int b, int dir,
			int count);
static void fbcon_bmove(struct vc_data *vc, int sy, int sx, int dy, int dx,
			int height, int width);
static int fbcon_switch(struct vc_data *vc);
static int fbcon_blank(struct vc_data *vc, int blank, int mode_switch);
static int fbcon_set_palette(struct vc_data *vc, unsigned char *table);
static int fbcon_scrolldelta(struct vc_data *vc, int lines);

/*
 *  Internal routines
 */
static __inline__ void ywrap_up(struct vc_data *vc, int count);
static __inline__ void ywrap_down(struct vc_data *vc, int count);
static __inline__ void ypan_up(struct vc_data *vc, int count);
static __inline__ void ypan_down(struct vc_data *vc, int count);
static void fbcon_bmove_rec(struct vc_data *vc, struct display *p, int sy, int sx,
			    int dy, int dx, int height, int width, u_int y_break);
static void fbcon_set_disp(struct fb_info *info, struct fb_var_screeninfo *var,
			   struct vc_data *vc);
static void fbcon_preset_disp(struct fb_info *info, struct fb_var_screeninfo *var,
			      int unit);
static void fbcon_redraw_move(struct vc_data *vc, struct display *p,
			      int line, int count, int dy);
static void fbcon_modechanged(struct fb_info *info);
static void fbcon_set_all_vcs(struct fb_info *info);
static void fbcon_start(void);
static void fbcon_exit(void);
static struct class_device *fbcon_class_device;

#ifdef CONFIG_MAC
/*
 * On the Macintoy, there may or may not be a working VBL int. We need to probe
 */
static int vbl_detected;

static irqreturn_t fb_vbl_detect(int irq, void *dummy)
{
	vbl_detected++;
	return IRQ_HANDLED;
}
#endif

#ifdef CONFIG_FRAMEBUFFER_CONSOLE_ROTATION
static inline void fbcon_set_rotation(struct fb_info *info)
{
	struct fbcon_ops *ops = info->fbcon_par;

	if (!(info->flags & FBINFO_MISC_TILEBLITTING) &&
	    ops->p->con_rotate < 4)
		ops->rotate = ops->p->con_rotate;
	else
		ops->rotate = 0;
}

static void fbcon_rotate(struct fb_info *info, u32 rotate)
{
	struct fbcon_ops *ops= info->fbcon_par;
	struct fb_info *fb_info;

	if (!ops || ops->currcon == -1)
		return;

	fb_info = registered_fb[con2fb_map[ops->currcon]];

	if (info == fb_info) {
		struct display *p = &fb_display[ops->currcon];

		if (rotate < 4)
			p->con_rotate = rotate;
		else
			p->con_rotate = 0;

		fbcon_modechanged(info);
	}
}

static void fbcon_rotate_all(struct fb_info *info, u32 rotate)
{
	struct fbcon_ops *ops = info->fbcon_par;
	struct vc_data *vc;
	struct display *p;
	int i;

	if (!ops || ops->currcon < 0 || rotate > 3)
		return;

	for (i = first_fb_vc; i <= last_fb_vc; i++) {
		vc = vc_cons[i].d;
		if (!vc || vc->vc_mode != KD_TEXT ||
		    registered_fb[con2fb_map[i]] != info)
			continue;

		p = &fb_display[vc->vc_num];
		p->con_rotate = rotate;
	}

	fbcon_set_all_vcs(info);
}
#else
static inline void fbcon_set_rotation(struct fb_info *info)
{
	struct fbcon_ops *ops = info->fbcon_par;

	ops->rotate = FB_ROTATE_UR;
}

static void fbcon_rotate(struct fb_info *info, u32 rotate)
{
	return;
}

static void fbcon_rotate_all(struct fb_info *info, u32 rotate)
{
	return;
}
#endif /* CONFIG_FRAMEBUFFER_CONSOLE_ROTATION */

static int fbcon_get_rotate(struct fb_info *info)
{
	struct fbcon_ops *ops = info->fbcon_par;

	return (ops) ? ops->rotate : 0;
}

static inline int fbcon_is_inactive(struct vc_data *vc, struct fb_info *info)
{
	struct fbcon_ops *ops = info->fbcon_par;

	return (info->state != FBINFO_STATE_RUNNING ||
		vc->vc_mode != KD_TEXT || ops->graphics);
}

static inline int get_color(struct vc_data *vc, struct fb_info *info,
	      u16 c, int is_fg)
{
	int depth = fb_get_color_depth(&info->var, &info->fix);
	int color = 0;

	if (console_blanked) {
		unsigned short charmask = vc->vc_hi_font_mask ? 0x1ff : 0xff;

		c = vc->vc_video_erase_char & charmask;
	}

	if (depth != 1)
		color = (is_fg) ? attr_fgcol((vc->vc_hi_font_mask) ? 9 : 8, c)
			: attr_bgcol((vc->vc_hi_font_mask) ? 13 : 12, c);

	switch (depth) {
	case 1:
	{
		int col = ~(0xfff << (max(info->var.green.length,
					  max(info->var.red.length,
					      info->var.blue.length)))) & 0xff;

		/* 0 or 1 */
		int fg = (info->fix.visual != FB_VISUAL_MONO01) ? col : 0;
		int bg = (info->fix.visual != FB_VISUAL_MONO01) ? 0 : col;

		if (console_blanked)
			fg = bg;

		color = (is_fg) ? fg : bg;
		break;
	}
	case 2:
		/*
		 * Scale down 16-colors to 4 colors. Default 4-color palette
		 * is grayscale. However, simply dividing the values by 4
		 * will not work, as colors 1, 2 and 3 will be scaled-down
		 * to zero rendering them invisible.  So empirically convert
		 * colors to a sane 4-level grayscale.
		 */
		switch (color) {
		case 0:
			color = 0; /* black */
			break;
		case 1 ... 6:
			color = 2; /* white */
			break;
		case 7 ... 8:
			color = 1; /* gray */
			break;
		default:
			color = 3; /* intense white */
			break;
		}
		break;
	case 3:
		/*
		 * Last 8 entries of default 16-color palette is a more intense
		 * version of the first 8 (i.e., same chrominance, different
		 * luminance).
		 */
		color &= 7;
		break;
	}


	return color;
}

static void fbcon_update_softback(struct vc_data *vc)
{
	int l = fbcon_softback_size / vc->vc_size_row;

	if (l > 5)
		softback_end = softback_buf + l * vc->vc_size_row;
	else
		/* Smaller scrollback makes no sense, and 0 would screw
		   the operation totally */
		softback_top = 0;
}

static void fb_flashcursor(struct work_struct *work)
{
	struct fb_info *info = container_of(work, struct fb_info, queue);
	struct fbcon_ops *ops = info->fbcon_par;
	struct display *p;
	struct vc_data *vc = NULL;
	int c;
	int mode;

	acquire_console_sem();
	if (ops && ops->currcon != -1)
		vc = vc_cons[ops->currcon].d;

	if (!vc || !CON_IS_VISIBLE(vc) ||
 	    registered_fb[con2fb_map[vc->vc_num]] != info ||
	    vc->vc_deccm != 1) {
		release_console_sem();
		return;
	}

	p = &fb_display[vc->vc_num];
	c = scr_readw((u16 *) vc->vc_pos);
	mode = (!ops->cursor_flash || ops->cursor_state.enable) ?
		CM_ERASE : CM_DRAW;
	ops->cursor(vc, info, mode, softback_lines, get_color(vc, info, c, 1),
		    get_color(vc, info, c, 0));
	release_console_sem();
}

#if defined(CONFIG_ATARI) || defined(CONFIG_MAC)
static int cursor_blink_rate;
static irqreturn_t fb_vbl_handler(int irq, void *dev_id)
{
	struct fb_info *info = dev_id;

	if (vbl_cursor_cnt && --vbl_cursor_cnt == 0) {
		schedule_work(&info->queue);	
		vbl_cursor_cnt = cursor_blink_rate; 
	}
	return IRQ_HANDLED;
}
#endif
	
static void cursor_timer_handler(unsigned long dev_addr)
{
	struct fb_info *info = (struct fb_info *) dev_addr;
	struct fbcon_ops *ops = info->fbcon_par;

	schedule_work(&info->queue);
	mod_timer(&ops->cursor_timer, jiffies + HZ/5);
}

static void fbcon_add_cursor_timer(struct fb_info *info)
{
	struct fbcon_ops *ops = info->fbcon_par;

	if ((!info->queue.func || info->queue.func == fb_flashcursor) &&
	    !(ops->flags & FBCON_FLAGS_CURSOR_TIMER)) {
		if (!info->queue.func)
			INIT_WORK(&info->queue, fb_flashcursor);

		init_timer(&ops->cursor_timer);
		ops->cursor_timer.function = cursor_timer_handler;
		ops->cursor_timer.expires = jiffies + HZ / 5;
		ops->cursor_timer.data = (unsigned long ) info;
		add_timer(&ops->cursor_timer);
		ops->flags |= FBCON_FLAGS_CURSOR_TIMER;
	}
}

static void fbcon_del_cursor_timer(struct fb_info *info)
{
	struct fbcon_ops *ops = info->fbcon_par;

	if (info->queue.func == fb_flashcursor &&
	    ops->flags & FBCON_FLAGS_CURSOR_TIMER) {
		del_timer_sync(&ops->cursor_timer);
		ops->flags &= ~FBCON_FLAGS_CURSOR_TIMER;
	}
}

#ifndef MODULE
static int __init fb_console_setup(char *this_opt)
{
	char *options;
	int i, j;

	if (!this_opt || !*this_opt)
		return 1;

	while ((options = strsep(&this_opt, ",")) != NULL) {
		if (!strncmp(options, "font:", 5))
			strcpy(fontname, options + 5);
		
		if (!strncmp(options, "scrollback:", 11)) {
			options += 11;
			if (*options) {
				fbcon_softback_size = simple_strtoul(options, &options, 0);
				if (*options == 'k' || *options == 'K') {
					fbcon_softback_size *= 1024;
					options++;
				}
				if (*options != ',')
					return 1;
				options++;
			} else
				return 1;
		}
		
		if (!strncmp(options, "map:", 4)) {
			options += 4;
			if (*options)
				for (i = 0, j = 0; i < MAX_NR_CONSOLES; i++) {
					if (!options[j])
						j = 0;
					con2fb_map_boot[i] =
						(options[j++]-'0') % FB_MAX;
				}
			return 1;
		}

		if (!strncmp(options, "vc:", 3)) {
			options += 3;
			if (*options)
				first_fb_vc = simple_strtoul(options, &options, 10) - 1;
			if (first_fb_vc < 0)
				first_fb_vc = 0;
			if (*options++ == '-')
				last_fb_vc = simple_strtoul(options, &options, 10) - 1;
			fbcon_is_default = 0; 
		}	

		if (!strncmp(options, "rotate:", 7)) {
			options += 7;
			if (*options)
				rotate = simple_strtoul(options, &options, 0);
			if (rotate > 3)
				rotate = 0;
		}
	}
	return 1;
}

__setup("fbcon=", fb_console_setup);
#endif

static int search_fb_in_map(int idx)
{
	int i, retval = 0;

	for (i = first_fb_vc; i <= last_fb_vc; i++) {
		if (con2fb_map[i] == idx)
			retval = 1;
	}
	return retval;
}

static int search_for_mapped_con(void)
{
	int i, retval = 0;

	for (i = first_fb_vc; i <= last_fb_vc; i++) {
		if (con2fb_map[i] != -1)
			retval = 1;
	}
	return retval;
}

static int fbcon_takeover(int show_logo)
{
	int err, i;

	if (!num_registered_fb)
		return -ENODEV;

	if (!show_logo)
		logo_shown = FBCON_LOGO_DONTSHOW;

	for (i = first_fb_vc; i <= last_fb_vc; i++)
		con2fb_map[i] = info_idx;

	err = take_over_console(&fb_con, first_fb_vc, last_fb_vc,
				fbcon_is_default);

	if (err) {
		for (i = first_fb_vc; i <= last_fb_vc; i++) {
			con2fb_map[i] = -1;
		}
		info_idx = -1;
	}

	return err;
}

#ifdef MODULE
static void fbcon_prepare_logo(struct vc_data *vc, struct fb_info *info,
			       int cols, int rows, int new_cols, int new_rows)
{
	logo_shown = FBCON_LOGO_DONTSHOW;
}
#else
static void fbcon_prepare_logo(struct vc_data *vc, struct fb_info *info,
			       int cols, int rows, int new_cols, int new_rows)
{
	/* Need to make room for the logo */
	struct fbcon_ops *ops = info->fbcon_par;
	int cnt, erase = vc->vc_video_erase_char, step;
	unsigned short *save = NULL, *r, *q;

	if (info->flags & FBINFO_MODULE) {
		logo_shown = FBCON_LOGO_DONTSHOW;
		return;
	}

	/*
	 * remove underline attribute from erase character
	 * if black and white framebuffer.
	 */
	if (fb_get_color_depth(&info->var, &info->fix) == 1)
		erase &= ~0x400;
	logo_height = fb_prepare_logo(info, ops->rotate);
	logo_lines = (logo_height + vc->vc_font.height - 1) /
		vc->vc_font.height;
	q = (unsigned short *) (vc->vc_origin +
				vc->vc_size_row * rows);
	step = logo_lines * cols;
	for (r = q - logo_lines * cols; r < q; r++)
		if (scr_readw(r) != vc->vc_video_erase_char)
			break;
	if (r != q && new_rows >= rows + logo_lines) {
		save = kmalloc(logo_lines * new_cols * 2, GFP_KERNEL);
		if (save) {
			int i = cols < new_cols ? cols : new_cols;
			scr_memsetw(save, erase, logo_lines * new_cols * 2);
			r = q - step;
			for (cnt = 0; cnt < logo_lines; cnt++, r += i)
				scr_memcpyw(save + cnt * new_cols, r, 2 * i);
			r = q;
		}
	}
	if (r == q) {
		/* We can scroll screen down */
		r = q - step - cols;
		for (cnt = rows - logo_lines; cnt > 0; cnt--) {
			scr_memcpyw(r + step, r, vc->vc_size_row);
			r -= cols;
		}
		if (!save) {
			int lines;
			if (vc->vc_y + logo_lines >= rows)
				lines = rows - vc->vc_y - 1;
			else
				lines = logo_lines;
			vc->vc_y += lines;
			vc->vc_pos += lines * vc->vc_size_row;
		}
	}
	scr_memsetw((unsigned short *) vc->vc_origin,
		    erase,
		    vc->vc_size_row * logo_lines);

	if (CON_IS_VISIBLE(vc) && vc->vc_mode == KD_TEXT) {
		fbcon_clear_margins(vc, 0);
		update_screen(vc);
	}

	if (save) {
		q = (unsigned short *) (vc->vc_origin +
					vc->vc_size_row *
					rows);
		scr_memcpyw(q, save, logo_lines * new_cols * 2);
		vc->vc_y += logo_lines;
		vc->vc_pos += logo_lines * vc->vc_size_row;
		kfree(save);
	}

	if (logo_lines > vc->vc_bottom) {
		logo_shown = FBCON_LOGO_CANSHOW;
		printk(KERN_INFO
		       "fbcon_init: disable boot-logo (boot-logo bigger than screen).\n");
	} else if (logo_shown != FBCON_LOGO_DONTSHOW) {
		logo_shown = FBCON_LOGO_DRAW;
		vc->vc_top = logo_lines;
	}
}
#endif /* MODULE */

#ifdef CONFIG_FB_TILEBLITTING
static void set_blitting_type(struct vc_data *vc, struct fb_info *info)
{
	struct fbcon_ops *ops = info->fbcon_par;

	ops->p = &fb_display[vc->vc_num];

	if ((info->flags & FBINFO_MISC_TILEBLITTING))
		fbcon_set_tileops(vc, info);
	else {
		fbcon_set_rotation(info);
		fbcon_set_bitops(ops);
	}
}

static int fbcon_invalid_charcount(struct fb_info *info, unsigned charcount)
{
	int err = 0;

	if (info->flags & FBINFO_MISC_TILEBLITTING &&
	    info->tileops->fb_get_tilemax(info) < charcount)
		err = 1;

	return err;
}
#else
static void set_blitting_type(struct vc_data *vc, struct fb_info *info)
{
	struct fbcon_ops *ops = info->fbcon_par;

	info->flags &= ~FBINFO_MISC_TILEBLITTING;
	ops->p = &fb_display[vc->vc_num];
	fbcon_set_rotation(info);
	fbcon_set_bitops(ops);
}

static int fbcon_invalid_charcount(struct fb_info *info, unsigned charcount)
{
	return 0;
}

#endif /* CONFIG_MISC_TILEBLITTING */


static int con2fb_acquire_newinfo(struct vc_data *vc, struct fb_info *info,
				  int unit, int oldidx)
{
	struct fbcon_ops *ops = NULL;
	int err = 0;

	if (!try_module_get(info->fbops->owner))
		err = -ENODEV;

	if (!err && info->fbops->fb_open &&
	    info->fbops->fb_open(info, 0))
		err = -ENODEV;

	if (!err) {
		ops = kzalloc(sizeof(struct fbcon_ops), GFP_KERNEL);
		if (!ops)
			err = -ENOMEM;
	}

	if (!err) {
		info->fbcon_par = ops;
		set_blitting_type(vc, info);
	}

	if (err) {
		con2fb_map[unit] = oldidx;
		module_put(info->fbops->owner);
	}

	return err;
}

static int con2fb_release_oldinfo(struct vc_data *vc, struct fb_info *oldinfo,
				  struct fb_info *newinfo, int unit,
				  int oldidx, int found)
{
	struct fbcon_ops *ops = oldinfo->fbcon_par;
	int err = 0;

	if (oldinfo->fbops->fb_release &&
	    oldinfo->fbops->fb_release(oldinfo, 0)) {
		con2fb_map[unit] = oldidx;
		if (!found && newinfo->fbops->fb_release)
			newinfo->fbops->fb_release(newinfo, 0);
		if (!found)
			module_put(newinfo->fbops->owner);
		err = -ENODEV;
	}

	if (!err) {
		fbcon_del_cursor_timer(oldinfo);
		kfree(ops->cursor_state.mask);
		kfree(ops->cursor_data);
		kfree(ops->fontbuffer);
		kfree(oldinfo->fbcon_par);
		oldinfo->fbcon_par = NULL;
		module_put(oldinfo->fbops->owner);
		/*
		  If oldinfo and newinfo are driving the same hardware,
		  the fb_release() method of oldinfo may attempt to
		  restore the hardware state.  This will leave the
		  newinfo in an undefined state. Thus, a call to
		  fb_set_par() may be needed for the newinfo.
		*/
		if (newinfo->fbops->fb_set_par)
			newinfo->fbops->fb_set_par(newinfo);
	}

	return err;
}

static void con2fb_init_display(struct vc_data *vc, struct fb_info *info,
				int unit, int show_logo)
{
	struct fbcon_ops *ops = info->fbcon_par;

	ops->currcon = fg_console;

	if (info->fbops->fb_set_par && !(ops->flags & FBCON_FLAGS_INIT))
		info->fbops->fb_set_par(info);

	ops->flags |= FBCON_FLAGS_INIT;
	ops->graphics = 0;

	if (vc)
		fbcon_set_disp(info, &info->var, vc);
	else
		fbcon_preset_disp(info, &info->var, unit);

	if (show_logo) {
		struct vc_data *fg_vc = vc_cons[fg_console].d;
		struct fb_info *fg_info =
			registered_fb[con2fb_map[fg_console]];

		fbcon_prepare_logo(fg_vc, fg_info, fg_vc->vc_cols,
				   fg_vc->vc_rows, fg_vc->vc_cols,
				   fg_vc->vc_rows);
	}

	update_screen(vc_cons[fg_console].d);
}

/**
 *	set_con2fb_map - map console to frame buffer device
 *	@unit: virtual console number to map
 *	@newidx: frame buffer index to map virtual console to
 *      @user: user request
 *
 *	Maps a virtual console @unit to a frame buffer device
 *	@newidx.
 */
static int set_con2fb_map(int unit, int newidx, int user)
{
	struct vc_data *vc = vc_cons[unit].d;
	int oldidx = con2fb_map[unit];
	struct fb_info *info = registered_fb[newidx];
	struct fb_info *oldinfo = NULL;
 	int found, err = 0;

	if (oldidx == newidx)
		return 0;

	if (!info || fbcon_has_exited)
		return -EINVAL;

 	if (!err && !search_for_mapped_con()) {
		info_idx = newidx;
		return fbcon_takeover(0);
	}

	if (oldidx != -1)
		oldinfo = registered_fb[oldidx];

	found = search_fb_in_map(newidx);

	acquire_console_sem();
	con2fb_map[unit] = newidx;
	if (!err && !found)
 		err = con2fb_acquire_newinfo(vc, info, unit, oldidx);


	/*
	 * If old fb is not mapped to any of the consoles,
	 * fbcon should release it.
	 */
 	if (!err && oldinfo && !search_fb_in_map(oldidx))
 		err = con2fb_release_oldinfo(vc, oldinfo, info, unit, oldidx,
 					     found);

 	if (!err) {
 		int show_logo = (fg_console == 0 && !user &&
 				 logo_shown != FBCON_LOGO_DONTSHOW);

 		if (!found)
 			fbcon_add_cursor_timer(info);
 		con2fb_map_boot[unit] = newidx;
 		con2fb_init_display(vc, info, unit, show_logo);
	}

	if (!search_fb_in_map(info_idx))
		info_idx = newidx;

	release_console_sem();
 	return err;
}

/*
 *  Low Level Operations
 */
/* NOTE: fbcon cannot be __init: it may be called from take_over_console later */
static int var_to_display(struct display *disp,
			  struct fb_var_screeninfo *var,
			  struct fb_info *info)
{
	disp->xres_virtual = var->xres_virtual;
	disp->yres_virtual = var->yres_virtual;
	disp->bits_per_pixel = var->bits_per_pixel;
	disp->grayscale = var->grayscale;
	disp->nonstd = var->nonstd;
	disp->accel_flags = var->accel_flags;
	disp->height = var->height;
	disp->width = var->width;
	disp->red = var->red;
	disp->green = var->green;
	disp->blue = var->blue;
	disp->transp = var->transp;
	disp->rotate = var->rotate;
	disp->mode = fb_match_mode(var, &info->modelist);
	if (disp->mode == NULL)
		/* This should not happen */
		return -EINVAL;
	return 0;
}

static void display_to_var(struct fb_var_screeninfo *var,
			   struct display *disp)
{
	fb_videomode_to_var(var, disp->mode);
	var->xres_virtual = disp->xres_virtual;
	var->yres_virtual = disp->yres_virtual;
	var->bits_per_pixel = disp->bits_per_pixel;
	var->grayscale = disp->grayscale;
	var->nonstd = disp->nonstd;
	var->accel_flags = disp->accel_flags;
	var->height = disp->height;
	var->width = disp->width;
	var->red = disp->red;
	var->green = disp->green;
	var->blue = disp->blue;
	var->transp = disp->transp;
	var->rotate = disp->rotate;
}

static const char *fbcon_startup(void)
{
	const char *display_desc = "frame buffer device";
	struct display *p = &fb_display[fg_console];
	struct vc_data *vc = vc_cons[fg_console].d;
	const struct font_desc *font = NULL;
	struct module *owner;
	struct fb_info *info = NULL;
	struct fbcon_ops *ops;
	int rows, cols;
	int irqres;

	irqres = 1;
	/*
	 *  If num_registered_fb is zero, this is a call for the dummy part.
	 *  The frame buffer devices weren't initialized yet.
	 */
	if (!num_registered_fb || info_idx == -1)
		return display_desc;
	/*
	 * Instead of blindly using registered_fb[0], we use info_idx, set by
	 * fb_console_init();
	 */
	info = registered_fb[info_idx];
	if (!info)
		return NULL;
	
	owner = info->fbops->owner;
	if (!try_module_get(owner))
		return NULL;
	if (info->fbops->fb_open && info->fbops->fb_open(info, 0)) {
		module_put(owner);
		return NULL;
	}

	ops = kzalloc(sizeof(struct fbcon_ops), GFP_KERNEL);
	if (!ops) {
		module_put(owner);
		return NULL;
	}

	ops->currcon = -1;
	ops->graphics = 1;
	ops->cur_rotate = -1;
	info->fbcon_par = ops;
	p->con_rotate = rotate;
	set_blitting_type(vc, info);

	if (info->fix.type != FB_TYPE_TEXT) {
		if (fbcon_softback_size) {
			if (!softback_buf) {
				softback_buf =
				    (unsigned long)
				    kmalloc(fbcon_softback_size,
					    GFP_KERNEL);
				if (!softback_buf) {
					fbcon_softback_size = 0;
					softback_top = 0;
				}
			}
		} else {
			if (softback_buf) {
				kfree((void *) softback_buf);
				softback_buf = 0;
				softback_top = 0;
			}
		}
		if (softback_buf)
			softback_in = softback_top = softback_curr =
			    softback_buf;
		softback_lines = 0;
	}

	/* Setup default font */
	if (!p->fontdata) {
		if (!fontname[0] || !(font = find_font(fontname)))
			font = get_default_font(info->var.xres,
						info->var.yres,
						info->pixmap.blit_x,
						info->pixmap.blit_y);
		vc->vc_font.width = font->width;
		vc->vc_font.height = font->height;
		vc->vc_font.data = (void *)(p->fontdata = font->data);
		vc->vc_font.charcount = 256; /* FIXME  Need to support more fonts */
	}

	cols = FBCON_SWAP(ops->rotate, info->var.xres, info->var.yres);
	rows = FBCON_SWAP(ops->rotate, info->var.yres, info->var.xres);
	cols /= vc->vc_font.width;
	rows /= vc->vc_font.height;
	vc_resize(vc, cols, rows);

	DPRINTK("mode:   %s\n", info->fix.id);
	DPRINTK("visual: %d\n", info->fix.visual);
	DPRINTK("res:    %dx%d-%d\n", info->var.xres,
		info->var.yres,
		info->var.bits_per_pixel);

#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI) {
		cursor_blink_rate = ATARI_CURSOR_BLINK_RATE;
		irqres =
		    request_irq(IRQ_AUTO_4, fb_vbl_handler,
				IRQ_TYPE_PRIO, "framebuffer vbl",
				info);
	}
#endif				/* CONFIG_ATARI */

#ifdef CONFIG_MAC
	/*
	 * On a Macintoy, the VBL interrupt may or may not be active. 
	 * As interrupt based cursor is more reliable and race free, we 
	 * probe for VBL interrupts.
	 */
	if (MACH_IS_MAC) {
		int ct = 0;
		/*
		 * Probe for VBL: set temp. handler ...
		 */
		irqres = request_irq(IRQ_MAC_VBL, fb_vbl_detect, 0,
				     "framebuffer vbl", info);
		vbl_detected = 0;

		/*
		 * ... and spin for 20 ms ...
		 */
		while (!vbl_detected && ++ct < 1000)
			udelay(20);

		if (ct == 1000)
			printk
			    ("fbcon_startup: No VBL detected, using timer based cursor.\n");

		free_irq(IRQ_MAC_VBL, fb_vbl_detect);

		if (vbl_detected) {
			/*
			 * interrupt based cursor ok
			 */
			cursor_blink_rate = MAC_CURSOR_BLINK_RATE;
			irqres =
			    request_irq(IRQ_MAC_VBL, fb_vbl_handler, 0,
					"framebuffer vbl", info);
		} else {
			/*
			 * VBL not detected: fall through, use timer based cursor
			 */
			irqres = 1;
		}
	}
#endif				/* CONFIG_MAC */

	fbcon_add_cursor_timer(info);
	fbcon_has_exited = 0;
	return display_desc;
}

static void fbcon_init(struct vc_data *vc, int init)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct fbcon_ops *ops;
	struct vc_data **default_mode = vc->vc_display_fg;
	struct vc_data *svc = *default_mode;
	struct display *t, *p = &fb_display[vc->vc_num];
	int logo = 1, new_rows, new_cols, rows, cols, charcnt = 256;
	int cap;

	if (info_idx == -1 || info == NULL)
	    return;

	cap = info->flags;

	if (vc != svc || logo_shown == FBCON_LOGO_DONTSHOW ||
	    (info->fix.type == FB_TYPE_TEXT))
		logo = 0;

	if (var_to_display(p, &info->var, info))
		return;

	/* If we are not the first console on this
	   fb, copy the font from that console */
	t = &fb_display[fg_console];
	if (!p->fontdata) {
		if (t->fontdata) {
			struct vc_data *fvc = vc_cons[fg_console].d;

			vc->vc_font.data = (void *)(p->fontdata =
						    fvc->vc_font.data);
			vc->vc_font.width = fvc->vc_font.width;
			vc->vc_font.height = fvc->vc_font.height;
			p->userfont = t->userfont;

			if (p->userfont)
				REFCOUNT(p->fontdata)++;
		} else {
			const struct font_desc *font = NULL;

			if (!fontname[0] || !(font = find_font(fontname)))
				font = get_default_font(info->var.xres,
							info->var.yres,
							info->pixmap.blit_x,
							info->pixmap.blit_y);
			vc->vc_font.width = font->width;
			vc->vc_font.height = font->height;
			vc->vc_font.data = (void *)(p->fontdata = font->data);
			vc->vc_font.charcount = 256; /* FIXME  Need to
							support more fonts */
		}
	}

	if (p->userfont)
		charcnt = FNTCHARCNT(p->fontdata);

	vc->vc_can_do_color = (fb_get_color_depth(&info->var, &info->fix)!=1);
	vc->vc_complement_mask = vc->vc_can_do_color ? 0x7700 : 0x0800;
	if (charcnt == 256) {
		vc->vc_hi_font_mask = 0;
	} else {
		vc->vc_hi_font_mask = 0x100;
		if (vc->vc_can_do_color)
			vc->vc_complement_mask <<= 1;
	}

	if (!*svc->vc_uni_pagedir_loc)
		con_set_default_unimap(svc);
	if (!*vc->vc_uni_pagedir_loc)
		con_copy_unimap(vc, svc);

	ops = info->fbcon_par;
	p->con_rotate = rotate;
	set_blitting_type(vc, info);

	cols = vc->vc_cols;
	rows = vc->vc_rows;
	new_cols = FBCON_SWAP(ops->rotate, info->var.xres, info->var.yres);
	new_rows = FBCON_SWAP(ops->rotate, info->var.yres, info->var.xres);
	new_cols /= vc->vc_font.width;
	new_rows /= vc->vc_font.height;
	vc_resize(vc, new_cols, new_rows);

	/*
	 * We must always set the mode. The mode of the previous console
	 * driver could be in the same resolution but we are using different
	 * hardware so we have to initialize the hardware.
	 *
	 * We need to do it in fbcon_init() to prevent screen corruption.
	 */
	if (CON_IS_VISIBLE(vc) && vc->vc_mode == KD_TEXT) {
		if (info->fbops->fb_set_par &&
		    !(ops->flags & FBCON_FLAGS_INIT))
			info->fbops->fb_set_par(info);
		ops->flags |= FBCON_FLAGS_INIT;
	}

	ops->graphics = 0;

	if ((cap & FBINFO_HWACCEL_COPYAREA) &&
	    !(cap & FBINFO_HWACCEL_DISABLED))
		p->scrollmode = SCROLL_MOVE;
	else /* default to something safe */
		p->scrollmode = SCROLL_REDRAW;

	/*
	 *  ++guenther: console.c:vc_allocate() relies on initializing
	 *  vc_{cols,rows}, but we must not set those if we are only
	 *  resizing the console.
	 */
	if (!init) {
		vc->vc_cols = new_cols;
		vc->vc_rows = new_rows;
	}

	if (logo)
		fbcon_prepare_logo(vc, info, cols, rows, new_cols, new_rows);

	if (vc == svc && softback_buf)
		fbcon_update_softback(vc);

	if (ops->rotate_font && ops->rotate_font(info, vc)) {
		ops->rotate = FB_ROTATE_UR;
		set_blitting_type(vc, info);
	}

	ops->p = &fb_display[fg_console];
}

static void fbcon_free_font(struct display *p)
{
	if (p->userfont && p->fontdata && (--REFCOUNT(p->fontdata) == 0))
		kfree(p->fontdata - FONT_EXTRA_WORDS * sizeof(int));
	p->fontdata = NULL;
	p->userfont = 0;
}

static void fbcon_deinit(struct vc_data *vc)
{
	struct display *p = &fb_display[vc->vc_num];
	struct fb_info *info;
	struct fbcon_ops *ops;
	int idx;

	fbcon_free_font(p);
	idx = con2fb_map[vc->vc_num];

	if (idx == -1)
		goto finished;

	info = registered_fb[idx];

	if (!info)
		goto finished;

	ops = info->fbcon_par;

	if (!ops)
		goto finished;

	if (CON_IS_VISIBLE(vc))
		fbcon_del_cursor_timer(info);

	ops->flags &= ~FBCON_FLAGS_INIT;
finished:

	if (!con_is_bound(&fb_con))
		fbcon_exit();

	return;
}

/* ====================================================================== */

/*  fbcon_XXX routines - interface used by the world
 *
 *  This system is now divided into two levels because of complications
 *  caused by hardware scrolling. Top level functions:
 *
 *	fbcon_bmove(), fbcon_clear(), fbcon_putc(), fbcon_clear_margins()
 *
 *  handles y values in range [0, scr_height-1] that correspond to real
 *  screen positions. y_wrap shift means that first line of bitmap may be
 *  anywhere on this display. These functions convert lineoffsets to
 *  bitmap offsets and deal with the wrap-around case by splitting blits.
 *
 *	fbcon_bmove_physical_8()    -- These functions fast implementations
 *	fbcon_clear_physical_8()    -- of original fbcon_XXX fns.
 *	fbcon_putc_physical_8()	    -- (font width != 8) may be added later
 *
 *  WARNING:
 *
 *  At the moment fbcon_putc() cannot blit across vertical wrap boundary
 *  Implies should only really hardware scroll in rows. Only reason for
 *  restriction is simplicity & efficiency at the moment.
 */

static void fbcon_clear(struct vc_data *vc, int sy, int sx, int height,
			int width)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct fbcon_ops *ops = info->fbcon_par;

	struct display *p = &fb_display[vc->vc_num];
	u_int y_break;

	if (fbcon_is_inactive(vc, info))
		return;

	if (!height || !width)
		return;

	/* Split blits that cross physical y_wrap boundary */

	y_break = p->vrows - p->yscroll;
	if (sy < y_break && sy + height - 1 >= y_break) {
		u_int b = y_break - sy;
		ops->clear(vc, info, real_y(p, sy), sx, b, width);
		ops->clear(vc, info, real_y(p, sy + b), sx, height - b,
				 width);
	} else
		ops->clear(vc, info, real_y(p, sy), sx, height, width);
}

static void fbcon_putcs(struct vc_data *vc, const unsigned short *s,
			int count, int ypos, int xpos)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];
	struct fbcon_ops *ops = info->fbcon_par;

	if (!fbcon_is_inactive(vc, info))
		ops->putcs(vc, info, s, count, real_y(p, ypos), xpos,
			   get_color(vc, info, scr_readw(s), 1),
			   get_color(vc, info, scr_readw(s), 0));
}

static void fbcon_putc(struct vc_data *vc, int c, int ypos, int xpos)
{
	unsigned short chr;

	scr_writew(c, &chr);
	fbcon_putcs(vc, &chr, 1, ypos, xpos);
}

static void fbcon_clear_margins(struct vc_data *vc, int bottom_only)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct fbcon_ops *ops = info->fbcon_par;

	if (!fbcon_is_inactive(vc, info))
		ops->clear_margins(vc, info, bottom_only);
}

static void fbcon_cursor(struct vc_data *vc, int mode)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct fbcon_ops *ops = info->fbcon_par;
	int y;
 	int c = scr_readw((u16 *) vc->vc_pos);

	if (fbcon_is_inactive(vc, info) || vc->vc_deccm != 1)
		return;

	ops->cursor_flash = (mode == CM_ERASE) ? 0 : 1;
	if (mode & CM_SOFTBACK) {
		mode &= ~CM_SOFTBACK;
		y = softback_lines;
	} else {
		if (softback_lines)
			fbcon_set_origin(vc);
		y = 0;
	}

	ops->cursor(vc, info, mode, y, get_color(vc, info, c, 1),
		    get_color(vc, info, c, 0));
	vbl_cursor_cnt = CURSOR_DRAW_DELAY;
}

static int scrollback_phys_max = 0;
static int scrollback_max = 0;
static int scrollback_current = 0;

/*
 * If no vc is existent yet, just set struct display
 */
static void fbcon_preset_disp(struct fb_info *info, struct fb_var_screeninfo *var,
			      int unit)
{
	struct display *p = &fb_display[unit];
	struct display *t = &fb_display[fg_console];

	if (var_to_display(p, var, info))
		return;

	p->fontdata = t->fontdata;
	p->userfont = t->userfont;
	if (p->userfont)
		REFCOUNT(p->fontdata)++;
}

static void fbcon_set_disp(struct fb_info *info, struct fb_var_screeninfo *var,
			   struct vc_data *vc)
{
	struct display *p = &fb_display[vc->vc_num], *t;
	struct vc_data **default_mode = vc->vc_display_fg;
	struct vc_data *svc = *default_mode;
	struct fbcon_ops *ops = info->fbcon_par;
	int rows, cols, charcnt = 256;

	if (var_to_display(p, var, info))
		return;
	t = &fb_display[svc->vc_num];
	if (!vc->vc_font.data) {
		vc->vc_font.data = (void *)(p->fontdata = t->fontdata);
		vc->vc_font.width = (*default_mode)->vc_font.width;
		vc->vc_font.height = (*default_mode)->vc_font.height;
		p->userfont = t->userfont;
		if (p->userfont)
			REFCOUNT(p->fontdata)++;
	}
	if (p->userfont)
		charcnt = FNTCHARCNT(p->fontdata);

	var->activate = FB_ACTIVATE_NOW;
	info->var.activate = var->activate;
	var->yoffset = info->var.yoffset;
	var->xoffset = info->var.xoffset;
	fb_set_var(info, var);
	ops->var = info->var;
	vc->vc_can_do_color = (fb_get_color_depth(&info->var, &info->fix)!=1);
	vc->vc_complement_mask = vc->vc_can_do_color ? 0x7700 : 0x0800;
	if (charcnt == 256) {
		vc->vc_hi_font_mask = 0;
	} else {
		vc->vc_hi_font_mask = 0x100;
		if (vc->vc_can_do_color)
			vc->vc_complement_mask <<= 1;
	}

	if (!*svc->vc_uni_pagedir_loc)
		con_set_default_unimap(svc);
	if (!*vc->vc_uni_pagedir_loc)
		con_copy_unimap(vc, svc);

	cols = FBCON_SWAP(ops->rotate, info->var.xres, info->var.yres);
	rows = FBCON_SWAP(ops->rotate, info->var.yres, info->var.xres);
	cols /= vc->vc_font.width;
	rows /= vc->vc_font.height;
	vc_resize(vc, cols, rows);

	if (CON_IS_VISIBLE(vc)) {
		update_screen(vc);
		if (softback_buf)
			fbcon_update_softback(vc);
	}
}

static __inline__ void ywrap_up(struct vc_data *vc, int count)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct fbcon_ops *ops = info->fbcon_par;
	struct display *p = &fb_display[vc->vc_num];
	
	p->yscroll += count;
	if (p->yscroll >= p->vrows)	/* Deal with wrap */
		p->yscroll -= p->vrows;
	ops->var.xoffset = 0;
	ops->var.yoffset = p->yscroll * vc->vc_font.height;
	ops->var.vmode |= FB_VMODE_YWRAP;
	ops->update_start(info);
	scrollback_max += count;
	if (scrollback_max > scrollback_phys_max)
		scrollback_max = scrollback_phys_max;
	scrollback_current = 0;
}

static __inline__ void ywrap_down(struct vc_data *vc, int count)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct fbcon_ops *ops = info->fbcon_par;
	struct display *p = &fb_display[vc->vc_num];
	
	p->yscroll -= count;
	if (p->yscroll < 0)	/* Deal with wrap */
		p->yscroll += p->vrows;
	ops->var.xoffset = 0;
	ops->var.yoffset = p->yscroll * vc->vc_font.height;
	ops->var.vmode |= FB_VMODE_YWRAP;
	ops->update_start(info);
	scrollback_max -= count;
	if (scrollback_max < 0)
		scrollback_max = 0;
	scrollback_current = 0;
}

static __inline__ void ypan_up(struct vc_data *vc, int count)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];
	struct fbcon_ops *ops = info->fbcon_par;

	p->yscroll += count;
	if (p->yscroll > p->vrows - vc->vc_rows) {
		ops->bmove(vc, info, p->vrows - vc->vc_rows,
			    0, 0, 0, vc->vc_rows, vc->vc_cols);
		p->yscroll -= p->vrows - vc->vc_rows;
	}

	ops->var.xoffset = 0;
	ops->var.yoffset = p->yscroll * vc->vc_font.height;
	ops->var.vmode &= ~FB_VMODE_YWRAP;
	ops->update_start(info);
	fbcon_clear_margins(vc, 1);
	scrollback_max += count;
	if (scrollback_max > scrollback_phys_max)
		scrollback_max = scrollback_phys_max;
	scrollback_current = 0;
}

static __inline__ void ypan_up_redraw(struct vc_data *vc, int t, int count)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct fbcon_ops *ops = info->fbcon_par;
	struct display *p = &fb_display[vc->vc_num];

	p->yscroll += count;

	if (p->yscroll > p->vrows - vc->vc_rows) {
		p->yscroll -= p->vrows - vc->vc_rows;
		fbcon_redraw_move(vc, p, t + count, vc->vc_rows - count, t);
	}

	ops->var.xoffset = 0;
	ops->var.yoffset = p->yscroll * vc->vc_font.height;
	ops->var.vmode &= ~FB_VMODE_YWRAP;
	ops->update_start(info);
	fbcon_clear_margins(vc, 1);
	scrollback_max += count;
	if (scrollback_max > scrollback_phys_max)
		scrollback_max = scrollback_phys_max;
	scrollback_current = 0;
}

static __inline__ void ypan_down(struct vc_data *vc, int count)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];
	struct fbcon_ops *ops = info->fbcon_par;
	
	p->yscroll -= count;
	if (p->yscroll < 0) {
		ops->bmove(vc, info, 0, 0, p->vrows - vc->vc_rows,
			    0, vc->vc_rows, vc->vc_cols);
		p->yscroll += p->vrows - vc->vc_rows;
	}

	ops->var.xoffset = 0;
	ops->var.yoffset = p->yscroll * vc->vc_font.height;
	ops->var.vmode &= ~FB_VMODE_YWRAP;
	ops->update_start(info);
	fbcon_clear_margins(vc, 1);
	scrollback_max -= count;
	if (scrollback_max < 0)
		scrollback_max = 0;
	scrollback_current = 0;
}

static __inline__ void ypan_down_redraw(struct vc_data *vc, int t, int count)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct fbcon_ops *ops = info->fbcon_par;
	struct display *p = &fb_display[vc->vc_num];

	p->yscroll -= count;

	if (p->yscroll < 0) {
		p->yscroll += p->vrows - vc->vc_rows;
		fbcon_redraw_move(vc, p, t, vc->vc_rows - count, t + count);
	}

	ops->var.xoffset = 0;
	ops->var.yoffset = p->yscroll * vc->vc_font.height;
	ops->var.vmode &= ~FB_VMODE_YWRAP;
	ops->update_start(info);
	fbcon_clear_margins(vc, 1);
	scrollback_max -= count;
	if (scrollback_max < 0)
		scrollback_max = 0;
	scrollback_current = 0;
}

static void fbcon_redraw_softback(struct vc_data *vc, struct display *p,
				  long delta)
{
	int count = vc->vc_rows;
	unsigned short *d, *s;
	unsigned long n;
	int line = 0;

	d = (u16 *) softback_curr;
	if (d == (u16 *) softback_in)
		d = (u16 *) vc->vc_origin;
	n = softback_curr + delta * vc->vc_size_row;
	softback_lines -= delta;
	if (delta < 0) {
		if (softback_curr < softback_top && n < softback_buf) {
			n += softback_end - softback_buf;
			if (n < softback_top) {
				softback_lines -=
				    (softback_top - n) / vc->vc_size_row;
				n = softback_top;
			}
		} else if (softback_curr >= softback_top
			   && n < softback_top) {
			softback_lines -=
			    (softback_top - n) / vc->vc_size_row;
			n = softback_top;
		}
	} else {
		if (softback_curr > softback_in && n >= softback_end) {
			n += softback_buf - softback_end;
			if (n > softback_in) {
				n = softback_in;
				softback_lines = 0;
			}
		} else if (softback_curr <= softback_in && n > softback_in) {
			n = softback_in;
			softback_lines = 0;
		}
	}
	if (n == softback_curr)
		return;
	softback_curr = n;
	s = (u16 *) softback_curr;
	if (s == (u16 *) softback_in)
		s = (u16 *) vc->vc_origin;
	while (count--) {
		unsigned short *start;
		unsigned short *le;
		unsigned short c;
		int x = 0;
		unsigned short attr = 1;

		start = s;
		le = advance_row(s, 1);
		do {
			c = scr_readw(s);
			if (attr != (c & 0xff00)) {
				attr = c & 0xff00;
				if (s > start) {
					fbcon_putcs(vc, start, s - start,
						    line, x);
					x += s - start;
					start = s;
				}
			}
			if (c == scr_readw(d)) {
				if (s > start) {
					fbcon_putcs(vc, start, s - start,
						    line, x);
					x += s - start + 1;
					start = s + 1;
				} else {
					x++;
					start++;
				}
			}
			s++;
			d++;
		} while (s < le);
		if (s > start)
			fbcon_putcs(vc, start, s - start, line, x);
		line++;
		if (d == (u16 *) softback_end)
			d = (u16 *) softback_buf;
		if (d == (u16 *) softback_in)
			d = (u16 *) vc->vc_origin;
		if (s == (u16 *) softback_end)
			s = (u16 *) softback_buf;
		if (s == (u16 *) softback_in)
			s = (u16 *) vc->vc_origin;
	}
}

static void fbcon_redraw_move(struct vc_data *vc, struct display *p,
			      int line, int count, int dy)
{
	unsigned short *s = (unsigned short *)
		(vc->vc_origin + vc->vc_size_row * line);

	while (count--) {
		unsigned short *start = s;
		unsigned short *le = advance_row(s, 1);
		unsigned short c;
		int x = 0;
		unsigned short attr = 1;

		do {
			c = scr_readw(s);
			if (attr != (c & 0xff00)) {
				attr = c & 0xff00;
				if (s > start) {
					fbcon_putcs(vc, start, s - start,
						    dy, x);
					x += s - start;
					start = s;
				}
			}
			console_conditional_schedule();
			s++;
		} while (s < le);
		if (s > start)
			fbcon_putcs(vc, start, s - start, dy, x);
		console_conditional_schedule();
		dy++;
	}
}

static void fbcon_redraw(struct vc_data *vc, struct display *p,
			 int line, int count, int offset)
{
	unsigned short *d = (unsigned short *)
	    (vc->vc_origin + vc->vc_size_row * line);
	unsigned short *s = d + offset;

	while (count--) {
		unsigned short *start = s;
		unsigned short *le = advance_row(s, 1);
		unsigned short c;
		int x = 0;
		unsigned short attr = 1;

		do {
			c = scr_readw(s);
			if (attr != (c & 0xff00)) {
				attr = c & 0xff00;
				if (s > start) {
					fbcon_putcs(vc, start, s - start,
						    line, x);
					x += s - start;
					start = s;
				}
			}
			if (c == scr_readw(d)) {
				if (s > start) {
					fbcon_putcs(vc, start, s - start,
						     line, x);
					x += s - start + 1;
					start = s + 1;
				} else {
					x++;
					start++;
				}
			}
			scr_writew(c, d);
			console_conditional_schedule();
			s++;
			d++;
		} while (s < le);
		if (s > start)
			fbcon_putcs(vc, start, s - start, line, x);
		console_conditional_schedule();
		if (offset > 0)
			line++;
		else {
			line--;
			/* NOTE: We subtract two lines from these pointers */
			s -= vc->vc_size_row;
			d -= vc->vc_size_row;
		}
	}
}

static inline void fbcon_softback_note(struct vc_data *vc, int t,
				       int count)
{
	unsigned short *p;

	if (vc->vc_num != fg_console)
		return;
	p = (unsigned short *) (vc->vc_origin + t * vc->vc_size_row);

	while (count) {
		scr_memcpyw((u16 *) softback_in, p, vc->vc_size_row);
		count--;
		p = advance_row(p, 1);
		softback_in += vc->vc_size_row;
		if (softback_in == softback_end)
			softback_in = softback_buf;
		if (softback_in == softback_top) {
			softback_top += vc->vc_size_row;
			if (softback_top == softback_end)
				softback_top = softback_buf;
		}
	}
	softback_curr = softback_in;
}

static int fbcon_scroll(struct vc_data *vc, int t, int b, int dir,
			int count)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];
	struct fbcon_ops *ops = info->fbcon_par;
	int scroll_partial = info->flags & FBINFO_PARTIAL_PAN_OK;

	if (fbcon_is_inactive(vc, info))
		return -EINVAL;

	fbcon_cursor(vc, CM_ERASE);

	/*
	 * ++Geert: Only use ywrap/ypan if the console is in text mode
	 * ++Andrew: Only use ypan on hardware text mode when scrolling the
	 *           whole screen (prevents flicker).
	 */

	switch (dir) {
	case SM_UP:
		if (count > vc->vc_rows)	/* Maximum realistic size */
			count = vc->vc_rows;
		if (softback_top)
			fbcon_softback_note(vc, t, count);
		if (logo_shown >= 0)
			goto redraw_up;
		switch (p->scrollmode) {
		case SCROLL_MOVE:
			ops->bmove(vc, info, t + count, 0, t, 0,
				    b - t - count, vc->vc_cols);
			ops->clear(vc, info, b - count, 0, count,
				  vc->vc_cols);
			break;

		case SCROLL_WRAP_MOVE:
			if (b - t - count > 3 * vc->vc_rows >> 2) {
				if (t > 0)
					fbcon_bmove(vc, 0, 0, count, 0, t,
						    vc->vc_cols);
				ywrap_up(vc, count);
				if (vc->vc_rows - b > 0)
					fbcon_bmove(vc, b - count, 0, b, 0,
						    vc->vc_rows - b,
						    vc->vc_cols);
			} else if (info->flags & FBINFO_READS_FAST)
				fbcon_bmove(vc, t + count, 0, t, 0,
					    b - t - count, vc->vc_cols);
			else
				goto redraw_up;
			fbcon_clear(vc, b - count, 0, count, vc->vc_cols);
			break;

		case SCROLL_PAN_REDRAW:
			if ((p->yscroll + count <=
			     2 * (p->vrows - vc->vc_rows))
			    && ((!scroll_partial && (b - t == vc->vc_rows))
				|| (scroll_partial
				    && (b - t - count >
					3 * vc->vc_rows >> 2)))) {
				if (t > 0)
					fbcon_redraw_move(vc, p, 0, t, count);
				ypan_up_redraw(vc, t, count);
				if (vc->vc_rows - b > 0)
					fbcon_redraw_move(vc, p, b,
							  vc->vc_rows - b, b);
			} else
				fbcon_redraw_move(vc, p, t + count, b - t - count, t);
			fbcon_clear(vc, b - count, 0, count, vc->vc_cols);
			break;

		case SCROLL_PAN_MOVE:
			if ((p->yscroll + count <=
			     2 * (p->vrows - vc->vc_rows))
			    && ((!scroll_partial && (b - t == vc->vc_rows))
				|| (scroll_partial
				    && (b - t - count >
					3 * vc->vc_rows >> 2)))) {
				if (t > 0)
					fbcon_bmove(vc, 0, 0, count, 0, t,
						    vc->vc_cols);
				ypan_up(vc, count);
				if (vc->vc_rows - b > 0)
					fbcon_bmove(vc, b - count, 0, b, 0,
						    vc->vc_rows - b,
						    vc->vc_cols);
			} else if (info->flags & FBINFO_READS_FAST)
				fbcon_bmove(vc, t + count, 0, t, 0,
					    b - t - count, vc->vc_cols);
			else
				goto redraw_up;
			fbcon_clear(vc, b - count, 0, count, vc->vc_cols);
			break;

		case SCROLL_REDRAW:
		      redraw_up:
			fbcon_redraw(vc, p, t, b - t - count,
				     count * vc->vc_cols);
			fbcon_clear(vc, b - count, 0, count, vc->vc_cols);
			scr_memsetw((unsigned short *) (vc->vc_origin +
							vc->vc_size_row *
							(b - count)),
				    vc->vc_video_erase_char,
				    vc->vc_size_row * count);
			return 1;
		}
		break;

	case SM_DOWN:
		if (count > vc->vc_rows)	/* Maximum realistic size */
			count = vc->vc_rows;
		if (logo_shown >= 0)
			goto redraw_down;
		switch (p->scrollmode) {
		case SCROLL_MOVE:
			ops->bmove(vc, info, t, 0, t + count, 0,
				    b - t - count, vc->vc_cols);
			ops->clear(vc, info, t, 0, count, vc->vc_cols);
			break;

		case SCROLL_WRAP_MOVE:
			if (b - t - count > 3 * vc->vc_rows >> 2) {
				if (vc->vc_rows - b > 0)
					fbcon_bmove(vc, b, 0, b - count, 0,
						    vc->vc_rows - b,
						    vc->vc_cols);
				ywrap_down(vc, count);
				if (t > 0)
					fbcon_bmove(vc, count, 0, 0, 0, t,
						    vc->vc_cols);
			} else if (info->flags & FBINFO_READS_FAST)
				fbcon_bmove(vc, t, 0, t + count, 0,
					    b - t - count, vc->vc_cols);
			else
				goto redraw_down;
			fbcon_clear(vc, t, 0, count, vc->vc_cols);
			break;

		case SCROLL_PAN_MOVE:
			if ((count - p->yscroll <= p->vrows - vc->vc_rows)
			    && ((!scroll_partial && (b - t == vc->vc_rows))
				|| (scroll_partial
				    && (b - t - count >
					3 * vc->vc_rows >> 2)))) {
				if (vc->vc_rows - b > 0)
					fbcon_bmove(vc, b, 0, b - count, 0,
						    vc->vc_rows - b,
						    vc->vc_cols);
				ypan_down(vc, count);
				if (t > 0)
					fbcon_bmove(vc, count, 0, 0, 0, t,
						    vc->vc_cols);
			} else if (info->flags & FBINFO_READS_FAST)
				fbcon_bmove(vc, t, 0, t + count, 0,
					    b - t - count, vc->vc_cols);
			else
				goto redraw_down;
			fbcon_clear(vc, t, 0, count, vc->vc_cols);
			break;

		case SCROLL_PAN_REDRAW:
			if ((count - p->yscroll <= p->vrows - vc->vc_rows)
			    && ((!scroll_partial && (b - t == vc->vc_rows))
				|| (scroll_partial
				    && (b - t - count >
					3 * vc->vc_rows >> 2)))) {
				if (vc->vc_rows - b > 0)
					fbcon_redraw_move(vc, p, b, vc->vc_rows - b,
							  b - count);
				ypan_down_redraw(vc, t, count);
				if (t > 0)
					fbcon_redraw_move(vc, p, count, t, 0);
			} else
				fbcon_redraw_move(vc, p, t, b - t - count, t + count);
			fbcon_clear(vc, t, 0, count, vc->vc_cols);
			break;

		case SCROLL_REDRAW:
		      redraw_down:
			fbcon_redraw(vc, p, b - 1, b - t - count,
				     -count * vc->vc_cols);
			fbcon_clear(vc, t, 0, count, vc->vc_cols);
			scr_memsetw((unsigned short *) (vc->vc_origin +
							vc->vc_size_row *
							t),
				    vc->vc_video_erase_char,
				    vc->vc_size_row * count);
			return 1;
		}
	}
	return 0;
}


static void fbcon_bmove(struct vc_data *vc, int sy, int sx, int dy, int dx,
			int height, int width)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct display *p = &fb_display[vc->vc_num];
	
	if (fbcon_is_inactive(vc, info))
		return;

	if (!width || !height)
		return;

	/*  Split blits that cross physical y_wrap case.
	 *  Pathological case involves 4 blits, better to use recursive
	 *  code rather than unrolled case
	 *
	 *  Recursive invocations don't need to erase the cursor over and
	 *  over again, so we use fbcon_bmove_rec()
	 */
	fbcon_bmove_rec(vc, p, sy, sx, dy, dx, height, width,
			p->vrows - p->yscroll);
}

static void fbcon_bmove_rec(struct vc_data *vc, struct display *p, int sy, int sx, 
			    int dy, int dx, int height, int width, u_int y_break)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct fbcon_ops *ops = info->fbcon_par;
	u_int b;

	if (sy < y_break && sy + height > y_break) {
		b = y_break - sy;
		if (dy < sy) {	/* Avoid trashing self */
			fbcon_bmove_rec(vc, p, sy, sx, dy, dx, b, width,
					y_break);
			fbcon_bmove_rec(vc, p, sy + b, sx, dy + b, dx,
					height - b, width, y_break);
		} else {
			fbcon_bmove_rec(vc, p, sy + b, sx, dy + b, dx,
					height - b, width, y_break);
			fbcon_bmove_rec(vc, p, sy, sx, dy, dx, b, width,
					y_break);
		}
		return;
	}

	if (dy < y_break && dy + height > y_break) {
		b = y_break - dy;
		if (dy < sy) {	/* Avoid trashing self */
			fbcon_bmove_rec(vc, p, sy, sx, dy, dx, b, width,
					y_break);
			fbcon_bmove_rec(vc, p, sy + b, sx, dy + b, dx,
					height - b, width, y_break);
		} else {
			fbcon_bmove_rec(vc, p, sy + b, sx, dy + b, dx,
					height - b, width, y_break);
			fbcon_bmove_rec(vc, p, sy, sx, dy, dx, b, width,
					y_break);
		}
		return;
	}
	ops->bmove(vc, info, real_y(p, sy), sx, real_y(p, dy), dx,
		   height, width);
}

static __inline__ void updatescrollmode(struct display *p,
					struct fb_info *info,
					struct vc_data *vc)
{
	struct fbcon_ops *ops = info->fbcon_par;
	int fh = vc->vc_font.height;
	int cap = info->flags;
	u16 t = 0;
	int ypan = FBCON_SWAP(ops->rotate, info->fix.ypanstep,
				  info->fix.xpanstep);
	int ywrap = FBCON_SWAP(ops->rotate, info->fix.ywrapstep, t);
	int yres = FBCON_SWAP(ops->rotate, info->var.yres, info->var.xres);
	int vyres = FBCON_SWAP(ops->rotate, info->var.yres_virtual,
				   info->var.xres_virtual);
	int good_pan = (cap & FBINFO_HWACCEL_YPAN) &&
		divides(ypan, vc->vc_font.height) && vyres > yres;
	int good_wrap = (cap & FBINFO_HWACCEL_YWRAP) &&
		divides(ywrap, vc->vc_font.height) &&
		divides(vc->vc_font.height, vyres) &&
		divides(vc->vc_font.height, yres);
	int reading_fast = cap & FBINFO_READS_FAST;
	int fast_copyarea = (cap & FBINFO_HWACCEL_COPYAREA) &&
		!(cap & FBINFO_HWACCEL_DISABLED);
	int fast_imageblit = (cap & FBINFO_HWACCEL_IMAGEBLIT) &&
		!(cap & FBINFO_HWACCEL_DISABLED);

	p->vrows = vyres/fh;
	if (yres > (fh * (vc->vc_rows + 1)))
		p->vrows -= (yres - (fh * vc->vc_rows)) / fh;
	if ((yres % fh) && (vyres % fh < yres % fh))
		p->vrows--;

	if (good_wrap || good_pan) {
		if (reading_fast || fast_copyarea)
			p->scrollmode = good_wrap ?
				SCROLL_WRAP_MOVE : SCROLL_PAN_MOVE;
		else
			p->scrollmode = good_wrap ? SCROLL_REDRAW :
				SCROLL_PAN_REDRAW;
	} else {
		if (reading_fast || (fast_copyarea && !fast_imageblit))
			p->scrollmode = SCROLL_MOVE;
		else
			p->scrollmode = SCROLL_REDRAW;
	}
}

static int fbcon_resize(struct vc_data *vc, unsigned int width, 
			unsigned int height)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct fbcon_ops *ops = info->fbcon_par;
	struct display *p = &fb_display[vc->vc_num];
	struct fb_var_screeninfo var = info->var;
	int x_diff, y_diff, virt_w, virt_h, virt_fw, virt_fh;

	virt_w = FBCON_SWAP(ops->rotate, width, height);
	virt_h = FBCON_SWAP(ops->rotate, height, width);
	virt_fw = FBCON_SWAP(ops->rotate, vc->vc_font.width,
				 vc->vc_font.height);
	virt_fh = FBCON_SWAP(ops->rotate, vc->vc_font.height,
				 vc->vc_font.width);
	var.xres = virt_w * virt_fw;
	var.yres = virt_h * virt_fh;
	x_diff = info->var.xres - var.xres;
	y_diff = info->var.yres - var.yres;
	if (x_diff < 0 || x_diff > virt_fw ||
	    y_diff < 0 || y_diff > virt_fh) {
		const struct fb_videomode *mode;

		DPRINTK("attempting resize %ix%i\n", var.xres, var.yres);
		mode = fb_find_best_mode(&var, &info->modelist);
		if (mode == NULL)
			return -EINVAL;
		display_to_var(&var, p);
		fb_videomode_to_var(&var, mode);

		if (virt_w > var.xres/virt_fw || virt_h > var.yres/virt_fh)
			return -EINVAL;

		DPRINTK("resize now %ix%i\n", var.xres, var.yres);
		if (CON_IS_VISIBLE(vc)) {
			var.activate = FB_ACTIVATE_NOW |
				FB_ACTIVATE_FORCE;
			fb_set_var(info, &var);
		}
		var_to_display(p, &info->var, info);
		ops->var = info->var;
	}
	updatescrollmode(p, info, vc);
	return 0;
}

static int fbcon_switch(struct vc_data *vc)
{
	struct fb_info *info, *old_info = NULL;
	struct fbcon_ops *ops;
	struct display *p = &fb_display[vc->vc_num];
	struct fb_var_screeninfo var;
	int i, prev_console, charcnt = 256;

	info = registered_fb[con2fb_map[vc->vc_num]];
	ops = info->fbcon_par;

	if (softback_top) {
		if (softback_lines)
			fbcon_set_origin(vc);
		softback_top = softback_curr = softback_in = softback_buf;
		softback_lines = 0;
		fbcon_update_softback(vc);
	}

	if (logo_shown >= 0) {
		struct vc_data *conp2 = vc_cons[logo_shown].d;

		if (conp2->vc_top == logo_lines
		    && conp2->vc_bottom == conp2->vc_rows)
			conp2->vc_top = 0;
		logo_shown = FBCON_LOGO_CANSHOW;
	}

	prev_console = ops->currcon;
	if (prev_console != -1)
		old_info = registered_fb[con2fb_map[prev_console]];
	/*
	 * FIXME: If we have multiple fbdev's loaded, we need to
	 * update all info->currcon.  Perhaps, we can place this
	 * in a centralized structure, but this might break some
	 * drivers.
	 *
	 * info->currcon = vc->vc_num;
	 */
	for (i = 0; i < FB_MAX; i++) {
		if (registered_fb[i] != NULL && registered_fb[i]->fbcon_par) {
			struct fbcon_ops *o = registered_fb[i]->fbcon_par;

			o->currcon = vc->vc_num;
		}
	}
	memset(&var, 0, sizeof(struct fb_var_screeninfo));
	display_to_var(&var, p);
	var.activate = FB_ACTIVATE_NOW;

	/*
	 * make sure we don't unnecessarily trip the memcmp()
	 * in fb_set_var()
	 */
	info->var.activate = var.activate;
	var.yoffset = info->var.yoffset;
	var.xoffset = info->var.xoffset;
	var.vmode = info->var.vmode;
	fb_set_var(info, &var);
	ops->var = info->var;

	if (old_info != NULL && (old_info != info ||
				 info->flags & FBINFO_MISC_ALWAYS_SETPAR)) {
		if (info->fbops->fb_set_par)
			info->fbops->fb_set_par(info);

		if (old_info != info)
			fbcon_del_cursor_timer(old_info);
	}

	if (fbcon_is_inactive(vc, info) ||
	    ops->blank_state != FB_BLANK_UNBLANK)
		fbcon_del_cursor_timer(info);
	else
		fbcon_add_cursor_timer(info);

	set_blitting_type(vc, info);
	ops->cursor_reset = 1;

	if (ops->rotate_font && ops->rotate_font(info, vc)) {
		ops->rotate = FB_ROTATE_UR;
		set_blitting_type(vc, info);
	}

	vc->vc_can_do_color = (fb_get_color_depth(&info->var, &info->fix)!=1);
	vc->vc_complement_mask = vc->vc_can_do_color ? 0x7700 : 0x0800;

	if (p->userfont)
		charcnt = FNTCHARCNT(vc->vc_font.data);

	if (charcnt > 256)
		vc->vc_complement_mask <<= 1;

	updatescrollmode(p, info, vc);

	switch (p->scrollmode) {
	case SCROLL_WRAP_MOVE:
		scrollback_phys_max = p->vrows - vc->vc_rows;
		break;
	case SCROLL_PAN_MOVE:
	case SCROLL_PAN_REDRAW:
		scrollback_phys_max = p->vrows - 2 * vc->vc_rows;
		if (scrollback_phys_max < 0)
			scrollback_phys_max = 0;
		break;
	default:
		scrollback_phys_max = 0;
		break;
	}

	scrollback_max = 0;
	scrollback_current = 0;

	if (!fbcon_is_inactive(vc, info)) {
	    ops->var.xoffset = ops->var.yoffset = p->yscroll = 0;
	    ops->update_start(info);
	}

	fbcon_set_palette(vc, color_table); 	
	fbcon_clear_margins(vc, 0);

	if (logo_shown == FBCON_LOGO_DRAW) {

		logo_shown = fg_console;
		/* This is protected above by initmem_freed */
		fb_show_logo(info, ops->rotate);
		update_region(vc,
			      vc->vc_origin + vc->vc_size_row * vc->vc_top,
			      vc->vc_size_row * (vc->vc_bottom -
						 vc->vc_top) / 2);
		return 0;
	}
	return 1;
}

static void fbcon_generic_blank(struct vc_data *vc, struct fb_info *info,
				int blank)
{
	struct fb_event event;

	if (blank) {
		unsigned short charmask = vc->vc_hi_font_mask ?
			0x1ff : 0xff;
		unsigned short oldc;

		oldc = vc->vc_video_erase_char;
		vc->vc_video_erase_char &= charmask;
		fbcon_clear(vc, 0, 0, vc->vc_rows, vc->vc_cols);
		vc->vc_video_erase_char = oldc;
	}


	event.info = info;
	event.data = &blank;
	fb_notifier_call_chain(FB_EVENT_CONBLANK, &event);
}

static int fbcon_blank(struct vc_data *vc, int blank, int mode_switch)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct fbcon_ops *ops = info->fbcon_par;

	if (mode_switch) {
		struct fb_var_screeninfo var = info->var;

		ops->graphics = 1;

		if (!blank) {
			if (info->fbops->fb_save_state)
				info->fbops->fb_save_state(info);
			var.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
			fb_set_var(info, &var);
			ops->graphics = 0;
			ops->var = info->var;
		} else if (info->fbops->fb_restore_state)
			info->fbops->fb_restore_state(info);
	}

 	if (!fbcon_is_inactive(vc, info)) {
		if (ops->blank_state != blank) {
			ops->blank_state = blank;
			fbcon_cursor(vc, blank ? CM_ERASE : CM_DRAW);
			ops->cursor_flash = (!blank);

			if (fb_blank(info, blank))
				fbcon_generic_blank(vc, info, blank);
		}

		if (!blank)
			update_screen(vc);
	}

	if (fbcon_is_inactive(vc, info) ||
	    ops->blank_state != FB_BLANK_UNBLANK)
		fbcon_del_cursor_timer(info);
	else
		fbcon_add_cursor_timer(info);

	return 0;
}

static int fbcon_get_font(struct vc_data *vc, struct console_font *font)
{
	u8 *fontdata = vc->vc_font.data;
	u8 *data = font->data;
	int i, j;

	font->width = vc->vc_font.width;
	font->height = vc->vc_font.height;
	font->charcount = vc->vc_hi_font_mask ? 512 : 256;
	if (!font->data)
		return 0;

	if (font->width <= 8) {
		j = vc->vc_font.height;
		for (i = 0; i < font->charcount; i++) {
			memcpy(data, fontdata, j);
			memset(data + j, 0, 32 - j);
			data += 32;
			fontdata += j;
		}
	} else if (font->width <= 16) {
		j = vc->vc_font.height * 2;
		for (i = 0; i < font->charcount; i++) {
			memcpy(data, fontdata, j);
			memset(data + j, 0, 64 - j);
			data += 64;
			fontdata += j;
		}
	} else if (font->width <= 24) {
		for (i = 0; i < font->charcount; i++) {
			for (j = 0; j < vc->vc_font.height; j++) {
				*data++ = fontdata[0];
				*data++ = fontdata[1];
				*data++ = fontdata[2];
				fontdata += sizeof(u32);
			}
			memset(data, 0, 3 * (32 - j));
			data += 3 * (32 - j);
		}
	} else {
		j = vc->vc_font.height * 4;
		for (i = 0; i < font->charcount; i++) {
			memcpy(data, fontdata, j);
			memset(data + j, 0, 128 - j);
			data += 128;
			fontdata += j;
		}
	}
	return 0;
}

static int fbcon_do_set_font(struct vc_data *vc, int w, int h,
			     const u8 * data, int userfont)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	struct fbcon_ops *ops = info->fbcon_par;
	struct display *p = &fb_display[vc->vc_num];
	int resize;
	int cnt;
	char *old_data = NULL;

	if (CON_IS_VISIBLE(vc) && softback_lines)
		fbcon_set_origin(vc);

	resize = (w != vc->vc_font.width) || (h != vc->vc_font.height);
	if (p->userfont)
		old_data = vc->vc_font.data;
	if (userfont)
		cnt = FNTCHARCNT(data);
	else
		cnt = 256;
	vc->vc_font.data = (void *)(p->fontdata = data);
	if ((p->userfont = userfont))
		REFCOUNT(data)++;
	vc->vc_font.width = w;
	vc->vc_font.height = h;
	if (vc->vc_hi_font_mask && cnt == 256) {
		vc->vc_hi_font_mask = 0;
		if (vc->vc_can_do_color) {
			vc->vc_complement_mask >>= 1;
			vc->vc_s_complement_mask >>= 1;
		}
			
		/* ++Edmund: reorder the attribute bits */
		if (vc->vc_can_do_color) {
			unsigned short *cp =
			    (unsigned short *) vc->vc_origin;
			int count = vc->vc_screenbuf_size / 2;
			unsigned short c;
			for (; count > 0; count--, cp++) {
				c = scr_readw(cp);
				scr_writew(((c & 0xfe00) >> 1) |
					   (c & 0xff), cp);
			}
			c = vc->vc_video_erase_char;
			vc->vc_video_erase_char =
			    ((c & 0xfe00) >> 1) | (c & 0xff);
			vc->vc_attr >>= 1;
		}
	} else if (!vc->vc_hi_font_mask && cnt == 512) {
		vc->vc_hi_font_mask = 0x100;
		if (vc->vc_can_do_color) {
			vc->vc_complement_mask <<= 1;
			vc->vc_s_complement_mask <<= 1;
		}
			
		/* ++Edmund: reorder the attribute bits */
		{
			unsigned short *cp =
			    (unsigned short *) vc->vc_origin;
			int count = vc->vc_screenbuf_size / 2;
			unsigned short c;
			for (; count > 0; count--, cp++) {
				unsigned short newc;
				c = scr_readw(cp);
				if (vc->vc_can_do_color)
					newc =
					    ((c & 0xff00) << 1) | (c &
								   0xff);
				else
					newc = c & ~0x100;
				scr_writew(newc, cp);
			}
			c = vc->vc_video_erase_char;
			if (vc->vc_can_do_color) {
				vc->vc_video_erase_char =
				    ((c & 0xff00) << 1) | (c & 0xff);
				vc->vc_attr <<= 1;
			} else
				vc->vc_video_erase_char = c & ~0x100;
		}

	}

	if (resize) {
		int cols, rows;

		cols = FBCON_SWAP(ops->rotate, info->var.xres, info->var.yres);
		rows = FBCON_SWAP(ops->rotate, info->var.yres, info->var.xres);
		cols /= w;
		rows /= h;
		vc_resize(vc, cols, rows);
		if (CON_IS_VISIBLE(vc) && softback_buf)
			fbcon_update_softback(vc);
	} else if (CON_IS_VISIBLE(vc)
		   && vc->vc_mode == KD_TEXT) {
		fbcon_clear_margins(vc, 0);
		update_screen(vc);
	}

	if (old_data && (--REFCOUNT(old_data) == 0))
		kfree(old_data - FONT_EXTRA_WORDS * sizeof(int));
	return 0;
}

static int fbcon_copy_font(struct vc_data *vc, int con)
{
	struct display *od = &fb_display[con];
	struct console_font *f = &vc->vc_font;

	if (od->fontdata == f->data)
		return 0;	/* already the same font... */
	return fbcon_do_set_font(vc, f->width, f->height, od->fontdata, od->userfont);
}

/*
 *  User asked to set font; we are guaranteed that
 *	a) width and height are in range 1..32
 *	b) charcount does not exceed 512
 *  but lets not assume that, since someone might someday want to use larger
 *  fonts. And charcount of 512 is small for unicode support.
 *
 *  However, user space gives the font in 32 rows , regardless of
 *  actual font height. So a new API is needed if support for larger fonts
 *  is ever implemented.
 */

static int fbcon_set_font(struct vc_data *vc, struct console_font *font, unsigned flags)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	unsigned charcount = font->charcount;
	int w = font->width;
	int h = font->height;
	int size;
	int i, csum;
	u8 *new_data, *data = font->data;
	int pitch = (font->width+7) >> 3;

	/* Is there a reason why fbconsole couldn't handle any charcount >256?
	 * If not this check should be changed to charcount < 256 */
	if (charcount != 256 && charcount != 512)
		return -EINVAL;

	/* Make sure drawing engine can handle the font */
	if (!(info->pixmap.blit_x & (1 << (font->width - 1))) ||
	    !(info->pixmap.blit_y & (1 << (font->height - 1))))
		return -EINVAL;

	/* Make sure driver can handle the font length */
	if (fbcon_invalid_charcount(info, charcount))
		return -EINVAL;

	size = h * pitch * charcount;

	new_data = kmalloc(FONT_EXTRA_WORDS * sizeof(int) + size, GFP_USER);

	if (!new_data)
		return -ENOMEM;

	new_data += FONT_EXTRA_WORDS * sizeof(int);
	FNTSIZE(new_data) = size;
	FNTCHARCNT(new_data) = charcount;
	REFCOUNT(new_data) = 0;	/* usage counter */
	for (i=0; i< charcount; i++) {
		memcpy(new_data + i*h*pitch, data +  i*32*pitch, h*pitch);
	}

	/* Since linux has a nice crc32 function use it for counting font
	 * checksums. */
	csum = crc32(0, new_data, size);

	FNTSUM(new_data) = csum;
	/* Check if the same font is on some other console already */
	for (i = first_fb_vc; i <= last_fb_vc; i++) {
		struct vc_data *tmp = vc_cons[i].d;
		
		if (fb_display[i].userfont &&
		    fb_display[i].fontdata &&
		    FNTSUM(fb_display[i].fontdata) == csum &&
		    FNTSIZE(fb_display[i].fontdata) == size &&
		    tmp->vc_font.width == w &&
		    !memcmp(fb_display[i].fontdata, new_data, size)) {
			kfree(new_data - FONT_EXTRA_WORDS * sizeof(int));
			new_data = (u8 *)fb_display[i].fontdata;
			break;
		}
	}
	return fbcon_do_set_font(vc, font->width, font->height, new_data, 1);
}

static int fbcon_set_def_font(struct vc_data *vc, struct console_font *font, char *name)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	const struct font_desc *f;

	if (!name)
		f = get_default_font(info->var.xres, info->var.yres,
				     info->pixmap.blit_x, info->pixmap.blit_y);
	else if (!(f = find_font(name)))
		return -ENOENT;

	font->width = f->width;
	font->height = f->height;
	return fbcon_do_set_font(vc, f->width, f->height, f->data, 0);
}

static u16 palette_red[16];
static u16 palette_green[16];
static u16 palette_blue[16];

static struct fb_cmap palette_cmap = {
	0, 16, palette_red, palette_green, palette_blue, NULL
};

static int fbcon_set_palette(struct vc_data *vc, unsigned char *table)
{
	struct fb_info *info = registered_fb[con2fb_map[vc->vc_num]];
	int i, j, k, depth;
	u8 val;

	if (fbcon_is_inactive(vc, info))
		return -EINVAL;

	if (!CON_IS_VISIBLE(vc))
		return 0;

	depth = fb_get_color_depth(&info->var, &info->fix);
	if (depth > 3) {
		for (i = j = 0; i < 16; i++) {
			k = table[i];
			val = vc->vc_palette[j++];
			palette_red[k] = (val << 8) | val;
			val = vc->vc_palette[j++];
			palette_green[k] = (val << 8) | val;
			val = vc->vc_palette[j++];
			palette_blue[k] = (val << 8) | val;
		}
		palette_cmap.len = 16;
		palette_cmap.start = 0;
	/*
	 * If framebuffer is capable of less than 16 colors,
	 * use default palette of fbcon.
	 */
	} else
		fb_copy_cmap(fb_default_cmap(1 << depth), &palette_cmap);

	return fb_set_cmap(&palette_cmap, info);
}

static u16 *fbcon_screen_pos(struct vc_data *vc, int offset)
{
	unsigned long p;
	int line;
	
	if (vc->vc_num != fg_console || !softback_lines)
		return (u16 *) (vc->vc_origin + offset);
	line = offset / vc->vc_size_row;
	if (line >= softback_lines)
		return (u16 *) (vc->vc_origin + offset -
				softback_lines * vc->vc_size_row);
	p = softback_curr + offset;
	if (p >= softback_end)
		p += softback_buf - softback_end;
	return (u16 *) p;
}

static unsigned long fbcon_getxy(struct vc_data *vc, unsigned long pos,
				 int *px, int *py)
{
	unsigned long ret;
	int x, y;

	if (pos >= vc->vc_origin && pos < vc->vc_scr_end) {
		unsigned long offset = (pos - vc->vc_origin) / 2;

		x = offset % vc->vc_cols;
		y = offset / vc->vc_cols;
		if (vc->vc_num == fg_console)
			y += softback_lines;
		ret = pos + (vc->vc_cols - x) * 2;
	} else if (vc->vc_num == fg_console && softback_lines) {
		unsigned long offset = pos - softback_curr;

		if (pos < softback_curr)
			offset += softback_end - softback_buf;
		offset /= 2;
		x = offset % vc->vc_cols;
		y = offset / vc->vc_cols;
		ret = pos + (vc->vc_cols - x) * 2;
		if (ret == softback_end)
			ret = softback_buf;
		if (ret == softback_in)
			ret = vc->vc_origin;
	} else {
		/* Should not happen */
		x = y = 0;
		ret = vc->vc_origin;
	}
	if (px)
		*px = x;
	if (py)
		*py = y;
	return ret;
}

/* As we might be inside of softback, we may work with non-contiguous buffer,
   that's why we have to use a separate routine. */
static void fbcon_invert_region(struct vc_data *vc, u16 * p, int cnt)
{
	while (cnt--) {
		u16 a = scr_readw(p);
		if (!vc->vc_can_do_color)
			a ^= 0x0800;
		else if (vc->vc_hi_font_mask == 0x100)
			a = ((a) & 0x11ff) | (((a) & 0xe000) >> 4) |
			    (((a) & 0x0e00) << 4);
		else
			a = ((a) & 0x88ff) | (((a) & 0x7000) >> 4) |
			    (((a) & 0x0700) << 4);
		scr_writew(a, p++);
		if (p == (u16 *) softback_end)
			p = (u16 *) softback_buf;
		if (p == (u16 *) softback_in)
			p = (u16 *) vc->vc_origin;
	}
}

static int fbcon_scrolldelta(struct vc_data *vc, int lines)
{
	struct fb_info *info = registered_fb[con2fb_map[fg_console]];
	struct fbcon_ops *ops = info->fbcon_par;
	struct display *p = &fb_display[fg_console];
	int offset, limit, scrollback_old;

	if (softback_top) {
		if (vc->vc_num != fg_console)
			return 0;
		if (vc->vc_mode != KD_TEXT || !lines)
			return 0;
		if (logo_shown >= 0) {
			struct vc_data *conp2 = vc_cons[logo_shown].d;

			if (conp2->vc_top == logo_lines
			    && conp2->vc_bottom == conp2->vc_rows)
				conp2->vc_top = 0;
			if (logo_shown == vc->vc_num) {
				unsigned long p, q;
				int i;

				p = softback_in;
				q = vc->vc_origin +
				    logo_lines * vc->vc_size_row;
				for (i = 0; i < logo_lines; i++) {
					if (p == softback_top)
						break;
					if (p == softback_buf)
						p = softback_end;
					p -= vc->vc_size_row;
					q -= vc->vc_size_row;
					scr_memcpyw((u16 *) q, (u16 *) p,
						    vc->vc_size_row);
				}
				softback_in = softback_curr = p;
				update_region(vc, vc->vc_origin,
					      logo_lines * vc->vc_cols);
			}
			logo_shown = FBCON_LOGO_CANSHOW;
		}
		fbcon_cursor(vc, CM_ERASE | CM_SOFTBACK);
		fbcon_redraw_softback(vc, p, lines);
		fbcon_cursor(vc, CM_DRAW | CM_SOFTBACK);
		return 0;
	}

	if (!scrollback_phys_max)
		return -ENOSYS;

	scrollback_old = scrollback_current;
	scrollback_current -= lines;
	if (scrollback_current < 0)
		scrollback_current = 0;
	else if (scrollback_current > scrollback_max)
		scrollback_current = scrollback_max;
	if (scrollback_current == scrollback_old)
		return 0;

	if (fbcon_is_inactive(vc, info))
		return 0;

	fbcon_cursor(vc, CM_ERASE);

	offset = p->yscroll - scrollback_current;
	limit = p->vrows;
	switch (p->scrollmode) {
	case SCROLL_WRAP_MOVE:
		info->var.vmode |= FB_VMODE_YWRAP;
		break;
	case SCROLL_PAN_MOVE:
	case SCROLL_PAN_REDRAW:
		limit -= vc->vc_rows;
		info->var.vmode &= ~FB_VMODE_YWRAP;
		break;
	}
	if (offset < 0)
		offset += limit;
	else if (offset >= limit)
		offset -= limit;

	ops->var.xoffset = 0;
	ops->var.yoffset = offset * vc->vc_font.height;
	ops->update_start(info);

	if (!scrollback_current)
		fbcon_cursor(vc, CM_DRAW);
	return 0;
}

static int fbcon_set_origin(struct vc_data *vc)
{
	if (softback_lines)
		fbcon_scrolldelta(vc, softback_lines);
	return 0;
}

static void fbcon_suspended(struct fb_info *info)
{
	struct vc_data *vc = NULL;
	struct fbcon_ops *ops = info->fbcon_par;

	if (!ops || ops->currcon < 0)
		return;
	vc = vc_cons[ops->currcon].d;

	/* Clear cursor, restore saved data */
	fbcon_cursor(vc, CM_ERASE);
}

static void fbcon_resumed(struct fb_info *info)
{
	struct vc_data *vc;
	struct fbcon_ops *ops = info->fbcon_par;

	if (!ops || ops->currcon < 0)
		return;
	vc = vc_cons[ops->currcon].d;

	update_screen(vc);
}

static void fbcon_modechanged(struct fb_info *info)
{
	struct fbcon_ops *ops = info->fbcon_par;
	struct vc_data *vc;
	struct display *p;
	int rows, cols;

	if (!ops || ops->currcon < 0)
		return;
	vc = vc_cons[ops->currcon].d;
	if (vc->vc_mode != KD_TEXT ||
	    registered_fb[con2fb_map[ops->currcon]] != info)
		return;

	p = &fb_display[vc->vc_num];
	set_blitting_type(vc, info);

	if (CON_IS_VISIBLE(vc)) {
		var_to_display(p, &info->var, info);
		cols = FBCON_SWAP(ops->rotate, info->var.xres, info->var.yres);
		rows = FBCON_SWAP(ops->rotate, info->var.yres, info->var.xres);
		cols /= vc->vc_font.width;
		rows /= vc->vc_font.height;
		vc_resize(vc, cols, rows);
		updatescrollmode(p, info, vc);
		scrollback_max = 0;
		scrollback_current = 0;

		if (!fbcon_is_inactive(vc, info)) {
		    ops->var.xoffset = ops->var.yoffset = p->yscroll = 0;
		    ops->update_start(info);
		}

		fbcon_set_palette(vc, color_table);
		update_screen(vc);
		if (softback_buf)
			fbcon_update_softback(vc);
	}
}

static void fbcon_set_all_vcs(struct fb_info *info)
{
	struct fbcon_ops *ops = info->fbcon_par;
	struct vc_data *vc;
	struct display *p;
	int i, rows, cols, fg = -1;

	if (!ops || ops->currcon < 0)
		return;

	for (i = first_fb_vc; i <= last_fb_vc; i++) {
		vc = vc_cons[i].d;
		if (!vc || vc->vc_mode != KD_TEXT ||
		    registered_fb[con2fb_map[i]] != info)
			continue;

		if (CON_IS_VISIBLE(vc)) {
			fg = i;
			continue;
		}

		p = &fb_display[vc->vc_num];
		set_blitting_type(vc, info);
		var_to_display(p, &info->var, info);
		cols = FBCON_SWAP(p->rotate, info->var.xres, info->var.yres);
		rows = FBCON_SWAP(p->rotate, info->var.yres, info->var.xres);
		cols /= vc->vc_font.width;
		rows /= vc->vc_font.height;
		vc_resize(vc, cols, rows);
	}

	if (fg != -1)
		fbcon_modechanged(info);
}

static int fbcon_mode_deleted(struct fb_info *info,
			      struct fb_videomode *mode)
{
	struct fb_info *fb_info;
	struct display *p;
	int i, j, found = 0;

	/* before deletion, ensure that mode is not in use */
	for (i = first_fb_vc; i <= last_fb_vc; i++) {
		j = con2fb_map[i];
		if (j == -1)
			continue;
		fb_info = registered_fb[j];
		if (fb_info != info)
			continue;
		p = &fb_display[i];
		if (!p || !p->mode)
			continue;
		if (fb_mode_is_equal(p->mode, mode)) {
			found = 1;
			break;
		}
	}
	return found;
}

static int fbcon_fb_unregistered(int idx)
{
	int i;

	for (i = first_fb_vc; i <= last_fb_vc; i++) {
		if (con2fb_map[i] == idx)
			con2fb_map[i] = -1;
	}

	if (idx == info_idx) {
		info_idx = -1;

		for (i = 0; i < FB_MAX; i++) {
			if (registered_fb[i] != NULL) {
				info_idx = i;
				break;
			}
		}
	}

	if (info_idx != -1) {
		for (i = first_fb_vc; i <= last_fb_vc; i++) {
			if (con2fb_map[i] == -1)
				con2fb_map[i] = info_idx;
		}
	}

	if (!num_registered_fb)
		unregister_con_driver(&fb_con);

	return 0;
}

static int fbcon_fb_registered(int idx)
{
	int ret = 0, i;

	if (info_idx == -1) {
		for (i = first_fb_vc; i <= last_fb_vc; i++) {
			if (con2fb_map_boot[i] == idx) {
				info_idx = idx;
				break;
			}
		}

		if (info_idx != -1)
			ret = fbcon_takeover(1);
	} else {
		for (i = first_fb_vc; i <= last_fb_vc; i++) {
			if (con2fb_map_boot[i] == idx &&
			    con2fb_map[i] == -1)
				set_con2fb_map(i, idx, 0);
		}
	}

	return ret;
}

static void fbcon_fb_blanked(struct fb_info *info, int blank)
{
	struct fbcon_ops *ops = info->fbcon_par;
	struct vc_data *vc;

	if (!ops || ops->currcon < 0)
		return;

	vc = vc_cons[ops->currcon].d;
	if (vc->vc_mode != KD_TEXT ||
			registered_fb[con2fb_map[ops->currcon]] != info)
		return;

	if (CON_IS_VISIBLE(vc)) {
		if (blank)
			do_blank_screen(0);
		else
			do_unblank_screen(0);
	}
	ops->blank_state = blank;
}

static void fbcon_new_modelist(struct fb_info *info)
{
	int i;
	struct vc_data *vc;
	struct fb_var_screeninfo var;
	const struct fb_videomode *mode;

	for (i = first_fb_vc; i <= last_fb_vc; i++) {
		if (registered_fb[con2fb_map[i]] != info)
			continue;
		if (!fb_display[i].mode)
			continue;
		vc = vc_cons[i].d;
		display_to_var(&var, &fb_display[i]);
		mode = fb_find_nearest_mode(fb_display[i].mode,
					    &info->modelist);
		fb_videomode_to_var(&var, mode);

		if (vc)
			fbcon_set_disp(info, &var, vc);
		else
			fbcon_preset_disp(info, &var, i);

	}
}

static int fbcon_event_notify(struct notifier_block *self, 
			      unsigned long action, void *data)
{
	struct fb_event *event = data;
	struct fb_info *info = event->info;
	struct fb_videomode *mode;
	struct fb_con2fbmap *con2fb;
	int ret = 0;

	/*
	 * ignore all events except driver registration and deregistration
	 * if fbcon is not active
	 */
	if (fbcon_has_exited && !(action == FB_EVENT_FB_REGISTERED ||
				  action == FB_EVENT_FB_UNREGISTERED))
		goto done;

	switch(action) {
	case FB_EVENT_SUSPEND:
		fbcon_suspended(info);
		break;
	case FB_EVENT_RESUME:
		fbcon_resumed(info);
		break;
	case FB_EVENT_MODE_CHANGE:
		fbcon_modechanged(info);
		break;
	case FB_EVENT_MODE_CHANGE_ALL:
		fbcon_set_all_vcs(info);
		break;
	case FB_EVENT_MODE_DELETE:
		mode = event->data;
		ret = fbcon_mode_deleted(info, mode);
		break;
	case FB_EVENT_FB_REGISTERED:
		ret = fbcon_fb_registered(info->node);
		break;
	case FB_EVENT_FB_UNREGISTERED:
		ret = fbcon_fb_unregistered(info->node);
		break;
	case FB_EVENT_SET_CONSOLE_MAP:
		con2fb = event->data;
		ret = set_con2fb_map(con2fb->console - 1,
				     con2fb->framebuffer, 1);
		break;
	case FB_EVENT_GET_CONSOLE_MAP:
		con2fb = event->data;
		con2fb->framebuffer = con2fb_map[con2fb->console - 1];
		break;
	case FB_EVENT_BLANK:
		fbcon_fb_blanked(info, *(int *)event->data);
		break;
	case FB_EVENT_NEW_MODELIST:
		fbcon_new_modelist(info);
		break;
	}

done:
	return ret;
}

/*
 *  The console `switch' structure for the frame buffer based console
 */

static const struct consw fb_con = {
	.owner			= THIS_MODULE,
	.con_startup 		= fbcon_startup,
	.con_init 		= fbcon_init,
	.con_deinit 		= fbcon_deinit,
	.con_clear 		= fbcon_clear,
	.con_putc 		= fbcon_putc,
	.con_putcs 		= fbcon_putcs,
	.con_cursor 		= fbcon_cursor,
	.con_scroll 		= fbcon_scroll,
	.con_bmove 		= fbcon_bmove,
	.con_switch 		= fbcon_switch,
	.con_blank 		= fbcon_blank,
	.con_font_set 		= fbcon_set_font,
	.con_font_get 		= fbcon_get_font,
	.con_font_default	= fbcon_set_def_font,
	.con_font_copy 		= fbcon_copy_font,
	.con_set_palette 	= fbcon_set_palette,
	.con_scrolldelta 	= fbcon_scrolldelta,
	.con_set_origin 	= fbcon_set_origin,
	.con_invert_region 	= fbcon_invert_region,
	.con_screen_pos 	= fbcon_screen_pos,
	.con_getxy 		= fbcon_getxy,
	.con_resize             = fbcon_resize,
};

static struct notifier_block fbcon_event_notifier = {
	.notifier_call	= fbcon_event_notify,
};

static ssize_t store_rotate(struct class_device *class_device,
			    const char *buf, size_t count)
{
	struct fb_info *info;
	int rotate, idx;
	char **last = NULL;

	if (fbcon_has_exited)
		return count;

	acquire_console_sem();
	idx = con2fb_map[fg_console];

	if (idx == -1 || registered_fb[idx] == NULL)
		goto err;

	info = registered_fb[idx];
	rotate = simple_strtoul(buf, last, 0);
	fbcon_rotate(info, rotate);
err:
	release_console_sem();
	return count;
}

static ssize_t store_rotate_all(struct class_device *class_device,
				const char *buf, size_t count)
{
	struct fb_info *info;
	int rotate, idx;
	char **last = NULL;

	if (fbcon_has_exited)
		return count;

	acquire_console_sem();
	idx = con2fb_map[fg_console];

	if (idx == -1 || registered_fb[idx] == NULL)
		goto err;

	info = registered_fb[idx];
	rotate = simple_strtoul(buf, last, 0);
	fbcon_rotate_all(info, rotate);
err:
	release_console_sem();
	return count;
}

static ssize_t show_rotate(struct class_device *class_device, char *buf)
{
	struct fb_info *info;
	int rotate = 0, idx;

	if (fbcon_has_exited)
		return 0;

	acquire_console_sem();
	idx = con2fb_map[fg_console];

	if (idx == -1 || registered_fb[idx] == NULL)
		goto err;

	info = registered_fb[idx];
	rotate = fbcon_get_rotate(info);
err:
	release_console_sem();
	return snprintf(buf, PAGE_SIZE, "%d\n", rotate);
}

static struct class_device_attribute class_device_attrs[] = {
	__ATTR(rotate, S_IRUGO|S_IWUSR, show_rotate, store_rotate),
	__ATTR(rotate_all, S_IWUSR, NULL, store_rotate_all),
};

static int fbcon_init_class_device(void)
{
	int i, error = 0;

	fbcon_has_sysfs = 1;

	for (i = 0; i < ARRAY_SIZE(class_device_attrs); i++) {
		error = class_device_create_file(fbcon_class_device,
						 &class_device_attrs[i]);

		if (error)
			break;
	}

	if (error) {
		while (--i >= 0)
			class_device_remove_file(fbcon_class_device,
						 &class_device_attrs[i]);

		fbcon_has_sysfs = 0;
	}

	return 0;
}

static void fbcon_start(void)
{
	if (num_registered_fb) {
		int i;

		acquire_console_sem();

		for (i = 0; i < FB_MAX; i++) {
			if (registered_fb[i] != NULL) {
				info_idx = i;
				break;
			}
		}

		release_console_sem();
		fbcon_takeover(0);
	}
}

static void fbcon_exit(void)
{
	struct fb_info *info;
	int i, j, mapped;

	if (fbcon_has_exited)
		return;

#ifdef CONFIG_ATARI
	free_irq(IRQ_AUTO_4, fb_vbl_handler);
#endif
#ifdef CONFIG_MAC
	if (MACH_IS_MAC && vbl_detected)
		free_irq(IRQ_MAC_VBL, fb_vbl_handler);
#endif

	kfree((void *)softback_buf);
	softback_buf = 0UL;

	for (i = 0; i < FB_MAX; i++) {
		mapped = 0;
		info = registered_fb[i];

		if (info == NULL)
			continue;

		for (j = first_fb_vc; j <= last_fb_vc; j++) {
			if (con2fb_map[j] == i)
				mapped = 1;
		}

		if (mapped) {
			if (info->fbops->fb_release)
				info->fbops->fb_release(info, 0);
			module_put(info->fbops->owner);

			if (info->fbcon_par) {
				struct fbcon_ops *ops = info->fbcon_par;

				fbcon_del_cursor_timer(info);
				kfree(ops->cursor_src);
				kfree(info->fbcon_par);
				info->fbcon_par = NULL;
			}

			if (info->queue.func == fb_flashcursor)
				info->queue.func = NULL;
		}
	}

	fbcon_has_exited = 1;
}

static int __init fb_console_init(void)
{
	int i;

	acquire_console_sem();
	fb_register_client(&fbcon_event_notifier);
	fbcon_class_device =
	    class_device_create(fb_class, NULL, MKDEV(0, 0), NULL, "fbcon");

	if (IS_ERR(fbcon_class_device)) {
		printk(KERN_WARNING "Unable to create class_device "
		       "for fbcon; errno = %ld\n",
		       PTR_ERR(fbcon_class_device));
		fbcon_class_device = NULL;
	} else
		fbcon_init_class_device();

	for (i = 0; i < MAX_NR_CONSOLES; i++)
		con2fb_map[i] = -1;

	release_console_sem();
	fbcon_start();
	return 0;
}

module_init(fb_console_init);

#ifdef MODULE

static void __exit fbcon_deinit_class_device(void)
{
	int i;

	if (fbcon_has_sysfs) {
		for (i = 0; i < ARRAY_SIZE(class_device_attrs); i++)
			class_device_remove_file(fbcon_class_device,
						 &class_device_attrs[i]);

		fbcon_has_sysfs = 0;
	}
}

static void __exit fb_console_exit(void)
{
	acquire_console_sem();
	fb_unregister_client(&fbcon_event_notifier);
	fbcon_deinit_class_device();
	class_device_destroy(fb_class, MKDEV(0, 0));
	fbcon_exit();
	release_console_sem();
	unregister_con_driver(&fb_con);
}	

module_exit(fb_console_exit);

#endif

MODULE_LICENSE("GPL");
