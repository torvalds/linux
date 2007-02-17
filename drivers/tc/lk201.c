/*
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999-2002 Harald Koerfgen <hkoerfg@web.de>
 * Copyright (C) 2001, 2002, 2003, 2004  Maciej W. Rozycki
 */


#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/kbd_ll.h>
#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>

#include <asm/keyboard.h>
#include <asm/dec/tc.h>
#include <asm/dec/machtype.h>
#include <asm/dec/serial.h>

#include "lk201.h"

/*
 * Only handle DECstations that have an LK201 interface.
 * Maxine uses LK501 at the Access.Bus and various DECsystems
 * have no keyboard interface at all.
 */
#define LK_IFACE	(mips_machtype == MACH_DS23100    || \
			 mips_machtype == MACH_DS5000_200 || \
			 mips_machtype == MACH_DS5000_1XX || \
			 mips_machtype == MACH_DS5000_2X0)
/*
 * These use the Z8530 SCC.  Others use the DZ11.
 */
#define LK_IFACE_ZS	(mips_machtype == MACH_DS5000_1XX || \
			 mips_machtype == MACH_DS5000_2X0)

/* Simple translation table for the SysRq keys */

#ifdef CONFIG_MAGIC_SYSRQ
/*
 * Actually no translation at all, at least until we figure out
 * how to define SysRq for LK201 and friends. --macro
 */
unsigned char lk201_sysrq_xlate[128];
unsigned char *kbd_sysrq_xlate = lk201_sysrq_xlate;

unsigned char kbd_sysrq_key = -1;
#endif

#define KEYB_LINE	3

static int __init lk201_init(void *);
static void __init lk201_info(void *);
static void lk201_rx_char(unsigned char, unsigned char);

static struct dec_serial_hook lk201_hook = {
	.init_channel	= lk201_init,
	.init_info	= lk201_info,
	.rx_char	= NULL,
	.poll_rx_char	= NULL,
	.poll_tx_char	= NULL,
	.cflags		= B4800 | CS8 | CSTOPB | CLOCAL,
};

/*
 * This is used during keyboard initialisation
 */
static unsigned char lk201_reset_string[] = {
	LK_CMD_SET_DEFAULTS,
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 1),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 2),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 3),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 4),
	LK_CMD_MODE(LK_MODE_DOWN_UP, 5),
	LK_CMD_MODE(LK_MODE_DOWN_UP, 6),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 7),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 8),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 9),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 10),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 11),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 12),
	LK_CMD_MODE(LK_MODE_DOWN, 13),
	LK_CMD_MODE(LK_MODE_RPT_DOWN, 14),
	LK_CMD_DIS_KEYCLK,
	LK_CMD_ENB_BELL, LK_PARAM_VOLUME(4),
};

static void *lk201_handle;

static int lk201_send(unsigned char ch)
{
	if (lk201_hook.poll_tx_char(lk201_handle, ch)) {
		printk(KERN_ERR "lk201: transmit timeout\n");
		return -EIO;
	}
	return 0;
}

static inline int lk201_get_id(void)
{
	return lk201_send(LK_CMD_REQ_ID);
}

static int lk201_reset(void)
{
	int i, r;

	for (i = 0; i < sizeof(lk201_reset_string); i++) {
		r = lk201_send(lk201_reset_string[i]);
		if (r < 0)
			return r;
	}
	return 0;
}

static void lk201_report(unsigned char id[6])
{
	char *report = "lk201: keyboard attached, ";

	switch (id[2]) {
	case LK_STAT_PWRUP_OK:
		printk(KERN_INFO "%sself-test OK\n", report);
		break;
	case LK_STAT_PWRUP_KDOWN:
		/* The keyboard will resend the power-up ID
		   after all keys are released, so we don't
		   bother handling the error specially.  Still
		   there may be a short-circuit inside.
		 */
		printk(KERN_ERR "%skey down (stuck?), code: 0x%02x\n",
		       report, id[3]);
		break;
	case LK_STAT_PWRUP_ERROR:
		printk(KERN_ERR "%sself-test failure\n", report);
		break;
	default:
		printk(KERN_ERR "%sunknown error: 0x%02x\n",
		       report, id[2]);
	}
}

static void lk201_id(unsigned char id[6])
{
	/*
	 * Report whether there is an LK201 or an LK401
	 * The LK401 has ALT keys...
	 */
	switch (id[4]) {
	case 1:
		printk(KERN_INFO "lk201: LK201 detected\n");
		break;
	case 2:
		printk(KERN_INFO "lk201: LK401 detected\n");
		break;
	case 3:
		printk(KERN_INFO "lk201: LK443 detected\n");
		break;
	case 4:
		printk(KERN_INFO "lk201: LK421 detected\n");
		break;
	default:
		printk(KERN_WARNING
		       "lk201: unknown keyboard detected, ID %d\n", id[4]);
		printk(KERN_WARNING "lk201: ... please report to "
		       "<linux-mips@linux-mips.org>\n");
	}
}

#define DEFAULT_KEYB_REP_DELAY	(250/5)	/* [5ms] */
#define DEFAULT_KEYB_REP_RATE	30	/* [cps] */

static struct kbd_repeat kbdrate = {
	DEFAULT_KEYB_REP_DELAY,
	DEFAULT_KEYB_REP_RATE
};

static void parse_kbd_rate(struct kbd_repeat *r)
{
	if (r->delay <= 0)
		r->delay = kbdrate.delay;
	if (r->rate <= 0)
		r->rate = kbdrate.rate;

	if (r->delay < 5)
		r->delay = 5;
	if (r->delay > 630)
		r->delay = 630;
	if (r->rate < 12)
		r->rate = 12;
	if (r->rate > 127)
		r->rate = 127;
	if (r->rate == 125)
		r->rate = 124;
}

static int write_kbd_rate(struct kbd_repeat *rep)
{
	int delay, rate;
	int i;

	delay = rep->delay / 5;
	rate = rep->rate;
	for (i = 0; i < 4; i++) {
		if (lk201_hook.poll_tx_char(lk201_handle,
					    LK_CMD_RPT_RATE(i)))
			return 1;
		if (lk201_hook.poll_tx_char(lk201_handle,
					    LK_PARAM_DELAY(delay)))
			return 1;
		if (lk201_hook.poll_tx_char(lk201_handle,
					    LK_PARAM_RATE(rate)))
			return 1;
	}
	return 0;
}

static int lk201_kbd_rate(struct kbd_repeat *rep)
{
	if (rep == NULL)
		return -EINVAL;

	parse_kbd_rate(rep);

	if (write_kbd_rate(rep)) {
		memcpy(rep, &kbdrate, sizeof(struct kbd_repeat));
		return -EIO;
	}

	memcpy(&kbdrate, rep, sizeof(struct kbd_repeat));

	return 0;
}

static void lk201_kd_mksound(unsigned int hz, unsigned int ticks)
{
	if (!ticks)
		return;

	/*
	 * Can't set frequency and we "approximate"
	 * duration by volume. ;-)
	 */
	ticks /= HZ / 32;
	if (ticks > 7)
		ticks = 7;
	ticks = 7 - ticks;

	if (lk201_hook.poll_tx_char(lk201_handle, LK_CMD_ENB_BELL))
		return;
	if (lk201_hook.poll_tx_char(lk201_handle, LK_PARAM_VOLUME(ticks)))
		return;
	if (lk201_hook.poll_tx_char(lk201_handle, LK_CMD_BELL))
		return;
}

void kbd_leds(unsigned char leds)
{
	unsigned char l = 0;

	if (!lk201_handle)		/* FIXME */
		return;

	/* FIXME -- Only Hold and Lock LEDs for now. --macro */
	if (leds & LED_SCR)
		l |= LK_LED_HOLD;
	if (leds & LED_CAP)
		l |= LK_LED_LOCK;

	if (lk201_hook.poll_tx_char(lk201_handle, LK_CMD_LEDS_ON))
		return;
	if (lk201_hook.poll_tx_char(lk201_handle, LK_PARAM_LED_MASK(l)))
		return;
	if (lk201_hook.poll_tx_char(lk201_handle, LK_CMD_LEDS_OFF))
		return;
	if (lk201_hook.poll_tx_char(lk201_handle, LK_PARAM_LED_MASK(~l)))
		return;
}

int kbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	return -EINVAL;
}

int kbd_getkeycode(unsigned int scancode)
{
	return -EINVAL;
}

int kbd_translate(unsigned char scancode, unsigned char *keycode,
		  char raw_mode)
{
	*keycode = scancode;
	return 1;
}

char kbd_unexpected_up(unsigned char keycode)
{
	return 0x80;
}

static void lk201_rx_char(unsigned char ch, unsigned char fl)
{
	static unsigned char id[6];
	static int id_i;

	static int shift_state = 0;
	static int prev_scancode;
	unsigned char c = scancodeRemap[ch];

	if (fl != TTY_NORMAL && fl != TTY_OVERRUN) {
		printk(KERN_ERR "lk201: keyboard receive error: 0x%02x\n", fl);
		return;
	}

	/* Assume this is a power-up ID. */
	if (ch == LK_STAT_PWRUP_ID && !id_i) {
		id[id_i++] = ch;
		return;
	}

	/* Handle the power-up sequence. */
	if (id_i) {
		id[id_i++] = ch;
		if (id_i == 4) {
			/* OK, the power-up concluded. */
			lk201_report(id);
			if (id[2] == LK_STAT_PWRUP_OK)
				lk201_get_id();
			else {
				id_i = 0;
				printk(KERN_ERR "lk201: keyboard power-up "
				       "error, skipping initialization\n");
			}
		} else if (id_i == 6) {
			/* We got the ID; report it and start operation. */
			id_i = 0;
			lk201_id(id);
			lk201_reset();
		}
		return;
	}

	/* Everything else is a scancode/status response. */
	id_i = 0;
	switch (ch) {
	case LK_STAT_RESUME_ERR:
	case LK_STAT_ERROR:
	case LK_STAT_INHIBIT_ACK:
	case LK_STAT_TEST_ACK:
	case LK_STAT_MODE_KEYDOWN:
	case LK_STAT_MODE_ACK:
		break;
	case LK_KEY_LOCK:
		shift_state ^= LK_LOCK;
		handle_scancode(c, (shift_state & LK_LOCK) ? 1 : 0);
		break;
	case LK_KEY_SHIFT:
		shift_state ^= LK_SHIFT;
		handle_scancode(c, (shift_state & LK_SHIFT) ? 1 : 0);
		break;
	case LK_KEY_CTRL:
		shift_state ^= LK_CTRL;
		handle_scancode(c, (shift_state & LK_CTRL) ? 1 : 0);
		break;
	case LK_KEY_COMP:
		shift_state ^= LK_COMP;
		handle_scancode(c, (shift_state & LK_COMP) ? 1 : 0);
		break;
	case LK_KEY_RELEASE:
		if (shift_state & LK_SHIFT)
			handle_scancode(scancodeRemap[LK_KEY_SHIFT], 0);
		if (shift_state & LK_CTRL)
			handle_scancode(scancodeRemap[LK_KEY_CTRL], 0);
		if (shift_state & LK_COMP)
			handle_scancode(scancodeRemap[LK_KEY_COMP], 0);
		if (shift_state & LK_LOCK)
			handle_scancode(scancodeRemap[LK_KEY_LOCK], 0);
		shift_state = 0;
		break;
	case LK_KEY_REPEAT:
		handle_scancode(prev_scancode, 1);
		break;
	default:
		prev_scancode = c;
		handle_scancode(c, 1);
		break;
	}
	tasklet_schedule(&keyboard_tasklet);
}

static void __init lk201_info(void *handle)
{
}

static int __init lk201_init(void *handle)
{
	/* First install handlers. */
	lk201_handle = handle;
	kbd_rate = lk201_kbd_rate;
	kd_mksound = lk201_kd_mksound;

	lk201_hook.rx_char = lk201_rx_char;

	/* Then just issue a reset -- the handlers will do the rest. */
	lk201_send(LK_CMD_POWER_UP);

	return 0;
}

void __init kbd_init_hw(void)
{
	/* Maxine uses LK501 at the Access.Bus. */
	if (!LK_IFACE)
		return;

	printk(KERN_INFO "lk201: DECstation LK keyboard driver v0.05.\n");

	if (LK_IFACE_ZS) {
		/*
		 * kbd_init_hw() is being called before
		 * rs_init() so just register the kbd hook
		 * and let zs_init do the rest :-)
		 */
		if (!register_dec_serial_hook(KEYB_LINE, &lk201_hook))
			unregister_dec_serial_hook(KEYB_LINE);
	} else {
		/*
		 * TODO: modify dz.c to allow similar hooks
		 * for LK201 handling on DS2100, DS3100, and DS5000/200
		 */
		printk(KERN_ERR "lk201: support for DZ11 not yet ready.\n");
	}
}
