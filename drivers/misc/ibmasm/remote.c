/*
 * IBM ASM Service Processor Device Driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2004
 *
 * Authors: Max Asböck <amax@us.ibm.com>
 *          Vernon Mauery <vernux@us.ibm.com>
 *
 */

/* Remote mouse and keyboard event handling functions */

#include <linux/pci.h>
#include "ibmasm.h"
#include "remote.h"

static int xmax = 1600;
static int ymax = 1200;


static unsigned short xlate_high[XLATE_SIZE] = {
	[KEY_SYM_ENTER & 0xff] = KEY_ENTER,
	[KEY_SYM_KPSLASH & 0xff] = KEY_KPSLASH,
	[KEY_SYM_KPSTAR & 0xff] = KEY_KPASTERISK,
	[KEY_SYM_KPMINUS & 0xff] = KEY_KPMINUS,
	[KEY_SYM_KPDOT & 0xff] = KEY_KPDOT,
	[KEY_SYM_KPPLUS & 0xff] = KEY_KPPLUS,
	[KEY_SYM_KP0 & 0xff] = KEY_KP0,
	[KEY_SYM_KP1 & 0xff] = KEY_KP1,
	[KEY_SYM_KP2 & 0xff] = KEY_KP2, [KEY_SYM_KPDOWN & 0xff] = KEY_KP2,
	[KEY_SYM_KP3 & 0xff] = KEY_KP3,
	[KEY_SYM_KP4 & 0xff] = KEY_KP4, [KEY_SYM_KPLEFT & 0xff] = KEY_KP4,
	[KEY_SYM_KP5 & 0xff] = KEY_KP5,
	[KEY_SYM_KP6 & 0xff] = KEY_KP6, [KEY_SYM_KPRIGHT & 0xff] = KEY_KP6,
	[KEY_SYM_KP7 & 0xff] = KEY_KP7,
	[KEY_SYM_KP8 & 0xff] = KEY_KP8, [KEY_SYM_KPUP & 0xff] = KEY_KP8,
	[KEY_SYM_KP9 & 0xff] = KEY_KP9,
	[KEY_SYM_BK_SPC & 0xff] = KEY_BACKSPACE,
	[KEY_SYM_TAB & 0xff] = KEY_TAB,
	[KEY_SYM_CTRL & 0xff] = KEY_LEFTCTRL,
	[KEY_SYM_ALT & 0xff] = KEY_LEFTALT,
	[KEY_SYM_INSERT & 0xff] = KEY_INSERT,
	[KEY_SYM_DELETE & 0xff] = KEY_DELETE,
	[KEY_SYM_SHIFT & 0xff] = KEY_LEFTSHIFT,
	[KEY_SYM_UARROW & 0xff] = KEY_UP,
	[KEY_SYM_DARROW & 0xff] = KEY_DOWN,
	[KEY_SYM_LARROW & 0xff] = KEY_LEFT,
	[KEY_SYM_RARROW & 0xff] = KEY_RIGHT,
	[KEY_SYM_ESCAPE & 0xff] = KEY_ESC,
        [KEY_SYM_PAGEUP & 0xff] = KEY_PAGEUP,
        [KEY_SYM_PAGEDOWN & 0xff] = KEY_PAGEDOWN,
        [KEY_SYM_HOME & 0xff] = KEY_HOME,
        [KEY_SYM_END & 0xff] = KEY_END,
	[KEY_SYM_F1 & 0xff] = KEY_F1,
	[KEY_SYM_F2 & 0xff] = KEY_F2,
	[KEY_SYM_F3 & 0xff] = KEY_F3,
	[KEY_SYM_F4 & 0xff] = KEY_F4,
	[KEY_SYM_F5 & 0xff] = KEY_F5,
	[KEY_SYM_F6 & 0xff] = KEY_F6,
	[KEY_SYM_F7 & 0xff] = KEY_F7,
	[KEY_SYM_F8 & 0xff] = KEY_F8,
	[KEY_SYM_F9 & 0xff] = KEY_F9,
	[KEY_SYM_F10 & 0xff] = KEY_F10,
	[KEY_SYM_F11 & 0xff] = KEY_F11,
	[KEY_SYM_F12 & 0xff] = KEY_F12,
	[KEY_SYM_CAP_LOCK & 0xff] = KEY_CAPSLOCK,
	[KEY_SYM_NUM_LOCK & 0xff] = KEY_NUMLOCK,
	[KEY_SYM_SCR_LOCK & 0xff] = KEY_SCROLLLOCK,
};
static unsigned short xlate[XLATE_SIZE] = {
	[NO_KEYCODE] = KEY_RESERVED,
	[KEY_SYM_SPACE] = KEY_SPACE,
	[KEY_SYM_TILDE] = KEY_GRAVE,        [KEY_SYM_BKTIC] = KEY_GRAVE,
	[KEY_SYM_ONE] = KEY_1,              [KEY_SYM_BANG] = KEY_1,
	[KEY_SYM_TWO] = KEY_2,              [KEY_SYM_AT] = KEY_2,
	[KEY_SYM_THREE] = KEY_3,            [KEY_SYM_POUND] = KEY_3,
	[KEY_SYM_FOUR] = KEY_4,             [KEY_SYM_DOLLAR] = KEY_4,
	[KEY_SYM_FIVE] = KEY_5,             [KEY_SYM_PERCENT] = KEY_5,
	[KEY_SYM_SIX] = KEY_6,              [KEY_SYM_CARAT] = KEY_6,
	[KEY_SYM_SEVEN] = KEY_7,            [KEY_SYM_AMPER] = KEY_7,
	[KEY_SYM_EIGHT] = KEY_8,            [KEY_SYM_STAR] = KEY_8,
	[KEY_SYM_NINE] = KEY_9,             [KEY_SYM_LPAREN] = KEY_9,
	[KEY_SYM_ZERO] = KEY_0,             [KEY_SYM_RPAREN] = KEY_0,
	[KEY_SYM_MINUS] = KEY_MINUS,        [KEY_SYM_USCORE] = KEY_MINUS,
	[KEY_SYM_EQUAL] = KEY_EQUAL,        [KEY_SYM_PLUS] = KEY_EQUAL,
	[KEY_SYM_LBRKT] = KEY_LEFTBRACE,    [KEY_SYM_LCURLY] = KEY_LEFTBRACE,
	[KEY_SYM_RBRKT] = KEY_RIGHTBRACE,   [KEY_SYM_RCURLY] = KEY_RIGHTBRACE,
	[KEY_SYM_SLASH] = KEY_BACKSLASH,    [KEY_SYM_PIPE] = KEY_BACKSLASH,
	[KEY_SYM_TIC] = KEY_APOSTROPHE,     [KEY_SYM_QUOTE] = KEY_APOSTROPHE,
	[KEY_SYM_SEMIC] = KEY_SEMICOLON,    [KEY_SYM_COLON] = KEY_SEMICOLON,
	[KEY_SYM_COMMA] = KEY_COMMA,        [KEY_SYM_LT] = KEY_COMMA,
	[KEY_SYM_PERIOD] = KEY_DOT,         [KEY_SYM_GT] = KEY_DOT,
	[KEY_SYM_BSLASH] = KEY_SLASH,       [KEY_SYM_QMARK] = KEY_SLASH,
	[KEY_SYM_A] = KEY_A,                [KEY_SYM_a] = KEY_A,
	[KEY_SYM_B] = KEY_B,                [KEY_SYM_b] = KEY_B,
	[KEY_SYM_C] = KEY_C,                [KEY_SYM_c] = KEY_C,
	[KEY_SYM_D] = KEY_D,                [KEY_SYM_d] = KEY_D,
	[KEY_SYM_E] = KEY_E,                [KEY_SYM_e] = KEY_E,
	[KEY_SYM_F] = KEY_F,                [KEY_SYM_f] = KEY_F,
	[KEY_SYM_G] = KEY_G,                [KEY_SYM_g] = KEY_G,
	[KEY_SYM_H] = KEY_H,                [KEY_SYM_h] = KEY_H,
	[KEY_SYM_I] = KEY_I,                [KEY_SYM_i] = KEY_I,
	[KEY_SYM_J] = KEY_J,                [KEY_SYM_j] = KEY_J,
	[KEY_SYM_K] = KEY_K,                [KEY_SYM_k] = KEY_K,
	[KEY_SYM_L] = KEY_L,                [KEY_SYM_l] = KEY_L,
	[KEY_SYM_M] = KEY_M,                [KEY_SYM_m] = KEY_M,
	[KEY_SYM_N] = KEY_N,                [KEY_SYM_n] = KEY_N,
	[KEY_SYM_O] = KEY_O,                [KEY_SYM_o] = KEY_O,
	[KEY_SYM_P] = KEY_P,                [KEY_SYM_p] = KEY_P,
	[KEY_SYM_Q] = KEY_Q,                [KEY_SYM_q] = KEY_Q,
	[KEY_SYM_R] = KEY_R,                [KEY_SYM_r] = KEY_R,
	[KEY_SYM_S] = KEY_S,                [KEY_SYM_s] = KEY_S,
	[KEY_SYM_T] = KEY_T,                [KEY_SYM_t] = KEY_T,
	[KEY_SYM_U] = KEY_U,                [KEY_SYM_u] = KEY_U,
	[KEY_SYM_V] = KEY_V,                [KEY_SYM_v] = KEY_V,
	[KEY_SYM_W] = KEY_W,                [KEY_SYM_w] = KEY_W,
	[KEY_SYM_X] = KEY_X,                [KEY_SYM_x] = KEY_X,
	[KEY_SYM_Y] = KEY_Y,                [KEY_SYM_y] = KEY_Y,
	[KEY_SYM_Z] = KEY_Z,                [KEY_SYM_z] = KEY_Z,
};

static char remote_mouse_name[] = "ibmasm RSA I remote mouse";
static char remote_keybd_name[] = "ibmasm RSA I remote keyboard";

static void print_input(struct remote_input *input)
{
	if (input->type == INPUT_TYPE_MOUSE) {
		unsigned char buttons = input->mouse_buttons;
		dbg("remote mouse movement: (x,y)=(%d,%d)%s%s%s%s\n",
			input->data.mouse.x, input->data.mouse.y,
			(buttons)?" -- buttons:":"",
			(buttons & REMOTE_BUTTON_LEFT)?"left ":"",
			(buttons & REMOTE_BUTTON_MIDDLE)?"middle ":"",
			(buttons & REMOTE_BUTTON_RIGHT)?"right":""
		      );
	} else {
		dbg("remote keypress (code, flag, down):"
			   "%d (0x%x) [0x%x] [0x%x]\n",
				input->data.keyboard.key_code,
				input->data.keyboard.key_code,
				input->data.keyboard.key_flag,
				input->data.keyboard.key_down
		      );
	}
}

static void send_mouse_event(struct input_dev *dev, struct pt_regs *regs,
		struct remote_input *input)
{
	unsigned char buttons = input->mouse_buttons;

	input_regs(dev, regs);
	input_report_abs(dev, ABS_X, input->data.mouse.x);
	input_report_abs(dev, ABS_Y, input->data.mouse.y);
	input_report_key(dev, BTN_LEFT, buttons & REMOTE_BUTTON_LEFT);
	input_report_key(dev, BTN_MIDDLE, buttons & REMOTE_BUTTON_MIDDLE);
	input_report_key(dev, BTN_RIGHT, buttons & REMOTE_BUTTON_RIGHT);
	input_sync(dev);
}

static void send_keyboard_event(struct input_dev *dev, struct pt_regs *regs,
		struct remote_input *input)
{
	unsigned int key;
	unsigned short code = input->data.keyboard.key_code;

	if (code & 0xff00)
		key = xlate_high[code & 0xff];
	else
		key = xlate[code];
	input_regs(dev, regs);
	input_report_key(dev, key, (input->data.keyboard.key_down) ? 1 : 0);
	input_sync(dev);
}

void ibmasm_handle_mouse_interrupt(struct service_processor *sp,
		struct pt_regs *regs)
{
	unsigned long reader;
	unsigned long writer;
	struct remote_input input;

	reader = get_queue_reader(sp);
	writer = get_queue_writer(sp);

	while (reader != writer) {
		memcpy_fromio(&input, get_queue_entry(sp, reader),
				sizeof(struct remote_input));

		print_input(&input);
		if (input.type == INPUT_TYPE_MOUSE) {
			send_mouse_event(&sp->remote->mouse_dev, regs, &input);
		} else if (input.type == INPUT_TYPE_KEYBOARD) {
			send_keyboard_event(&sp->remote->keybd_dev, regs, &input);
		} else
			break;

		reader = advance_queue_reader(sp, reader);
		writer = get_queue_writer(sp);
	}
}

int ibmasm_init_remote_input_dev(struct service_processor *sp)
{
	/* set up the mouse input device */
	struct ibmasm_remote *remote;
	struct pci_dev *pdev = to_pci_dev(sp->dev);
	int i;

	sp->remote = remote = kmalloc(sizeof(*remote), GFP_KERNEL);
	if (!remote)
		return -ENOMEM;

	memset(remote, 0, sizeof(*remote));

	remote->mouse_dev.private = remote;
	init_input_dev(&remote->mouse_dev);
	remote->mouse_dev.id.vendor = pdev->vendor;
	remote->mouse_dev.id.product = pdev->device;
	remote->mouse_dev.evbit[0]  = BIT(EV_KEY) | BIT(EV_ABS);
	remote->mouse_dev.keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) |
		BIT(BTN_RIGHT) | BIT(BTN_MIDDLE);
	set_bit(BTN_TOUCH, remote->mouse_dev.keybit);
	remote->mouse_dev.name = remote_mouse_name;
	input_set_abs_params(&remote->mouse_dev, ABS_X, 0, xmax, 0, 0);
	input_set_abs_params(&remote->mouse_dev, ABS_Y, 0, ymax, 0, 0);

	remote->keybd_dev.private = remote;
	init_input_dev(&remote->keybd_dev);
	remote->keybd_dev.id.vendor = pdev->vendor;
	remote->keybd_dev.id.product = pdev->device;
	remote->keybd_dev.evbit[0]  = BIT(EV_KEY);
	remote->keybd_dev.name = remote_keybd_name;

	for (i=0; i<XLATE_SIZE; i++) {
		if (xlate_high[i])
			set_bit(xlate_high[i], remote->keybd_dev.keybit);
		if (xlate[i])
			set_bit(xlate[i], remote->keybd_dev.keybit);
	}

	input_register_device(&remote->mouse_dev);
	input_register_device(&remote->keybd_dev);
	enable_mouse_interrupts(sp);

	printk(KERN_INFO "ibmasm remote responding to events on RSA card %d\n", sp->number);

	return 0;
}

void ibmasm_free_remote_input_dev(struct service_processor *sp)
{
	disable_mouse_interrupts(sp);
	input_unregister_device(&sp->remote->keybd_dev);
	input_unregister_device(&sp->remote->mouse_dev);
	kfree(sp->remote);
}

