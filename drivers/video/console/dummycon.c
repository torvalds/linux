// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/video/dummycon.c -- A dummy console driver
 *
 *  To be used if there's no other console driver (e.g. for plain VGA text)
 *  available, usually until fbcon takes console over.
 */

#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/console.h>
#include <linux/vt_kern.h>
#include <linux/screen_info.h>
#include <linux/init.h>
#include <linux/module.h>

/*
 *  Dummy console driver
 */

#if defined(__arm__)
#define DUMMY_COLUMNS	screen_info.orig_video_cols
#define DUMMY_ROWS	screen_info.orig_video_lines
#else
/* set by Kconfig. Use 80x25 for 640x480 and 160x64 for 1280x1024 */
#define DUMMY_COLUMNS	CONFIG_DUMMY_CONSOLE_COLUMNS
#define DUMMY_ROWS	CONFIG_DUMMY_CONSOLE_ROWS
#endif

#ifdef CONFIG_FRAMEBUFFER_CONSOLE_DEFERRED_TAKEOVER
/* These are both protected by the console_lock */
static RAW_NOTIFIER_HEAD(dummycon_output_nh);
static bool dummycon_putc_called;

void dummycon_register_output_notifier(struct notifier_block *nb)
{
	raw_notifier_chain_register(&dummycon_output_nh, nb);

	if (dummycon_putc_called)
		nb->notifier_call(nb, 0, NULL);
}

void dummycon_unregister_output_notifier(struct notifier_block *nb)
{
	raw_notifier_chain_unregister(&dummycon_output_nh, nb);
}

static void dummycon_putc(struct vc_data *vc, int c, int ypos, int xpos)
{
	dummycon_putc_called = true;
	raw_notifier_call_chain(&dummycon_output_nh, 0, NULL);
}

static void dummycon_putcs(struct vc_data *vc, const unsigned short *s,
			   int count, int ypos, int xpos)
{
	int i;

	if (!dummycon_putc_called) {
		/* Ignore erases */
		for (i = 0 ; i < count; i++) {
			if (s[i] != vc->vc_video_erase_char)
				break;
		}
		if (i == count)
			return;

		dummycon_putc_called = true;
	}

	raw_notifier_call_chain(&dummycon_output_nh, 0, NULL);
}

static int dummycon_blank(struct vc_data *vc, int blank, int mode_switch)
{
	/* Redraw, so that we get putc(s) for output done while blanked */
	return 1;
}
#else
static void dummycon_putc(struct vc_data *vc, int c, int ypos, int xpos) { }
static void dummycon_putcs(struct vc_data *vc, const unsigned short *s,
			   int count, int ypos, int xpos) { }
static int dummycon_blank(struct vc_data *vc, int blank, int mode_switch)
{
	return 0;
}
#endif

static const char *dummycon_startup(void)
{
    return "dummy device";
}

static void dummycon_init(struct vc_data *vc, int init)
{
    vc->vc_can_do_color = 1;
    if (init) {
	vc->vc_cols = DUMMY_COLUMNS;
	vc->vc_rows = DUMMY_ROWS;
    } else
	vc_resize(vc, DUMMY_COLUMNS, DUMMY_ROWS);
}

static void dummycon_deinit(struct vc_data *vc) { }
static void dummycon_clear(struct vc_data *vc, int sy, int sx, int height,
			   int width) { }
static void dummycon_cursor(struct vc_data *vc, int mode) { }

static bool dummycon_scroll(struct vc_data *vc, unsigned int top,
			    unsigned int bottom, enum con_scroll dir,
			    unsigned int lines)
{
	return false;
}

static int dummycon_switch(struct vc_data *vc)
{
	return 0;
}

static int dummycon_font_set(struct vc_data *vc, struct console_font *font,
			     unsigned int flags)
{
	return 0;
}

static int dummycon_font_default(struct vc_data *vc,
				 struct console_font *font, char *name)
{
	return 0;
}

static int dummycon_font_copy(struct vc_data *vc, int con)
{
	return 0;
}

/*
 *  The console `switch' structure for the dummy console
 *
 *  Most of the operations are dummies.
 */

const struct consw dummy_con = {
	.owner =		THIS_MODULE,
	.con_startup =	dummycon_startup,
	.con_init =		dummycon_init,
	.con_deinit =	dummycon_deinit,
	.con_clear =	dummycon_clear,
	.con_putc =		dummycon_putc,
	.con_putcs =	dummycon_putcs,
	.con_cursor =	dummycon_cursor,
	.con_scroll =	dummycon_scroll,
	.con_switch =	dummycon_switch,
	.con_blank =	dummycon_blank,
	.con_font_set =	dummycon_font_set,
	.con_font_default =	dummycon_font_default,
	.con_font_copy =	dummycon_font_copy,
};
EXPORT_SYMBOL_GPL(dummy_con);
