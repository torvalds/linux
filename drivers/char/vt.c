/*
 *  linux/drivers/char/vt.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Hopefully this will be a rather complete VT102 implementation.
 *
 * Beeping thanks to John T Kohl.
 *
 * Virtual Consoles, Screen Blanking, Screen Dumping, Color, Graphics
 *   Chars, and VT100 enhancements by Peter MacDonald.
 *
 * Copy and paste function by Andrew Haylett,
 *   some enhancements by Alessandro Rubini.
 *
 * Code to check for different video-cards mostly by Galen Hunt,
 * <g-hunt@ee.utah.edu>
 *
 * Rudimentary ISO 10646/Unicode/UTF-8 character set support by
 * Markus Kuhn, <mskuhn@immd4.informatik.uni-erlangen.de>.
 *
 * Dynamic allocation of consoles, aeb@cwi.nl, May 1994
 * Resizing of consoles, aeb, 940926
 *
 * Code for xterm like mouse click reporting by Peter Orbaek 20-Jul-94
 * <poe@daimi.aau.dk>
 *
 * User-defined bell sound, new setterm control sequences and printk
 * redirection by Martin Mares <mj@k332.feld.cvut.cz> 19-Nov-95
 *
 * APM screenblank bug fixed Takashi Manabe <manabe@roy.dsl.tutics.tut.jp>
 *
 * Merge with the abstract console driver by Geert Uytterhoeven
 * <geert@linux-m68k.org>, Jan 1997.
 *
 *   Original m68k console driver modifications by
 *
 *     - Arno Griffioen <arno@usn.nl>
 *     - David Carter <carter@cs.bris.ac.uk>
 * 
 *   The abstract console driver provides a generic interface for a text
 *   console. It supports VGA text mode, frame buffer based graphical consoles
 *   and special graphics processors that are only accessible through some
 *   registers (e.g. a TMS340x0 GSP).
 *
 *   The interface to the hardware is specified using a special structure
 *   (struct consw) which contains function pointers to console operations
 *   (see <linux/console.h> for more information).
 *
 * Support for changeable cursor shape
 * by Pavel Machek <pavel@atrey.karlin.mff.cuni.cz>, August 1997
 *
 * Ported to i386 and con_scrolldelta fixed
 * by Emmanuel Marty <core@ggi-project.org>, April 1998
 *
 * Resurrected character buffers in videoram plus lots of other trickery
 * by Martin Mares <mj@atrey.karlin.mff.cuni.cz>, July 1998
 *
 * Removed old-style timers, introduced console_timer, made timer
 * deletion SMP-safe.  17Jun00, Andrew Morton <andrewm@uow.edu.au>
 *
 * Removed console_lock, enabled interrupts across all console operations
 * 13 March 2001, Andrew Morton
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kd.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/tiocl.h>
#include <linux/kbd_kern.h>
#include <linux/consolemap.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/workqueue.h>
#include <linux/bootmem.h>
#include <linux/pm.h>
#include <linux/font.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>


const struct consw *conswitchp;

/* A bitmap for codes <32. A bit of 1 indicates that the code
 * corresponding to that bit number invokes some special action
 * (such as cursor movement) and should not be displayed as a
 * glyph unless the disp_ctrl mode is explicitly enabled.
 */
#define CTRL_ACTION 0x0d00ff81
#define CTRL_ALWAYS 0x0800f501	/* Cannot be overridden by disp_ctrl */

/*
 * Here is the default bell parameters: 750HZ, 1/8th of a second
 */
#define DEFAULT_BELL_PITCH	750
#define DEFAULT_BELL_DURATION	(HZ/8)

extern void vcs_make_devfs(struct tty_struct *tty);
extern void vcs_remove_devfs(struct tty_struct *tty);

extern void console_map_init(void);
#ifdef CONFIG_PROM_CONSOLE
extern void prom_con_init(void);
#endif
#ifdef CONFIG_MDA_CONSOLE
extern int mda_console_init(void);
#endif

struct vc vc_cons [MAX_NR_CONSOLES];

#ifndef VT_SINGLE_DRIVER
static const struct consw *con_driver_map[MAX_NR_CONSOLES];
#endif

static int con_open(struct tty_struct *, struct file *);
static void vc_init(struct vc_data *vc, unsigned int rows,
		    unsigned int cols, int do_clear);
static void gotoxy(struct vc_data *vc, int new_x, int new_y);
static void save_cur(struct vc_data *vc);
static void reset_terminal(struct vc_data *vc, int do_clear);
static void con_flush_chars(struct tty_struct *tty);
static void set_vesa_blanking(char __user *p);
static void set_cursor(struct vc_data *vc);
static void hide_cursor(struct vc_data *vc);
static void console_callback(void *ignored);
static void blank_screen_t(unsigned long dummy);
static void set_palette(struct vc_data *vc);

static int printable;		/* Is console ready for printing? */

/*
 * ignore_poke: don't unblank the screen when things are typed.  This is
 * mainly for the privacy of braille terminal users.
 */
static int ignore_poke;

int do_poke_blanked_console;
int console_blanked;

static int vesa_blank_mode; /* 0:none 1:suspendV 2:suspendH 3:powerdown */
static int blankinterval = 10*60*HZ;
static int vesa_off_interval;

static DECLARE_WORK(console_work, console_callback, NULL);

/*
 * fg_console is the current virtual console,
 * last_console is the last used one,
 * want_console is the console we want to switch to,
 * kmsg_redirect is the console for kernel messages,
 */
int fg_console;
int last_console;
int want_console = -1;
int kmsg_redirect;

/*
 * For each existing display, we have a pointer to console currently visible
 * on that display, allowing consoles other than fg_console to be refreshed
 * appropriately. Unless the low-level driver supplies its own display_fg
 * variable, we use this one for the "master display".
 */
static struct vc_data *master_display_fg;

/*
 * Unfortunately, we need to delay tty echo when we're currently writing to the
 * console since the code is (and always was) not re-entrant, so we schedule
 * all flip requests to process context with schedule-task() and run it from
 * console_callback().
 */

/*
 * For the same reason, we defer scrollback to the console callback.
 */
static int scrollback_delta;

/*
 * Hook so that the power management routines can (un)blank
 * the console on our behalf.
 */
int (*console_blank_hook)(int);

static struct timer_list console_timer;
static int blank_state;
static int blank_timer_expired;
enum {
	blank_off = 0,
	blank_normal_wait,
	blank_vesa_wait,
};

/*
 *	Low-Level Functions
 */

#define IS_FG(vc)	((vc)->vc_num == fg_console)

#ifdef VT_BUF_VRAM_ONLY
#define DO_UPDATE(vc)	0
#else
#define DO_UPDATE(vc)	CON_IS_VISIBLE(vc)
#endif

static inline unsigned short *screenpos(struct vc_data *vc, int offset, int viewed)
{
	unsigned short *p;
	
	if (!viewed)
		p = (unsigned short *)(vc->vc_origin + offset);
	else if (!vc->vc_sw->con_screen_pos)
		p = (unsigned short *)(vc->vc_visible_origin + offset);
	else
		p = vc->vc_sw->con_screen_pos(vc, offset);
	return p;
}

static inline void scrolldelta(int lines)
{
	scrollback_delta += lines;
	schedule_console_callback();
}

void schedule_console_callback(void)
{
	schedule_work(&console_work);
}

static void scrup(struct vc_data *vc, unsigned int t, unsigned int b, int nr)
{
	unsigned short *d, *s;

	if (t+nr >= b)
		nr = b - t - 1;
	if (b > vc->vc_rows || t >= b || nr < 1)
		return;
	if (CON_IS_VISIBLE(vc) && vc->vc_sw->con_scroll(vc, t, b, SM_UP, nr))
		return;
	d = (unsigned short *)(vc->vc_origin + vc->vc_size_row * t);
	s = (unsigned short *)(vc->vc_origin + vc->vc_size_row * (t + nr));
	scr_memmovew(d, s, (b - t - nr) * vc->vc_size_row);
	scr_memsetw(d + (b - t - nr) * vc->vc_cols, vc->vc_video_erase_char,
		    vc->vc_size_row * nr);
}

static void scrdown(struct vc_data *vc, unsigned int t, unsigned int b, int nr)
{
	unsigned short *s;
	unsigned int step;

	if (t+nr >= b)
		nr = b - t - 1;
	if (b > vc->vc_rows || t >= b || nr < 1)
		return;
	if (CON_IS_VISIBLE(vc) && vc->vc_sw->con_scroll(vc, t, b, SM_DOWN, nr))
		return;
	s = (unsigned short *)(vc->vc_origin + vc->vc_size_row * t);
	step = vc->vc_cols * nr;
	scr_memmovew(s + step, s, (b - t - nr) * vc->vc_size_row);
	scr_memsetw(s, vc->vc_video_erase_char, 2 * step);
}

static void do_update_region(struct vc_data *vc, unsigned long start, int count)
{
#ifndef VT_BUF_VRAM_ONLY
	unsigned int xx, yy, offset;
	u16 *p;

	p = (u16 *) start;
	if (!vc->vc_sw->con_getxy) {
		offset = (start - vc->vc_origin) / 2;
		xx = offset % vc->vc_cols;
		yy = offset / vc->vc_cols;
	} else {
		int nxx, nyy;
		start = vc->vc_sw->con_getxy(vc, start, &nxx, &nyy);
		xx = nxx; yy = nyy;
	}
	for(;;) {
		u16 attrib = scr_readw(p) & 0xff00;
		int startx = xx;
		u16 *q = p;
		while (xx < vc->vc_cols && count) {
			if (attrib != (scr_readw(p) & 0xff00)) {
				if (p > q)
					vc->vc_sw->con_putcs(vc, q, p-q, yy, startx);
				startx = xx;
				q = p;
				attrib = scr_readw(p) & 0xff00;
			}
			p++;
			xx++;
			count--;
		}
		if (p > q)
			vc->vc_sw->con_putcs(vc, q, p-q, yy, startx);
		if (!count)
			break;
		xx = 0;
		yy++;
		if (vc->vc_sw->con_getxy) {
			p = (u16 *)start;
			start = vc->vc_sw->con_getxy(vc, start, NULL, NULL);
		}
	}
#endif
}

void update_region(struct vc_data *vc, unsigned long start, int count)
{
	WARN_CONSOLE_UNLOCKED();

	if (DO_UPDATE(vc)) {
		hide_cursor(vc);
		do_update_region(vc, start, count);
		set_cursor(vc);
	}
}

/* Structure of attributes is hardware-dependent */

static u8 build_attr(struct vc_data *vc, u8 _color, u8 _intensity, u8 _blink, u8 _underline, u8 _reverse)
{
	if (vc->vc_sw->con_build_attr)
		return vc->vc_sw->con_build_attr(vc, _color, _intensity, _blink, _underline, _reverse);

#ifndef VT_BUF_VRAM_ONLY
/*
 * ++roman: I completely changed the attribute format for monochrome
 * mode (!can_do_color). The formerly used MDA (monochrome display
 * adapter) format didn't allow the combination of certain effects.
 * Now the attribute is just a bit vector:
 *  Bit 0..1: intensity (0..2)
 *  Bit 2   : underline
 *  Bit 3   : reverse
 *  Bit 7   : blink
 */
	{
	u8 a = vc->vc_color;
	if (!vc->vc_can_do_color)
		return _intensity |
		       (_underline ? 4 : 0) |
		       (_reverse ? 8 : 0) |
		       (_blink ? 0x80 : 0);
	if (_underline)
		a = (a & 0xf0) | vc->vc_ulcolor;
	else if (_intensity == 0)
		a = (a & 0xf0) | vc->vc_ulcolor;
	if (_reverse)
		a = ((a) & 0x88) | ((((a) >> 4) | ((a) << 4)) & 0x77);
	if (_blink)
		a ^= 0x80;
	if (_intensity == 2)
		a ^= 0x08;
	if (vc->vc_hi_font_mask == 0x100)
		a <<= 1;
	return a;
	}
#else
	return 0;
#endif
}

static void update_attr(struct vc_data *vc)
{
	vc->vc_attr = build_attr(vc, vc->vc_color, vc->vc_intensity, vc->vc_blink, vc->vc_underline, vc->vc_reverse ^ vc->vc_decscnm);
	vc->vc_video_erase_char = (build_attr(vc, vc->vc_color, 1, vc->vc_blink, 0, vc->vc_decscnm) << 8) | ' ';
}

/* Note: inverting the screen twice should revert to the original state */
void invert_screen(struct vc_data *vc, int offset, int count, int viewed)
{
	unsigned short *p;

	WARN_CONSOLE_UNLOCKED();

	count /= 2;
	p = screenpos(vc, offset, viewed);
	if (vc->vc_sw->con_invert_region)
		vc->vc_sw->con_invert_region(vc, p, count);
#ifndef VT_BUF_VRAM_ONLY
	else {
		u16 *q = p;
		int cnt = count;
		u16 a;

		if (!vc->vc_can_do_color) {
			while (cnt--) {
			    a = scr_readw(q);
			    a ^= 0x0800;
			    scr_writew(a, q);
			    q++;
			}
		} else if (vc->vc_hi_font_mask == 0x100) {
			while (cnt--) {
				a = scr_readw(q);
				a = ((a) & 0x11ff) | (((a) & 0xe000) >> 4) | (((a) & 0x0e00) << 4);
				scr_writew(a, q);
				q++;
			}
		} else {
			while (cnt--) {
				a = scr_readw(q);
				a = ((a) & 0x88ff) | (((a) & 0x7000) >> 4) | (((a) & 0x0700) << 4);
				scr_writew(a, q);
				q++;
			}
		}
	}
#endif
	if (DO_UPDATE(vc))
		do_update_region(vc, (unsigned long) p, count);
}

/* used by selection: complement pointer position */
void complement_pos(struct vc_data *vc, int offset)
{
	static int old_offset = -1;
	static unsigned short old;
	static unsigned short oldx, oldy;

	WARN_CONSOLE_UNLOCKED();

	if (old_offset != -1 && old_offset >= 0 &&
	    old_offset < vc->vc_screenbuf_size) {
		scr_writew(old, screenpos(vc, old_offset, 1));
		if (DO_UPDATE(vc))
			vc->vc_sw->con_putc(vc, old, oldy, oldx);
	}

	old_offset = offset;

	if (offset != -1 && offset >= 0 &&
	    offset < vc->vc_screenbuf_size) {
		unsigned short new;
		unsigned short *p;
		p = screenpos(vc, offset, 1);
		old = scr_readw(p);
		new = old ^ vc->vc_complement_mask;
		scr_writew(new, p);
		if (DO_UPDATE(vc)) {
			oldx = (offset >> 1) % vc->vc_cols;
			oldy = (offset >> 1) / vc->vc_cols;
			vc->vc_sw->con_putc(vc, new, oldy, oldx);
		}
	}

}

static void insert_char(struct vc_data *vc, unsigned int nr)
{
	unsigned short *p, *q = (unsigned short *)vc->vc_pos;

	p = q + vc->vc_cols - nr - vc->vc_x;
	while (--p >= q)
		scr_writew(scr_readw(p), p + nr);
	scr_memsetw(q, vc->vc_video_erase_char, nr * 2);
	vc->vc_need_wrap = 0;
	if (DO_UPDATE(vc)) {
		unsigned short oldattr = vc->vc_attr;
		vc->vc_sw->con_bmove(vc, vc->vc_y, vc->vc_x, vc->vc_y, vc->vc_x + nr, 1,
				     vc->vc_cols - vc->vc_x - nr);
		vc->vc_attr = vc->vc_video_erase_char >> 8;
		while (nr--)
			vc->vc_sw->con_putc(vc, vc->vc_video_erase_char, vc->vc_y, vc->vc_x + nr);
		vc->vc_attr = oldattr;
	}
}

static void delete_char(struct vc_data *vc, unsigned int nr)
{
	unsigned int i = vc->vc_x;
	unsigned short *p = (unsigned short *)vc->vc_pos;

	while (++i <= vc->vc_cols - nr) {
		scr_writew(scr_readw(p+nr), p);
		p++;
	}
	scr_memsetw(p, vc->vc_video_erase_char, nr * 2);
	vc->vc_need_wrap = 0;
	if (DO_UPDATE(vc)) {
		unsigned short oldattr = vc->vc_attr;
		vc->vc_sw->con_bmove(vc, vc->vc_y, vc->vc_x + nr, vc->vc_y, vc->vc_x, 1,
				     vc->vc_cols - vc->vc_x - nr);
		vc->vc_attr = vc->vc_video_erase_char >> 8;
		while (nr--)
			vc->vc_sw->con_putc(vc, vc->vc_video_erase_char, vc->vc_y,
				     vc->vc_cols - 1 - nr);
		vc->vc_attr = oldattr;
	}
}

static int softcursor_original;

static void add_softcursor(struct vc_data *vc)
{
	int i = scr_readw((u16 *) vc->vc_pos);
	u32 type = vc->vc_cursor_type;

	if (! (type & 0x10)) return;
	if (softcursor_original != -1) return;
	softcursor_original = i;
	i |= ((type >> 8) & 0xff00 );
	i ^= ((type) & 0xff00 );
	if ((type & 0x20) && ((softcursor_original & 0x7000) == (i & 0x7000))) i ^= 0x7000;
	if ((type & 0x40) && ((i & 0x700) == ((i & 0x7000) >> 4))) i ^= 0x0700;
	scr_writew(i, (u16 *) vc->vc_pos);
	if (DO_UPDATE(vc))
		vc->vc_sw->con_putc(vc, i, vc->vc_y, vc->vc_x);
}

static void hide_softcursor(struct vc_data *vc)
{
	if (softcursor_original != -1) {
		scr_writew(softcursor_original, (u16 *)vc->vc_pos);
		if (DO_UPDATE(vc))
			vc->vc_sw->con_putc(vc, softcursor_original,
					vc->vc_y, vc->vc_x);
		softcursor_original = -1;
	}
}

static void hide_cursor(struct vc_data *vc)
{
	if (vc == sel_cons)
		clear_selection();
	vc->vc_sw->con_cursor(vc, CM_ERASE);
	hide_softcursor(vc);
}

static void set_cursor(struct vc_data *vc)
{
	if (!IS_FG(vc) || console_blanked ||
	    vc->vc_mode == KD_GRAPHICS)
		return;
	if (vc->vc_deccm) {
		if (vc == sel_cons)
			clear_selection();
		add_softcursor(vc);
		if ((vc->vc_cursor_type & 0x0f) != 1)
			vc->vc_sw->con_cursor(vc, CM_DRAW);
	} else
		hide_cursor(vc);
}

static void set_origin(struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

	if (!CON_IS_VISIBLE(vc) ||
	    !vc->vc_sw->con_set_origin ||
	    !vc->vc_sw->con_set_origin(vc))
		vc->vc_origin = (unsigned long)vc->vc_screenbuf;
	vc->vc_visible_origin = vc->vc_origin;
	vc->vc_scr_end = vc->vc_origin + vc->vc_screenbuf_size;
	vc->vc_pos = vc->vc_origin + vc->vc_size_row * vc->vc_y + 2 * vc->vc_x;
}

static inline void save_screen(struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

	if (vc->vc_sw->con_save_screen)
		vc->vc_sw->con_save_screen(vc);
}

/*
 *	Redrawing of screen
 */

static void clear_buffer_attributes(struct vc_data *vc)
{
	unsigned short *p = (unsigned short *)vc->vc_origin;
	int count = vc->vc_screenbuf_size / 2;
	int mask = vc->vc_hi_font_mask | 0xff;

	for (; count > 0; count--, p++) {
		scr_writew((scr_readw(p)&mask) | (vc->vc_video_erase_char & ~mask), p);
	}
}

void redraw_screen(struct vc_data *vc, int is_switch)
{
	int redraw = 0;

	WARN_CONSOLE_UNLOCKED();

	if (!vc) {
		/* strange ... */
		/* printk("redraw_screen: tty %d not allocated ??\n", new_console+1); */
		return;
	}

	if (is_switch) {
		struct vc_data *old_vc = vc_cons[fg_console].d;
		if (old_vc == vc)
			return;
		if (!CON_IS_VISIBLE(vc))
			redraw = 1;
		*vc->vc_display_fg = vc;
		fg_console = vc->vc_num;
		hide_cursor(old_vc);
		if (!CON_IS_VISIBLE(old_vc)) {
			save_screen(old_vc);
			set_origin(old_vc);
		}
	} else {
		hide_cursor(vc);
		redraw = 1;
	}

	if (redraw) {
		int update;
		int old_was_color = vc->vc_can_do_color;

		set_origin(vc);
		update = vc->vc_sw->con_switch(vc);
		set_palette(vc);
		/*
		 * If console changed from mono<->color, the best we can do
		 * is to clear the buffer attributes. As it currently stands,
		 * rebuilding new attributes from the old buffer is not doable
		 * without overly complex code.
		 */
		if (old_was_color != vc->vc_can_do_color) {
			update_attr(vc);
			clear_buffer_attributes(vc);
		}
		if (update && vc->vc_mode != KD_GRAPHICS)
			do_update_region(vc, vc->vc_origin, vc->vc_screenbuf_size / 2);
	}
	set_cursor(vc);
	if (is_switch) {
		set_leds();
		compute_shiftstate();
	}
}

/*
 *	Allocation, freeing and resizing of VTs.
 */

int vc_cons_allocated(unsigned int i)
{
	return (i < MAX_NR_CONSOLES && vc_cons[i].d);
}

static void visual_init(struct vc_data *vc, int num, int init)
{
	/* ++Geert: vc->vc_sw->con_init determines console size */
	if (vc->vc_sw)
		module_put(vc->vc_sw->owner);
	vc->vc_sw = conswitchp;
#ifndef VT_SINGLE_DRIVER
	if (con_driver_map[num])
		vc->vc_sw = con_driver_map[num];
#endif
	__module_get(vc->vc_sw->owner);
	vc->vc_num = num;
	vc->vc_display_fg = &master_display_fg;
	vc->vc_uni_pagedir_loc = &vc->vc_uni_pagedir;
	vc->vc_uni_pagedir = 0;
	vc->vc_hi_font_mask = 0;
	vc->vc_complement_mask = 0;
	vc->vc_can_do_color = 0;
	vc->vc_sw->con_init(vc, init);
	if (!vc->vc_complement_mask)
		vc->vc_complement_mask = vc->vc_can_do_color ? 0x7700 : 0x0800;
	vc->vc_s_complement_mask = vc->vc_complement_mask;
	vc->vc_size_row = vc->vc_cols << 1;
	vc->vc_screenbuf_size = vc->vc_rows * vc->vc_size_row;
}

int vc_allocate(unsigned int currcons)	/* return 0 on success */
{
	WARN_CONSOLE_UNLOCKED();

	if (currcons >= MAX_NR_CONSOLES)
		return -ENXIO;
	if (!vc_cons[currcons].d) {
	    struct vc_data *vc;

	    /* prevent users from taking too much memory */
	    if (currcons >= MAX_NR_USER_CONSOLES && !capable(CAP_SYS_RESOURCE))
	      return -EPERM;

	    /* due to the granularity of kmalloc, we waste some memory here */
	    /* the alloc is done in two steps, to optimize the common situation
	       of a 25x80 console (structsize=216, screenbuf_size=4000) */
	    /* although the numbers above are not valid since long ago, the
	       point is still up-to-date and the comment still has its value
	       even if only as a historical artifact.  --mj, July 1998 */
	    vc = kmalloc(sizeof(struct vc_data), GFP_KERNEL);
	    if (!vc)
		return -ENOMEM;
	    memset(vc, 0, sizeof(*vc));
	    vc_cons[currcons].d = vc;
	    visual_init(vc, currcons, 1);
	    if (!*vc->vc_uni_pagedir_loc)
		con_set_default_unimap(vc);
	    vc->vc_screenbuf = kmalloc(vc->vc_screenbuf_size, GFP_KERNEL);
	    if (!vc->vc_screenbuf) {
		kfree(vc);
		vc_cons[currcons].d = NULL;
		return -ENOMEM;
	    }
	    vc->vc_kmalloced = 1;
	    vc_init(vc, vc->vc_rows, vc->vc_cols, 1);
	}
	return 0;
}

static inline int resize_screen(struct vc_data *vc, int width, int height)
{
	/* Resizes the resolution of the display adapater */
	int err = 0;

	if (vc->vc_mode != KD_GRAPHICS && vc->vc_sw->con_resize)
		err = vc->vc_sw->con_resize(vc, width, height);
	return err;
}

/*
 * Change # of rows and columns (0 means unchanged/the size of fg_console)
 * [this is to be used together with some user program
 * like resize that changes the hardware videomode]
 */
#define VC_RESIZE_MAXCOL (32767)
#define VC_RESIZE_MAXROW (32767)
int vc_resize(struct vc_data *vc, unsigned int cols, unsigned int lines)
{
	unsigned long old_origin, new_origin, new_scr_end, rlth, rrem, err = 0;
	unsigned int old_cols, old_rows, old_row_size, old_screen_size;
	unsigned int new_cols, new_rows, new_row_size, new_screen_size;
	unsigned int end;
	unsigned short *newscreen;

	WARN_CONSOLE_UNLOCKED();

	if (!vc)
		return -ENXIO;

	if (cols > VC_RESIZE_MAXCOL || lines > VC_RESIZE_MAXROW)
		return -EINVAL;

	new_cols = (cols ? cols : vc->vc_cols);
	new_rows = (lines ? lines : vc->vc_rows);
	new_row_size = new_cols << 1;
	new_screen_size = new_row_size * new_rows;

	if (new_cols == vc->vc_cols && new_rows == vc->vc_rows)
		return 0;

	newscreen = (unsigned short *) kmalloc(new_screen_size, GFP_USER);
	if (!newscreen)
		return -ENOMEM;

	old_rows = vc->vc_rows;
	old_cols = vc->vc_cols;
	old_row_size = vc->vc_size_row;
	old_screen_size = vc->vc_screenbuf_size;

	err = resize_screen(vc, new_cols, new_rows);
	if (err) {
		kfree(newscreen);
		return err;
	}

	vc->vc_rows = new_rows;
	vc->vc_cols = new_cols;
	vc->vc_size_row = new_row_size;
	vc->vc_screenbuf_size = new_screen_size;

	rlth = min(old_row_size, new_row_size);
	rrem = new_row_size - rlth;
	old_origin = vc->vc_origin;
	new_origin = (long) newscreen;
	new_scr_end = new_origin + new_screen_size;

	if (vc->vc_y > new_rows) {
		if (old_rows - vc->vc_y < new_rows) {
			/*
			 * Cursor near the bottom, copy contents from the
			 * bottom of buffer
			 */
			old_origin += (old_rows - new_rows) * old_row_size;
			end = vc->vc_scr_end;
		} else {
			/*
			 * Cursor is in no man's land, copy 1/2 screenful
			 * from the top and bottom of cursor position
			 */
			old_origin += (vc->vc_y - new_rows/2) * old_row_size;
			end = old_origin + (old_row_size * new_rows);
		}
	} else
		/*
		 * Cursor near the top, copy contents from the top of buffer
		 */
		end = (old_rows > new_rows) ? old_origin +
			(old_row_size * new_rows) :
			vc->vc_scr_end;

	update_attr(vc);

	while (old_origin < end) {
		scr_memcpyw((unsigned short *) new_origin,
			    (unsigned short *) old_origin, rlth);
		if (rrem)
			scr_memsetw((void *)(new_origin + rlth),
				    vc->vc_video_erase_char, rrem);
		old_origin += old_row_size;
		new_origin += new_row_size;
	}
	if (new_scr_end > new_origin)
		scr_memsetw((void *)new_origin, vc->vc_video_erase_char,
			    new_scr_end - new_origin);
	if (vc->vc_kmalloced)
		kfree(vc->vc_screenbuf);
	vc->vc_screenbuf = newscreen;
	vc->vc_kmalloced = 1;
	vc->vc_screenbuf_size = new_screen_size;
	set_origin(vc);

	/* do part of a reset_terminal() */
	vc->vc_top = 0;
	vc->vc_bottom = vc->vc_rows;
	gotoxy(vc, vc->vc_x, vc->vc_y);
	save_cur(vc);

	if (vc->vc_tty) {
		struct winsize ws, *cws = &vc->vc_tty->winsize;

		memset(&ws, 0, sizeof(ws));
		ws.ws_row = vc->vc_rows;
		ws.ws_col = vc->vc_cols;
		ws.ws_ypixel = vc->vc_scan_lines;
		if ((ws.ws_row != cws->ws_row || ws.ws_col != cws->ws_col) &&
		    vc->vc_tty->pgrp > 0)
			kill_pg(vc->vc_tty->pgrp, SIGWINCH, 1);
		*cws = ws;
	}

	if (CON_IS_VISIBLE(vc))
		update_screen(vc);
	return err;
}


void vc_disallocate(unsigned int currcons)
{
	WARN_CONSOLE_UNLOCKED();

	if (vc_cons_allocated(currcons)) {
		struct vc_data *vc = vc_cons[currcons].d;
		vc->vc_sw->con_deinit(vc);
		if (vc->vc_kmalloced)
			kfree(vc->vc_screenbuf);
		if (currcons >= MIN_NR_CONSOLES)
			kfree(vc);
		vc_cons[currcons].d = NULL;
	}
}

/*
 *	VT102 emulator
 */

#define set_kbd(vc, x)	set_vc_kbd_mode(kbd_table + (vc)->vc_num, (x))
#define clr_kbd(vc, x)	clr_vc_kbd_mode(kbd_table + (vc)->vc_num, (x))
#define is_kbd(vc, x)	vc_kbd_mode(kbd_table + (vc)->vc_num, (x))

#define decarm		VC_REPEAT
#define decckm		VC_CKMODE
#define kbdapplic	VC_APPLIC
#define lnm		VC_CRLF

/*
 * this is what the terminal answers to a ESC-Z or csi0c query.
 */
#define VT100ID "\033[?1;2c"
#define VT102ID "\033[?6c"

unsigned char color_table[] = { 0, 4, 2, 6, 1, 5, 3, 7,
				       8,12,10,14, 9,13,11,15 };

/* the default colour table, for VGA+ colour systems */
int default_red[] = {0x00,0xaa,0x00,0xaa,0x00,0xaa,0x00,0xaa,
    0x55,0xff,0x55,0xff,0x55,0xff,0x55,0xff};
int default_grn[] = {0x00,0x00,0xaa,0x55,0x00,0x00,0xaa,0xaa,
    0x55,0x55,0xff,0xff,0x55,0x55,0xff,0xff};
int default_blu[] = {0x00,0x00,0x00,0x00,0xaa,0xaa,0xaa,0xaa,
    0x55,0x55,0x55,0x55,0xff,0xff,0xff,0xff};

/*
 * gotoxy() must verify all boundaries, because the arguments
 * might also be negative. If the given position is out of
 * bounds, the cursor is placed at the nearest margin.
 */
static void gotoxy(struct vc_data *vc, int new_x, int new_y)
{
	int min_y, max_y;

	if (new_x < 0)
		vc->vc_x = 0;
	else {
		if (new_x >= vc->vc_cols)
			vc->vc_x = vc->vc_cols - 1;
		else
			vc->vc_x = new_x;
	}

 	if (vc->vc_decom) {
		min_y = vc->vc_top;
		max_y = vc->vc_bottom;
	} else {
		min_y = 0;
		max_y = vc->vc_rows;
	}
	if (new_y < min_y)
		vc->vc_y = min_y;
	else if (new_y >= max_y)
		vc->vc_y = max_y - 1;
	else
		vc->vc_y = new_y;
	vc->vc_pos = vc->vc_origin + vc->vc_y * vc->vc_size_row + (vc->vc_x<<1);
	vc->vc_need_wrap = 0;
}

/* for absolute user moves, when decom is set */
static void gotoxay(struct vc_data *vc, int new_x, int new_y)
{
	gotoxy(vc, new_x, vc->vc_decom ? (vc->vc_top + new_y) : new_y);
}

void scrollback(struct vc_data *vc, int lines)
{
	if (!lines)
		lines = vc->vc_rows / 2;
	scrolldelta(-lines);
}

void scrollfront(struct vc_data *vc, int lines)
{
	if (!lines)
		lines = vc->vc_rows / 2;
	scrolldelta(lines);
}

static void lf(struct vc_data *vc)
{
    	/* don't scroll if above bottom of scrolling region, or
	 * if below scrolling region
	 */
    	if (vc->vc_y + 1 == vc->vc_bottom)
		scrup(vc, vc->vc_top, vc->vc_bottom, 1);
	else if (vc->vc_y < vc->vc_rows - 1) {
	    	vc->vc_y++;
		vc->vc_pos += vc->vc_size_row;
	}
	vc->vc_need_wrap = 0;
}

static void ri(struct vc_data *vc)
{
    	/* don't scroll if below top of scrolling region, or
	 * if above scrolling region
	 */
	if (vc->vc_y == vc->vc_top)
		scrdown(vc, vc->vc_top, vc->vc_bottom, 1);
	else if (vc->vc_y > 0) {
		vc->vc_y--;
		vc->vc_pos -= vc->vc_size_row;
	}
	vc->vc_need_wrap = 0;
}

static inline void cr(struct vc_data *vc)
{
	vc->vc_pos -= vc->vc_x << 1;
	vc->vc_need_wrap = vc->vc_x = 0;
}

static inline void bs(struct vc_data *vc)
{
	if (vc->vc_x) {
		vc->vc_pos -= 2;
		vc->vc_x--;
		vc->vc_need_wrap = 0;
	}
}

static inline void del(struct vc_data *vc)
{
	/* ignored */
}

static void csi_J(struct vc_data *vc, int vpar)
{
	unsigned int count;
	unsigned short * start;

	switch (vpar) {
		case 0:	/* erase from cursor to end of display */
			count = (vc->vc_scr_end - vc->vc_pos) >> 1;
			start = (unsigned short *)vc->vc_pos;
			if (DO_UPDATE(vc)) {
				/* do in two stages */
				vc->vc_sw->con_clear(vc, vc->vc_y, vc->vc_x, 1,
					      vc->vc_cols - vc->vc_x);
				vc->vc_sw->con_clear(vc, vc->vc_y + 1, 0,
					      vc->vc_rows - vc->vc_y - 1,
					      vc->vc_cols);
			}
			break;
		case 1:	/* erase from start to cursor */
			count = ((vc->vc_pos - vc->vc_origin) >> 1) + 1;
			start = (unsigned short *)vc->vc_origin;
			if (DO_UPDATE(vc)) {
				/* do in two stages */
				vc->vc_sw->con_clear(vc, 0, 0, vc->vc_y,
					      vc->vc_cols);
				vc->vc_sw->con_clear(vc, vc->vc_y, 0, 1,
					      vc->vc_x + 1);
			}
			break;
		case 2: /* erase whole display */
			count = vc->vc_cols * vc->vc_rows;
			start = (unsigned short *)vc->vc_origin;
			if (DO_UPDATE(vc))
				vc->vc_sw->con_clear(vc, 0, 0,
					      vc->vc_rows,
					      vc->vc_cols);
			break;
		default:
			return;
	}
	scr_memsetw(start, vc->vc_video_erase_char, 2 * count);
	vc->vc_need_wrap = 0;
}

static void csi_K(struct vc_data *vc, int vpar)
{
	unsigned int count;
	unsigned short * start;

	switch (vpar) {
		case 0:	/* erase from cursor to end of line */
			count = vc->vc_cols - vc->vc_x;
			start = (unsigned short *)vc->vc_pos;
			if (DO_UPDATE(vc))
				vc->vc_sw->con_clear(vc, vc->vc_y, vc->vc_x, 1,
						     vc->vc_cols - vc->vc_x);
			break;
		case 1:	/* erase from start of line to cursor */
			start = (unsigned short *)(vc->vc_pos - (vc->vc_x << 1));
			count = vc->vc_x + 1;
			if (DO_UPDATE(vc))
				vc->vc_sw->con_clear(vc, vc->vc_y, 0, 1,
						     vc->vc_x + 1);
			break;
		case 2: /* erase whole line */
			start = (unsigned short *)(vc->vc_pos - (vc->vc_x << 1));
			count = vc->vc_cols;
			if (DO_UPDATE(vc))
				vc->vc_sw->con_clear(vc, vc->vc_y, 0, 1,
					      vc->vc_cols);
			break;
		default:
			return;
	}
	scr_memsetw(start, vc->vc_video_erase_char, 2 * count);
	vc->vc_need_wrap = 0;
}

static void csi_X(struct vc_data *vc, int vpar) /* erase the following vpar positions */
{					  /* not vt100? */
	int count;

	if (!vpar)
		vpar++;
	count = (vpar > vc->vc_cols - vc->vc_x) ? (vc->vc_cols - vc->vc_x) : vpar;

	scr_memsetw((unsigned short *)vc->vc_pos, vc->vc_video_erase_char, 2 * count);
	if (DO_UPDATE(vc))
		vc->vc_sw->con_clear(vc, vc->vc_y, vc->vc_x, 1, count);
	vc->vc_need_wrap = 0;
}

static void default_attr(struct vc_data *vc)
{
	vc->vc_intensity = 1;
	vc->vc_underline = 0;
	vc->vc_reverse = 0;
	vc->vc_blink = 0;
	vc->vc_color = vc->vc_def_color;
}

/* console_sem is held */
static void csi_m(struct vc_data *vc)
{
	int i;

	for (i = 0; i <= vc->vc_npar; i++)
		switch (vc->vc_par[i]) {
			case 0:	/* all attributes off */
				default_attr(vc);
				break;
			case 1:
				vc->vc_intensity = 2;
				break;
			case 2:
				vc->vc_intensity = 0;
				break;
			case 4:
				vc->vc_underline = 1;
				break;
			case 5:
				vc->vc_blink = 1;
				break;
			case 7:
				vc->vc_reverse = 1;
				break;
			case 10: /* ANSI X3.64-1979 (SCO-ish?)
				  * Select primary font, don't display
				  * control chars if defined, don't set
				  * bit 8 on output.
				  */
				vc->vc_translate = set_translate(vc->vc_charset == 0
						? vc->vc_G0_charset
						: vc->vc_G1_charset, vc);
				vc->vc_disp_ctrl = 0;
				vc->vc_toggle_meta = 0;
				break;
			case 11: /* ANSI X3.64-1979 (SCO-ish?)
				  * Select first alternate font, lets
				  * chars < 32 be displayed as ROM chars.
				  */
				vc->vc_translate = set_translate(IBMPC_MAP, vc);
				vc->vc_disp_ctrl = 1;
				vc->vc_toggle_meta = 0;
				break;
			case 12: /* ANSI X3.64-1979 (SCO-ish?)
				  * Select second alternate font, toggle
				  * high bit before displaying as ROM char.
				  */
				vc->vc_translate = set_translate(IBMPC_MAP, vc);
				vc->vc_disp_ctrl = 1;
				vc->vc_toggle_meta = 1;
				break;
			case 21:
			case 22:
				vc->vc_intensity = 1;
				break;
			case 24:
				vc->vc_underline = 0;
				break;
			case 25:
				vc->vc_blink = 0;
				break;
			case 27:
				vc->vc_reverse = 0;
				break;
			case 38: /* ANSI X3.64-1979 (SCO-ish?)
				  * Enables underscore, white foreground
				  * with white underscore (Linux - use
				  * default foreground).
				  */
				vc->vc_color = (vc->vc_def_color & 0x0f) | (vc->vc_color & 0xf0);
				vc->vc_underline = 1;
				break;
			case 39: /* ANSI X3.64-1979 (SCO-ish?)
				  * Disable underline option.
				  * Reset colour to default? It did this
				  * before...
				  */
				vc->vc_color = (vc->vc_def_color & 0x0f) | (vc->vc_color & 0xf0);
				vc->vc_underline = 0;
				break;
			case 49:
				vc->vc_color = (vc->vc_def_color & 0xf0) | (vc->vc_color & 0x0f);
				break;
			default:
				if (vc->vc_par[i] >= 30 && vc->vc_par[i] <= 37)
					vc->vc_color = color_table[vc->vc_par[i] - 30]
						| (vc->vc_color & 0xf0);
				else if (vc->vc_par[i] >= 40 && vc->vc_par[i] <= 47)
					vc->vc_color = (color_table[vc->vc_par[i] - 40] << 4)
						| (vc->vc_color & 0x0f);
				break;
		}
	update_attr(vc);
}

static void respond_string(const char *p, struct tty_struct *tty)
{
	while (*p) {
		tty_insert_flip_char(tty, *p, 0);
		p++;
	}
	con_schedule_flip(tty);
}

static void cursor_report(struct vc_data *vc, struct tty_struct *tty)
{
	char buf[40];

	sprintf(buf, "\033[%d;%dR", vc->vc_y + (vc->vc_decom ? vc->vc_top + 1 : 1), vc->vc_x + 1);
	respond_string(buf, tty);
}

static inline void status_report(struct tty_struct *tty)
{
	respond_string("\033[0n", tty);	/* Terminal ok */
}

static inline void respond_ID(struct tty_struct * tty)
{
	respond_string(VT102ID, tty);
}

void mouse_report(struct tty_struct *tty, int butt, int mrx, int mry)
{
	char buf[8];

	sprintf(buf, "\033[M%c%c%c", (char)(' ' + butt), (char)('!' + mrx),
		(char)('!' + mry));
	respond_string(buf, tty);
}

/* invoked via ioctl(TIOCLINUX) and through set_selection */
int mouse_reporting(void)
{
	return vc_cons[fg_console].d->vc_report_mouse;
}

/* console_sem is held */
static void set_mode(struct vc_data *vc, int on_off)
{
	int i;

	for (i = 0; i <= vc->vc_npar; i++)
		if (vc->vc_ques) {
			switch(vc->vc_par[i]) {	/* DEC private modes set/reset */
			case 1:			/* Cursor keys send ^[Ox/^[[x */
				if (on_off)
					set_kbd(vc, decckm);
				else
					clr_kbd(vc, decckm);
				break;
			case 3:	/* 80/132 mode switch unimplemented */
				vc->vc_deccolm = on_off;
#if 0
				vc_resize(deccolm ? 132 : 80, vc->vc_rows);
				/* this alone does not suffice; some user mode
				   utility has to change the hardware regs */
#endif
				break;
			case 5:			/* Inverted screen on/off */
				if (vc->vc_decscnm != on_off) {
					vc->vc_decscnm = on_off;
					invert_screen(vc, 0, vc->vc_screenbuf_size, 0);
					update_attr(vc);
				}
				break;
			case 6:			/* Origin relative/absolute */
				vc->vc_decom = on_off;
				gotoxay(vc, 0, 0);
				break;
			case 7:			/* Autowrap on/off */
				vc->vc_decawm = on_off;
				break;
			case 8:			/* Autorepeat on/off */
				if (on_off)
					set_kbd(vc, decarm);
				else
					clr_kbd(vc, decarm);
				break;
			case 9:
				vc->vc_report_mouse = on_off ? 1 : 0;
				break;
			case 25:		/* Cursor on/off */
				vc->vc_deccm = on_off;
				break;
			case 1000:
				vc->vc_report_mouse = on_off ? 2 : 0;
				break;
			}
		} else {
			switch(vc->vc_par[i]) {	/* ANSI modes set/reset */
			case 3:			/* Monitor (display ctrls) */
				vc->vc_disp_ctrl = on_off;
				break;
			case 4:			/* Insert Mode on/off */
				vc->vc_decim = on_off;
				break;
			case 20:		/* Lf, Enter == CrLf/Lf */
				if (on_off)
					set_kbd(vc, lnm);
				else
					clr_kbd(vc, lnm);
				break;
			}
		}
}

/* console_sem is held */
static void setterm_command(struct vc_data *vc)
{
	switch(vc->vc_par[0]) {
		case 1:	/* set color for underline mode */
			if (vc->vc_can_do_color &&
					vc->vc_par[1] < 16) {
				vc->vc_ulcolor = color_table[vc->vc_par[1]];
				if (vc->vc_underline)
					update_attr(vc);
			}
			break;
		case 2:	/* set color for half intensity mode */
			if (vc->vc_can_do_color &&
					vc->vc_par[1] < 16) {
				vc->vc_halfcolor = color_table[vc->vc_par[1]];
				if (vc->vc_intensity == 0)
					update_attr(vc);
			}
			break;
		case 8:	/* store colors as defaults */
			vc->vc_def_color = vc->vc_attr;
			if (vc->vc_hi_font_mask == 0x100)
				vc->vc_def_color >>= 1;
			default_attr(vc);
			update_attr(vc);
			break;
		case 9:	/* set blanking interval */
			blankinterval = ((vc->vc_par[1] < 60) ? vc->vc_par[1] : 60) * 60 * HZ;
			poke_blanked_console();
			break;
		case 10: /* set bell frequency in Hz */
			if (vc->vc_npar >= 1)
				vc->vc_bell_pitch = vc->vc_par[1];
			else
				vc->vc_bell_pitch = DEFAULT_BELL_PITCH;
			break;
		case 11: /* set bell duration in msec */
			if (vc->vc_npar >= 1)
				vc->vc_bell_duration = (vc->vc_par[1] < 2000) ?
					vc->vc_par[1] * HZ / 1000 : 0;
			else
				vc->vc_bell_duration = DEFAULT_BELL_DURATION;
			break;
		case 12: /* bring specified console to the front */
			if (vc->vc_par[1] >= 1 && vc_cons_allocated(vc->vc_par[1] - 1))
				set_console(vc->vc_par[1] - 1);
			break;
		case 13: /* unblank the screen */
			poke_blanked_console();
			break;
		case 14: /* set vesa powerdown interval */
			vesa_off_interval = ((vc->vc_par[1] < 60) ? vc->vc_par[1] : 60) * 60 * HZ;
			break;
		case 15: /* activate the previous console */
			set_console(last_console);
			break;
	}
}

/* console_sem is held */
static void csi_at(struct vc_data *vc, unsigned int nr)
{
	if (nr > vc->vc_cols - vc->vc_x)
		nr = vc->vc_cols - vc->vc_x;
	else if (!nr)
		nr = 1;
	insert_char(vc, nr);
}

/* console_sem is held */
static void csi_L(struct vc_data *vc, unsigned int nr)
{
	if (nr > vc->vc_rows - vc->vc_y)
		nr = vc->vc_rows - vc->vc_y;
	else if (!nr)
		nr = 1;
	scrdown(vc, vc->vc_y, vc->vc_bottom, nr);
	vc->vc_need_wrap = 0;
}

/* console_sem is held */
static void csi_P(struct vc_data *vc, unsigned int nr)
{
	if (nr > vc->vc_cols - vc->vc_x)
		nr = vc->vc_cols - vc->vc_x;
	else if (!nr)
		nr = 1;
	delete_char(vc, nr);
}

/* console_sem is held */
static void csi_M(struct vc_data *vc, unsigned int nr)
{
	if (nr > vc->vc_rows - vc->vc_y)
		nr = vc->vc_rows - vc->vc_y;
	else if (!nr)
		nr=1;
	scrup(vc, vc->vc_y, vc->vc_bottom, nr);
	vc->vc_need_wrap = 0;
}

/* console_sem is held (except via vc_init->reset_terminal */
static void save_cur(struct vc_data *vc)
{
	vc->vc_saved_x		= vc->vc_x;
	vc->vc_saved_y		= vc->vc_y;
	vc->vc_s_intensity	= vc->vc_intensity;
	vc->vc_s_underline	= vc->vc_underline;
	vc->vc_s_blink		= vc->vc_blink;
	vc->vc_s_reverse	= vc->vc_reverse;
	vc->vc_s_charset	= vc->vc_charset;
	vc->vc_s_color		= vc->vc_color;
	vc->vc_saved_G0		= vc->vc_G0_charset;
	vc->vc_saved_G1		= vc->vc_G1_charset;
}

/* console_sem is held */
static void restore_cur(struct vc_data *vc)
{
	gotoxy(vc, vc->vc_saved_x, vc->vc_saved_y);
	vc->vc_intensity	= vc->vc_s_intensity;
	vc->vc_underline	= vc->vc_s_underline;
	vc->vc_blink		= vc->vc_s_blink;
	vc->vc_reverse		= vc->vc_s_reverse;
	vc->vc_charset		= vc->vc_s_charset;
	vc->vc_color		= vc->vc_s_color;
	vc->vc_G0_charset	= vc->vc_saved_G0;
	vc->vc_G1_charset	= vc->vc_saved_G1;
	vc->vc_translate	= set_translate(vc->vc_charset ? vc->vc_G1_charset : vc->vc_G0_charset, vc);
	update_attr(vc);
	vc->vc_need_wrap = 0;
}

enum { ESnormal, ESesc, ESsquare, ESgetpars, ESgotpars, ESfunckey,
	EShash, ESsetG0, ESsetG1, ESpercent, ESignore, ESnonstd,
	ESpalette };

/* console_sem is held (except via vc_init()) */
static void reset_terminal(struct vc_data *vc, int do_clear)
{
	vc->vc_top		= 0;
	vc->vc_bottom		= vc->vc_rows;
	vc->vc_state		= ESnormal;
	vc->vc_ques		= 0;
	vc->vc_translate	= set_translate(LAT1_MAP, vc);
	vc->vc_G0_charset	= LAT1_MAP;
	vc->vc_G1_charset	= GRAF_MAP;
	vc->vc_charset		= 0;
	vc->vc_need_wrap	= 0;
	vc->vc_report_mouse	= 0;
	vc->vc_utf		= 0;
	vc->vc_utf_count	= 0;

	vc->vc_disp_ctrl	= 0;
	vc->vc_toggle_meta	= 0;

	vc->vc_decscnm		= 0;
	vc->vc_decom		= 0;
	vc->vc_decawm		= 1;
	vc->vc_deccm		= 1;
	vc->vc_decim		= 0;

	set_kbd(vc, decarm);
	clr_kbd(vc, decckm);
	clr_kbd(vc, kbdapplic);
	clr_kbd(vc, lnm);
	kbd_table[vc->vc_num].lockstate = 0;
	kbd_table[vc->vc_num].slockstate = 0;
	kbd_table[vc->vc_num].ledmode = LED_SHOW_FLAGS;
	kbd_table[vc->vc_num].ledflagstate = kbd_table[vc->vc_num].default_ledflagstate;
	/* do not do set_leds here because this causes an endless tasklet loop
	   when the keyboard hasn't been initialized yet */

	vc->vc_cursor_type = CUR_DEFAULT;
	vc->vc_complement_mask = vc->vc_s_complement_mask;

	default_attr(vc);
	update_attr(vc);

	vc->vc_tab_stop[0]	= 0x01010100;
	vc->vc_tab_stop[1]	=
	vc->vc_tab_stop[2]	=
	vc->vc_tab_stop[3]	=
	vc->vc_tab_stop[4]	= 0x01010101;

	vc->vc_bell_pitch = DEFAULT_BELL_PITCH;
	vc->vc_bell_duration = DEFAULT_BELL_DURATION;

	gotoxy(vc, 0, 0);
	save_cur(vc);
	if (do_clear)
	    csi_J(vc, 2);
}

/* console_sem is held */
static void do_con_trol(struct tty_struct *tty, struct vc_data *vc, int c)
{
	/*
	 *  Control characters can be used in the _middle_
	 *  of an escape sequence.
	 */
	switch (c) {
	case 0:
		return;
	case 7:
		if (vc->vc_bell_duration)
			kd_mksound(vc->vc_bell_pitch, vc->vc_bell_duration);
		return;
	case 8:
		bs(vc);
		return;
	case 9:
		vc->vc_pos -= (vc->vc_x << 1);
		while (vc->vc_x < vc->vc_cols - 1) {
			vc->vc_x++;
			if (vc->vc_tab_stop[vc->vc_x >> 5] & (1 << (vc->vc_x & 31)))
				break;
		}
		vc->vc_pos += (vc->vc_x << 1);
		return;
	case 10: case 11: case 12:
		lf(vc);
		if (!is_kbd(vc, lnm))
			return;
	case 13:
		cr(vc);
		return;
	case 14:
		vc->vc_charset = 1;
		vc->vc_translate = set_translate(vc->vc_G1_charset, vc);
		vc->vc_disp_ctrl = 1;
		return;
	case 15:
		vc->vc_charset = 0;
		vc->vc_translate = set_translate(vc->vc_G0_charset, vc);
		vc->vc_disp_ctrl = 0;
		return;
	case 24: case 26:
		vc->vc_state = ESnormal;
		return;
	case 27:
		vc->vc_state = ESesc;
		return;
	case 127:
		del(vc);
		return;
	case 128+27:
		vc->vc_state = ESsquare;
		return;
	}
	switch(vc->vc_state) {
	case ESesc:
		vc->vc_state = ESnormal;
		switch (c) {
		case '[':
			vc->vc_state = ESsquare;
			return;
		case ']':
			vc->vc_state = ESnonstd;
			return;
		case '%':
			vc->vc_state = ESpercent;
			return;
		case 'E':
			cr(vc);
			lf(vc);
			return;
		case 'M':
			ri(vc);
			return;
		case 'D':
			lf(vc);
			return;
		case 'H':
			vc->vc_tab_stop[vc->vc_x >> 5] |= (1 << (vc->vc_x & 31));
			return;
		case 'Z':
			respond_ID(tty);
			return;
		case '7':
			save_cur(vc);
			return;
		case '8':
			restore_cur(vc);
			return;
		case '(':
			vc->vc_state = ESsetG0;
			return;
		case ')':
			vc->vc_state = ESsetG1;
			return;
		case '#':
			vc->vc_state = EShash;
			return;
		case 'c':
			reset_terminal(vc, 1);
			return;
		case '>':  /* Numeric keypad */
			clr_kbd(vc, kbdapplic);
			return;
		case '=':  /* Appl. keypad */
			set_kbd(vc, kbdapplic);
			return;
		}
		return;
	case ESnonstd:
		if (c=='P') {   /* palette escape sequence */
			for (vc->vc_npar = 0; vc->vc_npar < NPAR; vc->vc_npar++)
				vc->vc_par[vc->vc_npar] = 0;
			vc->vc_npar = 0;
			vc->vc_state = ESpalette;
			return;
		} else if (c=='R') {   /* reset palette */
			reset_palette(vc);
			vc->vc_state = ESnormal;
		} else
			vc->vc_state = ESnormal;
		return;
	case ESpalette:
		if ( (c>='0'&&c<='9') || (c>='A'&&c<='F') || (c>='a'&&c<='f') ) {
			vc->vc_par[vc->vc_npar++] = (c > '9' ? (c & 0xDF) - 'A' + 10 : c - '0');
			if (vc->vc_npar == 7) {
				int i = vc->vc_par[0] * 3, j = 1;
				vc->vc_palette[i] = 16 * vc->vc_par[j++];
				vc->vc_palette[i++] += vc->vc_par[j++];
				vc->vc_palette[i] = 16 * vc->vc_par[j++];
				vc->vc_palette[i++] += vc->vc_par[j++];
				vc->vc_palette[i] = 16 * vc->vc_par[j++];
				vc->vc_palette[i] += vc->vc_par[j];
				set_palette(vc);
				vc->vc_state = ESnormal;
			}
		} else
			vc->vc_state = ESnormal;
		return;
	case ESsquare:
		for (vc->vc_npar = 0; vc->vc_npar < NPAR; vc->vc_npar++)
			vc->vc_par[vc->vc_npar] = 0;
		vc->vc_npar = 0;
		vc->vc_state = ESgetpars;
		if (c == '[') { /* Function key */
			vc->vc_state=ESfunckey;
			return;
		}
		vc->vc_ques = (c == '?');
		if (vc->vc_ques)
			return;
	case ESgetpars:
		if (c == ';' && vc->vc_npar < NPAR - 1) {
			vc->vc_npar++;
			return;
		} else if (c>='0' && c<='9') {
			vc->vc_par[vc->vc_npar] *= 10;
			vc->vc_par[vc->vc_npar] += c - '0';
			return;
		} else
			vc->vc_state = ESgotpars;
	case ESgotpars:
		vc->vc_state = ESnormal;
		switch(c) {
		case 'h':
			set_mode(vc, 1);
			return;
		case 'l':
			set_mode(vc, 0);
			return;
		case 'c':
			if (vc->vc_ques) {
				if (vc->vc_par[0])
					vc->vc_cursor_type = vc->vc_par[0] | (vc->vc_par[1] << 8) | (vc->vc_par[2] << 16);
				else
					vc->vc_cursor_type = CUR_DEFAULT;
				return;
			}
			break;
		case 'm':
			if (vc->vc_ques) {
				clear_selection();
				if (vc->vc_par[0])
					vc->vc_complement_mask = vc->vc_par[0] << 8 | vc->vc_par[1];
				else
					vc->vc_complement_mask = vc->vc_s_complement_mask;
				return;
			}
			break;
		case 'n':
			if (!vc->vc_ques) {
				if (vc->vc_par[0] == 5)
					status_report(tty);
				else if (vc->vc_par[0] == 6)
					cursor_report(vc, tty);
			}
			return;
		}
		if (vc->vc_ques) {
			vc->vc_ques = 0;
			return;
		}
		switch(c) {
		case 'G': case '`':
			if (vc->vc_par[0])
				vc->vc_par[0]--;
			gotoxy(vc, vc->vc_par[0], vc->vc_y);
			return;
		case 'A':
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			gotoxy(vc, vc->vc_x, vc->vc_y - vc->vc_par[0]);
			return;
		case 'B': case 'e':
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			gotoxy(vc, vc->vc_x, vc->vc_y + vc->vc_par[0]);
			return;
		case 'C': case 'a':
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			gotoxy(vc, vc->vc_x + vc->vc_par[0], vc->vc_y);
			return;
		case 'D':
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			gotoxy(vc, vc->vc_x - vc->vc_par[0], vc->vc_y);
			return;
		case 'E':
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			gotoxy(vc, 0, vc->vc_y + vc->vc_par[0]);
			return;
		case 'F':
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			gotoxy(vc, 0, vc->vc_y - vc->vc_par[0]);
			return;
		case 'd':
			if (vc->vc_par[0])
				vc->vc_par[0]--;
			gotoxay(vc, vc->vc_x ,vc->vc_par[0]);
			return;
		case 'H': case 'f':
			if (vc->vc_par[0])
				vc->vc_par[0]--;
			if (vc->vc_par[1])
				vc->vc_par[1]--;
			gotoxay(vc, vc->vc_par[1], vc->vc_par[0]);
			return;
		case 'J':
			csi_J(vc, vc->vc_par[0]);
			return;
		case 'K':
			csi_K(vc, vc->vc_par[0]);
			return;
		case 'L':
			csi_L(vc, vc->vc_par[0]);
			return;
		case 'M':
			csi_M(vc, vc->vc_par[0]);
			return;
		case 'P':
			csi_P(vc, vc->vc_par[0]);
			return;
		case 'c':
			if (!vc->vc_par[0])
				respond_ID(tty);
			return;
		case 'g':
			if (!vc->vc_par[0])
				vc->vc_tab_stop[vc->vc_x >> 5] &= ~(1 << (vc->vc_x & 31));
			else if (vc->vc_par[0] == 3) {
				vc->vc_tab_stop[0] =
					vc->vc_tab_stop[1] =
					vc->vc_tab_stop[2] =
					vc->vc_tab_stop[3] =
					vc->vc_tab_stop[4] = 0;
			}
			return;
		case 'm':
			csi_m(vc);
			return;
		case 'q': /* DECLL - but only 3 leds */
			/* map 0,1,2,3 to 0,1,2,4 */
			if (vc->vc_par[0] < 4)
				setledstate(kbd_table + vc->vc_num,
					    (vc->vc_par[0] < 3) ? vc->vc_par[0] : 4);
			return;
		case 'r':
			if (!vc->vc_par[0])
				vc->vc_par[0]++;
			if (!vc->vc_par[1])
				vc->vc_par[1] = vc->vc_rows;
			/* Minimum allowed region is 2 lines */
			if (vc->vc_par[0] < vc->vc_par[1] &&
			    vc->vc_par[1] <= vc->vc_rows) {
				vc->vc_top = vc->vc_par[0] - 1;
				vc->vc_bottom = vc->vc_par[1];
				gotoxay(vc, 0, 0);
			}
			return;
		case 's':
			save_cur(vc);
			return;
		case 'u':
			restore_cur(vc);
			return;
		case 'X':
			csi_X(vc, vc->vc_par[0]);
			return;
		case '@':
			csi_at(vc, vc->vc_par[0]);
			return;
		case ']': /* setterm functions */
			setterm_command(vc);
			return;
		}
		return;
	case ESpercent:
		vc->vc_state = ESnormal;
		switch (c) {
		case '@':  /* defined in ISO 2022 */
			vc->vc_utf = 0;
			return;
		case 'G':  /* prelim official escape code */
		case '8':  /* retained for compatibility */
			vc->vc_utf = 1;
			return;
		}
		return;
	case ESfunckey:
		vc->vc_state = ESnormal;
		return;
	case EShash:
		vc->vc_state = ESnormal;
		if (c == '8') {
			/* DEC screen alignment test. kludge :-) */
			vc->vc_video_erase_char =
				(vc->vc_video_erase_char & 0xff00) | 'E';
			csi_J(vc, 2);
			vc->vc_video_erase_char =
				(vc->vc_video_erase_char & 0xff00) | ' ';
			do_update_region(vc, vc->vc_origin, vc->vc_screenbuf_size / 2);
		}
		return;
	case ESsetG0:
		if (c == '0')
			vc->vc_G0_charset = GRAF_MAP;
		else if (c == 'B')
			vc->vc_G0_charset = LAT1_MAP;
		else if (c == 'U')
			vc->vc_G0_charset = IBMPC_MAP;
		else if (c == 'K')
			vc->vc_G0_charset = USER_MAP;
		if (vc->vc_charset == 0)
			vc->vc_translate = set_translate(vc->vc_G0_charset, vc);
		vc->vc_state = ESnormal;
		return;
	case ESsetG1:
		if (c == '0')
			vc->vc_G1_charset = GRAF_MAP;
		else if (c == 'B')
			vc->vc_G1_charset = LAT1_MAP;
		else if (c == 'U')
			vc->vc_G1_charset = IBMPC_MAP;
		else if (c == 'K')
			vc->vc_G1_charset = USER_MAP;
		if (vc->vc_charset == 1)
			vc->vc_translate = set_translate(vc->vc_G1_charset, vc);
		vc->vc_state = ESnormal;
		return;
	default:
		vc->vc_state = ESnormal;
	}
}

/* This is a temporary buffer used to prepare a tty console write
 * so that we can easily avoid touching user space while holding the
 * console spinlock.  It is allocated in con_init and is shared by
 * this code and the vc_screen read/write tty calls.
 *
 * We have to allocate this statically in the kernel data section
 * since console_init (and thus con_init) are called before any
 * kernel memory allocation is available.
 */
char con_buf[CON_BUF_SIZE];
DECLARE_MUTEX(con_buf_sem);

/* acquires console_sem */
static int do_con_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
#ifdef VT_BUF_VRAM_ONLY
#define FLUSH do { } while(0);
#else
#define FLUSH if (draw_x >= 0) { \
	vc->vc_sw->con_putcs(vc, (u16 *)draw_from, (u16 *)draw_to - (u16 *)draw_from, vc->vc_y, draw_x); \
	draw_x = -1; \
	}
#endif

	int c, tc, ok, n = 0, draw_x = -1;
	unsigned int currcons;
	unsigned long draw_from = 0, draw_to = 0;
	struct vc_data *vc;
	u16 himask, charmask;
	const unsigned char *orig_buf = NULL;
	int orig_count;

	if (in_interrupt())
		return count;

	might_sleep();

	acquire_console_sem();
	vc = tty->driver_data;
	if (vc == NULL) {
		printk(KERN_ERR "vt: argh, driver_data is NULL !\n");
		release_console_sem();
		return 0;
	}

	currcons = vc->vc_num;
	if (!vc_cons_allocated(currcons)) {
	    /* could this happen? */
	    static int error = 0;
	    if (!error) {
		error = 1;
		printk("con_write: tty %d not allocated\n", currcons+1);
	    }
	    release_console_sem();
	    return 0;
	}
	release_console_sem();

	orig_buf = buf;
	orig_count = count;

	/* At this point 'buf' is guaranteed to be a kernel buffer
	 * and therefore no access to userspace (and therefore sleeping)
	 * will be needed.  The con_buf_sem serializes all tty based
	 * console rendering and vcs write/read operations.  We hold
	 * the console spinlock during the entire write.
	 */

	acquire_console_sem();

	vc = tty->driver_data;
	if (vc == NULL) {
		printk(KERN_ERR "vt: argh, driver_data _became_ NULL !\n");
		release_console_sem();
		goto out;
	}

	himask = vc->vc_hi_font_mask;
	charmask = himask ? 0x1ff : 0xff;

	/* undraw cursor first */
	if (IS_FG(vc))
		hide_cursor(vc);

	while (!tty->stopped && count) {
		int orig = *buf;
		c = orig;
		buf++;
		n++;
		count--;

		/* Do no translation at all in control states */
		if (vc->vc_state != ESnormal) {
			tc = c;
		} else if (vc->vc_utf) {
		    /* Combine UTF-8 into Unicode */
		    /* Incomplete characters silently ignored */
		    if(c > 0x7f) {
			if (vc->vc_utf_count > 0 && (c & 0xc0) == 0x80) {
				vc->vc_utf_char = (vc->vc_utf_char << 6) | (c & 0x3f);
				vc->vc_utf_count--;
				if (vc->vc_utf_count == 0)
				    tc = c = vc->vc_utf_char;
				else continue;
			} else {
				if ((c & 0xe0) == 0xc0) {
				    vc->vc_utf_count = 1;
				    vc->vc_utf_char = (c & 0x1f);
				} else if ((c & 0xf0) == 0xe0) {
				    vc->vc_utf_count = 2;
				    vc->vc_utf_char = (c & 0x0f);
				} else if ((c & 0xf8) == 0xf0) {
				    vc->vc_utf_count = 3;
				    vc->vc_utf_char = (c & 0x07);
				} else if ((c & 0xfc) == 0xf8) {
				    vc->vc_utf_count = 4;
				    vc->vc_utf_char = (c & 0x03);
				} else if ((c & 0xfe) == 0xfc) {
				    vc->vc_utf_count = 5;
				    vc->vc_utf_char = (c & 0x01);
				} else
				    vc->vc_utf_count = 0;
				continue;
			      }
		    } else {
		      tc = c;
		      vc->vc_utf_count = 0;
		    }
		} else {	/* no utf */
		  tc = vc->vc_translate[vc->vc_toggle_meta ? (c | 0x80) : c];
		}

                /* If the original code was a control character we
                 * only allow a glyph to be displayed if the code is
                 * not normally used (such as for cursor movement) or
                 * if the disp_ctrl mode has been explicitly enabled.
                 * Certain characters (as given by the CTRL_ALWAYS
                 * bitmap) are always displayed as control characters,
                 * as the console would be pretty useless without
                 * them; to display an arbitrary font position use the
                 * direct-to-font zone in UTF-8 mode.
                 */
                ok = tc && (c >= 32 ||
			    (!vc->vc_utf && !(((vc->vc_disp_ctrl ? CTRL_ALWAYS
						: CTRL_ACTION) >> c) & 1)))
			&& (c != 127 || vc->vc_disp_ctrl)
			&& (c != 128+27);

		if (vc->vc_state == ESnormal && ok) {
			/* Now try to find out how to display it */
			tc = conv_uni_to_pc(vc, tc);
			if ( tc == -4 ) {
                                /* If we got -4 (not found) then see if we have
                                   defined a replacement character (U+FFFD) */
                                tc = conv_uni_to_pc(vc, 0xfffd);

				/* One reason for the -4 can be that we just
				   did a clear_unimap();
				   try at least to show something. */
				if (tc == -4)
				     tc = c;
                        } else if ( tc == -3 ) {
                                /* Bad hash table -- hope for the best */
                                tc = c;
                        }
			if (tc & ~charmask)
                                continue; /* Conversion failed */

			if (vc->vc_need_wrap || vc->vc_decim)
				FLUSH
			if (vc->vc_need_wrap) {
				cr(vc);
				lf(vc);
			}
			if (vc->vc_decim)
				insert_char(vc, 1);
			scr_writew(himask ?
				     ((vc->vc_attr << 8) & ~himask) + ((tc & 0x100) ? himask : 0) + (tc & 0xff) :
				     (vc->vc_attr << 8) + tc,
				   (u16 *) vc->vc_pos);
			if (DO_UPDATE(vc) && draw_x < 0) {
				draw_x = vc->vc_x;
				draw_from = vc->vc_pos;
			}
			if (vc->vc_x == vc->vc_cols - 1) {
				vc->vc_need_wrap = vc->vc_decawm;
				draw_to = vc->vc_pos + 2;
			} else {
				vc->vc_x++;
				draw_to = (vc->vc_pos += 2);
			}
			continue;
		}
		FLUSH
		do_con_trol(tty, vc, orig);
	}
	FLUSH
	console_conditional_schedule();
	release_console_sem();

out:
	return n;
#undef FLUSH
}

/*
 * This is the console switching callback.
 *
 * Doing console switching in a process context allows
 * us to do the switches asynchronously (needed when we want
 * to switch due to a keyboard interrupt).  Synchronization
 * with other console code and prevention of re-entrancy is
 * ensured with console_sem.
 */
static void console_callback(void *ignored)
{
	acquire_console_sem();

	if (want_console >= 0) {
		if (want_console != fg_console &&
		    vc_cons_allocated(want_console)) {
			hide_cursor(vc_cons[fg_console].d);
			change_console(vc_cons[want_console].d);
			/* we only changed when the console had already
			   been allocated - a new console is not created
			   in an interrupt routine */
		}
		want_console = -1;
	}
	if (do_poke_blanked_console) { /* do not unblank for a LED change */
		do_poke_blanked_console = 0;
		poke_blanked_console();
	}
	if (scrollback_delta) {
		struct vc_data *vc = vc_cons[fg_console].d;
		clear_selection();
		if (vc->vc_mode == KD_TEXT)
			vc->vc_sw->con_scrolldelta(vc, scrollback_delta);
		scrollback_delta = 0;
	}
	if (blank_timer_expired) {
		do_blank_screen(0);
		blank_timer_expired = 0;
	}

	release_console_sem();
}

void set_console(int nr)
{
	want_console = nr;
	schedule_console_callback();
}

struct tty_driver *console_driver;

#ifdef CONFIG_VT_CONSOLE

/*
 *	Console on virtual terminal
 *
 * The console must be locked when we get here.
 */

static void vt_console_print(struct console *co, const char *b, unsigned count)
{
	struct vc_data *vc = vc_cons[fg_console].d;
	unsigned char c;
	static unsigned long printing;
	const ushort *start;
	ushort cnt = 0;
	ushort myx;

	/* console busy or not yet initialized */
	if (!printable || test_and_set_bit(0, &printing))
		return;

	if (kmsg_redirect && vc_cons_allocated(kmsg_redirect - 1))
		vc = vc_cons[kmsg_redirect - 1].d;

	/* read `x' only after setting currcons properly (otherwise
	   the `x' macro will read the x of the foreground console). */
	myx = vc->vc_x;

	if (!vc_cons_allocated(fg_console)) {
		/* impossible */
		/* printk("vt_console_print: tty %d not allocated ??\n", currcons+1); */
		goto quit;
	}

	if (vc->vc_mode != KD_TEXT)
		goto quit;

	/* undraw cursor first */
	if (IS_FG(vc))
		hide_cursor(vc);

	start = (ushort *)vc->vc_pos;

	/* Contrived structure to try to emulate original need_wrap behaviour
	 * Problems caused when we have need_wrap set on '\n' character */
	while (count--) {
		c = *b++;
		if (c == 10 || c == 13 || c == 8 || vc->vc_need_wrap) {
			if (cnt > 0) {
				if (CON_IS_VISIBLE(vc))
					vc->vc_sw->con_putcs(vc, start, cnt, vc->vc_y, vc->vc_x);
				vc->vc_x += cnt;
				if (vc->vc_need_wrap)
					vc->vc_x--;
				cnt = 0;
			}
			if (c == 8) {		/* backspace */
				bs(vc);
				start = (ushort *)vc->vc_pos;
				myx = vc->vc_x;
				continue;
			}
			if (c != 13)
				lf(vc);
			cr(vc);
			start = (ushort *)vc->vc_pos;
			myx = vc->vc_x;
			if (c == 10 || c == 13)
				continue;
		}
		scr_writew((vc->vc_attr << 8) + c, (unsigned short *)vc->vc_pos);
		cnt++;
		if (myx == vc->vc_cols - 1) {
			vc->vc_need_wrap = 1;
			continue;
		}
		vc->vc_pos += 2;
		myx++;
	}
	if (cnt > 0) {
		if (CON_IS_VISIBLE(vc))
			vc->vc_sw->con_putcs(vc, start, cnt, vc->vc_y, vc->vc_x);
		vc->vc_x += cnt;
		if (vc->vc_x == vc->vc_cols) {
			vc->vc_x--;
			vc->vc_need_wrap = 1;
		}
	}
	set_cursor(vc);

quit:
	clear_bit(0, &printing);
}

static struct tty_driver *vt_console_device(struct console *c, int *index)
{
	*index = c->index ? c->index-1 : fg_console;
	return console_driver;
}

static struct console vt_console_driver = {
	.name		= "tty",
	.write		= vt_console_print,
	.device		= vt_console_device,
	.unblank	= unblank_screen,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};
#endif

/*
 *	Handling of Linux-specific VC ioctls
 */

/*
 * Generally a bit racy with respect to console_sem().
 *
 * There are some functions which don't need it.
 *
 * There are some functions which can sleep for arbitrary periods
 * (paste_selection) but we don't need the lock there anyway.
 *
 * set_selection has locking, and definitely needs it
 */

int tioclinux(struct tty_struct *tty, unsigned long arg)
{
	char type, data;
	char __user *p = (char __user *)arg;
	int lines;
	int ret;

	if (tty->driver->type != TTY_DRIVER_TYPE_CONSOLE)
		return -EINVAL;
	if (current->signal->tty != tty && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (get_user(type, p))
		return -EFAULT;
	ret = 0;
	switch (type)
	{
		case TIOCL_SETSEL:
			acquire_console_sem();
			ret = set_selection((struct tiocl_selection __user *)(p+1), tty);
			release_console_sem();
			break;
		case TIOCL_PASTESEL:
			ret = paste_selection(tty);
			break;
		case TIOCL_UNBLANKSCREEN:
			acquire_console_sem();
			unblank_screen();
			release_console_sem();
			break;
		case TIOCL_SELLOADLUT:
			ret = sel_loadlut(p);
			break;
		case TIOCL_GETSHIFTSTATE:
			
	/*
	 * Make it possible to react to Shift+Mousebutton.
	 * Note that 'shift_state' is an undocumented
	 * kernel-internal variable; programs not closely
	 * related to the kernel should not use this.
	 */
	 		data = shift_state;
			ret = __put_user(data, p);
			break;
		case TIOCL_GETMOUSEREPORTING:
			data = mouse_reporting();
			ret = __put_user(data, p);
			break;
		case TIOCL_SETVESABLANK:
			set_vesa_blanking(p);
			break;
		case TIOCL_SETKMSGREDIRECT:
			if (!capable(CAP_SYS_ADMIN)) {
				ret = -EPERM;
			} else {
				if (get_user(data, p+1))
					ret = -EFAULT;
				else
					kmsg_redirect = data;
			}
			break;
		case TIOCL_GETFGCONSOLE:
			ret = fg_console;
			break;
		case TIOCL_SCROLLCONSOLE:
			if (get_user(lines, (s32 __user *)(p+4))) {
				ret = -EFAULT;
			} else {
				scrollfront(vc_cons[fg_console].d, lines);
				ret = 0;
			}
			break;
		case TIOCL_BLANKSCREEN:	/* until explicitly unblanked, not only poked */
			acquire_console_sem();
			ignore_poke = 1;
			do_blank_screen(0);
			release_console_sem();
			break;
		case TIOCL_BLANKEDSCREEN:
			ret = console_blanked;
			break;
		default:
			ret = -EINVAL;
			break;
	}
	return ret;
}

/*
 * /dev/ttyN handling
 */

static int con_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	int	retval;

	retval = do_con_write(tty, buf, count);
	con_flush_chars(tty);

	return retval;
}

static void con_put_char(struct tty_struct *tty, unsigned char ch)
{
	if (in_interrupt())
		return;	/* n_r3964 calls put_char() from interrupt context */
	do_con_write(tty, &ch, 1);
}

static int con_write_room(struct tty_struct *tty)
{
	if (tty->stopped)
		return 0;
	return 4096;		/* No limit, really; we're not buffering */
}

static int con_chars_in_buffer(struct tty_struct *tty)
{
	return 0;		/* we're not buffering */
}

/*
 * con_throttle and con_unthrottle are only used for
 * paste_selection(), which has to stuff in a large number of
 * characters...
 */
static void con_throttle(struct tty_struct *tty)
{
}

static void con_unthrottle(struct tty_struct *tty)
{
	struct vc_data *vc = tty->driver_data;

	wake_up_interruptible(&vc->paste_wait);
}

/*
 * Turn the Scroll-Lock LED on when the tty is stopped
 */
static void con_stop(struct tty_struct *tty)
{
	int console_num;
	if (!tty)
		return;
	console_num = tty->index;
	if (!vc_cons_allocated(console_num))
		return;
	set_vc_kbd_led(kbd_table + console_num, VC_SCROLLOCK);
	set_leds();
}

/*
 * Turn the Scroll-Lock LED off when the console is started
 */
static void con_start(struct tty_struct *tty)
{
	int console_num;
	if (!tty)
		return;
	console_num = tty->index;
	if (!vc_cons_allocated(console_num))
		return;
	clr_vc_kbd_led(kbd_table + console_num, VC_SCROLLOCK);
	set_leds();
}

static void con_flush_chars(struct tty_struct *tty)
{
	struct vc_data *vc;

	if (in_interrupt())	/* from flush_to_ldisc */
		return;

	/* if we race with con_close(), vt may be null */
	acquire_console_sem();
	vc = tty->driver_data;
	if (vc)
		set_cursor(vc);
	release_console_sem();
}

/*
 * Allocate the console screen memory.
 */
static int con_open(struct tty_struct *tty, struct file *filp)
{
	unsigned int currcons = tty->index;
	int ret = 0;

	acquire_console_sem();
	if (tty->driver_data == NULL) {
		ret = vc_allocate(currcons);
		if (ret == 0) {
			struct vc_data *vc = vc_cons[currcons].d;
			tty->driver_data = vc;
			vc->vc_tty = tty;

			if (!tty->winsize.ws_row && !tty->winsize.ws_col) {
				tty->winsize.ws_row = vc_cons[currcons].d->vc_rows;
				tty->winsize.ws_col = vc_cons[currcons].d->vc_cols;
			}
			release_console_sem();
			vcs_make_devfs(tty);
			return ret;
		}
	}
	release_console_sem();
	return ret;
}

/*
 * We take tty_sem in here to prevent another thread from coming in via init_dev
 * and taking a ref against the tty while we're in the process of forgetting
 * about it and cleaning things up.
 *
 * This is because vcs_remove_devfs() can sleep and will drop the BKL.
 */
static void con_close(struct tty_struct *tty, struct file *filp)
{
	down(&tty_sem);
	acquire_console_sem();
	if (tty && tty->count == 1) {
		struct vc_data *vc = tty->driver_data;

		if (vc)
			vc->vc_tty = NULL;
		tty->driver_data = NULL;
		release_console_sem();
		vcs_remove_devfs(tty);
		up(&tty_sem);
		/*
		 * tty_sem is released, but we still hold BKL, so there is
		 * still exclusion against init_dev()
		 */
		return;
	}
	release_console_sem();
	up(&tty_sem);
}

static void vc_init(struct vc_data *vc, unsigned int rows,
		    unsigned int cols, int do_clear)
{
	int j, k ;

	vc->vc_cols = cols;
	vc->vc_rows = rows;
	vc->vc_size_row = cols << 1;
	vc->vc_screenbuf_size = vc->vc_rows * vc->vc_size_row;

	set_origin(vc);
	vc->vc_pos = vc->vc_origin;
	reset_vc(vc);
	for (j=k=0; j<16; j++) {
		vc->vc_palette[k++] = default_red[j] ;
		vc->vc_palette[k++] = default_grn[j] ;
		vc->vc_palette[k++] = default_blu[j] ;
	}
	vc->vc_def_color       = 0x07;   /* white */
	vc->vc_ulcolor		= 0x0f;   /* bold white */
	vc->vc_halfcolor       = 0x08;   /* grey */
	init_waitqueue_head(&vc->paste_wait);
	reset_terminal(vc, do_clear);
}

/*
 * This routine initializes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequence.
 */

static int __init con_init(void)
{
	const char *display_desc = NULL;
	struct vc_data *vc;
	unsigned int currcons = 0;

	acquire_console_sem();

	if (conswitchp)
		display_desc = conswitchp->con_startup();
	if (!display_desc) {
		fg_console = 0;
		release_console_sem();
		return 0;
	}

	init_timer(&console_timer);
	console_timer.function = blank_screen_t;
	if (blankinterval) {
		blank_state = blank_normal_wait;
		mod_timer(&console_timer, jiffies + blankinterval);
	}

	/*
	 * kmalloc is not running yet - we use the bootmem allocator.
	 */
	for (currcons = 0; currcons < MIN_NR_CONSOLES; currcons++) {
		vc_cons[currcons].d = vc = alloc_bootmem(sizeof(struct vc_data));
		visual_init(vc, currcons, 1);
		vc->vc_screenbuf = (unsigned short *)alloc_bootmem(vc->vc_screenbuf_size);
		vc->vc_kmalloced = 0;
		vc_init(vc, vc->vc_rows, vc->vc_cols,
			currcons || !vc->vc_sw->con_save_screen);
	}
	currcons = fg_console = 0;
	master_display_fg = vc = vc_cons[currcons].d;
	set_origin(vc);
	save_screen(vc);
	gotoxy(vc, vc->vc_x, vc->vc_y);
	csi_J(vc, 0);
	update_screen(vc);
	printk("Console: %s %s %dx%d",
		vc->vc_can_do_color ? "colour" : "mono",
		display_desc, vc->vc_cols, vc->vc_rows);
	printable = 1;
	printk("\n");

	release_console_sem();

#ifdef CONFIG_VT_CONSOLE
	register_console(&vt_console_driver);
#endif
	return 0;
}
console_initcall(con_init);

static struct tty_operations con_ops = {
	.open = con_open,
	.close = con_close,
	.write = con_write,
	.write_room = con_write_room,
	.put_char = con_put_char,
	.flush_chars = con_flush_chars,
	.chars_in_buffer = con_chars_in_buffer,
	.ioctl = vt_ioctl,
	.stop = con_stop,
	.start = con_start,
	.throttle = con_throttle,
	.unthrottle = con_unthrottle,
};

int __init vty_init(void)
{
	vcs_init();

	console_driver = alloc_tty_driver(MAX_NR_CONSOLES);
	if (!console_driver)
		panic("Couldn't allocate console driver\n");
	console_driver->owner = THIS_MODULE;
	console_driver->devfs_name = "vc/";
	console_driver->name = "tty";
	console_driver->name_base = 1;
	console_driver->major = TTY_MAJOR;
	console_driver->minor_start = 1;
	console_driver->type = TTY_DRIVER_TYPE_CONSOLE;
	console_driver->init_termios = tty_std_termios;
	console_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_RESET_TERMIOS;
	tty_set_operations(console_driver, &con_ops);
	if (tty_register_driver(console_driver))
		panic("Couldn't register console driver\n");

	kbd_init();
	console_map_init();
#ifdef CONFIG_PROM_CONSOLE
	prom_con_init();
#endif
#ifdef CONFIG_MDA_CONSOLE
	mda_console_init();
#endif
	return 0;
}

#ifndef VT_SINGLE_DRIVER

/*
 *	If we support more console drivers, this function is used
 *	when a driver wants to take over some existing consoles
 *	and become default driver for newly opened ones.
 */

int take_over_console(const struct consw *csw, int first, int last, int deflt)
{
	int i, j = -1;
	const char *desc;
	struct module *owner;

	owner = csw->owner;
	if (!try_module_get(owner))
		return -ENODEV;

	acquire_console_sem();

	desc = csw->con_startup();
	if (!desc) {
		release_console_sem();
		module_put(owner);
		return -ENODEV;
	}
	if (deflt) {
		if (conswitchp)
			module_put(conswitchp->owner);
		__module_get(owner);
		conswitchp = csw;
	}

	for (i = first; i <= last; i++) {
		int old_was_color;
		struct vc_data *vc = vc_cons[i].d;

		if (con_driver_map[i])
			module_put(con_driver_map[i]->owner);
		__module_get(owner);
		con_driver_map[i] = csw;

		if (!vc || !vc->vc_sw)
			continue;

		j = i;
		if (CON_IS_VISIBLE(vc))
			save_screen(vc);
		old_was_color = vc->vc_can_do_color;
		vc->vc_sw->con_deinit(vc);
		vc->vc_origin = (unsigned long)vc->vc_screenbuf;
		vc->vc_visible_origin = vc->vc_origin;
		vc->vc_scr_end = vc->vc_origin + vc->vc_screenbuf_size;
		vc->vc_pos = vc->vc_origin + vc->vc_size_row * vc->vc_y + 2 * vc->vc_x;
		visual_init(vc, i, 0);
		update_attr(vc);

		/* If the console changed between mono <-> color, then
		 * the attributes in the screenbuf will be wrong.  The
		 * following resets all attributes to something sane.
		 */
		if (old_was_color != vc->vc_can_do_color)
			clear_buffer_attributes(vc);

		if (CON_IS_VISIBLE(vc))
			update_screen(vc);
	}
	printk("Console: switching ");
	if (!deflt)
		printk("consoles %d-%d ", first+1, last+1);
	if (j >= 0)
		printk("to %s %s %dx%d\n",
		       vc_cons[j].d->vc_can_do_color ? "colour" : "mono",
		       desc, vc_cons[j].d->vc_cols, vc_cons[j].d->vc_rows);
	else
		printk("to %s\n", desc);

	release_console_sem();

	module_put(owner);
	return 0;
}

void give_up_console(const struct consw *csw)
{
	int i;

	for(i = 0; i < MAX_NR_CONSOLES; i++)
		if (con_driver_map[i] == csw) {
			module_put(csw->owner);
			con_driver_map[i] = NULL;
		}
}

#endif

/*
 *	Screen blanking
 */

static void set_vesa_blanking(char __user *p)
{
    unsigned int mode;
    get_user(mode, p + 1);
    vesa_blank_mode = (mode < 4) ? mode : 0;
}

void do_blank_screen(int entering_gfx)
{
	struct vc_data *vc = vc_cons[fg_console].d;
	int i;

	WARN_CONSOLE_UNLOCKED();

	if (console_blanked) {
		if (blank_state == blank_vesa_wait) {
			blank_state = blank_off;
			vc->vc_sw->con_blank(vc, vesa_blank_mode + 1, 0);
		}
		return;
	}
	if (blank_state != blank_normal_wait)
		return;
	blank_state = blank_off;

	/* entering graphics mode? */
	if (entering_gfx) {
		hide_cursor(vc);
		save_screen(vc);
		vc->vc_sw->con_blank(vc, -1, 1);
		console_blanked = fg_console + 1;
		set_origin(vc);
		return;
	}

	/* don't blank graphics */
	if (vc->vc_mode != KD_TEXT) {
		console_blanked = fg_console + 1;
		return;
	}

	hide_cursor(vc);
	del_timer_sync(&console_timer);
	blank_timer_expired = 0;

	save_screen(vc);
	/* In case we need to reset origin, blanking hook returns 1 */
	i = vc->vc_sw->con_blank(vc, vesa_off_interval ? 1 : (vesa_blank_mode + 1), 0);
	console_blanked = fg_console + 1;
	if (i)
		set_origin(vc);

	if (console_blank_hook && console_blank_hook(1))
		return;

	if (vesa_off_interval && vesa_blank_mode) {
		blank_state = blank_vesa_wait;
		mod_timer(&console_timer, jiffies + vesa_off_interval);
	}
}
EXPORT_SYMBOL(do_blank_screen);

/*
 * Called by timer as well as from vt_console_driver
 */
void do_unblank_screen(int leaving_gfx)
{
	struct vc_data *vc;

	/* This should now always be called from a "sane" (read: can schedule)
	 * context for the sake of the low level drivers, except in the special
	 * case of oops_in_progress
	 */
	if (!oops_in_progress)
		might_sleep();

	WARN_CONSOLE_UNLOCKED();

	ignore_poke = 0;
	if (!console_blanked)
		return;
	if (!vc_cons_allocated(fg_console)) {
		/* impossible */
		printk("unblank_screen: tty %d not allocated ??\n", fg_console+1);
		return;
	}
	vc = vc_cons[fg_console].d;
	if (vc->vc_mode != KD_TEXT)
		return; /* but leave console_blanked != 0 */

	if (blankinterval) {
		mod_timer(&console_timer, jiffies + blankinterval);
		blank_state = blank_normal_wait;
	}

	console_blanked = 0;
	if (vc->vc_sw->con_blank(vc, 0, leaving_gfx))
		/* Low-level driver cannot restore -> do it ourselves */
		update_screen(vc);
	if (console_blank_hook)
		console_blank_hook(0);
	set_palette(vc);
	set_cursor(vc);
}
EXPORT_SYMBOL(do_unblank_screen);

/*
 * This is called by the outside world to cause a forced unblank, mostly for
 * oopses. Currently, I just call do_unblank_screen(0), but we could eventually
 * call it with 1 as an argument and so force a mode restore... that may kill
 * X or at least garbage the screen but would also make the Oops visible...
 */
void unblank_screen(void)
{
	do_unblank_screen(0);
}

/*
 * We defer the timer blanking to work queue so it can take the console semaphore
 * (console operations can still happen at irq time, but only from printk which
 * has the console semaphore. Not perfect yet, but better than no locking
 */
static void blank_screen_t(unsigned long dummy)
{
	if (unlikely(!keventd_up())) {
		mod_timer(&console_timer, jiffies + blankinterval);
		return;
	}
	blank_timer_expired = 1;
	schedule_work(&console_work);
}

void poke_blanked_console(void)
{
	WARN_CONSOLE_UNLOCKED();

	/* Add this so we quickly catch whoever might call us in a non
	 * safe context. Nowadays, unblank_screen() isn't to be called in
	 * atomic contexts and is allowed to schedule (with the special case
	 * of oops_in_progress, but that isn't of any concern for this
	 * function. --BenH.
	 */
	might_sleep();

	/* This isn't perfectly race free, but a race here would be mostly harmless,
	 * at worse, we'll do a spurrious blank and it's unlikely
	 */
	del_timer(&console_timer);
	blank_timer_expired = 0;

	if (ignore_poke || !vc_cons[fg_console].d || vc_cons[fg_console].d->vc_mode == KD_GRAPHICS)
		return;
	if (console_blanked)
		unblank_screen();
	else if (blankinterval) {
		mod_timer(&console_timer, jiffies + blankinterval);
		blank_state = blank_normal_wait;
	}
}

/*
 *	Palettes
 */

static void set_palette(struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

	if (vc->vc_mode != KD_GRAPHICS)
		vc->vc_sw->con_set_palette(vc, color_table);
}

static int set_get_cmap(unsigned char __user *arg, int set)
{
    int i, j, k;

    WARN_CONSOLE_UNLOCKED();

    for (i = 0; i < 16; i++)
	if (set) {
	    get_user(default_red[i], arg++);
	    get_user(default_grn[i], arg++);
	    get_user(default_blu[i], arg++);
	} else {
	    put_user(default_red[i], arg++);
	    put_user(default_grn[i], arg++);
	    put_user(default_blu[i], arg++);
	}
    if (set) {
	for (i = 0; i < MAX_NR_CONSOLES; i++)
	    if (vc_cons_allocated(i)) {
		for (j = k = 0; j < 16; j++) {
		    vc_cons[i].d->vc_palette[k++] = default_red[j];
		    vc_cons[i].d->vc_palette[k++] = default_grn[j];
		    vc_cons[i].d->vc_palette[k++] = default_blu[j];
		}
		set_palette(vc_cons[i].d);
	    }
    }
    return 0;
}

/*
 * Load palette into the DAC registers. arg points to a colour
 * map, 3 bytes per colour, 16 colours, range from 0 to 255.
 */

int con_set_cmap(unsigned char __user *arg)
{
	int rc;

	acquire_console_sem();
	rc = set_get_cmap (arg,1);
	release_console_sem();

	return rc;
}

int con_get_cmap(unsigned char __user *arg)
{
	int rc;

	acquire_console_sem();
	rc = set_get_cmap (arg,0);
	release_console_sem();

	return rc;
}

void reset_palette(struct vc_data *vc)
{
	int j, k;
	for (j=k=0; j<16; j++) {
		vc->vc_palette[k++] = default_red[j];
		vc->vc_palette[k++] = default_grn[j];
		vc->vc_palette[k++] = default_blu[j];
	}
	set_palette(vc);
}

/*
 *  Font switching
 *
 *  Currently we only support fonts up to 32 pixels wide, at a maximum height
 *  of 32 pixels. Userspace fontdata is stored with 32 bytes (shorts/ints, 
 *  depending on width) reserved for each character which is kinda wasty, but 
 *  this is done in order to maintain compatibility with the EGA/VGA fonts. It 
 *  is upto the actual low-level console-driver convert data into its favorite
 *  format (maybe we should add a `fontoffset' field to the `display'
 *  structure so we won't have to convert the fontdata all the time.
 *  /Jes
 */

#define max_font_size 65536

static int con_font_get(struct vc_data *vc, struct console_font_op *op)
{
	struct console_font font;
	int rc = -EINVAL;
	int c;

	if (vc->vc_mode != KD_TEXT)
		return -EINVAL;

	if (op->data) {
		font.data = kmalloc(max_font_size, GFP_KERNEL);
		if (!font.data)
			return -ENOMEM;
	} else
		font.data = NULL;

	acquire_console_sem();
	if (vc->vc_sw->con_font_get)
		rc = vc->vc_sw->con_font_get(vc, &font);
	else
		rc = -ENOSYS;
	release_console_sem();

	if (rc)
		goto out;

	c = (font.width+7)/8 * 32 * font.charcount;
	
	if (op->data && font.charcount > op->charcount)
		rc = -ENOSPC;
	if (!(op->flags & KD_FONT_FLAG_OLD)) {
		if (font.width > op->width || font.height > op->height) 
			rc = -ENOSPC;
	} else {
		if (font.width != 8)
			rc = -EIO;
		else if ((op->height && font.height > op->height) ||
			 font.height > 32)
			rc = -ENOSPC;
	}
	if (rc)
		goto out;

	op->height = font.height;
	op->width = font.width;
	op->charcount = font.charcount;

	if (op->data && copy_to_user(op->data, font.data, c))
		rc = -EFAULT;

out:
	kfree(font.data);
	return rc;
}

static int con_font_set(struct vc_data *vc, struct console_font_op *op)
{
	struct console_font font;
	int rc = -EINVAL;
	int size;

	if (vc->vc_mode != KD_TEXT)
		return -EINVAL;
	if (!op->data)
		return -EINVAL;
	if (op->charcount > 512)
		return -EINVAL;
	if (!op->height) {		/* Need to guess font height [compat] */
		int h, i;
		u8 __user *charmap = op->data;
		u8 tmp;
		
		/* If from KDFONTOP ioctl, don't allow things which can be done in userland,
		   so that we can get rid of this soon */
		if (!(op->flags & KD_FONT_FLAG_OLD))
			return -EINVAL;
		for (h = 32; h > 0; h--)
			for (i = 0; i < op->charcount; i++) {
				if (get_user(tmp, &charmap[32*i+h-1]))
					return -EFAULT;
				if (tmp)
					goto nonzero;
			}
		return -EINVAL;
	nonzero:
		op->height = h;
	}
	if (op->width <= 0 || op->width > 32 || op->height > 32)
		return -EINVAL;
	size = (op->width+7)/8 * 32 * op->charcount;
	if (size > max_font_size)
		return -ENOSPC;
	font.charcount = op->charcount;
	font.height = op->height;
	font.width = op->width;
	font.data = kmalloc(size, GFP_KERNEL);
	if (!font.data)
		return -ENOMEM;
	if (copy_from_user(font.data, op->data, size)) {
		kfree(font.data);
		return -EFAULT;
	}
	acquire_console_sem();
	if (vc->vc_sw->con_font_set)
		rc = vc->vc_sw->con_font_set(vc, &font, op->flags);
	else
		rc = -ENOSYS;
	release_console_sem();
	kfree(font.data);
	return rc;
}

static int con_font_default(struct vc_data *vc, struct console_font_op *op)
{
	struct console_font font = {.width = op->width, .height = op->height};
	char name[MAX_FONT_NAME];
	char *s = name;
	int rc;

	if (vc->vc_mode != KD_TEXT)
		return -EINVAL;

	if (!op->data)
		s = NULL;
	else if (strncpy_from_user(name, op->data, MAX_FONT_NAME - 1) < 0)
		return -EFAULT;
	else
		name[MAX_FONT_NAME - 1] = 0;

	acquire_console_sem();
	if (vc->vc_sw->con_font_default)
		rc = vc->vc_sw->con_font_default(vc, &font, s);
	else
		rc = -ENOSYS;
	release_console_sem();
	if (!rc) {
		op->width = font.width;
		op->height = font.height;
	}
	return rc;
}

static int con_font_copy(struct vc_data *vc, struct console_font_op *op)
{
	int con = op->height;
	int rc;

	if (vc->vc_mode != KD_TEXT)
		return -EINVAL;

	acquire_console_sem();
	if (!vc->vc_sw->con_font_copy)
		rc = -ENOSYS;
	else if (con < 0 || !vc_cons_allocated(con))
		rc = -ENOTTY;
	else if (con == vc->vc_num)	/* nothing to do */
		rc = 0;
	else
		rc = vc->vc_sw->con_font_copy(vc, con);
	release_console_sem();
	return rc;
}

int con_font_op(struct vc_data *vc, struct console_font_op *op)
{
	switch (op->op) {
	case KD_FONT_OP_SET:
		return con_font_set(vc, op);
	case KD_FONT_OP_GET:
		return con_font_get(vc, op);
	case KD_FONT_OP_SET_DEFAULT:
		return con_font_default(vc, op);
	case KD_FONT_OP_COPY:
		return con_font_copy(vc, op);
	}
	return -ENOSYS;
}

/*
 *	Interface exported to selection and vcs.
 */

/* used by selection */
u16 screen_glyph(struct vc_data *vc, int offset)
{
	u16 w = scr_readw(screenpos(vc, offset, 1));
	u16 c = w & 0xff;

	if (w & vc->vc_hi_font_mask)
		c |= 0x100;
	return c;
}

/* used by vcs - note the word offset */
unsigned short *screen_pos(struct vc_data *vc, int w_offset, int viewed)
{
	return screenpos(vc, 2 * w_offset, viewed);
}

void getconsxy(struct vc_data *vc, unsigned char *p)
{
	p[0] = vc->vc_x;
	p[1] = vc->vc_y;
}

void putconsxy(struct vc_data *vc, unsigned char *p)
{
	gotoxy(vc, p[0], p[1]);
	set_cursor(vc);
}

u16 vcs_scr_readw(struct vc_data *vc, const u16 *org)
{
	if ((unsigned long)org == vc->vc_pos && softcursor_original != -1)
		return softcursor_original;
	return scr_readw(org);
}

void vcs_scr_writew(struct vc_data *vc, u16 val, u16 *org)
{
	scr_writew(val, org);
	if ((unsigned long)org == vc->vc_pos) {
		softcursor_original = -1;
		add_softcursor(vc);
	}
}

/*
 *	Visible symbols for modules
 */

EXPORT_SYMBOL(color_table);
EXPORT_SYMBOL(default_red);
EXPORT_SYMBOL(default_grn);
EXPORT_SYMBOL(default_blu);
EXPORT_SYMBOL(update_region);
EXPORT_SYMBOL(redraw_screen);
EXPORT_SYMBOL(vc_resize);
EXPORT_SYMBOL(fg_console);
EXPORT_SYMBOL(console_blank_hook);
EXPORT_SYMBOL(console_blanked);
EXPORT_SYMBOL(vc_cons);
#ifndef VT_SINGLE_DRIVER
EXPORT_SYMBOL(take_over_console);
EXPORT_SYMBOL(give_up_console);
#endif
