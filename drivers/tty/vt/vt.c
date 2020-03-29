// SPDX-License-Identifier: GPL-2.0
/*
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
 * deletion SMP-safe.  17Jun00, Andrew Morton
 *
 * Removed console_lock, enabled interrupts across all console operations
 * 13 March 2001, Andrew Morton
 *
 * Fixed UTF-8 mode so alternate charset modes always work according
 * to control sequences interpreted in do_con_trol function
 * preserving backward VT100 semigraphics compatibility,
 * malformed UTF sequences represented as sequences of replacement glyphs,
 * original codes or '?' as a last resort if replacement glyph is undefined
 * by Adam Tla/lka <atlka@pg.gda.pl>, Aug 2006
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched/signal.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kd.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/tiocl.h>
#include <linux/kbd_kern.h>
#include <linux/consolemap.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/pm.h>
#include <linux/font.h>
#include <linux/bitops.h>
#include <linux/notifier.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/kdb.h>
#include <linux/ctype.h>
#include <linux/bsearch.h>
#include <linux/gcd.h>

#define MAX_NR_CON_DRIVER 16

#define CON_DRIVER_FLAG_MODULE 1
#define CON_DRIVER_FLAG_INIT   2
#define CON_DRIVER_FLAG_ATTR   4
#define CON_DRIVER_FLAG_ZOMBIE 8

struct con_driver {
	const struct consw *con;
	const char *desc;
	struct device *dev;
	int node;
	int first;
	int last;
	int flag;
};

static struct con_driver registered_con_driver[MAX_NR_CON_DRIVER];
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
#define DEFAULT_CURSOR_BLINK_MS	200

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
static int set_vesa_blanking(char __user *p);
static void set_cursor(struct vc_data *vc);
static void hide_cursor(struct vc_data *vc);
static void console_callback(struct work_struct *ignored);
static void con_driver_unregister_callback(struct work_struct *ignored);
static void blank_screen_t(struct timer_list *unused);
static void set_palette(struct vc_data *vc);

#define vt_get_kmsg_redirect() vt_kmsg_redirect(-1)

static int printable;		/* Is console ready for printing? */
int default_utf8 = true;
module_param(default_utf8, int, S_IRUGO | S_IWUSR);
int global_cursor_default = -1;
module_param(global_cursor_default, int, S_IRUGO | S_IWUSR);

static int cur_default = CUR_DEFAULT;
module_param(cur_default, int, S_IRUGO | S_IWUSR);

/*
 * ignore_poke: don't unblank the screen when things are typed.  This is
 * mainly for the privacy of braille terminal users.
 */
static int ignore_poke;

int do_poke_blanked_console;
int console_blanked;

static int vesa_blank_mode; /* 0:none 1:suspendV 2:suspendH 3:powerdown */
static int vesa_off_interval;
static int blankinterval;
core_param(consoleblank, blankinterval, int, 0444);

static DECLARE_WORK(console_work, console_callback);
static DECLARE_WORK(con_driver_unregister_work, con_driver_unregister_callback);

/*
 * fg_console is the current virtual console,
 * last_console is the last used one,
 * want_console is the console we want to switch to,
 * saved_* variants are for save/restore around kernel debugger enter/leave
 */
int fg_console;
int last_console;
int want_console = -1;
static int saved_fg_console;
static int saved_last_console;
static int saved_want_console;
static int saved_vc_mode;
static int saved_console_blanked;

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

static DEFINE_TIMER(console_timer, blank_screen_t);
static int blank_state;
static int blank_timer_expired;
enum {
	blank_off = 0,
	blank_normal_wait,
	blank_vesa_wait,
};

/*
 * /sys/class/tty/tty0/
 *
 * the attribute 'active' contains the name of the current vc
 * console and it supports poll() to detect vc switches
 */
static struct device *tty0dev;

/*
 * Notifier list for console events.
 */
static ATOMIC_NOTIFIER_HEAD(vt_notifier_list);

int register_vt_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&vt_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(register_vt_notifier);

int unregister_vt_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&vt_notifier_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_vt_notifier);

static void notify_write(struct vc_data *vc, unsigned int unicode)
{
	struct vt_notifier_param param = { .vc = vc, .c = unicode };
	atomic_notifier_call_chain(&vt_notifier_list, VT_WRITE, &param);
}

static void notify_update(struct vc_data *vc)
{
	struct vt_notifier_param param = { .vc = vc };
	atomic_notifier_call_chain(&vt_notifier_list, VT_UPDATE, &param);
}
/*
 *	Low-Level Functions
 */

static inline bool con_is_fg(const struct vc_data *vc)
{
	return vc->vc_num == fg_console;
}

static inline bool con_should_update(const struct vc_data *vc)
{
	return con_is_visible(vc) && !console_blanked;
}

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

/* Called  from the keyboard irq path.. */
static inline void scrolldelta(int lines)
{
	/* FIXME */
	/* scrolldelta needs some kind of consistency lock, but the BKL was
	   and still is not protecting versus the scheduled back end */
	scrollback_delta += lines;
	schedule_console_callback();
}

void schedule_console_callback(void)
{
	schedule_work(&console_work);
}

/*
 * Code to manage unicode-based screen buffers
 */

#ifdef NO_VC_UNI_SCREEN
/* this disables and optimizes related code away at compile time */
#define get_vc_uniscr(vc) NULL
#else
#define get_vc_uniscr(vc) vc->vc_uni_screen
#endif

#define VC_UNI_SCREEN_DEBUG 0

typedef uint32_t char32_t;

/*
 * Our screen buffer is preceded by an array of line pointers so that
 * scrolling only implies some pointer shuffling.
 */
struct uni_screen {
	char32_t *lines[0];
};

static struct uni_screen *vc_uniscr_alloc(unsigned int cols, unsigned int rows)
{
	struct uni_screen *uniscr;
	void *p;
	unsigned int memsize, i;

	/* allocate everything in one go */
	memsize = cols * rows * sizeof(char32_t);
	memsize += rows * sizeof(char32_t *);
	p = vmalloc(memsize);
	if (!p)
		return NULL;

	/* initial line pointers */
	uniscr = p;
	p = uniscr->lines + rows;
	for (i = 0; i < rows; i++) {
		uniscr->lines[i] = p;
		p += cols * sizeof(char32_t);
	}
	return uniscr;
}

static void vc_uniscr_set(struct vc_data *vc, struct uni_screen *new_uniscr)
{
	vfree(vc->vc_uni_screen);
	vc->vc_uni_screen = new_uniscr;
}

static void vc_uniscr_putc(struct vc_data *vc, char32_t uc)
{
	struct uni_screen *uniscr = get_vc_uniscr(vc);

	if (uniscr)
		uniscr->lines[vc->vc_y][vc->vc_x] = uc;
}

static void vc_uniscr_insert(struct vc_data *vc, unsigned int nr)
{
	struct uni_screen *uniscr = get_vc_uniscr(vc);

	if (uniscr) {
		char32_t *ln = uniscr->lines[vc->vc_y];
		unsigned int x = vc->vc_x, cols = vc->vc_cols;

		memmove(&ln[x + nr], &ln[x], (cols - x - nr) * sizeof(*ln));
		memset32(&ln[x], ' ', nr);
	}
}

static void vc_uniscr_delete(struct vc_data *vc, unsigned int nr)
{
	struct uni_screen *uniscr = get_vc_uniscr(vc);

	if (uniscr) {
		char32_t *ln = uniscr->lines[vc->vc_y];
		unsigned int x = vc->vc_x, cols = vc->vc_cols;

		memcpy(&ln[x], &ln[x + nr], (cols - x - nr) * sizeof(*ln));
		memset32(&ln[cols - nr], ' ', nr);
	}
}

static void vc_uniscr_clear_line(struct vc_data *vc, unsigned int x,
				 unsigned int nr)
{
	struct uni_screen *uniscr = get_vc_uniscr(vc);

	if (uniscr) {
		char32_t *ln = uniscr->lines[vc->vc_y];

		memset32(&ln[x], ' ', nr);
	}
}

static void vc_uniscr_clear_lines(struct vc_data *vc, unsigned int y,
				  unsigned int nr)
{
	struct uni_screen *uniscr = get_vc_uniscr(vc);

	if (uniscr) {
		unsigned int cols = vc->vc_cols;

		while (nr--)
			memset32(uniscr->lines[y++], ' ', cols);
	}
}

static void vc_uniscr_scroll(struct vc_data *vc, unsigned int t, unsigned int b,
			     enum con_scroll dir, unsigned int nr)
{
	struct uni_screen *uniscr = get_vc_uniscr(vc);

	if (uniscr) {
		unsigned int i, j, k, sz, d, clear;

		sz = b - t;
		clear = b - nr;
		d = nr;
		if (dir == SM_DOWN) {
			clear = t;
			d = sz - nr;
		}
		for (i = 0; i < gcd(d, sz); i++) {
			char32_t *tmp = uniscr->lines[t + i];
			j = i;
			while (1) {
				k = j + d;
				if (k >= sz)
					k -= sz;
				if (k == i)
					break;
				uniscr->lines[t + j] = uniscr->lines[t + k];
				j = k;
			}
			uniscr->lines[t + j] = tmp;
		}
		vc_uniscr_clear_lines(vc, clear, nr);
	}
}

static void vc_uniscr_copy_area(struct uni_screen *dst,
				unsigned int dst_cols,
				unsigned int dst_rows,
				struct uni_screen *src,
				unsigned int src_cols,
				unsigned int src_top_row,
				unsigned int src_bot_row)
{
	unsigned int dst_row = 0;

	if (!dst)
		return;

	while (src_top_row < src_bot_row) {
		char32_t *src_line = src->lines[src_top_row];
		char32_t *dst_line = dst->lines[dst_row];

		memcpy(dst_line, src_line, src_cols * sizeof(char32_t));
		if (dst_cols - src_cols)
			memset32(dst_line + src_cols, ' ', dst_cols - src_cols);
		src_top_row++;
		dst_row++;
	}
	while (dst_row < dst_rows) {
		char32_t *dst_line = dst->lines[dst_row];

		memset32(dst_line, ' ', dst_cols);
		dst_row++;
	}
}

/*
 * Called from vcs_read() to make sure unicode screen retrieval is possible.
 * This will initialize the unicode screen buffer if not already done.
 * This returns 0 if OK, or a negative error code otherwise.
 * In particular, -ENODATA is returned if the console is not in UTF-8 mode.
 */
int vc_uniscr_check(struct vc_data *vc)
{
	struct uni_screen *uniscr;
	unsigned short *p;
	int x, y, mask;

	if (__is_defined(NO_VC_UNI_SCREEN))
		return -EOPNOTSUPP;

	WARN_CONSOLE_UNLOCKED();

	if (!vc->vc_utf)
		return -ENODATA;

	if (vc->vc_uni_screen)
		return 0;

	uniscr = vc_uniscr_alloc(vc->vc_cols, vc->vc_rows);
	if (!uniscr)
		return -ENOMEM;

	/*
	 * Let's populate it initially with (imperfect) reverse translation.
	 * This is the next best thing we can do short of having it enabled
	 * from the start even when no users rely on this functionality. True
	 * unicode content will be available after a complete screen refresh.
	 */
	p = (unsigned short *)vc->vc_origin;
	mask = vc->vc_hi_font_mask | 0xff;
	for (y = 0; y < vc->vc_rows; y++) {
		char32_t *line = uniscr->lines[y];
		for (x = 0; x < vc->vc_cols; x++) {
			u16 glyph = scr_readw(p++) & mask;
			line[x] = inverse_translate(vc, glyph, true);
		}
	}

	vc->vc_uni_screen = uniscr;
	return 0;
}

/*
 * Called from vcs_read() to get the unicode data from the screen.
 * This must be preceded by a successful call to vc_uniscr_check() once
 * the console lock has been taken.
 */
void vc_uniscr_copy_line(struct vc_data *vc, void *dest, int viewed,
			 unsigned int row, unsigned int col, unsigned int nr)
{
	struct uni_screen *uniscr = get_vc_uniscr(vc);
	int offset = row * vc->vc_size_row + col * 2;
	unsigned long pos;

	BUG_ON(!uniscr);

	pos = (unsigned long)screenpos(vc, offset, viewed);
	if (pos >= vc->vc_origin && pos < vc->vc_scr_end) {
		/*
		 * Desired position falls in the main screen buffer.
		 * However the actual row/col might be different if
		 * scrollback is active.
		 */
		row = (pos - vc->vc_origin) / vc->vc_size_row;
		col = ((pos - vc->vc_origin) % vc->vc_size_row) / 2;
		memcpy(dest, &uniscr->lines[row][col], nr * sizeof(char32_t));
	} else {
		/*
		 * Scrollback is active. For now let's simply backtranslate
		 * the screen glyphs until the unicode screen buffer does
		 * synchronize with console display drivers for a scrollback
		 * buffer of its own.
		 */
		u16 *p = (u16 *)pos;
		int mask = vc->vc_hi_font_mask | 0xff;
		char32_t *uni_buf = dest;
		while (nr--) {
			u16 glyph = scr_readw(p++) & mask;
			*uni_buf++ = inverse_translate(vc, glyph, true);
		}
	}
}

/* this is for validation and debugging only */
static void vc_uniscr_debug_check(struct vc_data *vc)
{
	struct uni_screen *uniscr = get_vc_uniscr(vc);
	unsigned short *p;
	int x, y, mask;

	if (!VC_UNI_SCREEN_DEBUG || !uniscr)
		return;

	WARN_CONSOLE_UNLOCKED();

	/*
	 * Make sure our unicode screen translates into the same glyphs
	 * as the actual screen. This is brutal indeed.
	 */
	p = (unsigned short *)vc->vc_origin;
	mask = vc->vc_hi_font_mask | 0xff;
	for (y = 0; y < vc->vc_rows; y++) {
		char32_t *line = uniscr->lines[y];
		for (x = 0; x < vc->vc_cols; x++) {
			u16 glyph = scr_readw(p++) & mask;
			char32_t uc = line[x];
			int tc = conv_uni_to_pc(vc, uc);
			if (tc == -4)
				tc = conv_uni_to_pc(vc, 0xfffd);
			if (tc == -4)
				tc = conv_uni_to_pc(vc, '?');
			if (tc != glyph)
				pr_err_ratelimited(
					"%s: mismatch at %d,%d: glyph=%#x tc=%#x\n",
					__func__, x, y, glyph, tc);
		}
	}
}


static void con_scroll(struct vc_data *vc, unsigned int t, unsigned int b,
		enum con_scroll dir, unsigned int nr)
{
	u16 *clear, *d, *s;

	if (t + nr >= b)
		nr = b - t - 1;
	if (b > vc->vc_rows || t >= b || nr < 1)
		return;
	vc_uniscr_scroll(vc, t, b, dir, nr);
	if (con_is_visible(vc) && vc->vc_sw->con_scroll(vc, t, b, dir, nr))
		return;

	s = clear = (u16 *)(vc->vc_origin + vc->vc_size_row * t);
	d = (u16 *)(vc->vc_origin + vc->vc_size_row * (t + nr));

	if (dir == SM_UP) {
		clear = s + (b - t - nr) * vc->vc_cols;
		swap(s, d);
	}
	scr_memmovew(d, s, (b - t - nr) * vc->vc_size_row);
	scr_memsetw(clear, vc->vc_video_erase_char, vc->vc_size_row * nr);
}

static void do_update_region(struct vc_data *vc, unsigned long start, int count)
{
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
}

void update_region(struct vc_data *vc, unsigned long start, int count)
{
	WARN_CONSOLE_UNLOCKED();

	if (con_should_update(vc)) {
		hide_cursor(vc);
		do_update_region(vc, start, count);
		set_cursor(vc);
	}
}

/* Structure of attributes is hardware-dependent */

static u8 build_attr(struct vc_data *vc, u8 _color, u8 _intensity, u8 _blink,
    u8 _underline, u8 _reverse, u8 _italic)
{
	if (vc->vc_sw->con_build_attr)
		return vc->vc_sw->con_build_attr(vc, _color, _intensity,
		       _blink, _underline, _reverse, _italic);

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
	u8 a = _color;
	if (!vc->vc_can_do_color)
		return _intensity |
		       (_italic ? 2 : 0) |
		       (_underline ? 4 : 0) |
		       (_reverse ? 8 : 0) |
		       (_blink ? 0x80 : 0);
	if (_italic)
		a = (a & 0xF0) | vc->vc_itcolor;
	else if (_underline)
		a = (a & 0xf0) | vc->vc_ulcolor;
	else if (_intensity == 0)
		a = (a & 0xf0) | vc->vc_halfcolor;
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
}

static void update_attr(struct vc_data *vc)
{
	vc->vc_attr = build_attr(vc, vc->vc_color, vc->vc_intensity,
	              vc->vc_blink, vc->vc_underline,
	              vc->vc_reverse ^ vc->vc_decscnm, vc->vc_italic);
	vc->vc_video_erase_char = (build_attr(vc, vc->vc_color, 1, vc->vc_blink, 0, vc->vc_decscnm, 0) << 8) | ' ';
}

/* Note: inverting the screen twice should revert to the original state */
void invert_screen(struct vc_data *vc, int offset, int count, int viewed)
{
	unsigned short *p;

	WARN_CONSOLE_UNLOCKED();

	count /= 2;
	p = screenpos(vc, offset, viewed);
	if (vc->vc_sw->con_invert_region) {
		vc->vc_sw->con_invert_region(vc, p, count);
	} else {
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

	if (con_should_update(vc))
		do_update_region(vc, (unsigned long) p, count);
	notify_update(vc);
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
		if (con_should_update(vc))
			vc->vc_sw->con_putc(vc, old, oldy, oldx);
		notify_update(vc);
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
		if (con_should_update(vc)) {
			oldx = (offset >> 1) % vc->vc_cols;
			oldy = (offset >> 1) / vc->vc_cols;
			vc->vc_sw->con_putc(vc, new, oldy, oldx);
		}
		notify_update(vc);
	}
}

static void insert_char(struct vc_data *vc, unsigned int nr)
{
	unsigned short *p = (unsigned short *) vc->vc_pos;

	vc_uniscr_insert(vc, nr);
	scr_memmovew(p + nr, p, (vc->vc_cols - vc->vc_x - nr) * 2);
	scr_memsetw(p, vc->vc_video_erase_char, nr * 2);
	vc->vc_need_wrap = 0;
	if (con_should_update(vc))
		do_update_region(vc, (unsigned long) p,
			vc->vc_cols - vc->vc_x);
}

static void delete_char(struct vc_data *vc, unsigned int nr)
{
	unsigned short *p = (unsigned short *) vc->vc_pos;

	vc_uniscr_delete(vc, nr);
	scr_memcpyw(p, p + nr, (vc->vc_cols - vc->vc_x - nr) * 2);
	scr_memsetw(p + vc->vc_cols - vc->vc_x - nr, vc->vc_video_erase_char,
			nr * 2);
	vc->vc_need_wrap = 0;
	if (con_should_update(vc))
		do_update_region(vc, (unsigned long) p,
			vc->vc_cols - vc->vc_x);
}

static int softcursor_original = -1;

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
	if (con_should_update(vc))
		vc->vc_sw->con_putc(vc, i, vc->vc_y, vc->vc_x);
}

static void hide_softcursor(struct vc_data *vc)
{
	if (softcursor_original != -1) {
		scr_writew(softcursor_original, (u16 *)vc->vc_pos);
		if (con_should_update(vc))
			vc->vc_sw->con_putc(vc, softcursor_original,
					vc->vc_y, vc->vc_x);
		softcursor_original = -1;
	}
}

static void hide_cursor(struct vc_data *vc)
{
	if (vc_is_sel(vc))
		clear_selection();

	vc->vc_sw->con_cursor(vc, CM_ERASE);
	hide_softcursor(vc);
}

static void set_cursor(struct vc_data *vc)
{
	if (!con_is_fg(vc) || console_blanked || vc->vc_mode == KD_GRAPHICS)
		return;
	if (vc->vc_deccm) {
		if (vc_is_sel(vc))
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

	if (!con_is_visible(vc) ||
	    !vc->vc_sw->con_set_origin ||
	    !vc->vc_sw->con_set_origin(vc))
		vc->vc_origin = (unsigned long)vc->vc_screenbuf;
	vc->vc_visible_origin = vc->vc_origin;
	vc->vc_scr_end = vc->vc_origin + vc->vc_screenbuf_size;
	vc->vc_pos = vc->vc_origin + vc->vc_size_row * vc->vc_y + 2 * vc->vc_x;
}

static void save_screen(struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

	if (vc->vc_sw->con_save_screen)
		vc->vc_sw->con_save_screen(vc);
}

static void flush_scrollback(struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

	set_origin(vc);
	if (vc->vc_sw->con_flush_scrollback) {
		vc->vc_sw->con_flush_scrollback(vc);
	} else if (con_is_visible(vc)) {
		/*
		 * When no con_flush_scrollback method is provided then the
		 * legacy way for flushing the scrollback buffer is to use
		 * a side effect of the con_switch method. We do it only on
		 * the foreground console as background consoles have no
		 * scrollback buffers in that case and we obviously don't
		 * want to switch to them.
		 */
		hide_cursor(vc);
		vc->vc_sw->con_switch(vc);
		set_cursor(vc);
	}
}

/*
 *	Redrawing of screen
 */

void clear_buffer_attributes(struct vc_data *vc)
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
		if (!con_is_visible(vc))
			redraw = 1;
		*vc->vc_display_fg = vc;
		fg_console = vc->vc_num;
		hide_cursor(old_vc);
		if (!con_is_visible(old_vc)) {
			save_screen(old_vc);
			set_origin(old_vc);
		}
		if (tty0dev)
			sysfs_notify(&tty0dev->kobj, NULL, "active");
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

		/* Forcibly update if we're panicing */
		if ((update && vc->vc_mode != KD_GRAPHICS) ||
		    vt_force_oops_output(vc))
			do_update_region(vc, vc->vc_origin, vc->vc_screenbuf_size / 2);
	}
	set_cursor(vc);
	if (is_switch) {
		set_leds();
		compute_shiftstate();
		notify_update(vc);
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
	if (vc->vc_uni_pagedir_loc)
		con_free_unimap(vc);
	vc->vc_uni_pagedir_loc = &vc->vc_uni_pagedir;
	vc->vc_uni_pagedir = NULL;
	vc->vc_hi_font_mask = 0;
	vc->vc_complement_mask = 0;
	vc->vc_can_do_color = 0;
	vc->vc_panic_force_write = false;
	vc->vc_cur_blink_ms = DEFAULT_CURSOR_BLINK_MS;
	vc->vc_sw->con_init(vc, init);
	if (!vc->vc_complement_mask)
		vc->vc_complement_mask = vc->vc_can_do_color ? 0x7700 : 0x0800;
	vc->vc_s_complement_mask = vc->vc_complement_mask;
	vc->vc_size_row = vc->vc_cols << 1;
	vc->vc_screenbuf_size = vc->vc_rows * vc->vc_size_row;
}


static void visual_deinit(struct vc_data *vc)
{
	vc->vc_sw->con_deinit(vc);
	module_put(vc->vc_sw->owner);
}

static void vc_port_destruct(struct tty_port *port)
{
	struct vc_data *vc = container_of(port, struct vc_data, port);

	kfree(vc);
}

static const struct tty_port_operations vc_port_ops = {
	.destruct = vc_port_destruct,
};

int vc_allocate(unsigned int currcons)	/* return 0 on success */
{
	struct vt_notifier_param param;
	struct vc_data *vc;

	WARN_CONSOLE_UNLOCKED();

	if (currcons >= MAX_NR_CONSOLES)
		return -ENXIO;

	if (vc_cons[currcons].d)
		return 0;

	/* due to the granularity of kmalloc, we waste some memory here */
	/* the alloc is done in two steps, to optimize the common situation
	   of a 25x80 console (structsize=216, screenbuf_size=4000) */
	/* although the numbers above are not valid since long ago, the
	   point is still up-to-date and the comment still has its value
	   even if only as a historical artifact.  --mj, July 1998 */
	param.vc = vc = kzalloc(sizeof(struct vc_data), GFP_KERNEL);
	if (!vc)
		return -ENOMEM;

	vc_cons[currcons].d = vc;
	tty_port_init(&vc->port);
	vc->port.ops = &vc_port_ops;
	INIT_WORK(&vc_cons[currcons].SAK_work, vc_SAK);

	visual_init(vc, currcons, 1);

	if (!*vc->vc_uni_pagedir_loc)
		con_set_default_unimap(vc);

	vc->vc_screenbuf = kzalloc(vc->vc_screenbuf_size, GFP_KERNEL);
	if (!vc->vc_screenbuf)
		goto err_free;

	/* If no drivers have overridden us and the user didn't pass a
	   boot option, default to displaying the cursor */
	if (global_cursor_default == -1)
		global_cursor_default = 1;

	vc_init(vc, vc->vc_rows, vc->vc_cols, 1);
	vcs_make_sysfs(currcons);
	atomic_notifier_call_chain(&vt_notifier_list, VT_ALLOCATE, &param);

	return 0;
err_free:
	visual_deinit(vc);
	kfree(vc);
	vc_cons[currcons].d = NULL;
	return -ENOMEM;
}

static inline int resize_screen(struct vc_data *vc, int width, int height,
				int user)
{
	/* Resizes the resolution of the display adapater */
	int err = 0;

	if (vc->vc_mode != KD_GRAPHICS && vc->vc_sw->con_resize)
		err = vc->vc_sw->con_resize(vc, width, height, user);

	return err;
}

/*
 * Change # of rows and columns (0 means unchanged/the size of fg_console)
 * [this is to be used together with some user program
 * like resize that changes the hardware videomode]
 */
#define VC_RESIZE_MAXCOL (32767)
#define VC_RESIZE_MAXROW (32767)

/**
 *	vc_do_resize	-	resizing method for the tty
 *	@tty: tty being resized
 *	@real_tty: real tty (different to tty if a pty/tty pair)
 *	@vc: virtual console private data
 *	@cols: columns
 *	@lines: lines
 *
 *	Resize a virtual console, clipping according to the actual constraints.
 *	If the caller passes a tty structure then update the termios winsize
 *	information and perform any necessary signal handling.
 *
 *	Caller must hold the console semaphore. Takes the termios rwsem and
 *	ctrl_lock of the tty IFF a tty is passed.
 */

static int vc_do_resize(struct tty_struct *tty, struct vc_data *vc,
				unsigned int cols, unsigned int lines)
{
	unsigned long old_origin, new_origin, new_scr_end, rlth, rrem, err = 0;
	unsigned long end;
	unsigned int old_rows, old_row_size, first_copied_row;
	unsigned int new_cols, new_rows, new_row_size, new_screen_size;
	unsigned int user;
	unsigned short *newscreen;
	struct uni_screen *new_uniscr = NULL;

	WARN_CONSOLE_UNLOCKED();

	if (!vc)
		return -ENXIO;

	user = vc->vc_resize_user;
	vc->vc_resize_user = 0;

	if (cols > VC_RESIZE_MAXCOL || lines > VC_RESIZE_MAXROW)
		return -EINVAL;

	new_cols = (cols ? cols : vc->vc_cols);
	new_rows = (lines ? lines : vc->vc_rows);
	new_row_size = new_cols << 1;
	new_screen_size = new_row_size * new_rows;

	if (new_cols == vc->vc_cols && new_rows == vc->vc_rows)
		return 0;

	if (new_screen_size > KMALLOC_MAX_SIZE)
		return -EINVAL;
	newscreen = kzalloc(new_screen_size, GFP_USER);
	if (!newscreen)
		return -ENOMEM;

	if (get_vc_uniscr(vc)) {
		new_uniscr = vc_uniscr_alloc(new_cols, new_rows);
		if (!new_uniscr) {
			kfree(newscreen);
			return -ENOMEM;
		}
	}

	if (vc_is_sel(vc))
		clear_selection();

	old_rows = vc->vc_rows;
	old_row_size = vc->vc_size_row;

	err = resize_screen(vc, new_cols, new_rows, user);
	if (err) {
		kfree(newscreen);
		kfree(new_uniscr);
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
			first_copied_row = (old_rows - new_rows);
		} else {
			/*
			 * Cursor is in no man's land, copy 1/2 screenful
			 * from the top and bottom of cursor position
			 */
			first_copied_row = (vc->vc_y - new_rows/2);
		}
		old_origin += first_copied_row * old_row_size;
	} else
		first_copied_row = 0;
	end = old_origin + old_row_size * min(old_rows, new_rows);

	vc_uniscr_copy_area(new_uniscr, new_cols, new_rows,
			    get_vc_uniscr(vc), rlth/2, first_copied_row,
			    min(old_rows, new_rows));
	vc_uniscr_set(vc, new_uniscr);

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
	kfree(vc->vc_screenbuf);
	vc->vc_screenbuf = newscreen;
	vc->vc_screenbuf_size = new_screen_size;
	set_origin(vc);

	/* do part of a reset_terminal() */
	vc->vc_top = 0;
	vc->vc_bottom = vc->vc_rows;
	gotoxy(vc, vc->vc_x, vc->vc_y);
	save_cur(vc);

	if (tty) {
		/* Rewrite the requested winsize data with the actual
		   resulting sizes */
		struct winsize ws;
		memset(&ws, 0, sizeof(ws));
		ws.ws_row = vc->vc_rows;
		ws.ws_col = vc->vc_cols;
		ws.ws_ypixel = vc->vc_scan_lines;
		tty_do_resize(tty, &ws);
	}

	if (con_is_visible(vc))
		update_screen(vc);
	vt_event_post(VT_EVENT_RESIZE, vc->vc_num, vc->vc_num);
	notify_update(vc);
	return err;
}

/**
 *	vc_resize		-	resize a VT
 *	@vc: virtual console
 *	@cols: columns
 *	@rows: rows
 *
 *	Resize a virtual console as seen from the console end of things. We
 *	use the common vc_do_resize methods to update the structures. The
 *	caller must hold the console sem to protect console internals and
 *	vc->port.tty
 */

int vc_resize(struct vc_data *vc, unsigned int cols, unsigned int rows)
{
	return vc_do_resize(vc->port.tty, vc, cols, rows);
}

/**
 *	vt_resize		-	resize a VT
 *	@tty: tty to resize
 *	@ws: winsize attributes
 *
 *	Resize a virtual terminal. This is called by the tty layer as we
 *	register our own handler for resizing. The mutual helper does all
 *	the actual work.
 *
 *	Takes the console sem and the called methods then take the tty
 *	termios_rwsem and the tty ctrl_lock in that order.
 */
static int vt_resize(struct tty_struct *tty, struct winsize *ws)
{
	struct vc_data *vc = tty->driver_data;
	int ret;

	console_lock();
	ret = vc_do_resize(tty, vc, ws->ws_col, ws->ws_row);
	console_unlock();
	return ret;
}

struct vc_data *vc_deallocate(unsigned int currcons)
{
	struct vc_data *vc = NULL;

	WARN_CONSOLE_UNLOCKED();

	if (vc_cons_allocated(currcons)) {
		struct vt_notifier_param param;

		param.vc = vc = vc_cons[currcons].d;
		atomic_notifier_call_chain(&vt_notifier_list, VT_DEALLOCATE, &param);
		vcs_remove_sysfs(currcons);
		visual_deinit(vc);
		put_pid(vc->vt_pid);
		vc_uniscr_set(vc, NULL);
		kfree(vc->vc_screenbuf);
		vc_cons[currcons].d = NULL;
	}
	return vc;
}

/*
 *	VT102 emulator
 */

#define set_kbd(vc, x)	vt_set_kbd_mode_bit((vc)->vc_num, (x))
#define clr_kbd(vc, x)	vt_clr_kbd_mode_bit((vc)->vc_num, (x))
#define is_kbd(vc, x)	vt_get_kbd_mode_bit((vc)->vc_num, (x))

#define decarm		VC_REPEAT
#define decckm		VC_CKMODE
#define kbdapplic	VC_APPLIC
#define lnm		VC_CRLF

/*
 * this is what the terminal answers to a ESC-Z or csi0c query.
 */
#define VT100ID "\033[?1;2c"
#define VT102ID "\033[?6c"

const unsigned char color_table[] = { 0, 4, 2, 6, 1, 5, 3, 7,
				       8,12,10,14, 9,13,11,15 };

/* the default colour table, for VGA+ colour systems */
unsigned char default_red[] = {
	0x00, 0xaa, 0x00, 0xaa, 0x00, 0xaa, 0x00, 0xaa,
	0x55, 0xff, 0x55, 0xff, 0x55, 0xff, 0x55, 0xff
};
module_param_array(default_red, byte, NULL, S_IRUGO | S_IWUSR);

unsigned char default_grn[] = {
	0x00, 0x00, 0xaa, 0x55, 0x00, 0x00, 0xaa, 0xaa,
	0x55, 0x55, 0xff, 0xff, 0x55, 0x55, 0xff, 0xff
};
module_param_array(default_grn, byte, NULL, S_IRUGO | S_IWUSR);

unsigned char default_blu[] = {
	0x00, 0x00, 0x00, 0x00, 0xaa, 0xaa, 0xaa, 0xaa,
	0x55, 0x55, 0x55, 0x55, 0xff, 0xff, 0xff, 0xff
};
module_param_array(default_blu, byte, NULL, S_IRUGO | S_IWUSR);

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

void scrollback(struct vc_data *vc)
{
	scrolldelta(-(vc->vc_rows / 2));
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
		con_scroll(vc, vc->vc_top, vc->vc_bottom, SM_UP, 1);
	else if (vc->vc_y < vc->vc_rows - 1) {
	    	vc->vc_y++;
		vc->vc_pos += vc->vc_size_row;
	}
	vc->vc_need_wrap = 0;
	notify_write(vc, '\n');
}

static void ri(struct vc_data *vc)
{
    	/* don't scroll if below top of scrolling region, or
	 * if above scrolling region
	 */
	if (vc->vc_y == vc->vc_top)
		con_scroll(vc, vc->vc_top, vc->vc_bottom, SM_DOWN, 1);
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
	notify_write(vc, '\r');
}

static inline void bs(struct vc_data *vc)
{
	if (vc->vc_x) {
		vc->vc_pos -= 2;
		vc->vc_x--;
		vc->vc_need_wrap = 0;
		notify_write(vc, '\b');
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
			vc_uniscr_clear_line(vc, vc->vc_x,
					     vc->vc_cols - vc->vc_x);
			vc_uniscr_clear_lines(vc, vc->vc_y + 1,
					      vc->vc_rows - vc->vc_y - 1);
			count = (vc->vc_scr_end - vc->vc_pos) >> 1;
			start = (unsigned short *)vc->vc_pos;
			break;
		case 1:	/* erase from start to cursor */
			vc_uniscr_clear_line(vc, 0, vc->vc_x + 1);
			vc_uniscr_clear_lines(vc, 0, vc->vc_y);
			count = ((vc->vc_pos - vc->vc_origin) >> 1) + 1;
			start = (unsigned short *)vc->vc_origin;
			break;
		case 3: /* include scrollback */
			flush_scrollback(vc);
			/* fallthrough */
		case 2: /* erase whole display */
			vc_uniscr_clear_lines(vc, 0, vc->vc_rows);
			count = vc->vc_cols * vc->vc_rows;
			start = (unsigned short *)vc->vc_origin;
			break;
		default:
			return;
	}
	scr_memsetw(start, vc->vc_video_erase_char, 2 * count);
	if (con_should_update(vc))
		do_update_region(vc, (unsigned long) start, count);
	vc->vc_need_wrap = 0;
}

static void csi_K(struct vc_data *vc, int vpar)
{
	unsigned int count;
	unsigned short *start = (unsigned short *)vc->vc_pos;
	int offset;

	switch (vpar) {
		case 0:	/* erase from cursor to end of line */
			offset = 0;
			count = vc->vc_cols - vc->vc_x;
			break;
		case 1:	/* erase from start of line to cursor */
			offset = -vc->vc_x;
			count = vc->vc_x + 1;
			break;
		case 2: /* erase whole line */
			offset = -vc->vc_x;
			count = vc->vc_cols;
			break;
		default:
			return;
	}
	vc_uniscr_clear_line(vc, vc->vc_x + offset, count);
	scr_memsetw(start + offset, vc->vc_video_erase_char, 2 * count);
	vc->vc_need_wrap = 0;
	if (con_should_update(vc))
		do_update_region(vc, (unsigned long)(start + offset), count);
}

static void csi_X(struct vc_data *vc, int vpar) /* erase the following vpar positions */
{					  /* not vt100? */
	int count;

	if (!vpar)
		vpar++;
	count = (vpar > vc->vc_cols - vc->vc_x) ? (vc->vc_cols - vc->vc_x) : vpar;

	vc_uniscr_clear_line(vc, vc->vc_x, count);
	scr_memsetw((unsigned short *)vc->vc_pos, vc->vc_video_erase_char, 2 * count);
	if (con_should_update(vc))
		vc->vc_sw->con_clear(vc, vc->vc_y, vc->vc_x, 1, count);
	vc->vc_need_wrap = 0;
}

static void default_attr(struct vc_data *vc)
{
	vc->vc_intensity = 1;
	vc->vc_italic = 0;
	vc->vc_underline = 0;
	vc->vc_reverse = 0;
	vc->vc_blink = 0;
	vc->vc_color = vc->vc_def_color;
}

struct rgb { u8 r; u8 g; u8 b; };

static void rgb_from_256(int i, struct rgb *c)
{
	if (i < 8) {            /* Standard colours. */
		c->r = i&1 ? 0xaa : 0x00;
		c->g = i&2 ? 0xaa : 0x00;
		c->b = i&4 ? 0xaa : 0x00;
	} else if (i < 16) {
		c->r = i&1 ? 0xff : 0x55;
		c->g = i&2 ? 0xff : 0x55;
		c->b = i&4 ? 0xff : 0x55;
	} else if (i < 232) {   /* 6x6x6 colour cube. */
		c->r = (i - 16) / 36 * 85 / 2;
		c->g = (i - 16) / 6 % 6 * 85 / 2;
		c->b = (i - 16) % 6 * 85 / 2;
	} else                  /* Grayscale ramp. */
		c->r = c->g = c->b = i * 10 - 2312;
}

static void rgb_foreground(struct vc_data *vc, const struct rgb *c)
{
	u8 hue = 0, max = max3(c->r, c->g, c->b);

	if (c->r > max / 2)
		hue |= 4;
	if (c->g > max / 2)
		hue |= 2;
	if (c->b > max / 2)
		hue |= 1;

	if (hue == 7 && max <= 0x55) {
		hue = 0;
		vc->vc_intensity = 2;
	} else if (max > 0xaa)
		vc->vc_intensity = 2;
	else
		vc->vc_intensity = 1;

	vc->vc_color = (vc->vc_color & 0xf0) | hue;
}

static void rgb_background(struct vc_data *vc, const struct rgb *c)
{
	/* For backgrounds, err on the dark side. */
	vc->vc_color = (vc->vc_color & 0x0f)
		| (c->r&0x80) >> 1 | (c->g&0x80) >> 2 | (c->b&0x80) >> 3;
}

/*
 * ITU T.416 Higher colour modes. They break the usual properties of SGR codes
 * and thus need to be detected and ignored by hand. Strictly speaking, that
 * standard also wants : rather than ; as separators, contrary to ECMA-48, but
 * no one produces such codes and almost no one accepts them.
 *
 * Subcommands 3 (CMY) and 4 (CMYK) are so insane there's no point in
 * supporting them.
 */
static int vc_t416_color(struct vc_data *vc, int i,
		void(*set_color)(struct vc_data *vc, const struct rgb *c))
{
	struct rgb c;

	i++;
	if (i > vc->vc_npar)
		return i;

	if (vc->vc_par[i] == 5 && i + 1 <= vc->vc_npar) {
		/* 256 colours */
		i++;
		rgb_from_256(vc->vc_par[i], &c);
	} else if (vc->vc_par[i] == 2 && i + 3 <= vc->vc_npar) {
		/* 24 bit */
		c.r = vc->vc_par[i + 1];
		c.g = vc->vc_par[i + 2];
		c.b = vc->vc_par[i + 3];
		i += 3;
	} else
		return i;

	set_color(vc, &c);

	return i;
}

/* console_lock is held */
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
		case 3:
			vc->vc_italic = 1;
			break;
		case 21:
			/*
			 * No console drivers support double underline, so
			 * convert it to a single underline.
			 */
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
			  * Select primary font, don't display control chars if
			  * defined, don't set bit 8 on output.
			  */
			vc->vc_translate = set_translate(vc->vc_charset == 0
					? vc->vc_G0_charset
					: vc->vc_G1_charset, vc);
			vc->vc_disp_ctrl = 0;
			vc->vc_toggle_meta = 0;
			break;
		case 11: /* ANSI X3.64-1979 (SCO-ish?)
			  * Select first alternate font, lets chars < 32 be
			  * displayed as ROM chars.
			  */
			vc->vc_translate = set_translate(IBMPC_MAP, vc);
			vc->vc_disp_ctrl = 1;
			vc->vc_toggle_meta = 0;
			break;
		case 12: /* ANSI X3.64-1979 (SCO-ish?)
			  * Select second alternate font, toggle high bit
			  * before displaying as ROM char.
			  */
			vc->vc_translate = set_translate(IBMPC_MAP, vc);
			vc->vc_disp_ctrl = 1;
			vc->vc_toggle_meta = 1;
			break;
		case 22:
			vc->vc_intensity = 1;
			break;
		case 23:
			vc->vc_italic = 0;
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
		case 38:
			i = vc_t416_color(vc, i, rgb_foreground);
			break;
		case 48:
			i = vc_t416_color(vc, i, rgb_background);
			break;
		case 39:
			vc->vc_color = (vc->vc_def_color & 0x0f) |
				(vc->vc_color & 0xf0);
			break;
		case 49:
			vc->vc_color = (vc->vc_def_color & 0xf0) |
				(vc->vc_color & 0x0f);
			break;
		default:
			if (vc->vc_par[i] >= 90 && vc->vc_par[i] <= 107) {
				if (vc->vc_par[i] < 100)
					vc->vc_intensity = 2;
				vc->vc_par[i] -= 60;
			}
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

static void respond_string(const char *p, struct tty_port *port)
{
	while (*p) {
		tty_insert_flip_char(port, *p, 0);
		p++;
	}
	tty_schedule_flip(port);
}

static void cursor_report(struct vc_data *vc, struct tty_struct *tty)
{
	char buf[40];

	sprintf(buf, "\033[%d;%dR", vc->vc_y + (vc->vc_decom ? vc->vc_top + 1 : 1), vc->vc_x + 1);
	respond_string(buf, tty->port);
}

static inline void status_report(struct tty_struct *tty)
{
	respond_string("\033[0n", tty->port);	/* Terminal ok */
}

static inline void respond_ID(struct tty_struct *tty)
{
	respond_string(VT102ID, tty->port);
}

void mouse_report(struct tty_struct *tty, int butt, int mrx, int mry)
{
	char buf[8];

	sprintf(buf, "\033[M%c%c%c", (char)(' ' + butt), (char)('!' + mrx),
		(char)('!' + mry));
	respond_string(buf, tty->port);
}

/* invoked via ioctl(TIOCLINUX) and through set_selection */
int mouse_reporting(void)
{
	return vc_cons[fg_console].d->vc_report_mouse;
}

/* console_lock is held */
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

/* console_lock is held */
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
			blankinterval = ((vc->vc_par[1] < 60) ? vc->vc_par[1] : 60) * 60;
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
					msecs_to_jiffies(vc->vc_par[1]) : 0;
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
		case 16: /* set cursor blink duration in msec */
			if (vc->vc_npar >= 1 && vc->vc_par[1] >= 50 &&
					vc->vc_par[1] <= USHRT_MAX)
				vc->vc_cur_blink_ms = vc->vc_par[1];
			else
				vc->vc_cur_blink_ms = DEFAULT_CURSOR_BLINK_MS;
			break;
	}
}

/* console_lock is held */
static void csi_at(struct vc_data *vc, unsigned int nr)
{
	if (nr > vc->vc_cols - vc->vc_x)
		nr = vc->vc_cols - vc->vc_x;
	else if (!nr)
		nr = 1;
	insert_char(vc, nr);
}

/* console_lock is held */
static void csi_L(struct vc_data *vc, unsigned int nr)
{
	if (nr > vc->vc_rows - vc->vc_y)
		nr = vc->vc_rows - vc->vc_y;
	else if (!nr)
		nr = 1;
	con_scroll(vc, vc->vc_y, vc->vc_bottom, SM_DOWN, nr);
	vc->vc_need_wrap = 0;
}

/* console_lock is held */
static void csi_P(struct vc_data *vc, unsigned int nr)
{
	if (nr > vc->vc_cols - vc->vc_x)
		nr = vc->vc_cols - vc->vc_x;
	else if (!nr)
		nr = 1;
	delete_char(vc, nr);
}

/* console_lock is held */
static void csi_M(struct vc_data *vc, unsigned int nr)
{
	if (nr > vc->vc_rows - vc->vc_y)
		nr = vc->vc_rows - vc->vc_y;
	else if (!nr)
		nr=1;
	con_scroll(vc, vc->vc_y, vc->vc_bottom, SM_UP, nr);
	vc->vc_need_wrap = 0;
}

/* console_lock is held (except via vc_init->reset_terminal */
static void save_cur(struct vc_data *vc)
{
	vc->vc_saved_x		= vc->vc_x;
	vc->vc_saved_y		= vc->vc_y;
	vc->vc_s_intensity	= vc->vc_intensity;
	vc->vc_s_italic         = vc->vc_italic;
	vc->vc_s_underline	= vc->vc_underline;
	vc->vc_s_blink		= vc->vc_blink;
	vc->vc_s_reverse	= vc->vc_reverse;
	vc->vc_s_charset	= vc->vc_charset;
	vc->vc_s_color		= vc->vc_color;
	vc->vc_saved_G0		= vc->vc_G0_charset;
	vc->vc_saved_G1		= vc->vc_G1_charset;
}

/* console_lock is held */
static void restore_cur(struct vc_data *vc)
{
	gotoxy(vc, vc->vc_saved_x, vc->vc_saved_y);
	vc->vc_intensity	= vc->vc_s_intensity;
	vc->vc_italic		= vc->vc_s_italic;
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

enum { ESnormal, ESesc, ESsquare, ESgetpars, ESfunckey,
	EShash, ESsetG0, ESsetG1, ESpercent, ESignore, ESnonstd,
	ESpalette, ESosc };

/* console_lock is held (except via vc_init()) */
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
	vc->vc_utf              = default_utf8;
	vc->vc_utf_count	= 0;

	vc->vc_disp_ctrl	= 0;
	vc->vc_toggle_meta	= 0;

	vc->vc_decscnm		= 0;
	vc->vc_decom		= 0;
	vc->vc_decawm		= 1;
	vc->vc_deccm		= global_cursor_default;
	vc->vc_decim		= 0;

	vt_reset_keyboard(vc->vc_num);

	vc->vc_cursor_type = cur_default;
	vc->vc_complement_mask = vc->vc_s_complement_mask;

	default_attr(vc);
	update_attr(vc);

	vc->vc_tab_stop[0]	=
	vc->vc_tab_stop[1]	=
	vc->vc_tab_stop[2]	=
	vc->vc_tab_stop[3]	=
	vc->vc_tab_stop[4]	=
	vc->vc_tab_stop[5]	=
	vc->vc_tab_stop[6]	=
	vc->vc_tab_stop[7]	= 0x01010101;

	vc->vc_bell_pitch = DEFAULT_BELL_PITCH;
	vc->vc_bell_duration = DEFAULT_BELL_DURATION;
	vc->vc_cur_blink_ms = DEFAULT_CURSOR_BLINK_MS;

	gotoxy(vc, 0, 0);
	save_cur(vc);
	if (do_clear)
	    csi_J(vc, 2);
}

/* console_lock is held */
static void do_con_trol(struct tty_struct *tty, struct vc_data *vc, int c)
{
	/*
	 *  Control characters can be used in the _middle_
	 *  of an escape sequence.
	 */
	if (vc->vc_state == ESosc && c>=8 && c<=13) /* ... except for OSC */
		return;
	switch (c) {
	case 0:
		return;
	case 7:
		if (vc->vc_state == ESosc)
			vc->vc_state = ESnormal;
		else if (vc->vc_bell_duration)
			kd_mksound(vc->vc_bell_pitch, vc->vc_bell_duration);
		return;
	case 8:
		bs(vc);
		return;
	case 9:
		vc->vc_pos -= (vc->vc_x << 1);
		while (vc->vc_x < vc->vc_cols - 1) {
			vc->vc_x++;
			if (vc->vc_tab_stop[7 & (vc->vc_x >> 5)] & (1 << (vc->vc_x & 31)))
				break;
		}
		vc->vc_pos += (vc->vc_x << 1);
		notify_write(vc, '\t');
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
			vc->vc_tab_stop[7 & (vc->vc_x >> 5)] |= (1 << (vc->vc_x & 31));
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
		} else if (c>='0' && c<='9')
			vc->vc_state = ESosc;
		else
			vc->vc_state = ESnormal;
		return;
	case ESpalette:
		if (isxdigit(c)) {
			vc->vc_par[vc->vc_npar++] = hex_to_bin(c);
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
		}
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
					vc->vc_cursor_type = cur_default;
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
				vc->vc_tab_stop[7 & (vc->vc_x >> 5)] &= ~(1 << (vc->vc_x & 31));
			else if (vc->vc_par[0] == 3) {
				vc->vc_tab_stop[0] =
					vc->vc_tab_stop[1] =
					vc->vc_tab_stop[2] =
					vc->vc_tab_stop[3] =
					vc->vc_tab_stop[4] =
					vc->vc_tab_stop[5] =
					vc->vc_tab_stop[6] =
					vc->vc_tab_stop[7] = 0;
			}
			return;
		case 'm':
			csi_m(vc);
			return;
		case 'q': /* DECLL - but only 3 leds */
			/* map 0,1,2,3 to 0,1,2,4 */
			if (vc->vc_par[0] < 4)
				vt_set_led_state(vc->vc_num,
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
	case ESosc:
		return;
	default:
		vc->vc_state = ESnormal;
	}
}

/* is_double_width() is based on the wcwidth() implementation by
 * Markus Kuhn -- 2007-05-26 (Unicode 5.0)
 * Latest version: http://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c
 */
struct interval {
	uint32_t first;
	uint32_t last;
};

static int ucs_cmp(const void *key, const void *elt)
{
	uint32_t ucs = *(uint32_t *)key;
	struct interval e = *(struct interval *) elt;

	if (ucs > e.last)
		return 1;
	else if (ucs < e.first)
		return -1;
	return 0;
}

static int is_double_width(uint32_t ucs)
{
	static const struct interval double_width[] = {
		{ 0x1100, 0x115F }, { 0x2329, 0x232A }, { 0x2E80, 0x303E },
		{ 0x3040, 0xA4CF }, { 0xAC00, 0xD7A3 }, { 0xF900, 0xFAFF },
		{ 0xFE10, 0xFE19 }, { 0xFE30, 0xFE6F }, { 0xFF00, 0xFF60 },
		{ 0xFFE0, 0xFFE6 }, { 0x20000, 0x2FFFD }, { 0x30000, 0x3FFFD }
	};
	if (ucs < double_width[0].first ||
	    ucs > double_width[ARRAY_SIZE(double_width) - 1].last)
		return 0;

	return bsearch(&ucs, double_width, ARRAY_SIZE(double_width),
			sizeof(struct interval), ucs_cmp) != NULL;
}

static void con_flush(struct vc_data *vc, unsigned long draw_from,
		unsigned long draw_to, int *draw_x)
{
	if (*draw_x < 0)
		return;

	vc->vc_sw->con_putcs(vc, (u16 *)draw_from,
			(u16 *)draw_to - (u16 *)draw_from, vc->vc_y, *draw_x);
	*draw_x = -1;
}

/* acquires console_lock */
static int do_con_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	int c, next_c, tc, ok, n = 0, draw_x = -1;
	unsigned int currcons;
	unsigned long draw_from = 0, draw_to = 0;
	struct vc_data *vc;
	unsigned char vc_attr;
	struct vt_notifier_param param;
	uint8_t rescan;
	uint8_t inverse;
	uint8_t width;
	u16 himask, charmask;

	if (in_interrupt())
		return count;

	might_sleep();

	console_lock();
	vc = tty->driver_data;
	if (vc == NULL) {
		pr_err("vt: argh, driver_data is NULL !\n");
		console_unlock();
		return 0;
	}

	currcons = vc->vc_num;
	if (!vc_cons_allocated(currcons)) {
		/* could this happen? */
		pr_warn_once("con_write: tty %d not allocated\n", currcons+1);
		console_unlock();
		return 0;
	}

	himask = vc->vc_hi_font_mask;
	charmask = himask ? 0x1ff : 0xff;

	/* undraw cursor first */
	if (con_is_fg(vc))
		hide_cursor(vc);

	param.vc = vc;

	while (!tty->stopped && count) {
		int orig = *buf;
		c = orig;
		buf++;
		n++;
		count--;
		rescan = 0;
		inverse = 0;
		width = 1;

		/* Do no translation at all in control states */
		if (vc->vc_state != ESnormal) {
			tc = c;
		} else if (vc->vc_utf && !vc->vc_disp_ctrl) {
		    /* Combine UTF-8 into Unicode in vc_utf_char.
		     * vc_utf_count is the number of continuation bytes still
		     * expected to arrive.
		     * vc_npar is the number of continuation bytes arrived so
		     * far
		     */
rescan_last_byte:
		    if ((c & 0xc0) == 0x80) {
			/* Continuation byte received */
			static const uint32_t utf8_length_changes[] = { 0x0000007f, 0x000007ff, 0x0000ffff, 0x001fffff, 0x03ffffff, 0x7fffffff };
			if (vc->vc_utf_count) {
			    vc->vc_utf_char = (vc->vc_utf_char << 6) | (c & 0x3f);
			    vc->vc_npar++;
			    if (--vc->vc_utf_count) {
				/* Still need some bytes */
				continue;
			    }
			    /* Got a whole character */
			    c = vc->vc_utf_char;
			    /* Reject overlong sequences */
			    if (c <= utf8_length_changes[vc->vc_npar - 1] ||
					c > utf8_length_changes[vc->vc_npar])
				c = 0xfffd;
			} else {
			    /* Unexpected continuation byte */
			    vc->vc_utf_count = 0;
			    c = 0xfffd;
			}
		    } else {
			/* Single ASCII byte or first byte of a sequence received */
			if (vc->vc_utf_count) {
			    /* Continuation byte expected */
			    rescan = 1;
			    vc->vc_utf_count = 0;
			    c = 0xfffd;
			} else if (c > 0x7f) {
			    /* First byte of a multibyte sequence received */
			    vc->vc_npar = 0;
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
			    } else {
				/* 254 and 255 are invalid */
				c = 0xfffd;
			    }
			    if (vc->vc_utf_count) {
				/* Still need some bytes */
				continue;
			    }
			}
			/* Nothing to do if an ASCII byte was received */
		    }
		    /* End of UTF-8 decoding. */
		    /* c is the received character, or U+FFFD for invalid sequences. */
		    /* Replace invalid Unicode code points with U+FFFD too */
		    if ((c >= 0xd800 && c <= 0xdfff) || c == 0xfffe || c == 0xffff)
			c = 0xfffd;
		    tc = c;
		} else {	/* no utf or alternate charset mode */
		    tc = vc_translate(vc, c);
		}

		param.c = tc;
		if (atomic_notifier_call_chain(&vt_notifier_list, VT_PREWRITE,
					&param) == NOTIFY_STOP)
			continue;

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
			    !(vc->vc_disp_ctrl ? (CTRL_ALWAYS >> c) & 1 :
				  vc->vc_utf || ((CTRL_ACTION >> c) & 1)))
			&& (c != 127 || vc->vc_disp_ctrl)
			&& (c != 128+27);

		if (vc->vc_state == ESnormal && ok) {
			if (vc->vc_utf && !vc->vc_disp_ctrl) {
				if (is_double_width(c))
					width = 2;
			}
			/* Now try to find out how to display it */
			tc = conv_uni_to_pc(vc, tc);
			if (tc & ~charmask) {
				if (tc == -1 || tc == -2) {
				    continue; /* nothing to display */
				}
				/* Glyph not found */
				if ((!(vc->vc_utf && !vc->vc_disp_ctrl) || c < 128) && !(c & ~charmask)) {
				    /* In legacy mode use the glyph we get by a 1:1 mapping.
				       This would make absolutely no sense with Unicode in mind,
				       but do this for ASCII characters since a font may lack
				       Unicode mapping info and we don't want to end up with
				       having question marks only. */
				    tc = c;
				} else {
				    /* Display U+FFFD. If it's not found, display an inverse question mark. */
				    tc = conv_uni_to_pc(vc, 0xfffd);
				    if (tc < 0) {
					inverse = 1;
					tc = conv_uni_to_pc(vc, '?');
					if (tc < 0) tc = '?';
				    }
				}
			}

			if (!inverse) {
				vc_attr = vc->vc_attr;
			} else {
				/* invert vc_attr */
				if (!vc->vc_can_do_color) {
					vc_attr = (vc->vc_attr) ^ 0x08;
				} else if (vc->vc_hi_font_mask == 0x100) {
					vc_attr = ((vc->vc_attr) & 0x11) | (((vc->vc_attr) & 0xe0) >> 4) | (((vc->vc_attr) & 0x0e) << 4);
				} else {
					vc_attr = ((vc->vc_attr) & 0x88) | (((vc->vc_attr) & 0x70) >> 4) | (((vc->vc_attr) & 0x07) << 4);
				}
				con_flush(vc, draw_from, draw_to, &draw_x);
			}

			next_c = c;
			while (1) {
				if (vc->vc_need_wrap || vc->vc_decim)
					con_flush(vc, draw_from, draw_to,
							&draw_x);
				if (vc->vc_need_wrap) {
					cr(vc);
					lf(vc);
				}
				if (vc->vc_decim)
					insert_char(vc, 1);
				vc_uniscr_putc(vc, next_c);
				scr_writew(himask ?
					     ((vc_attr << 8) & ~himask) + ((tc & 0x100) ? himask : 0) + (tc & 0xff) :
					     (vc_attr << 8) + tc,
					   (u16 *) vc->vc_pos);
				if (con_should_update(vc) && draw_x < 0) {
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

				if (!--width) break;

				tc = conv_uni_to_pc(vc, ' '); /* A space is printed in the second column */
				if (tc < 0) tc = ' ';
				next_c = ' ';
			}
			notify_write(vc, c);

			if (inverse)
				con_flush(vc, draw_from, draw_to, &draw_x);

			if (rescan) {
				rescan = 0;
				inverse = 0;
				width = 1;
				c = orig;
				goto rescan_last_byte;
			}
			continue;
		}
		con_flush(vc, draw_from, draw_to, &draw_x);
		do_con_trol(tty, vc, orig);
	}
	con_flush(vc, draw_from, draw_to, &draw_x);
	vc_uniscr_debug_check(vc);
	console_conditional_schedule();
	notify_update(vc);
	console_unlock();
	return n;
}

/*
 * This is the console switching callback.
 *
 * Doing console switching in a process context allows
 * us to do the switches asynchronously (needed when we want
 * to switch due to a keyboard interrupt).  Synchronization
 * with other console code and prevention of re-entrancy is
 * ensured with console_lock.
 */
static void console_callback(struct work_struct *ignored)
{
	console_lock();

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
		if (vc->vc_mode == KD_TEXT && vc->vc_sw->con_scrolldelta)
			vc->vc_sw->con_scrolldelta(vc, scrollback_delta);
		scrollback_delta = 0;
	}
	if (blank_timer_expired) {
		do_blank_screen(0);
		blank_timer_expired = 0;
	}
	notify_update(vc_cons[fg_console].d);

	console_unlock();
}

int set_console(int nr)
{
	struct vc_data *vc = vc_cons[fg_console].d;

	if (!vc_cons_allocated(nr) || vt_dont_switch ||
		(vc->vt_mode.mode == VT_AUTO && vc->vc_mode == KD_GRAPHICS)) {

		/*
		 * Console switch will fail in console_callback() or
		 * change_console() so there is no point scheduling
		 * the callback
		 *
		 * Existing set_console() users don't check the return
		 * value so this shouldn't break anything
		 */
		return -EINVAL;
	}

	want_console = nr;
	schedule_console_callback();

	return 0;
}

struct tty_driver *console_driver;

#ifdef CONFIG_VT_CONSOLE

/**
 * vt_kmsg_redirect() - Sets/gets the kernel message console
 * @new:	The new virtual terminal number or -1 if the console should stay
 * 		unchanged
 *
 * By default, the kernel messages are always printed on the current virtual
 * console. However, the user may modify that default with the
 * TIOCL_SETKMSGREDIRECT ioctl call.
 *
 * This function sets the kernel message console to be @new. It returns the old
 * virtual console number. The virtual terminal number 0 (both as parameter and
 * return value) means no redirection (i.e. always printed on the currently
 * active console).
 *
 * The parameter -1 means that only the current console is returned, but the
 * value is not modified. You may use the macro vt_get_kmsg_redirect() in that
 * case to make the code more understandable.
 *
 * When the kernel is compiled without CONFIG_VT_CONSOLE, this function ignores
 * the parameter and always returns 0.
 */
int vt_kmsg_redirect(int new)
{
	static int kmsg_con;

	if (new != -1)
		return xchg(&kmsg_con, new);
	else
		return kmsg_con;
}

/*
 *	Console on virtual terminal
 *
 * The console must be locked when we get here.
 */

static void vt_console_print(struct console *co, const char *b, unsigned count)
{
	struct vc_data *vc = vc_cons[fg_console].d;
	unsigned char c;
	static DEFINE_SPINLOCK(printing_lock);
	const ushort *start;
	ushort start_x, cnt;
	int kmsg_console;

	/* console busy or not yet initialized */
	if (!printable)
		return;
	if (!spin_trylock(&printing_lock))
		return;

	kmsg_console = vt_get_kmsg_redirect();
	if (kmsg_console && vc_cons_allocated(kmsg_console - 1))
		vc = vc_cons[kmsg_console - 1].d;

	if (!vc_cons_allocated(fg_console)) {
		/* impossible */
		/* printk("vt_console_print: tty %d not allocated ??\n", currcons+1); */
		goto quit;
	}

	if (vc->vc_mode != KD_TEXT && !vt_force_oops_output(vc))
		goto quit;

	/* undraw cursor first */
	if (con_is_fg(vc))
		hide_cursor(vc);

	start = (ushort *)vc->vc_pos;
	start_x = vc->vc_x;
	cnt = 0;
	while (count--) {
		c = *b++;
		if (c == 10 || c == 13 || c == 8 || vc->vc_need_wrap) {
			if (cnt && con_is_visible(vc))
				vc->vc_sw->con_putcs(vc, start, cnt, vc->vc_y, start_x);
			cnt = 0;
			if (c == 8) {		/* backspace */
				bs(vc);
				start = (ushort *)vc->vc_pos;
				start_x = vc->vc_x;
				continue;
			}
			if (c != 13)
				lf(vc);
			cr(vc);
			start = (ushort *)vc->vc_pos;
			start_x = vc->vc_x;
			if (c == 10 || c == 13)
				continue;
		}
		vc_uniscr_putc(vc, c);
		scr_writew((vc->vc_attr << 8) + c, (unsigned short *)vc->vc_pos);
		notify_write(vc, c);
		cnt++;
		if (vc->vc_x == vc->vc_cols - 1) {
			vc->vc_need_wrap = 1;
		} else {
			vc->vc_pos += 2;
			vc->vc_x++;
		}
	}
	if (cnt && con_is_visible(vc))
		vc->vc_sw->con_putcs(vc, start, cnt, vc->vc_y, start_x);
	set_cursor(vc);
	notify_update(vc);

quit:
	spin_unlock(&printing_lock);
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
 * Generally a bit racy with respect to console_lock();.
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

	if (current->signal->tty != tty && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (get_user(type, p))
		return -EFAULT;
	ret = 0;

	switch (type)
	{
		case TIOCL_SETSEL:
			ret = set_selection((struct tiocl_selection __user *)(p+1), tty);
			break;
		case TIOCL_PASTESEL:
			ret = paste_selection(tty);
			break;
		case TIOCL_UNBLANKSCREEN:
			console_lock();
			unblank_screen();
			console_unlock();
			break;
		case TIOCL_SELLOADLUT:
			console_lock();
			ret = sel_loadlut(p);
			console_unlock();
			break;
		case TIOCL_GETSHIFTSTATE:

	/*
	 * Make it possible to react to Shift+Mousebutton.
	 * Note that 'shift_state' is an undocumented
	 * kernel-internal variable; programs not closely
	 * related to the kernel should not use this.
	 */
			data = vt_get_shift_state();
			ret = put_user(data, p);
			break;
		case TIOCL_GETMOUSEREPORTING:
			console_lock();	/* May be overkill */
			data = mouse_reporting();
			console_unlock();
			ret = put_user(data, p);
			break;
		case TIOCL_SETVESABLANK:
			console_lock();
			ret = set_vesa_blanking(p);
			console_unlock();
			break;
		case TIOCL_GETKMSGREDIRECT:
			data = vt_get_kmsg_redirect();
			ret = put_user(data, p);
			break;
		case TIOCL_SETKMSGREDIRECT:
			if (!capable(CAP_SYS_ADMIN)) {
				ret = -EPERM;
			} else {
				if (get_user(data, p+1))
					ret = -EFAULT;
				else
					vt_kmsg_redirect(data);
			}
			break;
		case TIOCL_GETFGCONSOLE:
			/* No locking needed as this is a transiently
			   correct return anyway if the caller hasn't
			   disabled switching */
			ret = fg_console;
			break;
		case TIOCL_SCROLLCONSOLE:
			if (get_user(lines, (s32 __user *)(p+4))) {
				ret = -EFAULT;
			} else {
				/* Need the console lock here. Note that lots
				   of other calls need fixing before the lock
				   is actually useful ! */
				console_lock();
				scrollfront(vc_cons[fg_console].d, lines);
				console_unlock();
				ret = 0;
			}
			break;
		case TIOCL_BLANKSCREEN:	/* until explicitly unblanked, not only poked */
			console_lock();
			ignore_poke = 1;
			do_blank_screen(0);
			console_unlock();
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

static int con_put_char(struct tty_struct *tty, unsigned char ch)
{
	if (in_interrupt())
		return 0;	/* n_r3964 calls put_char() from interrupt context */
	return do_con_write(tty, &ch, 1);
}

static int con_write_room(struct tty_struct *tty)
{
	if (tty->stopped)
		return 0;
	return 32768;		/* No limit, really; we're not buffering */
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
	vt_kbd_con_stop(console_num);
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
	vt_kbd_con_start(console_num);
}

static void con_flush_chars(struct tty_struct *tty)
{
	struct vc_data *vc;

	if (in_interrupt())	/* from flush_to_ldisc */
		return;

	/* if we race with con_close(), vt may be null */
	console_lock();
	vc = tty->driver_data;
	if (vc)
		set_cursor(vc);
	console_unlock();
}

/*
 * Allocate the console screen memory.
 */
static int con_install(struct tty_driver *driver, struct tty_struct *tty)
{
	unsigned int currcons = tty->index;
	struct vc_data *vc;
	int ret;

	console_lock();
	ret = vc_allocate(currcons);
	if (ret)
		goto unlock;

	vc = vc_cons[currcons].d;

	/* Still being freed */
	if (vc->port.tty) {
		ret = -ERESTARTSYS;
		goto unlock;
	}

	ret = tty_port_install(&vc->port, driver, tty);
	if (ret)
		goto unlock;

	tty->driver_data = vc;
	vc->port.tty = tty;
	tty_port_get(&vc->port);

	if (!tty->winsize.ws_row && !tty->winsize.ws_col) {
		tty->winsize.ws_row = vc_cons[currcons].d->vc_rows;
		tty->winsize.ws_col = vc_cons[currcons].d->vc_cols;
	}
	if (vc->vc_utf)
		tty->termios.c_iflag |= IUTF8;
	else
		tty->termios.c_iflag &= ~IUTF8;
unlock:
	console_unlock();
	return ret;
}

static int con_open(struct tty_struct *tty, struct file *filp)
{
	/* everything done in install */
	return 0;
}


static void con_close(struct tty_struct *tty, struct file *filp)
{
	/* Nothing to do - we defer to shutdown */
}

static void con_shutdown(struct tty_struct *tty)
{
	struct vc_data *vc = tty->driver_data;
	BUG_ON(vc == NULL);
	console_lock();
	vc->port.tty = NULL;
	console_unlock();
}

static void con_cleanup(struct tty_struct *tty)
{
	struct vc_data *vc = tty->driver_data;

	tty_port_put(&vc->port);
}

static int default_color           = 7; /* white */
static int default_italic_color    = 2; // green (ASCII)
static int default_underline_color = 3; // cyan (ASCII)
module_param_named(color, default_color, int, S_IRUGO | S_IWUSR);
module_param_named(italic, default_italic_color, int, S_IRUGO | S_IWUSR);
module_param_named(underline, default_underline_color, int, S_IRUGO | S_IWUSR);

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
	vc->vc_def_color       = default_color;
	vc->vc_ulcolor         = default_underline_color;
	vc->vc_itcolor         = default_italic_color;
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
	unsigned int currcons = 0, i;

	console_lock();

	if (conswitchp)
		display_desc = conswitchp->con_startup();
	if (!display_desc) {
		fg_console = 0;
		console_unlock();
		return 0;
	}

	for (i = 0; i < MAX_NR_CON_DRIVER; i++) {
		struct con_driver *con_driver = &registered_con_driver[i];

		if (con_driver->con == NULL) {
			con_driver->con = conswitchp;
			con_driver->desc = display_desc;
			con_driver->flag = CON_DRIVER_FLAG_INIT;
			con_driver->first = 0;
			con_driver->last = MAX_NR_CONSOLES - 1;
			break;
		}
	}

	for (i = 0; i < MAX_NR_CONSOLES; i++)
		con_driver_map[i] = conswitchp;

	if (blankinterval) {
		blank_state = blank_normal_wait;
		mod_timer(&console_timer, jiffies + (blankinterval * HZ));
	}

	for (currcons = 0; currcons < MIN_NR_CONSOLES; currcons++) {
		vc_cons[currcons].d = vc = kzalloc(sizeof(struct vc_data), GFP_NOWAIT);
		INIT_WORK(&vc_cons[currcons].SAK_work, vc_SAK);
		tty_port_init(&vc->port);
		visual_init(vc, currcons, 1);
		vc->vc_screenbuf = kzalloc(vc->vc_screenbuf_size, GFP_NOWAIT);
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
	pr_info("Console: %s %s %dx%d\n",
		vc->vc_can_do_color ? "colour" : "mono",
		display_desc, vc->vc_cols, vc->vc_rows);
	printable = 1;

	console_unlock();

#ifdef CONFIG_VT_CONSOLE
	register_console(&vt_console_driver);
#endif
	return 0;
}
console_initcall(con_init);

static const struct tty_operations con_ops = {
	.install = con_install,
	.open = con_open,
	.close = con_close,
	.write = con_write,
	.write_room = con_write_room,
	.put_char = con_put_char,
	.flush_chars = con_flush_chars,
	.chars_in_buffer = con_chars_in_buffer,
	.ioctl = vt_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vt_compat_ioctl,
#endif
	.stop = con_stop,
	.start = con_start,
	.throttle = con_throttle,
	.unthrottle = con_unthrottle,
	.resize = vt_resize,
	.shutdown = con_shutdown,
	.cleanup = con_cleanup,
};

static struct cdev vc0_cdev;

static ssize_t show_tty_active(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "tty%d\n", fg_console + 1);
}
static DEVICE_ATTR(active, S_IRUGO, show_tty_active, NULL);

static struct attribute *vt_dev_attrs[] = {
	&dev_attr_active.attr,
	NULL
};

ATTRIBUTE_GROUPS(vt_dev);

int __init vty_init(const struct file_operations *console_fops)
{
	cdev_init(&vc0_cdev, console_fops);
	if (cdev_add(&vc0_cdev, MKDEV(TTY_MAJOR, 0), 1) ||
	    register_chrdev_region(MKDEV(TTY_MAJOR, 0), 1, "/dev/vc/0") < 0)
		panic("Couldn't register /dev/tty0 driver\n");
	tty0dev = device_create_with_groups(tty_class, NULL,
					    MKDEV(TTY_MAJOR, 0), NULL,
					    vt_dev_groups, "tty0");
	if (IS_ERR(tty0dev))
		tty0dev = NULL;

	vcs_init();

	console_driver = alloc_tty_driver(MAX_NR_CONSOLES);
	if (!console_driver)
		panic("Couldn't allocate console driver\n");

	console_driver->name = "tty";
	console_driver->name_base = 1;
	console_driver->major = TTY_MAJOR;
	console_driver->minor_start = 1;
	console_driver->type = TTY_DRIVER_TYPE_CONSOLE;
	console_driver->init_termios = tty_std_termios;
	if (default_utf8)
		console_driver->init_termios.c_iflag |= IUTF8;
	console_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_RESET_TERMIOS;
	tty_set_operations(console_driver, &con_ops);
	if (tty_register_driver(console_driver))
		panic("Couldn't register console driver\n");
	kbd_init();
	console_map_init();
#ifdef CONFIG_MDA_CONSOLE
	mda_console_init();
#endif
	return 0;
}

#ifndef VT_SINGLE_DRIVER

static struct class *vtconsole_class;

static int do_bind_con_driver(const struct consw *csw, int first, int last,
			   int deflt)
{
	struct module *owner = csw->owner;
	const char *desc = NULL;
	struct con_driver *con_driver;
	int i, j = -1, k = -1, retval = -ENODEV;

	if (!try_module_get(owner))
		return -ENODEV;

	WARN_CONSOLE_UNLOCKED();

	/* check if driver is registered */
	for (i = 0; i < MAX_NR_CON_DRIVER; i++) {
		con_driver = &registered_con_driver[i];

		if (con_driver->con == csw) {
			desc = con_driver->desc;
			retval = 0;
			break;
		}
	}

	if (retval)
		goto err;

	if (!(con_driver->flag & CON_DRIVER_FLAG_INIT)) {
		csw->con_startup();
		con_driver->flag |= CON_DRIVER_FLAG_INIT;
	}

	if (deflt) {
		if (conswitchp)
			module_put(conswitchp->owner);

		__module_get(owner);
		conswitchp = csw;
	}

	first = max(first, con_driver->first);
	last = min(last, con_driver->last);

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

		if (con_is_visible(vc)) {
			k = i;
			save_screen(vc);
		}

		old_was_color = vc->vc_can_do_color;
		vc->vc_sw->con_deinit(vc);
		vc->vc_origin = (unsigned long)vc->vc_screenbuf;
		visual_init(vc, i, 0);
		set_origin(vc);
		update_attr(vc);

		/* If the console changed between mono <-> color, then
		 * the attributes in the screenbuf will be wrong.  The
		 * following resets all attributes to something sane.
		 */
		if (old_was_color != vc->vc_can_do_color)
			clear_buffer_attributes(vc);
	}

	pr_info("Console: switching ");
	if (!deflt)
		pr_cont("consoles %d-%d ", first + 1, last + 1);
	if (j >= 0) {
		struct vc_data *vc = vc_cons[j].d;

		pr_cont("to %s %s %dx%d\n",
			vc->vc_can_do_color ? "colour" : "mono",
			desc, vc->vc_cols, vc->vc_rows);

		if (k >= 0) {
			vc = vc_cons[k].d;
			update_screen(vc);
		}
	} else {
		pr_cont("to %s\n", desc);
	}

	retval = 0;
err:
	module_put(owner);
	return retval;
};


#ifdef CONFIG_VT_HW_CONSOLE_BINDING
/* unlocked version of unbind_con_driver() */
int do_unbind_con_driver(const struct consw *csw, int first, int last, int deflt)
{
	struct module *owner = csw->owner;
	const struct consw *defcsw = NULL;
	struct con_driver *con_driver = NULL, *con_back = NULL;
	int i, retval = -ENODEV;

	if (!try_module_get(owner))
		return -ENODEV;

	WARN_CONSOLE_UNLOCKED();

	/* check if driver is registered and if it is unbindable */
	for (i = 0; i < MAX_NR_CON_DRIVER; i++) {
		con_driver = &registered_con_driver[i];

		if (con_driver->con == csw &&
		    con_driver->flag & CON_DRIVER_FLAG_MODULE) {
			retval = 0;
			break;
		}
	}

	if (retval)
		goto err;

	retval = -ENODEV;

	/* check if backup driver exists */
	for (i = 0; i < MAX_NR_CON_DRIVER; i++) {
		con_back = &registered_con_driver[i];

		if (con_back->con && con_back->con != csw) {
			defcsw = con_back->con;
			retval = 0;
			break;
		}
	}

	if (retval)
		goto err;

	if (!con_is_bound(csw))
		goto err;

	first = max(first, con_driver->first);
	last = min(last, con_driver->last);

	for (i = first; i <= last; i++) {
		if (con_driver_map[i] == csw) {
			module_put(csw->owner);
			con_driver_map[i] = NULL;
		}
	}

	if (!con_is_bound(defcsw)) {
		const struct consw *defconsw = conswitchp;

		defcsw->con_startup();
		con_back->flag |= CON_DRIVER_FLAG_INIT;
		/*
		 * vgacon may change the default driver to point
		 * to dummycon, we restore it here...
		 */
		conswitchp = defconsw;
	}

	if (!con_is_bound(csw))
		con_driver->flag &= ~CON_DRIVER_FLAG_INIT;

	/* ignore return value, binding should not fail */
	do_bind_con_driver(defcsw, first, last, deflt);
err:
	module_put(owner);
	return retval;

}
EXPORT_SYMBOL_GPL(do_unbind_con_driver);

static int vt_bind(struct con_driver *con)
{
	const struct consw *defcsw = NULL, *csw = NULL;
	int i, more = 1, first = -1, last = -1, deflt = 0;

 	if (!con->con || !(con->flag & CON_DRIVER_FLAG_MODULE))
		goto err;

	csw = con->con;

	for (i = 0; i < MAX_NR_CON_DRIVER; i++) {
		struct con_driver *con = &registered_con_driver[i];

		if (con->con && !(con->flag & CON_DRIVER_FLAG_MODULE)) {
			defcsw = con->con;
			break;
		}
	}

	if (!defcsw)
		goto err;

	while (more) {
		more = 0;

		for (i = con->first; i <= con->last; i++) {
			if (con_driver_map[i] == defcsw) {
				if (first == -1)
					first = i;
				last = i;
				more = 1;
			} else if (first != -1)
				break;
		}

		if (first == 0 && last == MAX_NR_CONSOLES -1)
			deflt = 1;

		if (first != -1)
			do_bind_con_driver(csw, first, last, deflt);

		first = -1;
		last = -1;
		deflt = 0;
	}

err:
	return 0;
}

static int vt_unbind(struct con_driver *con)
{
	const struct consw *csw = NULL;
	int i, more = 1, first = -1, last = -1, deflt = 0;
	int ret;

 	if (!con->con || !(con->flag & CON_DRIVER_FLAG_MODULE))
		goto err;

	csw = con->con;

	while (more) {
		more = 0;

		for (i = con->first; i <= con->last; i++) {
			if (con_driver_map[i] == csw) {
				if (first == -1)
					first = i;
				last = i;
				more = 1;
			} else if (first != -1)
				break;
		}

		if (first == 0 && last == MAX_NR_CONSOLES -1)
			deflt = 1;

		if (first != -1) {
			ret = do_unbind_con_driver(csw, first, last, deflt);
			if (ret != 0)
				return ret;
		}

		first = -1;
		last = -1;
		deflt = 0;
	}

err:
	return 0;
}
#else
static inline int vt_bind(struct con_driver *con)
{
	return 0;
}
static inline int vt_unbind(struct con_driver *con)
{
	return 0;
}
#endif /* CONFIG_VT_HW_CONSOLE_BINDING */

static ssize_t store_bind(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct con_driver *con = dev_get_drvdata(dev);
	int bind = simple_strtoul(buf, NULL, 0);

	console_lock();

	if (bind)
		vt_bind(con);
	else
		vt_unbind(con);

	console_unlock();

	return count;
}

static ssize_t show_bind(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct con_driver *con = dev_get_drvdata(dev);
	int bind = con_is_bound(con->con);

	return snprintf(buf, PAGE_SIZE, "%i\n", bind);
}

static ssize_t show_name(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct con_driver *con = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s %s\n",
			(con->flag & CON_DRIVER_FLAG_MODULE) ? "(M)" : "(S)",
			 con->desc);

}

static DEVICE_ATTR(bind, S_IRUGO|S_IWUSR, show_bind, store_bind);
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static struct attribute *con_dev_attrs[] = {
	&dev_attr_bind.attr,
	&dev_attr_name.attr,
	NULL
};

ATTRIBUTE_GROUPS(con_dev);

static int vtconsole_init_device(struct con_driver *con)
{
	con->flag |= CON_DRIVER_FLAG_ATTR;
	return 0;
}

static void vtconsole_deinit_device(struct con_driver *con)
{
	con->flag &= ~CON_DRIVER_FLAG_ATTR;
}

/**
 * con_is_bound - checks if driver is bound to the console
 * @csw: console driver
 *
 * RETURNS: zero if unbound, nonzero if bound
 *
 * Drivers can call this and if zero, they should release
 * all resources allocated on con_startup()
 */
int con_is_bound(const struct consw *csw)
{
	int i, bound = 0;

	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		if (con_driver_map[i] == csw) {
			bound = 1;
			break;
		}
	}

	return bound;
}
EXPORT_SYMBOL(con_is_bound);

/**
 * con_debug_enter - prepare the console for the kernel debugger
 * @sw: console driver
 *
 * Called when the console is taken over by the kernel debugger, this
 * function needs to save the current console state, then put the console
 * into a state suitable for the kernel debugger.
 *
 * RETURNS:
 * Zero on success, nonzero if a failure occurred when trying to prepare
 * the console for the debugger.
 */
int con_debug_enter(struct vc_data *vc)
{
	int ret = 0;

	saved_fg_console = fg_console;
	saved_last_console = last_console;
	saved_want_console = want_console;
	saved_vc_mode = vc->vc_mode;
	saved_console_blanked = console_blanked;
	vc->vc_mode = KD_TEXT;
	console_blanked = 0;
	if (vc->vc_sw->con_debug_enter)
		ret = vc->vc_sw->con_debug_enter(vc);
#ifdef CONFIG_KGDB_KDB
	/* Set the initial LINES variable if it is not already set */
	if (vc->vc_rows < 999) {
		int linecount;
		char lns[4];
		const char *setargs[3] = {
			"set",
			"LINES",
			lns,
		};
		if (kdbgetintenv(setargs[0], &linecount)) {
			snprintf(lns, 4, "%i", vc->vc_rows);
			kdb_set(2, setargs);
		}
	}
	if (vc->vc_cols < 999) {
		int colcount;
		char cols[4];
		const char *setargs[3] = {
			"set",
			"COLUMNS",
			cols,
		};
		if (kdbgetintenv(setargs[0], &colcount)) {
			snprintf(cols, 4, "%i", vc->vc_cols);
			kdb_set(2, setargs);
		}
	}
#endif /* CONFIG_KGDB_KDB */
	return ret;
}
EXPORT_SYMBOL_GPL(con_debug_enter);

/**
 * con_debug_leave - restore console state
 * @sw: console driver
 *
 * Restore the console state to what it was before the kernel debugger
 * was invoked.
 *
 * RETURNS:
 * Zero on success, nonzero if a failure occurred when trying to restore
 * the console.
 */
int con_debug_leave(void)
{
	struct vc_data *vc;
	int ret = 0;

	fg_console = saved_fg_console;
	last_console = saved_last_console;
	want_console = saved_want_console;
	console_blanked = saved_console_blanked;
	vc_cons[fg_console].d->vc_mode = saved_vc_mode;

	vc = vc_cons[fg_console].d;
	if (vc->vc_sw->con_debug_leave)
		ret = vc->vc_sw->con_debug_leave(vc);
	return ret;
}
EXPORT_SYMBOL_GPL(con_debug_leave);

static int do_register_con_driver(const struct consw *csw, int first, int last)
{
	struct module *owner = csw->owner;
	struct con_driver *con_driver;
	const char *desc;
	int i, retval;

	WARN_CONSOLE_UNLOCKED();

	if (!try_module_get(owner))
		return -ENODEV;

	for (i = 0; i < MAX_NR_CON_DRIVER; i++) {
		con_driver = &registered_con_driver[i];

		/* already registered */
		if (con_driver->con == csw) {
			retval = -EBUSY;
			goto err;
		}
	}

	desc = csw->con_startup();
	if (!desc) {
		retval = -ENODEV;
		goto err;
	}

	retval = -EINVAL;

	for (i = 0; i < MAX_NR_CON_DRIVER; i++) {
		con_driver = &registered_con_driver[i];

		if (con_driver->con == NULL &&
		    !(con_driver->flag & CON_DRIVER_FLAG_ZOMBIE)) {
			con_driver->con = csw;
			con_driver->desc = desc;
			con_driver->node = i;
			con_driver->flag = CON_DRIVER_FLAG_MODULE |
			                   CON_DRIVER_FLAG_INIT;
			con_driver->first = first;
			con_driver->last = last;
			retval = 0;
			break;
		}
	}

	if (retval)
		goto err;

	con_driver->dev =
		device_create_with_groups(vtconsole_class, NULL,
					  MKDEV(0, con_driver->node),
					  con_driver, con_dev_groups,
					  "vtcon%i", con_driver->node);
	if (IS_ERR(con_driver->dev)) {
		pr_warn("Unable to create device for %s; errno = %ld\n",
			con_driver->desc, PTR_ERR(con_driver->dev));
		con_driver->dev = NULL;
	} else {
		vtconsole_init_device(con_driver);
	}

err:
	module_put(owner);
	return retval;
}


/**
 * do_unregister_con_driver - unregister console driver from console layer
 * @csw: console driver
 *
 * DESCRIPTION: All drivers that registers to the console layer must
 * call this function upon exit, or if the console driver is in a state
 * where it won't be able to handle console services, such as the
 * framebuffer console without loaded framebuffer drivers.
 *
 * The driver must unbind first prior to unregistration.
 */
int do_unregister_con_driver(const struct consw *csw)
{
	int i;

	/* cannot unregister a bound driver */
	if (con_is_bound(csw))
		return -EBUSY;

	if (csw == conswitchp)
		return -EINVAL;

	for (i = 0; i < MAX_NR_CON_DRIVER; i++) {
		struct con_driver *con_driver = &registered_con_driver[i];

		if (con_driver->con == csw) {
			/*
			 * Defer the removal of the sysfs entries since that
			 * will acquire the kernfs s_active lock and we can't
			 * acquire this lock while holding the console lock:
			 * the unbind sysfs entry imposes already the opposite
			 * order. Reset con already here to prevent any later
			 * lookup to succeed and mark this slot as zombie, so
			 * it won't get reused until we complete the removal
			 * in the deferred work.
			 */
			con_driver->con = NULL;
			con_driver->flag = CON_DRIVER_FLAG_ZOMBIE;
			schedule_work(&con_driver_unregister_work);

			return 0;
		}
	}

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(do_unregister_con_driver);

static void con_driver_unregister_callback(struct work_struct *ignored)
{
	int i;

	console_lock();

	for (i = 0; i < MAX_NR_CON_DRIVER; i++) {
		struct con_driver *con_driver = &registered_con_driver[i];

		if (!(con_driver->flag & CON_DRIVER_FLAG_ZOMBIE))
			continue;

		console_unlock();

		vtconsole_deinit_device(con_driver);
		device_destroy(vtconsole_class, MKDEV(0, con_driver->node));

		console_lock();

		if (WARN_ON_ONCE(con_driver->con))
			con_driver->con = NULL;
		con_driver->desc = NULL;
		con_driver->dev = NULL;
		con_driver->node = 0;
		WARN_ON_ONCE(con_driver->flag != CON_DRIVER_FLAG_ZOMBIE);
		con_driver->flag = 0;
		con_driver->first = 0;
		con_driver->last = 0;
	}

	console_unlock();
}

/*
 *	If we support more console drivers, this function is used
 *	when a driver wants to take over some existing consoles
 *	and become default driver for newly opened ones.
 *
 *	do_take_over_console is basically a register followed by unbind
 */
int do_take_over_console(const struct consw *csw, int first, int last, int deflt)
{
	int err;

	err = do_register_con_driver(csw, first, last);
	/*
	 * If we get an busy error we still want to bind the console driver
	 * and return success, as we may have unbound the console driver
	 * but not unregistered it.
	 */
	if (err == -EBUSY)
		err = 0;
	if (!err)
		do_bind_con_driver(csw, first, last, deflt);

	return err;
}
EXPORT_SYMBOL_GPL(do_take_over_console);


/*
 * give_up_console is a wrapper to unregister_con_driver. It will only
 * work if driver is fully unbound.
 */
void give_up_console(const struct consw *csw)
{
	console_lock();
	do_unregister_con_driver(csw);
	console_unlock();
}

static int __init vtconsole_class_init(void)
{
	int i;

	vtconsole_class = class_create(THIS_MODULE, "vtconsole");
	if (IS_ERR(vtconsole_class)) {
		pr_warn("Unable to create vt console class; errno = %ld\n",
			PTR_ERR(vtconsole_class));
		vtconsole_class = NULL;
	}

	/* Add system drivers to sysfs */
	for (i = 0; i < MAX_NR_CON_DRIVER; i++) {
		struct con_driver *con = &registered_con_driver[i];

		if (con->con && !con->dev) {
			con->dev =
				device_create_with_groups(vtconsole_class, NULL,
							  MKDEV(0, con->node),
							  con, con_dev_groups,
							  "vtcon%i", con->node);

			if (IS_ERR(con->dev)) {
				pr_warn("Unable to create device for %s; errno = %ld\n",
					con->desc, PTR_ERR(con->dev));
				con->dev = NULL;
			} else {
				vtconsole_init_device(con);
			}
		}
	}

	return 0;
}
postcore_initcall(vtconsole_class_init);

#endif

/*
 *	Screen blanking
 */

static int set_vesa_blanking(char __user *p)
{
	unsigned int mode;

	if (get_user(mode, p + 1))
		return -EFAULT;

	vesa_blank_mode = (mode < 4) ? mode : 0;
	return 0;
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

	/* entering graphics mode? */
	if (entering_gfx) {
		hide_cursor(vc);
		save_screen(vc);
		vc->vc_sw->con_blank(vc, -1, 1);
		console_blanked = fg_console + 1;
		blank_state = blank_off;
		set_origin(vc);
		return;
	}

	blank_state = blank_off;

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
	vt_event_post(VT_EVENT_BLANK, vc->vc_num, vc->vc_num);
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
		pr_warn("unblank_screen: tty %d not allocated ??\n",
			fg_console + 1);
		return;
	}
	vc = vc_cons[fg_console].d;
	/* Try to unblank in oops case too */
	if (vc->vc_mode != KD_TEXT && !vt_force_oops_output(vc))
		return; /* but leave console_blanked != 0 */

	if (blankinterval) {
		mod_timer(&console_timer, jiffies + (blankinterval * HZ));
		blank_state = blank_normal_wait;
	}

	console_blanked = 0;
	if (vc->vc_sw->con_blank(vc, 0, leaving_gfx) || vt_force_oops_output(vc))
		/* Low-level driver cannot restore -> do it ourselves */
		update_screen(vc);
	if (console_blank_hook)
		console_blank_hook(0);
	set_palette(vc);
	set_cursor(vc);
	vt_event_post(VT_EVENT_UNBLANK, vc->vc_num, vc->vc_num);
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
 * We defer the timer blanking to work queue so it can take the console mutex
 * (console operations can still happen at irq time, but only from printk which
 * has the console mutex. Not perfect yet, but better than no locking
 */
static void blank_screen_t(struct timer_list *unused)
{
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
		mod_timer(&console_timer, jiffies + (blankinterval * HZ));
		blank_state = blank_normal_wait;
	}
}

/*
 *	Palettes
 */

static void set_palette(struct vc_data *vc)
{
	WARN_CONSOLE_UNLOCKED();

	if (vc->vc_mode != KD_GRAPHICS && vc->vc_sw->con_set_palette)
		vc->vc_sw->con_set_palette(vc, color_table);
}

/*
 * Load palette into the DAC registers. arg points to a colour
 * map, 3 bytes per colour, 16 colours, range from 0 to 255.
 */

int con_set_cmap(unsigned char __user *arg)
{
	int i, j, k;
	unsigned char colormap[3*16];

	if (copy_from_user(colormap, arg, sizeof(colormap)))
		return -EFAULT;

	console_lock();
	for (i = k = 0; i < 16; i++) {
		default_red[i] = colormap[k++];
		default_grn[i] = colormap[k++];
		default_blu[i] = colormap[k++];
	}
	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		if (!vc_cons_allocated(i))
			continue;
		for (j = k = 0; j < 16; j++) {
			vc_cons[i].d->vc_palette[k++] = default_red[j];
			vc_cons[i].d->vc_palette[k++] = default_grn[j];
			vc_cons[i].d->vc_palette[k++] = default_blu[j];
		}
		set_palette(vc_cons[i].d);
	}
	console_unlock();

	return 0;
}

int con_get_cmap(unsigned char __user *arg)
{
	int i, k;
	unsigned char colormap[3*16];

	console_lock();
	for (i = k = 0; i < 16; i++) {
		colormap[k++] = default_red[i];
		colormap[k++] = default_grn[i];
		colormap[k++] = default_blu[i];
	}
	console_unlock();

	if (copy_to_user(arg, colormap, sizeof(colormap)))
		return -EFAULT;

	return 0;
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
 *  is up to the actual low-level console-driver convert data into its favorite
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

	if (op->data) {
		font.data = kmalloc(max_font_size, GFP_KERNEL);
		if (!font.data)
			return -ENOMEM;
	} else
		font.data = NULL;

	console_lock();
	if (vc->vc_mode != KD_TEXT)
		rc = -EINVAL;
	else if (vc->vc_sw->con_font_get)
		rc = vc->vc_sw->con_font_get(vc, &font);
	else
		rc = -ENOSYS;
	console_unlock();

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
	if (op->width <= 0 || op->width > 32 || op->height > 32)
		return -EINVAL;
	size = (op->width+7)/8 * 32 * op->charcount;
	if (size > max_font_size)
		return -ENOSPC;

	font.data = memdup_user(op->data, size);
	if (IS_ERR(font.data))
		return PTR_ERR(font.data);

	if (!op->height) {		/* Need to guess font height [compat] */
		int h, i;
		u8 *charmap = font.data;

		/*
		 * If from KDFONTOP ioctl, don't allow things which can be done
		 * in userland,so that we can get rid of this soon
		 */
		if (!(op->flags & KD_FONT_FLAG_OLD)) {
			kfree(font.data);
			return -EINVAL;
		}

		for (h = 32; h > 0; h--)
			for (i = 0; i < op->charcount; i++)
				if (charmap[32*i+h-1])
					goto nonzero;

		kfree(font.data);
		return -EINVAL;

	nonzero:
		op->height = h;
	}

	font.charcount = op->charcount;
	font.width = op->width;
	font.height = op->height;

	console_lock();
	if (vc->vc_mode != KD_TEXT)
		rc = -EINVAL;
	else if (vc->vc_sw->con_font_set)
		rc = vc->vc_sw->con_font_set(vc, &font, op->flags);
	else
		rc = -ENOSYS;
	console_unlock();
	kfree(font.data);
	return rc;
}

static int con_font_default(struct vc_data *vc, struct console_font_op *op)
{
	struct console_font font = {.width = op->width, .height = op->height};
	char name[MAX_FONT_NAME];
	char *s = name;
	int rc;


	if (!op->data)
		s = NULL;
	else if (strncpy_from_user(name, op->data, MAX_FONT_NAME - 1) < 0)
		return -EFAULT;
	else
		name[MAX_FONT_NAME - 1] = 0;

	console_lock();
	if (vc->vc_mode != KD_TEXT) {
		console_unlock();
		return -EINVAL;
	}
	if (vc->vc_sw->con_font_default)
		rc = vc->vc_sw->con_font_default(vc, &font, s);
	else
		rc = -ENOSYS;
	console_unlock();
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


	console_lock();
	if (vc->vc_mode != KD_TEXT)
		rc = -EINVAL;
	else if (!vc->vc_sw->con_font_copy)
		rc = -ENOSYS;
	else if (con < 0 || !vc_cons_allocated(con))
		rc = -ENOTTY;
	else if (con == vc->vc_num)	/* nothing to do */
		rc = 0;
	else
		rc = vc->vc_sw->con_font_copy(vc, con);
	console_unlock();
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
EXPORT_SYMBOL_GPL(screen_glyph);

u32 screen_glyph_unicode(struct vc_data *vc, int n)
{
	struct uni_screen *uniscr = get_vc_uniscr(vc);

	if (uniscr)
		return uniscr->lines[n / vc->vc_cols][n % vc->vc_cols];
	return inverse_translate(vc, screen_glyph(vc, n * 2), 1);
}
EXPORT_SYMBOL_GPL(screen_glyph_unicode);

/* used by vcs - note the word offset */
unsigned short *screen_pos(struct vc_data *vc, int w_offset, int viewed)
{
	return screenpos(vc, 2 * w_offset, viewed);
}
EXPORT_SYMBOL_GPL(screen_pos);

void getconsxy(struct vc_data *vc, unsigned char *p)
{
	p[0] = vc->vc_x;
	p[1] = vc->vc_y;
}

void putconsxy(struct vc_data *vc, unsigned char *p)
{
	hide_cursor(vc);
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

void vcs_scr_updated(struct vc_data *vc)
{
	notify_update(vc);
}

void vc_scrolldelta_helper(struct vc_data *c, int lines,
		unsigned int rolled_over, void *base, unsigned int size)
{
	unsigned long ubase = (unsigned long)base;
	ptrdiff_t scr_end = (void *)c->vc_scr_end - base;
	ptrdiff_t vorigin = (void *)c->vc_visible_origin - base;
	ptrdiff_t origin = (void *)c->vc_origin - base;
	int margin = c->vc_size_row * 4;
	int from, wrap, from_off, avail;

	/* Turn scrollback off */
	if (!lines) {
		c->vc_visible_origin = c->vc_origin;
		return;
	}

	/* Do we have already enough to allow jumping from 0 to the end? */
	if (rolled_over > scr_end + margin) {
		from = scr_end;
		wrap = rolled_over + c->vc_size_row;
	} else {
		from = 0;
		wrap = size;
	}

	from_off = (vorigin - from + wrap) % wrap + lines * c->vc_size_row;
	avail = (origin - from + wrap) % wrap;

	/* Only a little piece would be left? Show all incl. the piece! */
	if (avail < 2 * margin)
		margin = 0;
	if (from_off < margin)
		from_off = 0;
	if (from_off > avail - margin)
		from_off = avail;

	c->vc_visible_origin = ubase + (from + from_off) % wrap;
}
EXPORT_SYMBOL_GPL(vc_scrolldelta_helper);

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
EXPORT_SYMBOL(global_cursor_default);
#ifndef VT_SINGLE_DRIVER
EXPORT_SYMBOL(give_up_console);
#endif
