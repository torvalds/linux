/*
 * drivers/macintosh/adbhid.c
 *
 * ADB HID driver for Power Macintosh computers.
 *
 * Adapted from drivers/macintosh/mac_keyb.c by Franz Sirl.
 * drivers/macintosh/mac_keyb.c was Copyright (C) 1996 Paul Mackerras
 * with considerable contributions from Ben Herrenschmidt and others.
 *
 * Copyright (C) 2000 Franz Sirl.
 *
 * Adapted to ADB changes and support for more devices by
 * Benjamin Herrenschmidt. Adapted from code in MkLinux
 * and reworked.
 * 
 * Supported devices:
 *
 * - Standard 1 button mouse
 * - All standard Apple Extended protocol (handler ID 4)
 * - mouseman and trackman mice & trackballs 
 * - PowerBook Trackpad (default setup: enable tapping)
 * - MicroSpeed mouse & trackball (needs testing)
 * - CH Products Trackball Pro (needs testing)
 * - Contour Design (Contour Mouse)
 * - Hunter digital (NoHandsMouse)
 * - Kensignton TurboMouse 5 (needs testing)
 * - Mouse Systems A3 mice and trackballs <aidan@kublai.com>
 * - MacAlly 2-buttons mouse (needs testing) <pochini@denise.shiny.it>
 *
 * To do:
 *
 * Improve Kensington support.
 * Split mouse/kbd
 * Move to syfs
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/input.h>

#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/pmu.h>

#include <asm/machdep.h>
#ifdef CONFIG_PPC_PMAC
#include <asm/backlight.h>
#include <asm/pmac_feature.h>
#endif

MODULE_AUTHOR("Franz Sirl <Franz.Sirl-kernel@lauterbach.com>");

static int restore_capslock_events;
module_param(restore_capslock_events, int, 0644);
MODULE_PARM_DESC(restore_capslock_events,
	"Produce keypress events for capslock on both keyup and keydown.");

#define KEYB_KEYREG	0	/* register # for key up/down data */
#define KEYB_LEDREG	2	/* register # for leds on ADB keyboard */
#define MOUSE_DATAREG	0	/* reg# for movement/button codes from mouse */

static int adb_message_handler(struct notifier_block *, unsigned long, void *);
static struct notifier_block adbhid_adb_notifier = {
	.notifier_call	= adb_message_handler,
};

/* Some special keys */
#define ADB_KEY_DEL		0x33
#define ADB_KEY_CMD		0x37
#define ADB_KEY_CAPSLOCK	0x39
#define ADB_KEY_FN		0x3f
#define ADB_KEY_FWDEL		0x75
#define ADB_KEY_POWER_OLD	0x7e
#define ADB_KEY_POWER		0x7f

u16 adb_to_linux_keycodes[128] = {
	/* 0x00 */ KEY_A, 		/*  30 */
	/* 0x01 */ KEY_S, 		/*  31 */
	/* 0x02 */ KEY_D,		/*  32 */
	/* 0x03 */ KEY_F,		/*  33 */
	/* 0x04 */ KEY_H,		/*  35 */
	/* 0x05 */ KEY_G,		/*  34 */
	/* 0x06 */ KEY_Z,		/*  44 */
	/* 0x07 */ KEY_X,		/*  45 */
	/* 0x08 */ KEY_C,		/*  46 */
	/* 0x09 */ KEY_V,		/*  47 */
	/* 0x0a */ KEY_102ND,		/*  86 */
	/* 0x0b */ KEY_B,		/*  48 */
	/* 0x0c */ KEY_Q,		/*  16 */
	/* 0x0d */ KEY_W,		/*  17 */
	/* 0x0e */ KEY_E,		/*  18 */
	/* 0x0f */ KEY_R,		/*  19 */
	/* 0x10 */ KEY_Y,		/*  21 */
	/* 0x11 */ KEY_T,		/*  20 */
	/* 0x12 */ KEY_1,		/*   2 */
	/* 0x13 */ KEY_2,		/*   3 */
	/* 0x14 */ KEY_3,		/*   4 */
	/* 0x15 */ KEY_4,		/*   5 */
	/* 0x16 */ KEY_6,		/*   7 */
	/* 0x17 */ KEY_5,		/*   6 */
	/* 0x18 */ KEY_EQUAL,		/*  13 */
	/* 0x19 */ KEY_9,		/*  10 */
	/* 0x1a */ KEY_7,		/*   8 */
	/* 0x1b */ KEY_MINUS,		/*  12 */
	/* 0x1c */ KEY_8,		/*   9 */
	/* 0x1d */ KEY_0,		/*  11 */
	/* 0x1e */ KEY_RIGHTBRACE,	/*  27 */
	/* 0x1f */ KEY_O,		/*  24 */
	/* 0x20 */ KEY_U,		/*  22 */
	/* 0x21 */ KEY_LEFTBRACE,	/*  26 */
	/* 0x22 */ KEY_I,		/*  23 */
	/* 0x23 */ KEY_P,		/*  25 */
	/* 0x24 */ KEY_ENTER,		/*  28 */
	/* 0x25 */ KEY_L,		/*  38 */
	/* 0x26 */ KEY_J,		/*  36 */
	/* 0x27 */ KEY_APOSTROPHE,	/*  40 */
	/* 0x28 */ KEY_K,		/*  37 */
	/* 0x29 */ KEY_SEMICOLON,	/*  39 */
	/* 0x2a */ KEY_BACKSLASH,	/*  43 */
	/* 0x2b */ KEY_COMMA,		/*  51 */
	/* 0x2c */ KEY_SLASH,		/*  53 */
	/* 0x2d */ KEY_N,		/*  49 */
	/* 0x2e */ KEY_M,		/*  50 */
	/* 0x2f */ KEY_DOT,		/*  52 */
	/* 0x30 */ KEY_TAB,		/*  15 */
	/* 0x31 */ KEY_SPACE,		/*  57 */
	/* 0x32 */ KEY_GRAVE,		/*  41 */
	/* 0x33 */ KEY_BACKSPACE,	/*  14 */
	/* 0x34 */ KEY_KPENTER,		/*  96 */
	/* 0x35 */ KEY_ESC,		/*   1 */
	/* 0x36 */ KEY_LEFTCTRL,	/*  29 */
	/* 0x37 */ KEY_LEFTMETA,	/* 125 */
	/* 0x38 */ KEY_LEFTSHIFT,	/*  42 */
	/* 0x39 */ KEY_CAPSLOCK,	/*  58 */
	/* 0x3a */ KEY_LEFTALT,		/*  56 */
	/* 0x3b */ KEY_LEFT,		/* 105 */
	/* 0x3c */ KEY_RIGHT,		/* 106 */
	/* 0x3d */ KEY_DOWN,		/* 108 */
	/* 0x3e */ KEY_UP,		/* 103 */
	/* 0x3f */ KEY_FN,		/* 0x1d0 */
	/* 0x40 */ 0,
	/* 0x41 */ KEY_KPDOT,		/*  83 */
	/* 0x42 */ 0,
	/* 0x43 */ KEY_KPASTERISK,	/*  55 */
	/* 0x44 */ 0,
	/* 0x45 */ KEY_KPPLUS,		/*  78 */
	/* 0x46 */ 0,
	/* 0x47 */ KEY_NUMLOCK,		/*  69 */
	/* 0x48 */ 0,
	/* 0x49 */ 0,
	/* 0x4a */ 0,
	/* 0x4b */ KEY_KPSLASH,		/*  98 */
	/* 0x4c */ KEY_KPENTER,		/*  96 */
	/* 0x4d */ 0,
	/* 0x4e */ KEY_KPMINUS,		/*  74 */
	/* 0x4f */ 0,
	/* 0x50 */ 0,
	/* 0x51 */ KEY_KPEQUAL,		/* 117 */
	/* 0x52 */ KEY_KP0,		/*  82 */
	/* 0x53 */ KEY_KP1,		/*  79 */
	/* 0x54 */ KEY_KP2,		/*  80 */
	/* 0x55 */ KEY_KP3,		/*  81 */
	/* 0x56 */ KEY_KP4,		/*  75 */
	/* 0x57 */ KEY_KP5,		/*  76 */
	/* 0x58 */ KEY_KP6,		/*  77 */
	/* 0x59 */ KEY_KP7,		/*  71 */
	/* 0x5a */ 0,
	/* 0x5b */ KEY_KP8,		/*  72 */
	/* 0x5c */ KEY_KP9,		/*  73 */
	/* 0x5d */ KEY_YEN,		/* 124 */
	/* 0x5e */ KEY_RO,		/*  89 */
	/* 0x5f */ KEY_KPCOMMA,		/* 121 */
	/* 0x60 */ KEY_F5,		/*  63 */
	/* 0x61 */ KEY_F6,		/*  64 */
	/* 0x62 */ KEY_F7,		/*  65 */
	/* 0x63 */ KEY_F3,		/*  61 */
	/* 0x64 */ KEY_F8,		/*  66 */
	/* 0x65 */ KEY_F9,		/*  67 */
	/* 0x66 */ KEY_HANJA,		/* 123 */
	/* 0x67 */ KEY_F11,		/*  87 */
	/* 0x68 */ KEY_HANGEUL,		/* 122 */
	/* 0x69 */ KEY_SYSRQ,		/*  99 */
	/* 0x6a */ 0,
	/* 0x6b */ KEY_SCROLLLOCK,	/*  70 */
	/* 0x6c */ 0,
	/* 0x6d */ KEY_F10,		/*  68 */
	/* 0x6e */ KEY_COMPOSE,		/* 127 */
	/* 0x6f */ KEY_F12,		/*  88 */
	/* 0x70 */ 0,
	/* 0x71 */ KEY_PAUSE,		/* 119 */
	/* 0x72 */ KEY_INSERT,		/* 110 */
	/* 0x73 */ KEY_HOME,		/* 102 */
	/* 0x74 */ KEY_PAGEUP,		/* 104 */
	/* 0x75 */ KEY_DELETE,		/* 111 */
	/* 0x76 */ KEY_F4,		/*  62 */
	/* 0x77 */ KEY_END,		/* 107 */
	/* 0x78 */ KEY_F2,		/*  60 */
	/* 0x79 */ KEY_PAGEDOWN,	/* 109 */
	/* 0x7a */ KEY_F1,		/*  59 */
	/* 0x7b */ KEY_RIGHTSHIFT,	/*  54 */
	/* 0x7c */ KEY_RIGHTALT,	/* 100 */
	/* 0x7d */ KEY_RIGHTCTRL,	/*  97 */
	/* 0x7e */ KEY_RIGHTMETA,	/* 126 */
	/* 0x7f */ KEY_POWER,		/* 116 */
};

struct adbhid {
	struct input_dev *input;
	int id;
	int default_id;
	int original_handler_id;
	int current_handler_id;
	int mouse_kind;
	u16 *keycode;
	char name[64];
	char phys[32];
	int flags;
};

#define FLAG_FN_KEY_PRESSED	0x00000001
#define FLAG_POWER_FROM_FN	0x00000002
#define FLAG_EMU_FWDEL_DOWN	0x00000004
#define FLAG_CAPSLOCK_TRANSLATE	0x00000008
#define FLAG_CAPSLOCK_DOWN	0x00000010

static struct adbhid *adbhid[16];

static void adbhid_probe(void);

static void adbhid_input_keycode(int, int, int);

static void init_trackpad(int id);
static void init_trackball(int id);
static void init_turbomouse(int id);
static void init_microspeed(int id);
static void init_ms_a3(int id);

static struct adb_ids keyboard_ids;
static struct adb_ids mouse_ids;
static struct adb_ids buttons_ids;

/* Kind of keyboard, see Apple technote 1152  */
#define ADB_KEYBOARD_UNKNOWN	0
#define ADB_KEYBOARD_ANSI	0x0100
#define ADB_KEYBOARD_ISO	0x0200
#define ADB_KEYBOARD_JIS	0x0300

/* Kind of mouse  */
#define ADBMOUSE_STANDARD_100	0	/* Standard 100cpi mouse (handler 1) */
#define ADBMOUSE_STANDARD_200	1	/* Standard 200cpi mouse (handler 2) */
#define ADBMOUSE_EXTENDED	2	/* Apple Extended mouse (handler 4) */
#define ADBMOUSE_TRACKBALL	3	/* TrackBall (handler 4) */
#define ADBMOUSE_TRACKPAD       4	/* Apple's PowerBook trackpad (handler 4) */
#define ADBMOUSE_TURBOMOUSE5    5	/* Turbomouse 5 (previously req. mousehack) */
#define ADBMOUSE_MICROSPEED	6	/* Microspeed mouse (&trackball ?), MacPoint */
#define ADBMOUSE_TRACKBALLPRO	7	/* Trackball Pro (special buttons) */
#define ADBMOUSE_MS_A3		8	/* Mouse systems A3 trackball (handler 3) */
#define ADBMOUSE_MACALLY2	9	/* MacAlly 2-button mouse */

static void
adbhid_keyboard_input(unsigned char *data, int nb, int apoll)
{
	int id = (data[0] >> 4) & 0x0f;

	if (!adbhid[id]) {
		printk(KERN_ERR "ADB HID on ID %d not yet registered, packet %#02x, %#02x, %#02x, %#02x\n",
		       id, data[0], data[1], data[2], data[3]);
		return;
	}

	/* first check this is from register 0 */
	if (nb != 3 || (data[0] & 3) != KEYB_KEYREG)
		return;		/* ignore it */
	adbhid_input_keycode(id, data[1], 0);
	if (!(data[2] == 0xff || (data[2] == 0x7f && data[1] == 0x7f)))
		adbhid_input_keycode(id, data[2], 0);
}

static void
adbhid_input_keycode(int id, int scancode, int repeat)
{
	struct adbhid *ahid = adbhid[id];
	int keycode, up_flag, key;

	keycode = scancode & 0x7f;
	up_flag = scancode & 0x80;

	if (restore_capslock_events) {
		if (keycode == ADB_KEY_CAPSLOCK && !up_flag) {
			/* Key pressed, turning on the CapsLock LED.
			 * The next 0xff will be interpreted as a release. */
			ahid->flags |= FLAG_CAPSLOCK_TRANSLATE
					| FLAG_CAPSLOCK_DOWN;
		} else if (scancode == 0xff) {
			/* Scancode 0xff usually signifies that the capslock
			 * key was either pressed or released. */
			if (ahid->flags & FLAG_CAPSLOCK_TRANSLATE) {
				keycode = ADB_KEY_CAPSLOCK;
				if (ahid->flags & FLAG_CAPSLOCK_DOWN) {
					/* Key released */
					up_flag = 1;
					ahid->flags &= ~FLAG_CAPSLOCK_DOWN;
				} else {
					/* Key pressed */
					up_flag = 0;
					ahid->flags &= ~FLAG_CAPSLOCK_TRANSLATE;
				}
			} else {
				printk(KERN_INFO "Spurious caps lock event "
						"(scancode 0xff).");
			}
		}
	}

	switch (keycode) {
	case ADB_KEY_CAPSLOCK:
		if (!restore_capslock_events) {
			/* Generate down/up events for CapsLock everytime. */
			input_report_key(ahid->input, KEY_CAPSLOCK, 1);
			input_sync(ahid->input);
			input_report_key(ahid->input, KEY_CAPSLOCK, 0);
			input_sync(ahid->input);
			return;
		}
		break;
#ifdef CONFIG_PPC_PMAC
	case ADB_KEY_POWER_OLD: /* Power key on PBook 3400 needs remapping */
		switch(pmac_call_feature(PMAC_FTR_GET_MB_INFO,
			NULL, PMAC_MB_INFO_MODEL, 0)) {
		case PMAC_TYPE_COMET:
		case PMAC_TYPE_HOOPER:
		case PMAC_TYPE_KANGA:
			keycode = ADB_KEY_POWER;
		}
		break;
	case ADB_KEY_POWER:
		/* Fn + Command will produce a bogus "power" keycode */
		if (ahid->flags & FLAG_FN_KEY_PRESSED) {
			keycode = ADB_KEY_CMD;
			if (up_flag)
				ahid->flags &= ~FLAG_POWER_FROM_FN;
			else
				ahid->flags |= FLAG_POWER_FROM_FN;
		} else if (ahid->flags & FLAG_POWER_FROM_FN) {
			keycode = ADB_KEY_CMD;
			ahid->flags &= ~FLAG_POWER_FROM_FN;
		}
		break;
	case ADB_KEY_FN:
		/* Keep track of the Fn key state */
		if (up_flag) {
			ahid->flags &= ~FLAG_FN_KEY_PRESSED;
			/* Emulate Fn+delete = forward delete */
			if (ahid->flags & FLAG_EMU_FWDEL_DOWN) {
				ahid->flags &= ~FLAG_EMU_FWDEL_DOWN;
				keycode = ADB_KEY_FWDEL;
				break;
			}
		} else
			ahid->flags |= FLAG_FN_KEY_PRESSED;
		break;
	case ADB_KEY_DEL:
		/* Emulate Fn+delete = forward delete */
		if (ahid->flags & FLAG_FN_KEY_PRESSED) {
			keycode = ADB_KEY_FWDEL;
			if (up_flag)
				ahid->flags &= ~FLAG_EMU_FWDEL_DOWN;
			else
				ahid->flags |= FLAG_EMU_FWDEL_DOWN;
		}
		break;
#endif /* CONFIG_PPC_PMAC */
	}

	key = adbhid[id]->keycode[keycode];
	if (key) {
		input_report_key(adbhid[id]->input, key, !up_flag);
		input_sync(adbhid[id]->input);
	} else
		printk(KERN_INFO "Unhandled ADB key (scancode %#02x) %s.\n", keycode,
		       up_flag ? "released" : "pressed");

}

static void
adbhid_mouse_input(unsigned char *data, int nb, int autopoll)
{
	int id = (data[0] >> 4) & 0x0f;

	if (!adbhid[id]) {
		printk(KERN_ERR "ADB HID on ID %d not yet registered\n", id);
		return;
	}

  /*
    Handler 1 -- 100cpi original Apple mouse protocol.
    Handler 2 -- 200cpi original Apple mouse protocol.

    For Apple's standard one-button mouse protocol the data array will
    contain the following values:

                BITS    COMMENTS
    data[0] = dddd 1100 ADB command: Talk, register 0, for device dddd.
    data[1] = bxxx xxxx First button and x-axis motion.
    data[2] = byyy yyyy Second button and y-axis motion.

    Handler 4 -- Apple Extended mouse protocol.

    For Apple's 3-button mouse protocol the data array will contain the
    following values:

		BITS    COMMENTS
    data[0] = dddd 1100 ADB command: Talk, register 0, for device dddd.
    data[1] = bxxx xxxx Left button and x-axis motion.
    data[2] = byyy yyyy Second button and y-axis motion.
    data[3] = byyy bxxx Third button and fourth button.  Y is additional
	      high bits of y-axis motion.  XY is additional
	      high bits of x-axis motion.

    MacAlly 2-button mouse protocol.

    For MacAlly 2-button mouse protocol the data array will contain the
    following values:

		BITS    COMMENTS
    data[0] = dddd 1100 ADB command: Talk, register 0, for device dddd.
    data[1] = bxxx xxxx Left button and x-axis motion.
    data[2] = byyy yyyy Right button and y-axis motion.
    data[3] = ???? ???? unknown
    data[4] = ???? ???? unknown

  */

	/* If it's a trackpad, we alias the second button to the first.
	   NOTE: Apple sends an ADB flush command to the trackpad when
	         the first (the real) button is released. We could do
		 this here using async flush requests.
	*/
	switch (adbhid[id]->mouse_kind)
	{
	    case ADBMOUSE_TRACKPAD:
		data[1] = (data[1] & 0x7f) | ((data[1] & data[2]) & 0x80);
		data[2] = data[2] | 0x80;
		break;
	    case ADBMOUSE_MICROSPEED:
		data[1] = (data[1] & 0x7f) | ((data[3] & 0x01) << 7);
		data[2] = (data[2] & 0x7f) | ((data[3] & 0x02) << 6);
		data[3] = (data[3] & 0x77) | ((data[3] & 0x04) << 5)
			| (data[3] & 0x08);
		break;
	    case ADBMOUSE_TRACKBALLPRO:
		data[1] = (data[1] & 0x7f) | (((data[3] & 0x04) << 5)
			& ((data[3] & 0x08) << 4));
		data[2] = (data[2] & 0x7f) | ((data[3] & 0x01) << 7);
		data[3] = (data[3] & 0x77) | ((data[3] & 0x02) << 6);
		break;
	    case ADBMOUSE_MS_A3:
		data[1] = (data[1] & 0x7f) | ((data[3] & 0x01) << 7);
		data[2] = (data[2] & 0x7f) | ((data[3] & 0x02) << 6);
		data[3] = ((data[3] & 0x04) << 5);
		break;
            case ADBMOUSE_MACALLY2:
		data[3] = (data[2] & 0x80) ? 0x80 : 0x00;
		data[2] |= 0x80;  /* Right button is mapped as button 3 */
		nb=4;
                break;
	}

	input_report_key(adbhid[id]->input, BTN_LEFT,   !((data[1] >> 7) & 1));
	input_report_key(adbhid[id]->input, BTN_MIDDLE, !((data[2] >> 7) & 1));

	if (nb >= 4 && adbhid[id]->mouse_kind != ADBMOUSE_TRACKPAD)
		input_report_key(adbhid[id]->input, BTN_RIGHT,  !((data[3] >> 7) & 1));

	input_report_rel(adbhid[id]->input, REL_X,
			 ((data[2]&0x7f) < 64 ? (data[2]&0x7f) : (data[2]&0x7f)-128 ));
	input_report_rel(adbhid[id]->input, REL_Y,
			 ((data[1]&0x7f) < 64 ? (data[1]&0x7f) : (data[1]&0x7f)-128 ));

	input_sync(adbhid[id]->input);
}

static void
adbhid_buttons_input(unsigned char *data, int nb, int autopoll)
{
	int id = (data[0] >> 4) & 0x0f;

	if (!adbhid[id]) {
		printk(KERN_ERR "ADB HID on ID %d not yet registered\n", id);
		return;
	}

	switch (adbhid[id]->original_handler_id) {
	default:
	case 0x02: /* Adjustable keyboard button device */
	  {
		int down = (data[1] == (data[1] & 0xf));

		switch (data[1] & 0x0f) {
		case 0x0:	/* microphone */
			input_report_key(adbhid[id]->input, KEY_SOUND, down);
			break;

		case 0x1:	/* mute */
			input_report_key(adbhid[id]->input, KEY_MUTE, down);
			break;

		case 0x2:	/* volume decrease */
			input_report_key(adbhid[id]->input, KEY_VOLUMEDOWN, down);
			break;

		case 0x3:	/* volume increase */
			input_report_key(adbhid[id]->input, KEY_VOLUMEUP, down);
			break;

		default:
			printk(KERN_INFO "Unhandled ADB_MISC event %02x, %02x, %02x, %02x\n",
			       data[0], data[1], data[2], data[3]);
			break;
		}
	  }
	  break;

	case 0x1f: /* Powerbook button device */
	  {
		int down = (data[1] == (data[1] & 0xf));

		/*
		 * XXX: Where is the contrast control for the passive?
		 *  -- Cort
		 */

		switch (data[1] & 0x0f) {
		case 0x8:	/* mute */
			input_report_key(adbhid[id]->input, KEY_MUTE, down);
			break;

		case 0x7:	/* volume decrease */
			input_report_key(adbhid[id]->input, KEY_VOLUMEDOWN, down);
			break;

		case 0x6:	/* volume increase */
			input_report_key(adbhid[id]->input, KEY_VOLUMEUP, down);
			break;

		case 0xb:	/* eject */
			input_report_key(adbhid[id]->input, KEY_EJECTCD, down);
			break;

		case 0xa:	/* brightness decrease */
#ifdef CONFIG_PMAC_BACKLIGHT
			if (down)
				pmac_backlight_key_down();
#endif
			input_report_key(adbhid[id]->input, KEY_BRIGHTNESSDOWN, down);
			break;

		case 0x9:	/* brightness increase */
#ifdef CONFIG_PMAC_BACKLIGHT
			if (down)
				pmac_backlight_key_up();
#endif
			input_report_key(adbhid[id]->input, KEY_BRIGHTNESSUP, down);
			break;

		case 0xc:	/* videomode switch */
			input_report_key(adbhid[id]->input, KEY_SWITCHVIDEOMODE, down);
			break;

		case 0xd:	/* keyboard illumination toggle */
			input_report_key(adbhid[id]->input, KEY_KBDILLUMTOGGLE, down);
			break;

		case 0xe:	/* keyboard illumination decrease */
			input_report_key(adbhid[id]->input, KEY_KBDILLUMDOWN, down);
			break;

		case 0xf:
			switch (data[1]) {
			case 0x8f:
			case 0x0f:
				/* keyboard illumination increase */
				input_report_key(adbhid[id]->input, KEY_KBDILLUMUP, down);
				break;

			case 0x7f:
			case 0xff:
				/* keypad overlay toogle */
				break;

			default:
				printk(KERN_INFO "Unhandled ADB_MISC event %02x, %02x, %02x, %02x\n",
				       data[0], data[1], data[2], data[3]);
				break;
			}
			break;
		default:
			printk(KERN_INFO "Unhandled ADB_MISC event %02x, %02x, %02x, %02x\n",
			       data[0], data[1], data[2], data[3]);
			break;
		}
	  }
	  break;
	}

	input_sync(adbhid[id]->input);
}

static struct adb_request led_request;
static int leds_pending[16];
static int leds_req_pending;
static int pending_devs[16];
static int pending_led_start;
static int pending_led_end;
static DEFINE_SPINLOCK(leds_lock);

static void leds_done(struct adb_request *req)
{
	int leds = 0, device = 0, pending = 0;
	unsigned long flags;

	spin_lock_irqsave(&leds_lock, flags);

	if (pending_led_start != pending_led_end) {
		device = pending_devs[pending_led_start];
		leds = leds_pending[device] & 0xff;
		leds_pending[device] = 0;
		pending_led_start++;
		pending_led_start = (pending_led_start < 16) ? pending_led_start : 0;
		pending = leds_req_pending;
	} else
		leds_req_pending = 0;
	spin_unlock_irqrestore(&leds_lock, flags);
	if (pending)
		adb_request(&led_request, leds_done, 0, 3,
			    ADB_WRITEREG(device, KEYB_LEDREG), 0xff, ~leds);
}

static void real_leds(unsigned char leds, int device)
{
	unsigned long flags;

	spin_lock_irqsave(&leds_lock, flags);
	if (!leds_req_pending) {
		leds_req_pending = 1;
		spin_unlock_irqrestore(&leds_lock, flags);	       
		adb_request(&led_request, leds_done, 0, 3,
			    ADB_WRITEREG(device, KEYB_LEDREG), 0xff, ~leds);
		return;
	} else {
		if (!(leds_pending[device] & 0x100)) {
			pending_devs[pending_led_end] = device;
			pending_led_end++;
			pending_led_end = (pending_led_end < 16) ? pending_led_end : 0;
		}
		leds_pending[device] = leds | 0x100;
	}
	spin_unlock_irqrestore(&leds_lock, flags);	       
}

/*
 * Event callback from the input module. Events that change the state of
 * the hardware are processed here.
 */
static int adbhid_kbd_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct adbhid *adbhid = input_get_drvdata(dev);
	unsigned char leds;

	switch (type) {
	case EV_LED:
		leds =  (test_bit(LED_SCROLLL, dev->led) ? 4 : 0) |
			(test_bit(LED_NUML,    dev->led) ? 1 : 0) |
			(test_bit(LED_CAPSL,   dev->led) ? 2 : 0);
		real_leds(leds, adbhid->id);
		return 0;
	}

	return -1;
}

static int
adb_message_handler(struct notifier_block *this, unsigned long code, void *x)
{
	switch (code) {
	case ADB_MSG_PRE_RESET:
	case ADB_MSG_POWERDOWN:
		/* Stop the repeat timer. Autopoll is already off at this point */
		{
			int i;
			for (i = 1; i < 16; i++) {
				if (adbhid[i])
					del_timer_sync(&adbhid[i]->input->timer);
			}
		}

		/* Stop pending led requests */
		while(leds_req_pending)
			adb_poll();
		break;

	case ADB_MSG_POST_RESET:
		adbhid_probe();
		break;
	}
	return NOTIFY_DONE;
}

static int
adbhid_input_register(int id, int default_id, int original_handler_id,
		      int current_handler_id, int mouse_kind)
{
	struct adbhid *hid;
	struct input_dev *input_dev;
	int err;
	int i;

	if (adbhid[id]) {
		printk(KERN_ERR "Trying to reregister ADB HID on ID %d\n", id);
		return -EEXIST;
	}

	adbhid[id] = hid = kzalloc(sizeof(struct adbhid), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!hid || !input_dev) {
		err = -ENOMEM;
		goto fail;
	}

	sprintf(hid->phys, "adb%d:%d.%02x/input", id, default_id, original_handler_id);

	hid->input = input_dev;
	hid->id = default_id;
	hid->original_handler_id = original_handler_id;
	hid->current_handler_id = current_handler_id;
	hid->mouse_kind = mouse_kind;
	hid->flags = 0;
	input_set_drvdata(input_dev, hid);
	input_dev->name = hid->name;
	input_dev->phys = hid->phys;
	input_dev->id.bustype = BUS_ADB;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = (id << 12) | (default_id << 8) | original_handler_id;
	input_dev->id.version = 0x0100;

	switch (default_id) {
	case ADB_KEYBOARD:
		hid->keycode = kmalloc(sizeof(adb_to_linux_keycodes), GFP_KERNEL);
		if (!hid->keycode) {
			err = -ENOMEM;
			goto fail;
		}

		sprintf(hid->name, "ADB keyboard");

		memcpy(hid->keycode, adb_to_linux_keycodes, sizeof(adb_to_linux_keycodes));

		printk(KERN_INFO "Detected ADB keyboard, type ");
		switch (original_handler_id) {
		default:
			printk("<unknown>.\n");
			input_dev->id.version = ADB_KEYBOARD_UNKNOWN;
			break;

		case 0x01: case 0x02: case 0x03: case 0x06: case 0x08:
		case 0x0C: case 0x10: case 0x18: case 0x1B: case 0x1C:
		case 0xC0: case 0xC3: case 0xC6:
			printk("ANSI.\n");
			input_dev->id.version = ADB_KEYBOARD_ANSI;
			break;

		case 0x04: case 0x05: case 0x07: case 0x09: case 0x0D:
		case 0x11: case 0x14: case 0x19: case 0x1D: case 0xC1:
		case 0xC4: case 0xC7:
			printk("ISO, swapping keys.\n");
			input_dev->id.version = ADB_KEYBOARD_ISO;
			i = hid->keycode[10];
			hid->keycode[10] = hid->keycode[50];
			hid->keycode[50] = i;
			break;

		case 0x12: case 0x15: case 0x16: case 0x17: case 0x1A:
		case 0x1E: case 0xC2: case 0xC5: case 0xC8: case 0xC9:
			printk("JIS.\n");
			input_dev->id.version = ADB_KEYBOARD_JIS;
			break;
		}

		for (i = 0; i < 128; i++)
			if (hid->keycode[i])
				set_bit(hid->keycode[i], input_dev->keybit);

		input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_LED) |
			BIT_MASK(EV_REP);
		input_dev->ledbit[0] = BIT_MASK(LED_SCROLLL) |
			BIT_MASK(LED_CAPSL) | BIT_MASK(LED_NUML);
		input_dev->event = adbhid_kbd_event;
		input_dev->keycodemax = KEY_FN;
		input_dev->keycodesize = sizeof(hid->keycode[0]);
		break;

	case ADB_MOUSE:
		sprintf(hid->name, "ADB mouse");

		input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
		input_dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) |
			BIT_MASK(BTN_MIDDLE) | BIT_MASK(BTN_RIGHT);
		input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);
		break;

	case ADB_MISC:
		switch (original_handler_id) {
		case 0x02: /* Adjustable keyboard button device */
			sprintf(hid->name, "ADB adjustable keyboard buttons");
			input_dev->evbit[0] = BIT_MASK(EV_KEY) |
				BIT_MASK(EV_REP);
			set_bit(KEY_SOUND, input_dev->keybit);
			set_bit(KEY_MUTE, input_dev->keybit);
			set_bit(KEY_VOLUMEUP, input_dev->keybit);
			set_bit(KEY_VOLUMEDOWN, input_dev->keybit);
			break;
		case 0x1f: /* Powerbook button device */
			sprintf(hid->name, "ADB Powerbook buttons");
			input_dev->evbit[0] = BIT_MASK(EV_KEY) |
				BIT_MASK(EV_REP);
			set_bit(KEY_MUTE, input_dev->keybit);
			set_bit(KEY_VOLUMEUP, input_dev->keybit);
			set_bit(KEY_VOLUMEDOWN, input_dev->keybit);
			set_bit(KEY_BRIGHTNESSUP, input_dev->keybit);
			set_bit(KEY_BRIGHTNESSDOWN, input_dev->keybit);
			set_bit(KEY_EJECTCD, input_dev->keybit);
			set_bit(KEY_SWITCHVIDEOMODE, input_dev->keybit);
			set_bit(KEY_KBDILLUMTOGGLE, input_dev->keybit);
			set_bit(KEY_KBDILLUMDOWN, input_dev->keybit);
			set_bit(KEY_KBDILLUMUP, input_dev->keybit);
			break;
		}
		if (hid->name[0])
			break;
		/* else fall through */

	default:
		printk(KERN_INFO "Trying to register unknown ADB device to input layer.\n");
		err = -ENODEV;
		goto fail;
	}

	input_dev->keycode = hid->keycode;

	err = input_register_device(input_dev);
	if (err)
		goto fail;

	if (default_id == ADB_KEYBOARD) {
		/* HACK WARNING!! This should go away as soon there is an utility
		 * to control that for event devices.
		 */
		input_dev->rep[REP_DELAY] = 500;   /* input layer default: 250 */
		input_dev->rep[REP_PERIOD] = 66; /* input layer default: 33 */
	}

	return 0;

 fail:	input_free_device(input_dev);
	if (hid) {
		kfree(hid->keycode);
		kfree(hid);
	}
	adbhid[id] = NULL;
	return err;
}

static void adbhid_input_unregister(int id)
{
	input_unregister_device(adbhid[id]->input);
	kfree(adbhid[id]->keycode);
	kfree(adbhid[id]);
	adbhid[id] = NULL;
}


static u16
adbhid_input_reregister(int id, int default_id, int org_handler_id,
			int cur_handler_id, int mk)
{
	if (adbhid[id]) {
		if (adbhid[id]->input->id.product !=
		    ((id << 12)|(default_id << 8)|org_handler_id)) {
			adbhid_input_unregister(id);
			adbhid_input_register(id, default_id, org_handler_id,
					      cur_handler_id, mk);
		}
	} else
		adbhid_input_register(id, default_id, org_handler_id,
				      cur_handler_id, mk);
	return 1<<id;
}

static void
adbhid_input_devcleanup(u16 exist)
{
	int i;
	for(i=1; i<16; i++)
		if (adbhid[i] && !(exist&(1<<i)))
			adbhid_input_unregister(i);
}

static void
adbhid_probe(void)
{
	struct adb_request req;
	int i, default_id, org_handler_id, cur_handler_id;
	u16 reg = 0;

	adb_register(ADB_MOUSE, 0, &mouse_ids, adbhid_mouse_input);
	adb_register(ADB_KEYBOARD, 0, &keyboard_ids, adbhid_keyboard_input);
	adb_register(ADB_MISC, 0, &buttons_ids, adbhid_buttons_input);

	for (i = 0; i < keyboard_ids.nids; i++) {
		int id = keyboard_ids.id[i];

		adb_get_infos(id, &default_id, &org_handler_id);

		/* turn off all leds */
		adb_request(&req, NULL, ADBREQ_SYNC, 3,
			    ADB_WRITEREG(id, KEYB_LEDREG), 0xff, 0xff);

		/* Enable full feature set of the keyboard
		   ->get it to send separate codes for left and right shift,
		   control, option keys */
#if 0		/* handler 5 doesn't send separate codes for R modifiers */
		if (adb_try_handler_change(id, 5))
			printk("ADB keyboard at %d, handler set to 5\n", id);
		else
#endif
		if (adb_try_handler_change(id, 3))
			printk("ADB keyboard at %d, handler set to 3\n", id);
		else
			printk("ADB keyboard at %d, handler 1\n", id);

		adb_get_infos(id, &default_id, &cur_handler_id);
		reg |= adbhid_input_reregister(id, default_id, org_handler_id,
					       cur_handler_id, 0);
	}

	for (i = 0; i < buttons_ids.nids; i++) {
		int id = buttons_ids.id[i];

		adb_get_infos(id, &default_id, &org_handler_id);
		reg |= adbhid_input_reregister(id, default_id, org_handler_id,
					       org_handler_id, 0);
	}

	/* Try to switch all mice to handler 4, or 2 for three-button
	   mode and full resolution. */
	for (i = 0; i < mouse_ids.nids; i++) {
		int id = mouse_ids.id[i];
		int mouse_kind;

		adb_get_infos(id, &default_id, &org_handler_id);

		if (adb_try_handler_change(id, 4)) {
			printk("ADB mouse at %d, handler set to 4", id);
			mouse_kind = ADBMOUSE_EXTENDED;
		}
		else if (adb_try_handler_change(id, 0x2F)) {
			printk("ADB mouse at %d, handler set to 0x2F", id);
			mouse_kind = ADBMOUSE_MICROSPEED;
		}
		else if (adb_try_handler_change(id, 0x42)) {
			printk("ADB mouse at %d, handler set to 0x42", id);
			mouse_kind = ADBMOUSE_TRACKBALLPRO;
		}
		else if (adb_try_handler_change(id, 0x66)) {
			printk("ADB mouse at %d, handler set to 0x66", id);
			mouse_kind = ADBMOUSE_MICROSPEED;
		}
		else if (adb_try_handler_change(id, 0x5F)) {
			printk("ADB mouse at %d, handler set to 0x5F", id);
			mouse_kind = ADBMOUSE_MICROSPEED;
		}
		else if (adb_try_handler_change(id, 3)) {
			printk("ADB mouse at %d, handler set to 3", id);
			mouse_kind = ADBMOUSE_MS_A3;
		}
		else if (adb_try_handler_change(id, 2)) {
			printk("ADB mouse at %d, handler set to 2", id);
			mouse_kind = ADBMOUSE_STANDARD_200;
		}
		else {
			printk("ADB mouse at %d, handler 1", id);
			mouse_kind = ADBMOUSE_STANDARD_100;
		}

		if ((mouse_kind == ADBMOUSE_TRACKBALLPRO)
		    || (mouse_kind == ADBMOUSE_MICROSPEED)) {
			init_microspeed(id);
		} else if (mouse_kind == ADBMOUSE_MS_A3) {
			init_ms_a3(id);
		} else if (mouse_kind ==  ADBMOUSE_EXTENDED) {
			/*
			 * Register 1 is usually used for device
			 * identification.  Here, we try to identify
			 * a known device and call the appropriate
			 * init function.
			 */
			adb_request(&req, NULL, ADBREQ_SYNC | ADBREQ_REPLY, 1,
				    ADB_READREG(id, 1));

			if ((req.reply_len) &&
			    (req.reply[1] == 0x9a) && ((req.reply[2] == 0x21)
			    	|| (req.reply[2] == 0x20))) {
				mouse_kind = ADBMOUSE_TRACKBALL;
				init_trackball(id);
			}
			else if ((req.reply_len >= 4) &&
			    (req.reply[1] == 0x74) && (req.reply[2] == 0x70) &&
			    (req.reply[3] == 0x61) && (req.reply[4] == 0x64)) {
				mouse_kind = ADBMOUSE_TRACKPAD;
				init_trackpad(id);
			}
			else if ((req.reply_len >= 4) &&
			    (req.reply[1] == 0x4b) && (req.reply[2] == 0x4d) &&
			    (req.reply[3] == 0x4c) && (req.reply[4] == 0x31)) {
				mouse_kind = ADBMOUSE_TURBOMOUSE5;
				init_turbomouse(id);
			}
			else if ((req.reply_len == 9) &&
			    (req.reply[1] == 0x4b) && (req.reply[2] == 0x4f) &&
			    (req.reply[3] == 0x49) && (req.reply[4] == 0x54)) {
				if (adb_try_handler_change(id, 0x42)) {
					printk("\nADB MacAlly 2-button mouse at %d, handler set to 0x42", id);
					mouse_kind = ADBMOUSE_MACALLY2;
				}
			}
		}
		printk("\n");

		adb_get_infos(id, &default_id, &cur_handler_id);
		reg |= adbhid_input_reregister(id, default_id, org_handler_id,
					       cur_handler_id, mouse_kind);
	}
	adbhid_input_devcleanup(reg);
}

static void 
init_trackpad(int id)
{
	struct adb_request req;
	unsigned char r1_buffer[8];

	printk(" (trackpad)");

	adb_request(&req, NULL, ADBREQ_SYNC | ADBREQ_REPLY, 1,
		    ADB_READREG(id,1));
	if (req.reply_len < 8)
	    printk("bad length for reg. 1\n");
	else
	{
	    memcpy(r1_buffer, &req.reply[1], 8);

	    adb_request(&req, NULL, ADBREQ_SYNC, 9,
	        ADB_WRITEREG(id,1),
	            r1_buffer[0],
	            r1_buffer[1],
	            r1_buffer[2],
	            r1_buffer[3],
	            r1_buffer[4],
	            r1_buffer[5],
	            0x0d,
	            r1_buffer[7]);

            adb_request(&req, NULL, ADBREQ_SYNC, 9,
	        ADB_WRITEREG(id,2),
	    	    0x99,
	    	    0x94,
	    	    0x19,
	    	    0xff,
	    	    0xb2,
	    	    0x8a,
	    	    0x1b,
	    	    0x50);

	    adb_request(&req, NULL, ADBREQ_SYNC, 9,
	        ADB_WRITEREG(id,1),
	            r1_buffer[0],
	            r1_buffer[1],
	            r1_buffer[2],
	            r1_buffer[3],
	            r1_buffer[4],
	            r1_buffer[5],
	            0x03, /*r1_buffer[6],*/
	            r1_buffer[7]);

	    /* Without this flush, the trackpad may be locked up */
	    adb_request(&req, NULL, ADBREQ_SYNC, 1, ADB_FLUSH(id));
        }
}

static void 
init_trackball(int id)
{
	struct adb_request req;

	printk(" (trackman/mouseman)");

	adb_request(&req, NULL, ADBREQ_SYNC, 3,
	ADB_WRITEREG(id,1), 00,0x81);

	adb_request(&req, NULL, ADBREQ_SYNC, 3,
	ADB_WRITEREG(id,1), 01,0x81);

	adb_request(&req, NULL, ADBREQ_SYNC, 3,
	ADB_WRITEREG(id,1), 02,0x81);

	adb_request(&req, NULL, ADBREQ_SYNC, 3,
	ADB_WRITEREG(id,1), 03,0x38);

	adb_request(&req, NULL, ADBREQ_SYNC, 3,
	ADB_WRITEREG(id,1), 00,0x81);

	adb_request(&req, NULL, ADBREQ_SYNC, 3,
	ADB_WRITEREG(id,1), 01,0x81);

	adb_request(&req, NULL, ADBREQ_SYNC, 3,
	ADB_WRITEREG(id,1), 02,0x81);

	adb_request(&req, NULL, ADBREQ_SYNC, 3,
	ADB_WRITEREG(id,1), 03,0x38);
}

static void
init_turbomouse(int id)
{
	struct adb_request req;

        printk(" (TurboMouse 5)");

	adb_request(&req, NULL, ADBREQ_SYNC, 1, ADB_FLUSH(id));

	adb_request(&req, NULL, ADBREQ_SYNC, 1, ADB_FLUSH(3));

	adb_request(&req, NULL, ADBREQ_SYNC, 9,
	ADB_WRITEREG(3,2),
	    0xe7,
	    0x8c,
	    0,
	    0,
	    0,
	    0xff,
	    0xff,
	    0x94);

	adb_request(&req, NULL, ADBREQ_SYNC, 1, ADB_FLUSH(3));

	adb_request(&req, NULL, ADBREQ_SYNC, 9,
	ADB_WRITEREG(3,2),
	    0xa5,
	    0x14,
	    0,
	    0,
	    0x69,
	    0xff,
	    0xff,
	    0x27);
}

static void
init_microspeed(int id)
{
	struct adb_request req;

        printk(" (Microspeed/MacPoint or compatible)");

	adb_request(&req, NULL, ADBREQ_SYNC, 1, ADB_FLUSH(id));

	/* This will initialize mice using the Microspeed, MacPoint and
	   other compatible firmware. Bit 12 enables extended protocol.
	   
	   Register 1 Listen (4 Bytes)
            0 -  3     Button is mouse (set also for double clicking!!!)
            4 -  7     Button is locking (affects change speed also)
            8 - 11     Button changes speed
           12          1 = Extended mouse mode, 0 = normal mouse mode
           13 - 15     unused 0
           16 - 23     normal speed
           24 - 31     changed speed

       Register 1 talk holds version and product identification information.
       Register 1 Talk (4 Bytes):
            0 -  7     Product code
            8 - 23     undefined, reserved
           24 - 31     Version number
        
       Speed 0 is max. 1 to 255 set speed in increments of 1/256 of max.
 */
	adb_request(&req, NULL, ADBREQ_SYNC, 5,
	ADB_WRITEREG(id,1),
	    0x20,	/* alt speed = 0x20 (rather slow) */
	    0x00,	/* norm speed = 0x00 (fastest) */
	    0x10,	/* extended protocol, no speed change */
	    0x07);	/* all buttons enabled as mouse buttons, no locking */


	adb_request(&req, NULL, ADBREQ_SYNC, 1, ADB_FLUSH(id));
}

static void
init_ms_a3(int id)
{
	struct adb_request req;

	printk(" (Mouse Systems A3 Mouse, or compatible)");
	adb_request(&req, NULL, ADBREQ_SYNC, 3,
	ADB_WRITEREG(id, 0x2),
	    0x00,
	    0x07);
 
 	adb_request(&req, NULL, ADBREQ_SYNC, 1, ADB_FLUSH(id));
}

static int __init adbhid_init(void)
{
#ifndef CONFIG_MAC
	if (!machine_is(chrp) && !machine_is(powermac))
		return 0;
#endif

	led_request.complete = 1;

	adbhid_probe();

	blocking_notifier_chain_register(&adb_client_list,
			&adbhid_adb_notifier);

	return 0;
}

static void __exit adbhid_exit(void)
{
}
 
module_init(adbhid_init);
module_exit(adbhid_exit);
