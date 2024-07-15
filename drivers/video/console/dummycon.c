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

#if defined(CONFIG_ARCH_FOOTBRIDGE) && defined(CONFIG_VGA_CONSOLE)
#include <asm/vga.h>
#define DUMMY_COLUMNS	vgacon_screen_info.orig_video_cols
#define DUMMY_ROWS	vgacon_screen_info.orig_video_lines
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
	WARN_CONSOLE_UNLOCKED();

	raw_notifier_chain_register(&dummycon_output_nh, nb);

	if (dummycon_putc_called)
		nb->notifier_call(nb, 0, NULL);
}

void dummycon_unregister_output_notifier(struct notifier_block *nb)
{
	WARN_CONSOLE_UNLOCKED();

	raw_notifier_chain_unregister(&dummycon_output_nh, nb);
}

static void dummycon_putc(struct vc_data *vc, u16 c, unsigned int y,
                          unsigned int x)
{
	WARN_CONSOLE_UNLOCKED();

	dummycon_putc_called = true;
	raw_notifier_call_chain(&dummycon_output_nh, 0, NULL);
}

static void dummycon_putcs(struct vc_data *vc, const u16 *s, unsigned int count,
			   unsigned int ypos, unsigned int xpos)
{
	unsigned int i;

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

static bool dummycon_blank(struct vc_data *vc, enum vesa_blank_mode blank,
			   bool mode_switch)
{
	/* Redraw, so that we get putc(s) for output done while blanked */
	return true;
}
#else
static void dummycon_putc(struct vc_data *vc, u16 c, unsigned int y,
			  unsigned int x) { }
static void dummycon_putcs(struct vc_data *vc, const u16 *s, unsigned int count,
			   unsigned int ypos, unsigned int xpos) { }
static bool dummycon_blank(struct vc_data *vc, enum vesa_blank_mode blank,
			   bool mode_switch)
{
	return false;
}
#endif

static const char *dummycon_startup(void)
{
    return "dummy device";
}

static void dummycon_init(struct vc_data *vc, bool init)
{
    vc->vc_can_do_color = 1;
    if (init) {
	vc->vc_cols = DUMMY_COLUMNS;
	vc->vc_rows = DUMMY_ROWS;
    } else
	vc_resize(vc, DUMMY_COLUMNS, DUMMY_ROWS);
}

static void dummycon_deinit(struct vc_data *vc) { }
static void dummycon_clear(struct vc_data *vc, unsigned int sy, unsigned int sx,
			   unsigned int width) { }
static void dummycon_cursor(struct vc_data *vc, bool enable) { }

static bool dummycon_scroll(struct vc_data *vc, unsigned int top,
			    unsigned int bottom, enum con_scroll dir,
			    unsigned int lines)
{
	return false;
}

static bool dummycon_switch(struct vc_data *vc)
{
	return false;
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
};
EXPORT_SYMBOL_GPL(dummy_con);
