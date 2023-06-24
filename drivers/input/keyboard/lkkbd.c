// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2004 by Jan-Benedict Glaw <jbglaw@lug-owl.de>
 */

/*
 * LK keyboard driver for Linux, based on sunkbd.c (C) by Vojtech Pavlik
 */

/*
 * DEC LK201 and LK401 keyboard driver for Linux (primary for DECstations
 * and VAXstations, but can also be used on any standard RS232 with an
 * adaptor).
 *
 * DISCLAIMER: This works for _me_. If you break anything by using the
 * information given below, I will _not_ be liable!
 *
 * RJ10 pinout:		To DE9:		Or DB25:
 *	1 - RxD <---->	Pin 3 (TxD) <->	Pin 2 (TxD)
 *	2 - GND <---->	Pin 5 (GND) <->	Pin 7 (GND)
 *	4 - TxD <---->	Pin 2 (RxD) <->	Pin 3 (RxD)
 *	3 - +12V (from HDD drive connector), DON'T connect to DE9 or DB25!!!
 *
 * Pin numbers for DE9 and DB25 are noted on the plug (quite small:). For
 * RJ10, it's like this:
 *
 *      __=__	Hold the plug in front of you, cable downwards,
 *     /___/|	nose is hidden behind the plug. Now, pin 1 is at
 *    |1234||	the left side, pin 4 at the right and 2 and 3 are
 *    |IIII||	in between, of course:)
 *    |    ||
 *    |____|/
 *      ||	So the adaptor consists of three connected cables
 *      ||	for data transmission (RxD and TxD) and signal ground.
 *		Additionally, you have to get +12V from somewhere.
 * Most easily, you'll get that from a floppy or HDD power connector.
 * It's the yellow cable there (black is ground and red is +5V).
 *
 * The keyboard and all the commands it understands are documented in
 * "VCB02 Video Subsystem - Technical Manual", EK-104AA-TM-001. This
 * document is LK201 specific, but LK401 is mostly compatible. It comes
 * up in LK201 mode and doesn't report any of the additional keys it
 * has. These need to be switched on with the LK_CMD_ENABLE_LK401
 * command. You'll find this document (scanned .pdf file) on MANX,
 * a search engine specific to DEC documentation. Try
 * http://www.vt100.net/manx/details?pn=EK-104AA-TM-001;id=21;cp=1
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/workqueue.h>

#define DRIVER_DESC	"LK keyboard driver"

MODULE_AUTHOR("Jan-Benedict Glaw <jbglaw@lug-owl.de>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Known parameters:
 *	bell_volume
 *	keyclick_volume
 *	ctrlclick_volume
 *
 * Please notice that there's not yet an API to set these at runtime.
 */
static int bell_volume = 100; /* % */
module_param(bell_volume, int, 0);
MODULE_PARM_DESC(bell_volume, "Bell volume (in %). default is 100%");

static int keyclick_volume = 100; /* % */
module_param(keyclick_volume, int, 0);
MODULE_PARM_DESC(keyclick_volume, "Keyclick volume (in %), default is 100%");

static int ctrlclick_volume = 100; /* % */
module_param(ctrlclick_volume, int, 0);
MODULE_PARM_DESC(ctrlclick_volume, "Ctrlclick volume (in %), default is 100%");

static int lk201_compose_is_alt;
module_param(lk201_compose_is_alt, int, 0);
MODULE_PARM_DESC(lk201_compose_is_alt,
		 "If set non-zero, LK201' Compose key will act as an Alt key");



#undef LKKBD_DEBUG
#ifdef LKKBD_DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...) do {} while (0)
#endif

/* LED control */
#define LK_LED_WAIT		0x81
#define LK_LED_COMPOSE		0x82
#define LK_LED_SHIFTLOCK	0x84
#define LK_LED_SCROLLLOCK	0x88
#define LK_CMD_LED_ON		0x13
#define LK_CMD_LED_OFF		0x11

/* Mode control */
#define LK_MODE_DOWN		0x80
#define LK_MODE_AUTODOWN	0x82
#define LK_MODE_UPDOWN		0x86
#define LK_CMD_SET_MODE(mode, div)	((mode) | ((div) << 3))

/* Misc commands */
#define LK_CMD_ENABLE_KEYCLICK	0x1b
#define LK_CMD_DISABLE_KEYCLICK	0x99
#define LK_CMD_DISABLE_BELL	0xa1
#define LK_CMD_SOUND_BELL	0xa7
#define LK_CMD_ENABLE_BELL	0x23
#define LK_CMD_DISABLE_CTRCLICK	0xb9
#define LK_CMD_ENABLE_CTRCLICK	0xbb
#define LK_CMD_SET_DEFAULTS	0xd3
#define LK_CMD_POWERCYCLE_RESET	0xfd
#define LK_CMD_ENABLE_LK401	0xe9
#define LK_CMD_REQUEST_ID	0xab

/* Misc responses from keyboard */
#define LK_STUCK_KEY		0x3d
#define LK_SELFTEST_FAILED	0x3e
#define LK_ALL_KEYS_UP		0xb3
#define LK_METRONOME		0xb4
#define LK_OUTPUT_ERROR		0xb5
#define LK_INPUT_ERROR		0xb6
#define LK_KBD_LOCKED		0xb7
#define LK_KBD_TEST_MODE_ACK	0xb8
#define LK_PREFIX_KEY_DOWN	0xb9
#define LK_MODE_CHANGE_ACK	0xba
#define LK_RESPONSE_RESERVED	0xbb

#define LK_NUM_KEYCODES		256
#define LK_NUM_IGNORE_BYTES	6

static unsigned short lkkbd_keycode[LK_NUM_KEYCODES] = {
	[0x56] = KEY_F1,
	[0x57] = KEY_F2,
	[0x58] = KEY_F3,
	[0x59] = KEY_F4,
	[0x5a] = KEY_F5,
	[0x64] = KEY_F6,
	[0x65] = KEY_F7,
	[0x66] = KEY_F8,
	[0x67] = KEY_F9,
	[0x68] = KEY_F10,
	[0x71] = KEY_F11,
	[0x72] = KEY_F12,
	[0x73] = KEY_F13,
	[0x74] = KEY_F14,
	[0x7c] = KEY_F15,
	[0x7d] = KEY_F16,
	[0x80] = KEY_F17,
	[0x81] = KEY_F18,
	[0x82] = KEY_F19,
	[0x83] = KEY_F20,
	[0x8a] = KEY_FIND,
	[0x8b] = KEY_INSERT,
	[0x8c] = KEY_DELETE,
	[0x8d] = KEY_SELECT,
	[0x8e] = KEY_PAGEUP,
	[0x8f] = KEY_PAGEDOWN,
	[0x92] = KEY_KP0,
	[0x94] = KEY_KPDOT,
	[0x95] = KEY_KPENTER,
	[0x96] = KEY_KP1,
	[0x97] = KEY_KP2,
	[0x98] = KEY_KP3,
	[0x99] = KEY_KP4,
	[0x9a] = KEY_KP5,
	[0x9b] = KEY_KP6,
	[0x9c] = KEY_KPCOMMA,
	[0x9d] = KEY_KP7,
	[0x9e] = KEY_KP8,
	[0x9f] = KEY_KP9,
	[0xa0] = KEY_KPMINUS,
	[0xa1] = KEY_PROG1,
	[0xa2] = KEY_PROG2,
	[0xa3] = KEY_PROG3,
	[0xa4] = KEY_PROG4,
	[0xa7] = KEY_LEFT,
	[0xa8] = KEY_RIGHT,
	[0xa9] = KEY_DOWN,
	[0xaa] = KEY_UP,
	[0xab] = KEY_RIGHTSHIFT,
	[0xac] = KEY_LEFTALT,
	[0xad] = KEY_COMPOSE, /* Right Compose, that is. */
	[0xae] = KEY_LEFTSHIFT, /* Same as KEY_RIGHTSHIFT on LK201 */
	[0xaf] = KEY_LEFTCTRL,
	[0xb0] = KEY_CAPSLOCK,
	[0xb1] = KEY_COMPOSE, /* Left Compose, that is. */
	[0xb2] = KEY_RIGHTALT,
	[0xbc] = KEY_BACKSPACE,
	[0xbd] = KEY_ENTER,
	[0xbe] = KEY_TAB,
	[0xbf] = KEY_ESC,
	[0xc0] = KEY_1,
	[0xc1] = KEY_Q,
	[0xc2] = KEY_A,
	[0xc3] = KEY_Z,
	[0xc5] = KEY_2,
	[0xc6] = KEY_W,
	[0xc7] = KEY_S,
	[0xc8] = KEY_X,
	[0xc9] = KEY_102ND,
	[0xcb] = KEY_3,
	[0xcc] = KEY_E,
	[0xcd] = KEY_D,
	[0xce] = KEY_C,
	[0xd0] = KEY_4,
	[0xd1] = KEY_R,
	[0xd2] = KEY_F,
	[0xd3] = KEY_V,
	[0xd4] = KEY_SPACE,
	[0xd6] = KEY_5,
	[0xd7] = KEY_T,
	[0xd8] = KEY_G,
	[0xd9] = KEY_B,
	[0xdb] = KEY_6,
	[0xdc] = KEY_Y,
	[0xdd] = KEY_H,
	[0xde] = KEY_N,
	[0xe0] = KEY_7,
	[0xe1] = KEY_U,
	[0xe2] = KEY_J,
	[0xe3] = KEY_M,
	[0xe5] = KEY_8,
	[0xe6] = KEY_I,
	[0xe7] = KEY_K,
	[0xe8] = KEY_COMMA,
	[0xea] = KEY_9,
	[0xeb] = KEY_O,
	[0xec] = KEY_L,
	[0xed] = KEY_DOT,
	[0xef] = KEY_0,
	[0xf0] = KEY_P,
	[0xf2] = KEY_SEMICOLON,
	[0xf3] = KEY_SLASH,
	[0xf5] = KEY_EQUAL,
	[0xf6] = KEY_RIGHTBRACE,
	[0xf7] = KEY_BACKSLASH,
	[0xf9] = KEY_MINUS,
	[0xfa] = KEY_LEFTBRACE,
	[0xfb] = KEY_APOSTROPHE,
};

#define CHECK_LED(LK, VAR_ON, VAR_OFF, LED, BITS) do {		\
	if (test_bit(LED, (LK)->dev->led))			\
		VAR_ON |= BITS;					\
	else							\
		VAR_OFF |= BITS;				\
	} while (0)

/*
 * Per-keyboard data
 */
struct lkkbd {
	unsigned short keycode[LK_NUM_KEYCODES];
	int ignore_bytes;
	unsigned char id[LK_NUM_IGNORE_BYTES];
	struct input_dev *dev;
	struct serio *serio;
	struct work_struct tq;
	char name[64];
	char phys[32];
	char type;
	int bell_volume;
	int keyclick_volume;
	int ctrlclick_volume;
};

#ifdef LKKBD_DEBUG
/*
 * Responses from the keyboard and mapping back to their names.
 */
static struct {
	unsigned char value;
	unsigned char *name;
} lk_response[] = {
#define RESPONSE(x) { .value = (x), .name = #x, }
	RESPONSE(LK_STUCK_KEY),
	RESPONSE(LK_SELFTEST_FAILED),
	RESPONSE(LK_ALL_KEYS_UP),
	RESPONSE(LK_METRONOME),
	RESPONSE(LK_OUTPUT_ERROR),
	RESPONSE(LK_INPUT_ERROR),
	RESPONSE(LK_KBD_LOCKED),
	RESPONSE(LK_KBD_TEST_MODE_ACK),
	RESPONSE(LK_PREFIX_KEY_DOWN),
	RESPONSE(LK_MODE_CHANGE_ACK),
	RESPONSE(LK_RESPONSE_RESERVED),
#undef RESPONSE
};

static unsigned char *response_name(unsigned char value)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(lk_response); i++)
		if (lk_response[i].value == value)
			return lk_response[i].name;

	return "<unknown>";
}
#endif /* LKKBD_DEBUG */

/*
 * Calculate volume parameter byte for a given volume.
 */
static unsigned char volume_to_hw(int volume_percent)
{
	unsigned char ret = 0;

	if (volume_percent < 0)
		volume_percent = 0;
	if (volume_percent > 100)
		volume_percent = 100;

	if (volume_percent >= 0)
		ret = 7;
	if (volume_percent >= 13)	/* 12.5 */
		ret = 6;
	if (volume_percent >= 25)
		ret = 5;
	if (volume_percent >= 38)	/* 37.5 */
		ret = 4;
	if (volume_percent >= 50)
		ret = 3;
	if (volume_percent >= 63)	/* 62.5 */
		ret = 2;		/* This is the default volume */
	if (volume_percent >= 75)
		ret = 1;
	if (volume_percent >= 88)	/* 87.5 */
		ret = 0;

	ret |= 0x80;

	return ret;
}

static void lkkbd_detection_done(struct lkkbd *lk)
{
	int i;

	/*
	 * Reset setting for Compose key. Let Compose be KEY_COMPOSE.
	 */
	lk->keycode[0xb1] = KEY_COMPOSE;

	/*
	 * Print keyboard name and modify Compose=Alt on user's request.
	 */
	switch (lk->id[4]) {
	case 1:
		strscpy(lk->name, "DEC LK201 keyboard", sizeof(lk->name));

		if (lk201_compose_is_alt)
			lk->keycode[0xb1] = KEY_LEFTALT;
		break;

	case 2:
		strscpy(lk->name, "DEC LK401 keyboard", sizeof(lk->name));
		break;

	default:
		strscpy(lk->name, "Unknown DEC keyboard", sizeof(lk->name));
		printk(KERN_ERR
			"lkkbd: keyboard on %s is unknown, please report to "
			"Jan-Benedict Glaw <jbglaw@lug-owl.de>\n", lk->phys);
		printk(KERN_ERR "lkkbd: keyboard ID'ed as:");
		for (i = 0; i < LK_NUM_IGNORE_BYTES; i++)
			printk(" 0x%02x", lk->id[i]);
		printk("\n");
		break;
	}

	printk(KERN_INFO "lkkbd: keyboard on %s identified as: %s\n",
		lk->phys, lk->name);

	/*
	 * Report errors during keyboard boot-up.
	 */
	switch (lk->id[2]) {
	case 0x00:
		/* All okay */
		break;

	case LK_STUCK_KEY:
		printk(KERN_ERR "lkkbd: Stuck key on keyboard at %s\n",
			lk->phys);
		break;

	case LK_SELFTEST_FAILED:
		printk(KERN_ERR
			"lkkbd: Selftest failed on keyboard at %s, "
			"keyboard may not work properly\n", lk->phys);
		break;

	default:
		printk(KERN_ERR
			"lkkbd: Unknown error %02x on keyboard at %s\n",
			lk->id[2], lk->phys);
		break;
	}

	/*
	 * Try to hint user if there's a stuck key.
	 */
	if (lk->id[2] == LK_STUCK_KEY && lk->id[3] != 0)
		printk(KERN_ERR
			"Scancode of stuck key is 0x%02x, keycode is 0x%04x\n",
			lk->id[3], lk->keycode[lk->id[3]]);
}

/*
 * lkkbd_interrupt() is called by the low level driver when a character
 * is received.
 */
static irqreturn_t lkkbd_interrupt(struct serio *serio,
				   unsigned char data, unsigned int flags)
{
	struct lkkbd *lk = serio_get_drvdata(serio);
	struct input_dev *input_dev = lk->dev;
	unsigned int keycode;
	int i;

	DBG(KERN_INFO "Got byte 0x%02x\n", data);

	if (lk->ignore_bytes > 0) {
		DBG(KERN_INFO "Ignoring a byte on %s\n", lk->name);
		lk->id[LK_NUM_IGNORE_BYTES - lk->ignore_bytes--] = data;

		if (lk->ignore_bytes == 0)
			lkkbd_detection_done(lk);

		return IRQ_HANDLED;
	}

	switch (data) {
	case LK_ALL_KEYS_UP:
		for (i = 0; i < ARRAY_SIZE(lkkbd_keycode); i++)
			input_report_key(input_dev, lk->keycode[i], 0);
		input_sync(input_dev);
		break;

	case 0x01:
		DBG(KERN_INFO "Got 0x01, scheduling re-initialization\n");
		lk->ignore_bytes = LK_NUM_IGNORE_BYTES;
		lk->id[LK_NUM_IGNORE_BYTES - lk->ignore_bytes--] = data;
		schedule_work(&lk->tq);
		break;

	case LK_METRONOME:
	case LK_OUTPUT_ERROR:
	case LK_INPUT_ERROR:
	case LK_KBD_LOCKED:
	case LK_KBD_TEST_MODE_ACK:
	case LK_PREFIX_KEY_DOWN:
	case LK_MODE_CHANGE_ACK:
	case LK_RESPONSE_RESERVED:
		DBG(KERN_INFO "Got %s and don't know how to handle...\n",
			response_name(data));
		break;

	default:
		keycode = lk->keycode[data];
		if (keycode != KEY_RESERVED) {
			input_report_key(input_dev, keycode,
					 !test_bit(keycode, input_dev->key));
			input_sync(input_dev);
		} else {
			printk(KERN_WARNING
				"%s: Unknown key with scancode 0x%02x on %s.\n",
				__FILE__, data, lk->name);
		}
	}

	return IRQ_HANDLED;
}

static void lkkbd_toggle_leds(struct lkkbd *lk)
{
	struct serio *serio = lk->serio;
	unsigned char leds_on = 0;
	unsigned char leds_off = 0;

	CHECK_LED(lk, leds_on, leds_off, LED_CAPSL, LK_LED_SHIFTLOCK);
	CHECK_LED(lk, leds_on, leds_off, LED_COMPOSE, LK_LED_COMPOSE);
	CHECK_LED(lk, leds_on, leds_off, LED_SCROLLL, LK_LED_SCROLLLOCK);
	CHECK_LED(lk, leds_on, leds_off, LED_SLEEP, LK_LED_WAIT);
	if (leds_on != 0) {
		serio_write(serio, LK_CMD_LED_ON);
		serio_write(serio, leds_on);
	}
	if (leds_off != 0) {
		serio_write(serio, LK_CMD_LED_OFF);
		serio_write(serio, leds_off);
	}
}

static void lkkbd_toggle_keyclick(struct lkkbd *lk, bool on)
{
	struct serio *serio = lk->serio;

	if (on) {
		DBG("%s: Activating key clicks\n", __func__);
		serio_write(serio, LK_CMD_ENABLE_KEYCLICK);
		serio_write(serio, volume_to_hw(lk->keyclick_volume));
		serio_write(serio, LK_CMD_ENABLE_CTRCLICK);
		serio_write(serio, volume_to_hw(lk->ctrlclick_volume));
	} else {
		DBG("%s: Deactivating key clicks\n", __func__);
		serio_write(serio, LK_CMD_DISABLE_KEYCLICK);
		serio_write(serio, LK_CMD_DISABLE_CTRCLICK);
	}

}

/*
 * lkkbd_event() handles events from the input module.
 */
static int lkkbd_event(struct input_dev *dev,
			unsigned int type, unsigned int code, int value)
{
	struct lkkbd *lk = input_get_drvdata(dev);

	switch (type) {
	case EV_LED:
		lkkbd_toggle_leds(lk);
		return 0;

	case EV_SND:
		switch (code) {
		case SND_CLICK:
			lkkbd_toggle_keyclick(lk, value);
			return 0;

		case SND_BELL:
			if (value != 0)
				serio_write(lk->serio, LK_CMD_SOUND_BELL);

			return 0;
		}

		break;

	default:
		printk(KERN_ERR "%s(): Got unknown type %d, code %d, value %d\n",
			__func__, type, code, value);
	}

	return -1;
}

/*
 * lkkbd_reinit() sets leds and beeps to a state the computer remembers they
 * were in.
 */
static void lkkbd_reinit(struct work_struct *work)
{
	struct lkkbd *lk = container_of(work, struct lkkbd, tq);
	int division;

	/* Ask for ID */
	serio_write(lk->serio, LK_CMD_REQUEST_ID);

	/* Reset parameters */
	serio_write(lk->serio, LK_CMD_SET_DEFAULTS);

	/* Set LEDs */
	lkkbd_toggle_leds(lk);

	/*
	 * Try to activate extended LK401 mode. This command will
	 * only work with a LK401 keyboard and grants access to
	 * LAlt, RAlt, RCompose and RShift.
	 */
	serio_write(lk->serio, LK_CMD_ENABLE_LK401);

	/* Set all keys to UPDOWN mode */
	for (division = 1; division <= 14; division++)
		serio_write(lk->serio,
			    LK_CMD_SET_MODE(LK_MODE_UPDOWN, division));

	/* Enable bell and set volume */
	serio_write(lk->serio, LK_CMD_ENABLE_BELL);
	serio_write(lk->serio, volume_to_hw(lk->bell_volume));

	/* Enable/disable keyclick (and possibly set volume) */
	lkkbd_toggle_keyclick(lk, test_bit(SND_CLICK, lk->dev->snd));

	/* Sound the bell if needed */
	if (test_bit(SND_BELL, lk->dev->snd))
		serio_write(lk->serio, LK_CMD_SOUND_BELL);
}

/*
 * lkkbd_connect() probes for a LK keyboard and fills the necessary structures.
 */
static int lkkbd_connect(struct serio *serio, struct serio_driver *drv)
{
	struct lkkbd *lk;
	struct input_dev *input_dev;
	int i;
	int err;

	lk = kzalloc(sizeof(struct lkkbd), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!lk || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	lk->serio = serio;
	lk->dev = input_dev;
	INIT_WORK(&lk->tq, lkkbd_reinit);
	lk->bell_volume = bell_volume;
	lk->keyclick_volume = keyclick_volume;
	lk->ctrlclick_volume = ctrlclick_volume;
	memcpy(lk->keycode, lkkbd_keycode, sizeof(lk->keycode));

	strscpy(lk->name, "DEC LK keyboard", sizeof(lk->name));
	snprintf(lk->phys, sizeof(lk->phys), "%s/input0", serio->phys);

	input_dev->name = lk->name;
	input_dev->phys = lk->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_LKKBD;
	input_dev->id.product = 0;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;
	input_dev->event = lkkbd_event;

	input_set_drvdata(input_dev, lk);

	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_LED, input_dev->evbit);
	__set_bit(EV_SND, input_dev->evbit);
	__set_bit(EV_REP, input_dev->evbit);
	__set_bit(LED_CAPSL, input_dev->ledbit);
	__set_bit(LED_SLEEP, input_dev->ledbit);
	__set_bit(LED_COMPOSE, input_dev->ledbit);
	__set_bit(LED_SCROLLL, input_dev->ledbit);
	__set_bit(SND_BELL, input_dev->sndbit);
	__set_bit(SND_CLICK, input_dev->sndbit);

	input_dev->keycode = lk->keycode;
	input_dev->keycodesize = sizeof(lk->keycode[0]);
	input_dev->keycodemax = ARRAY_SIZE(lk->keycode);

	for (i = 0; i < LK_NUM_KEYCODES; i++)
		__set_bit(lk->keycode[i], input_dev->keybit);
	__clear_bit(KEY_RESERVED, input_dev->keybit);

	serio_set_drvdata(serio, lk);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	err = input_register_device(lk->dev);
	if (err)
		goto fail3;

	serio_write(lk->serio, LK_CMD_POWERCYCLE_RESET);

	return 0;

 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(lk);
	return err;
}

/*
 * lkkbd_disconnect() unregisters and closes behind us.
 */
static void lkkbd_disconnect(struct serio *serio)
{
	struct lkkbd *lk = serio_get_drvdata(serio);

	input_get_device(lk->dev);
	input_unregister_device(lk->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_put_device(lk->dev);
	kfree(lk);
}

static const struct serio_device_id lkkbd_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_LKKBD,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, lkkbd_serio_ids);

static struct serio_driver lkkbd_drv = {
	.driver		= {
		.name	= "lkkbd",
	},
	.description	= DRIVER_DESC,
	.id_table	= lkkbd_serio_ids,
	.connect	= lkkbd_connect,
	.disconnect	= lkkbd_disconnect,
	.interrupt	= lkkbd_interrupt,
};

module_serio_driver(lkkbd_drv);
