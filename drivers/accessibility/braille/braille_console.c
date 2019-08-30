// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Minimalistic braille device kernel support.
 *
 * By default, shows console messages on the braille device.
 * Pressing Insert switches to VC browsing.
 *
 *  Copyright (C) Samuel Thibault <samuel.thibault@ens-lyon.org>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/console.h>
#include <linux/notifier.h>

#include <linux/selection.h>
#include <linux/vt_kern.h>
#include <linux/consolemap.h>

#include <linux/keyboard.h>
#include <linux/kbd_kern.h>
#include <linux/input.h>

MODULE_AUTHOR("samuel.thibault@ens-lyon.org");
MODULE_DESCRIPTION("braille device");
MODULE_LICENSE("GPL");

/*
 * Braille device support part.
 */

/* Emit various sounds */
static bool sound;
module_param(sound, bool, 0);
MODULE_PARM_DESC(sound, "emit sounds");

static void beep(unsigned int freq)
{
	if (sound)
		kd_mksound(freq, HZ/10);
}

/* mini console */
#define WIDTH 40
#define BRAILLE_KEY KEY_INSERT
static u16 console_buf[WIDTH];
static int console_cursor;

/* mini view of VC */
static int vc_x, vc_y, lastvc_x, lastvc_y;

/* show console ? (or show VC) */
static int console_show = 1;
/* pending newline ? */
static int console_newline = 1;
static int lastVC = -1;

static struct console *braille_co;

/* Very VisioBraille-specific */
static void braille_write(u16 *buf)
{
	static u16 lastwrite[WIDTH];
	unsigned char data[1 + 1 + 2*WIDTH + 2 + 1], csum = 0, *c;
	u16 out;
	int i;

	if (!braille_co)
		return;

	if (!memcmp(lastwrite, buf, WIDTH * sizeof(*buf)))
		return;
	memcpy(lastwrite, buf, WIDTH * sizeof(*buf));

#define SOH 1
#define STX 2
#define ETX 2
#define EOT 4
#define ENQ 5
	data[0] = STX;
	data[1] = '>';
	csum ^= '>';
	c = &data[2];
	for (i = 0; i < WIDTH; i++) {
		out = buf[i];
		if (out >= 0x100)
			out = '?';
		else if (out == 0x00)
			out = ' ';
		csum ^= out;
		if (out <= 0x05) {
			*c++ = SOH;
			out |= 0x40;
		}
		*c++ = out;
	}

	if (csum <= 0x05) {
		*c++ = SOH;
		csum |= 0x40;
	}
	*c++ = csum;
	*c++ = ETX;

	braille_co->write(braille_co, data, c - data);
}

/* Follow the VC cursor*/
static void vc_follow_cursor(struct vc_data *vc)
{
	vc_x = vc->vc_x - (vc->vc_x % WIDTH);
	vc_y = vc->vc_y;
	lastvc_x = vc->vc_x;
	lastvc_y = vc->vc_y;
}

/* Maybe the VC cursor moved, if so follow it */
static void vc_maybe_cursor_moved(struct vc_data *vc)
{
	if (vc->vc_x != lastvc_x || vc->vc_y != lastvc_y)
		vc_follow_cursor(vc);
}

/* Show portion of VC at vc_x, vc_y */
static void vc_refresh(struct vc_data *vc)
{
	u16 buf[WIDTH];
	int i;

	for (i = 0; i < WIDTH; i++) {
		u16 glyph = screen_glyph(vc,
				2 * (vc_x + i) + vc_y * vc->vc_size_row);
		buf[i] = inverse_translate(vc, glyph, 1);
	}
	braille_write(buf);
}

/*
 * Link to keyboard
 */

static int keyboard_notifier_call(struct notifier_block *blk,
				  unsigned long code, void *_param)
{
	struct keyboard_notifier_param *param = _param;
	struct vc_data *vc = param->vc;
	int ret = NOTIFY_OK;

	if (!param->down)
		return ret;

	switch (code) {
	case KBD_KEYCODE:
		if (console_show) {
			if (param->value == BRAILLE_KEY) {
				console_show = 0;
				beep(880);
				vc_maybe_cursor_moved(vc);
				vc_refresh(vc);
				ret = NOTIFY_STOP;
			}
		} else {
			ret = NOTIFY_STOP;
			switch (param->value) {
			case KEY_INSERT:
				beep(440);
				console_show = 1;
				lastVC = -1;
				braille_write(console_buf);
				break;
			case KEY_LEFT:
				if (vc_x > 0) {
					vc_x -= WIDTH;
					if (vc_x < 0)
						vc_x = 0;
				} else if (vc_y >= 1) {
					beep(880);
					vc_y--;
					vc_x = vc->vc_cols-WIDTH;
				} else
					beep(220);
				break;
			case KEY_RIGHT:
				if (vc_x + WIDTH < vc->vc_cols) {
					vc_x += WIDTH;
				} else if (vc_y + 1 < vc->vc_rows) {
					beep(880);
					vc_y++;
					vc_x = 0;
				} else
					beep(220);
				break;
			case KEY_DOWN:
				if (vc_y + 1 < vc->vc_rows)
					vc_y++;
				else
					beep(220);
				break;
			case KEY_UP:
				if (vc_y >= 1)
					vc_y--;
				else
					beep(220);
				break;
			case KEY_HOME:
				vc_follow_cursor(vc);
				break;
			case KEY_PAGEUP:
				vc_x = 0;
				vc_y = 0;
				break;
			case KEY_PAGEDOWN:
				vc_x = 0;
				vc_y = vc->vc_rows-1;
				break;
			default:
				ret = NOTIFY_OK;
				break;
			}
			if (ret == NOTIFY_STOP)
				vc_refresh(vc);
		}
		break;
	case KBD_POST_KEYSYM:
	{
		unsigned char type = KTYP(param->value) - 0xf0;
		if (type == KT_SPEC) {
			unsigned char val = KVAL(param->value);
			int on_off = -1;

			switch (val) {
			case KVAL(K_CAPS):
				on_off = vt_get_leds(fg_console, VC_CAPSLOCK);
				break;
			case KVAL(K_NUM):
				on_off = vt_get_leds(fg_console, VC_NUMLOCK);
				break;
			case KVAL(K_HOLD):
				on_off = vt_get_leds(fg_console, VC_SCROLLOCK);
				break;
			}
			if (on_off == 1)
				beep(880);
			else if (on_off == 0)
				beep(440);
		}
	}
	case KBD_UNBOUND_KEYCODE:
	case KBD_UNICODE:
	case KBD_KEYSYM:
		/* Unused */
		break;
	}
	return ret;
}

static struct notifier_block keyboard_notifier_block = {
	.notifier_call = keyboard_notifier_call,
};

static int vt_notifier_call(struct notifier_block *blk,
			    unsigned long code, void *_param)
{
	struct vt_notifier_param *param = _param;
	struct vc_data *vc = param->vc;
	switch (code) {
	case VT_ALLOCATE:
		break;
	case VT_DEALLOCATE:
		break;
	case VT_WRITE:
	{
		unsigned char c = param->c;
		if (vc->vc_num != fg_console)
			break;
		switch (c) {
		case '\b':
		case 127:
			if (console_cursor > 0) {
				console_cursor--;
				console_buf[console_cursor] = ' ';
			}
			break;
		case '\n':
		case '\v':
		case '\f':
		case '\r':
			console_newline = 1;
			break;
		case '\t':
			c = ' ';
			/* Fallthrough */
		default:
			if (c < 32)
				/* Ignore other control sequences */
				break;
			if (console_newline) {
				memset(console_buf, 0, sizeof(console_buf));
				console_cursor = 0;
				console_newline = 0;
			}
			if (console_cursor == WIDTH)
				memmove(console_buf, &console_buf[1],
					(WIDTH-1) * sizeof(*console_buf));
			else
				console_cursor++;
			console_buf[console_cursor-1] = c;
			break;
		}
		if (console_show)
			braille_write(console_buf);
		else {
			vc_maybe_cursor_moved(vc);
			vc_refresh(vc);
		}
		break;
	}
	case VT_UPDATE:
		/* Maybe a VT switch, flush */
		if (console_show) {
			if (vc->vc_num != lastVC) {
				lastVC = vc->vc_num;
				memset(console_buf, 0, sizeof(console_buf));
				console_cursor = 0;
				braille_write(console_buf);
			}
		} else {
			vc_maybe_cursor_moved(vc);
			vc_refresh(vc);
		}
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block vt_notifier_block = {
	.notifier_call = vt_notifier_call,
};

/*
 * Called from printk.c when console=brl is given
 */

int braille_register_console(struct console *console, int index,
		char *console_options, char *braille_options)
{
	int ret;

	if (!(console->flags & CON_BRL))
		return 0;
	if (!console_options)
		/* Only support VisioBraille for now */
		console_options = "57600o8";
	if (braille_co)
		return -ENODEV;
	if (console->setup) {
		ret = console->setup(console, console_options);
		if (ret != 0)
			return ret;
	}
	console->flags |= CON_ENABLED;
	console->index = index;
	braille_co = console;
	register_keyboard_notifier(&keyboard_notifier_block);
	register_vt_notifier(&vt_notifier_block);
	return 1;
}

int braille_unregister_console(struct console *console)
{
	if (braille_co != console)
		return -EINVAL;
	if (!(console->flags & CON_BRL))
		return 0;
	unregister_keyboard_notifier(&keyboard_notifier_block);
	unregister_vt_notifier(&vt_notifier_block);
	braille_co = NULL;
	return 1;
}
