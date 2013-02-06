
#ifndef _SEC_KEYBOARD_H_
#define _SEC_KEYBOARD_H_

#include <linux/input.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/earlysuspend.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/30pin_con.h>
#include <linux/serio.h>
#include <linux/power_supply.h>

#define KEYBOARD_SIZE   128
#define US_KEYBOARD     0xeb
#define UK_KEYBOARD     0xec

#define KEYBOARD_MIN   0x4
#define KEYBOARD_MAX   0x80

enum KEY_LAYOUT {
	UNKOWN_KEYLAYOUT = 0,
	US_KEYLAYOUT,
	UK_KEYLAYOUT,
};

extern struct class *sec_class;

static struct serio_device_id sec_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= 0x3c,
		.id		= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, sec_serio_ids);

struct sec_keyboard_drvdata {
	struct input_dev *input_dev;
	struct device *keyboard_dev;
	struct delayed_work remap_dwork;
	struct delayed_work power_dwork;
	struct delayed_work ack_dwork;
	struct sec_keyboard_callbacks callbacks;
	struct serio *serio;
	struct serio_driver serio_driver;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	void	(*acc_power)(u8 token, bool active);
	void (*check_uart_path)(bool en);
	int (*noti_univ_kbd_dock)(unsigned int code);
	bool led_on;
	bool dockconnected;
	bool pre_connected;
	bool pressed[KEYBOARD_SIZE];
	bool pre_uart_path;
	bool tx_ready;
	bool univ_kbd_dock;
	int acc_int_gpio;
	unsigned int remap_key;
	unsigned int kl;
	unsigned int pre_kl;
	unsigned int ack_code;
	unsigned short keycode[KEYBOARD_SIZE];
	unsigned long connected_time;
	unsigned long disconnected_time;
};

static const unsigned short sec_keycodes[KEYBOARD_SIZE] = {
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_A,
	KEY_B,
	KEY_C,
	KEY_D,
	KEY_E,
	KEY_F,
	KEY_G,
	KEY_H,
	KEY_I,
	KEY_J,
	KEY_K,
	KEY_L,
	KEY_M,
	KEY_N,
	KEY_O,
	KEY_P,
	KEY_Q,
	KEY_R,
	KEY_S,
	KEY_T,
	KEY_U,
	KEY_V,
	KEY_W,
	KEY_X,
	KEY_Y,
	KEY_Z,
	KEY_1,
	KEY_2,
	KEY_3,
	KEY_4,
	KEY_5,
	KEY_6,
	KEY_7,
	KEY_8,
	KEY_9,
	KEY_0,
	KEY_ENTER,
	KEY_BACK,
	KEY_BACKSPACE,
	KEY_TAB,
	KEY_SPACE,
	KEY_MINUS,
	KEY_EQUAL,
	KEY_LEFTBRACE,
	KEY_RIGHTBRACE,
	KEY_HOME,
	KEY_RESERVED,
	KEY_SEMICOLON,
	KEY_APOSTROPHE,
	KEY_GRAVE,
	KEY_COMMA,
	KEY_DOT,
	KEY_SLASH,
	KEY_CAPSLOCK,
	KEY_TIME,
	KEY_F3,
	KEY_WWW,
	KEY_EMAIL,
	KEY_SCREENLOCK,
	KEY_BRIGHTNESSDOWN,
	KEY_BRIGHTNESSUP,
	KEY_MUTE,
	KEY_VOLUMEDOWN,
	KEY_VOLUMEUP,
	KEY_PLAY,
	KEY_REWIND,
	KEY_F15,
	KEY_RESERVED,
	KEY_FASTFORWARD,
	KEY_MENU,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_DELETE,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RIGHT,
	KEY_LEFT,
	KEY_DOWN,
	KEY_UP,
	KEY_NUMLOCK,
	KEY_KPSLASH,
	KEY_APOSTROPHE,
	KEY_KPMINUS,
	KEY_KPPLUS,
	KEY_KPENTER,
	KEY_KP1,
	KEY_KP2,
	KEY_KP3,
	KEY_KP4,
	KEY_KP5,
	KEY_KP6,
	KEY_KP7,
	KEY_KP8,
	KEY_KP9,
	KEY_KPDOT,
	KEY_RESERVED,
	KEY_BACKSLASH,
	KEY_F22,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_HANGEUL,
	KEY_HANJA,
	KEY_LEFTCTRL,
	KEY_LEFTSHIFT,
	KEY_F20,
	KEY_SEARCH,
	KEY_RIGHTALT,
	KEY_RIGHTSHIFT,
	KEY_F21,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_RESERVED,
	KEY_F17,
};
#endif  /*_SEC_KEYBOARD_H_*/

