// SPDX-License-Identifier: GPL-2.0-only
/*
 * AT and PS/2 keyboard driver
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 */


/*
 * This driver can handle standard AT keyboards and PS/2 keyboards in
 * Translated and Raw Set 2 and Set 3, as well as AT keyboards on dumb
 * input-only controllers and AT keyboards connected over a one way RS232
 * converter.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/workqueue.h>
#include <linux/libps2.h>
#include <linux/mutex.h>
#include <linux/dmi.h>
#include <linux/property.h>

#define DRIVER_DESC	"AT and PS/2 keyboard driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

static int atkbd_set = 2;
module_param_named(set, atkbd_set, int, 0);
MODULE_PARM_DESC(set, "Select keyboard code set (2 = default, 3 = PS/2 native)");

#if defined(__i386__) || defined(__x86_64__) || defined(__hppa__)
static bool atkbd_reset;
#else
static bool atkbd_reset = true;
#endif
module_param_named(reset, atkbd_reset, bool, 0);
MODULE_PARM_DESC(reset, "Reset keyboard during initialization");

static bool atkbd_softrepeat;
module_param_named(softrepeat, atkbd_softrepeat, bool, 0);
MODULE_PARM_DESC(softrepeat, "Use software keyboard repeat");

static bool atkbd_softraw = true;
module_param_named(softraw, atkbd_softraw, bool, 0);
MODULE_PARM_DESC(softraw, "Use software generated rawmode");

static bool atkbd_scroll;
module_param_named(scroll, atkbd_scroll, bool, 0);
MODULE_PARM_DESC(scroll, "Enable scroll-wheel on MS Office and similar keyboards");

static bool atkbd_extra;
module_param_named(extra, atkbd_extra, bool, 0);
MODULE_PARM_DESC(extra, "Enable extra LEDs and keys on IBM RapidAcces, EzKey and similar keyboards");

static bool atkbd_terminal;
module_param_named(terminal, atkbd_terminal, bool, 0);
MODULE_PARM_DESC(terminal, "Enable break codes on an IBM Terminal keyboard connected via AT/PS2");

#define MAX_FUNCTION_ROW_KEYS	24

#define SCANCODE(keymap)	((keymap >> 16) & 0xFFFF)
#define KEYCODE(keymap)		(keymap & 0xFFFF)

/*
 * Scancode to keycode tables. These are just the default setting, and
 * are loadable via a userland utility.
 */

#define ATKBD_KEYMAP_SIZE	512

static const unsigned short atkbd_set2_keycode[ATKBD_KEYMAP_SIZE] = {

#ifdef CONFIG_KEYBOARD_ATKBD_HP_KEYCODES

/* XXX: need a more general approach */

#include "hpps2atkbd.h"	/* include the keyboard scancodes */

#else
	  0, 67, 65, 63, 61, 59, 60, 88,  0, 68, 66, 64, 62, 15, 41,117,
	  0, 56, 42, 93, 29, 16,  2,  0,  0,  0, 44, 31, 30, 17,  3,  0,
	  0, 46, 45, 32, 18,  5,  4, 95,  0, 57, 47, 33, 20, 19,  6,183,
	  0, 49, 48, 35, 34, 21,  7,184,  0,  0, 50, 36, 22,  8,  9,185,
	  0, 51, 37, 23, 24, 11, 10,  0,  0, 52, 53, 38, 39, 25, 12,  0,
	  0, 89, 40,  0, 26, 13,  0,  0, 58, 54, 28, 27,  0, 43,  0, 85,
	  0, 86, 91, 90, 92,  0, 14, 94,  0, 79,124, 75, 71,121,  0,  0,
	 82, 83, 80, 76, 77, 72,  1, 69, 87, 78, 81, 74, 55, 73, 70, 99,

	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	217,100,255,  0, 97,165,  0,  0,156,  0,  0,  0,  0,  0,  0,125,
	173,114,  0,113,  0,  0,  0,126,128,  0,  0,140,  0,  0,  0,127,
	159,  0,115,  0,164,  0,  0,116,158,  0,172,166,  0,  0,  0,142,
	157,  0,  0,  0,  0,  0,  0,  0,155,  0, 98,  0,  0,163,  0,  0,
	226,  0,  0,  0,  0,  0,  0,  0,  0,255, 96,  0,  0,  0,143,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,107,  0,105,102,  0,  0,112,
	110,111,108,112,106,103,  0,119,  0,118,109,  0, 99,104,119,  0,

	  0,  0,  0, 65, 99,
#endif
};

static const unsigned short atkbd_set3_keycode[ATKBD_KEYMAP_SIZE] = {

	  0,  0,  0,  0,  0,  0,  0, 59,  1,138,128,129,130, 15, 41, 60,
	131, 29, 42, 86, 58, 16,  2, 61,133, 56, 44, 31, 30, 17,  3, 62,
	134, 46, 45, 32, 18,  5,  4, 63,135, 57, 47, 33, 20, 19,  6, 64,
	136, 49, 48, 35, 34, 21,  7, 65,137,100, 50, 36, 22,  8,  9, 66,
	125, 51, 37, 23, 24, 11, 10, 67,126, 52, 53, 38, 39, 25, 12, 68,
	113,114, 40, 43, 26, 13, 87, 99, 97, 54, 28, 27, 43, 43, 88, 70,
	108,105,119,103,111,107, 14,110,  0, 79,106, 75, 71,109,102,104,
	 82, 83, 80, 76, 77, 72, 69, 98,  0, 96, 81,  0, 78, 73, 55,183,

	184,185,186,187, 74, 94, 92, 93,  0,  0,  0,125,126,127,112,  0,
	  0,139,172,163,165,115,152,172,166,140,160,154,113,114,167,168,
	148,149,147,140
};

static const unsigned short atkbd_unxlate_table[128] = {
          0,118, 22, 30, 38, 37, 46, 54, 61, 62, 70, 69, 78, 85,102, 13,
         21, 29, 36, 45, 44, 53, 60, 67, 68, 77, 84, 91, 90, 20, 28, 27,
         35, 43, 52, 51, 59, 66, 75, 76, 82, 14, 18, 93, 26, 34, 33, 42,
         50, 49, 58, 65, 73, 74, 89,124, 17, 41, 88,  5,  6,  4, 12,  3,
         11,  2, 10,  1,  9,119,126,108,117,125,123,107,115,116,121,105,
        114,122,112,113,127, 96, 97,120,  7, 15, 23, 31, 39, 47, 55, 63,
         71, 79, 86, 94,  8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 87,111,
         19, 25, 57, 81, 83, 92, 95, 98, 99,100,101,103,104,106,109,110
};

#define ATKBD_CMD_SETLEDS	0x10ed
#define ATKBD_CMD_GSCANSET	0x11f0
#define ATKBD_CMD_SSCANSET	0x10f0
#define ATKBD_CMD_GETID		0x02f2
#define ATKBD_CMD_SETREP	0x10f3
#define ATKBD_CMD_ENABLE	0x00f4
#define ATKBD_CMD_RESET_DIS	0x00f5	/* Reset to defaults and disable */
#define ATKBD_CMD_RESET_DEF	0x00f6	/* Reset to defaults */
#define ATKBD_CMD_SETALL_MB	0x00f8	/* Set all keys to give break codes */
#define ATKBD_CMD_SETALL_MBR	0x00fa  /* ... and repeat */
#define ATKBD_CMD_RESET_BAT	0x02ff
#define ATKBD_CMD_RESEND	0x00fe
#define ATKBD_CMD_EX_ENABLE	0x10ea
#define ATKBD_CMD_EX_SETLEDS	0x20eb
#define ATKBD_CMD_OK_GETID	0x02e8

#define ATKBD_RET_ACK		0xfa
#define ATKBD_RET_NAK		0xfe
#define ATKBD_RET_BAT		0xaa
#define ATKBD_RET_EMUL0		0xe0
#define ATKBD_RET_EMUL1		0xe1
#define ATKBD_RET_RELEASE	0xf0
#define ATKBD_RET_HANJA		0xf1
#define ATKBD_RET_HANGEUL	0xf2
#define ATKBD_RET_ERR		0xff

#define ATKBD_KEY_UNKNOWN	0
#define ATKBD_KEY_NULL		255

#define ATKBD_SCR_1		0xfffe
#define ATKBD_SCR_2		0xfffd
#define ATKBD_SCR_4		0xfffc
#define ATKBD_SCR_8		0xfffb
#define ATKBD_SCR_CLICK		0xfffa
#define ATKBD_SCR_LEFT		0xfff9
#define ATKBD_SCR_RIGHT		0xfff8

#define ATKBD_SPECIAL		ATKBD_SCR_RIGHT

#define ATKBD_LED_EVENT_BIT	0
#define ATKBD_REP_EVENT_BIT	1

#define ATKBD_XL_ERR		0x01
#define ATKBD_XL_BAT		0x02
#define ATKBD_XL_ACK		0x04
#define ATKBD_XL_NAK		0x08
#define ATKBD_XL_HANGEUL	0x10
#define ATKBD_XL_HANJA		0x20

static const struct {
	unsigned short keycode;
	unsigned char set2;
} atkbd_scroll_keys[] = {
	{ ATKBD_SCR_1,     0xc5 },
	{ ATKBD_SCR_2,     0x9d },
	{ ATKBD_SCR_4,     0xa4 },
	{ ATKBD_SCR_8,     0x9b },
	{ ATKBD_SCR_CLICK, 0xe0 },
	{ ATKBD_SCR_LEFT,  0xcb },
	{ ATKBD_SCR_RIGHT, 0xd2 },
};

/*
 * The atkbd control structure
 */

struct atkbd {

	struct ps2dev ps2dev;
	struct input_dev *dev;

	/* Written only during init */
	char name[64];
	char phys[32];

	unsigned short id;
	unsigned short keycode[ATKBD_KEYMAP_SIZE];
	DECLARE_BITMAP(force_release_mask, ATKBD_KEYMAP_SIZE);
	unsigned char set;
	bool translated;
	bool extra;
	bool write;
	bool softrepeat;
	bool softraw;
	bool scroll;
	bool enabled;

	/* Accessed only from interrupt */
	unsigned char emul;
	bool resend;
	bool release;
	unsigned long xl_bit;
	unsigned int last;
	unsigned long time;
	unsigned long err_count;

	struct delayed_work event_work;
	unsigned long event_jiffies;
	unsigned long event_mask;

	/* Serializes reconnect(), attr->set() and event work */
	struct mutex mutex;

	u32 function_row_physmap[MAX_FUNCTION_ROW_KEYS];
	int num_function_row_keys;
};

/*
 * System-specific keymap fixup routine
 */
static void (*atkbd_platform_fixup)(struct atkbd *, const void *data);
static void *atkbd_platform_fixup_data;
static unsigned int (*atkbd_platform_scancode_fixup)(struct atkbd *, unsigned int);

/*
 * Certain keyboards to not like ATKBD_CMD_RESET_DIS and stop responding
 * to many commands until full reset (ATKBD_CMD_RESET_BAT) is performed.
 */
static bool atkbd_skip_deactivate;

static ssize_t atkbd_attr_show_helper(struct device *dev, char *buf,
				ssize_t (*handler)(struct atkbd *, char *));
static ssize_t atkbd_attr_set_helper(struct device *dev, const char *buf, size_t count,
				ssize_t (*handler)(struct atkbd *, const char *, size_t));
#define ATKBD_DEFINE_ATTR(_name)						\
static ssize_t atkbd_show_##_name(struct atkbd *, char *);			\
static ssize_t atkbd_set_##_name(struct atkbd *, const char *, size_t);		\
static ssize_t atkbd_do_show_##_name(struct device *d,				\
				struct device_attribute *attr, char *b)		\
{										\
	return atkbd_attr_show_helper(d, b, atkbd_show_##_name);		\
}										\
static ssize_t atkbd_do_set_##_name(struct device *d,				\
			struct device_attribute *attr, const char *b, size_t s)	\
{										\
	return atkbd_attr_set_helper(d, b, s, atkbd_set_##_name);		\
}										\
static struct device_attribute atkbd_attr_##_name =				\
	__ATTR(_name, S_IWUSR | S_IRUGO, atkbd_do_show_##_name, atkbd_do_set_##_name);

ATKBD_DEFINE_ATTR(extra);
ATKBD_DEFINE_ATTR(force_release);
ATKBD_DEFINE_ATTR(scroll);
ATKBD_DEFINE_ATTR(set);
ATKBD_DEFINE_ATTR(softrepeat);
ATKBD_DEFINE_ATTR(softraw);

#define ATKBD_DEFINE_RO_ATTR(_name)						\
static ssize_t atkbd_show_##_name(struct atkbd *, char *);			\
static ssize_t atkbd_do_show_##_name(struct device *d,				\
				struct device_attribute *attr, char *b)		\
{										\
	return atkbd_attr_show_helper(d, b, atkbd_show_##_name);		\
}										\
static struct device_attribute atkbd_attr_##_name =				\
	__ATTR(_name, S_IRUGO, atkbd_do_show_##_name, NULL);

ATKBD_DEFINE_RO_ATTR(err_count);
ATKBD_DEFINE_RO_ATTR(function_row_physmap);

static struct attribute *atkbd_attributes[] = {
	&atkbd_attr_extra.attr,
	&atkbd_attr_force_release.attr,
	&atkbd_attr_scroll.attr,
	&atkbd_attr_set.attr,
	&atkbd_attr_softrepeat.attr,
	&atkbd_attr_softraw.attr,
	&atkbd_attr_err_count.attr,
	&atkbd_attr_function_row_physmap.attr,
	NULL
};

static ssize_t atkbd_show_function_row_physmap(struct atkbd *atkbd, char *buf)
{
	ssize_t size = 0;
	int i;

	if (!atkbd->num_function_row_keys)
		return 0;

	for (i = 0; i < atkbd->num_function_row_keys; i++)
		size += scnprintf(buf + size, PAGE_SIZE - size, "%02X ",
				  atkbd->function_row_physmap[i]);
	size += scnprintf(buf + size, PAGE_SIZE - size, "\n");
	return size;
}

static umode_t atkbd_attr_is_visible(struct kobject *kobj,
				struct attribute *attr, int i)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct serio *serio = to_serio_port(dev);
	struct atkbd *atkbd = serio_get_drvdata(serio);

	if (attr == &atkbd_attr_function_row_physmap.attr &&
	    !atkbd->num_function_row_keys)
		return 0;

	return attr->mode;
}

static struct attribute_group atkbd_attribute_group = {
	.attrs	= atkbd_attributes,
	.is_visible = atkbd_attr_is_visible,
};

static const unsigned int xl_table[] = {
	ATKBD_RET_BAT, ATKBD_RET_ERR, ATKBD_RET_ACK,
	ATKBD_RET_NAK, ATKBD_RET_HANJA, ATKBD_RET_HANGEUL,
};

/*
 * Checks if we should mangle the scancode to extract 'release' bit
 * in translated mode.
 */
static bool atkbd_need_xlate(unsigned long xl_bit, unsigned char code)
{
	int i;

	if (code == ATKBD_RET_EMUL0 || code == ATKBD_RET_EMUL1)
		return false;

	for (i = 0; i < ARRAY_SIZE(xl_table); i++)
		if (code == xl_table[i])
			return test_bit(i, &xl_bit);

	return true;
}

/*
 * Calculates new value of xl_bit so the driver can distinguish
 * between make/break pair of scancodes for select keys and PS/2
 * protocol responses.
 */
static void atkbd_calculate_xl_bit(struct atkbd *atkbd, unsigned char code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(xl_table); i++) {
		if (!((code ^ xl_table[i]) & 0x7f)) {
			if (code & 0x80)
				__clear_bit(i, &atkbd->xl_bit);
			else
				__set_bit(i, &atkbd->xl_bit);
			break;
		}
	}
}

/*
 * Encode the scancode, 0xe0 prefix, and high bit into a single integer,
 * keeping kernel 2.4 compatibility for set 2
 */
static unsigned int atkbd_compat_scancode(struct atkbd *atkbd, unsigned int code)
{
	if (atkbd->set == 3) {
		if (atkbd->emul == 1)
			code |= 0x100;
        } else {
		code = (code & 0x7f) | ((code & 0x80) << 1);
		if (atkbd->emul == 1)
			code |= 0x80;
	}

	return code;
}

/*
 * atkbd_interrupt(). Here takes place processing of data received from
 * the keyboard into events.
 */

static irqreturn_t atkbd_interrupt(struct serio *serio, unsigned char data,
				   unsigned int flags)
{
	struct atkbd *atkbd = serio_get_drvdata(serio);
	struct input_dev *dev = atkbd->dev;
	unsigned int code = data;
	int scroll = 0, hscroll = 0, click = -1;
	int value;
	unsigned short keycode;

	dev_dbg(&serio->dev, "Received %02x flags %02x\n", data, flags);

#if !defined(__i386__) && !defined (__x86_64__)
	if ((flags & (SERIO_FRAME | SERIO_PARITY)) && (~flags & SERIO_TIMEOUT) && !atkbd->resend && atkbd->write) {
		dev_warn(&serio->dev, "Frame/parity error: %02x\n", flags);
		serio_write(serio, ATKBD_CMD_RESEND);
		atkbd->resend = true;
		goto out;
	}

	if (!flags && data == ATKBD_RET_ACK)
		atkbd->resend = false;
#endif

	if (unlikely(atkbd->ps2dev.flags & PS2_FLAG_ACK))
		if  (ps2_handle_ack(&atkbd->ps2dev, data))
			goto out;

	if (unlikely(atkbd->ps2dev.flags & PS2_FLAG_CMD))
		if  (ps2_handle_response(&atkbd->ps2dev, data))
			goto out;

	pm_wakeup_event(&serio->dev, 0);

	if (!atkbd->enabled)
		goto out;

	input_event(dev, EV_MSC, MSC_RAW, code);

	if (atkbd_platform_scancode_fixup)
		code = atkbd_platform_scancode_fixup(atkbd, code);

	if (atkbd->translated) {

		if (atkbd->emul || atkbd_need_xlate(atkbd->xl_bit, code)) {
			atkbd->release = code >> 7;
			code &= 0x7f;
		}

		if (!atkbd->emul)
			atkbd_calculate_xl_bit(atkbd, data);
	}

	switch (code) {
	case ATKBD_RET_BAT:
		atkbd->enabled = false;
		serio_reconnect(atkbd->ps2dev.serio);
		goto out;
	case ATKBD_RET_EMUL0:
		atkbd->emul = 1;
		goto out;
	case ATKBD_RET_EMUL1:
		atkbd->emul = 2;
		goto out;
	case ATKBD_RET_RELEASE:
		atkbd->release = true;
		goto out;
	case ATKBD_RET_ACK:
	case ATKBD_RET_NAK:
		if (printk_ratelimit())
			dev_warn(&serio->dev,
				 "Spurious %s on %s. "
				 "Some program might be trying to access hardware directly.\n",
				 data == ATKBD_RET_ACK ? "ACK" : "NAK", serio->phys);
		goto out;
	case ATKBD_RET_ERR:
		atkbd->err_count++;
		dev_dbg(&serio->dev, "Keyboard on %s reports too many keys pressed.\n",
			serio->phys);
		goto out;
	}

	code = atkbd_compat_scancode(atkbd, code);

	if (atkbd->emul && --atkbd->emul)
		goto out;

	keycode = atkbd->keycode[code];

	if (!(atkbd->release && test_bit(code, atkbd->force_release_mask)))
		if (keycode != ATKBD_KEY_NULL)
			input_event(dev, EV_MSC, MSC_SCAN, code);

	switch (keycode) {
	case ATKBD_KEY_NULL:
		break;
	case ATKBD_KEY_UNKNOWN:
		dev_warn(&serio->dev,
			 "Unknown key %s (%s set %d, code %#x on %s).\n",
			 atkbd->release ? "released" : "pressed",
			 atkbd->translated ? "translated" : "raw",
			 atkbd->set, code, serio->phys);
		dev_warn(&serio->dev,
			 "Use 'setkeycodes %s%02x <keycode>' to make it known.\n",
			 code & 0x80 ? "e0" : "", code & 0x7f);
		input_sync(dev);
		break;
	case ATKBD_SCR_1:
		scroll = 1;
		break;
	case ATKBD_SCR_2:
		scroll = 2;
		break;
	case ATKBD_SCR_4:
		scroll = 4;
		break;
	case ATKBD_SCR_8:
		scroll = 8;
		break;
	case ATKBD_SCR_CLICK:
		click = !atkbd->release;
		break;
	case ATKBD_SCR_LEFT:
		hscroll = -1;
		break;
	case ATKBD_SCR_RIGHT:
		hscroll = 1;
		break;
	default:
		if (atkbd->release) {
			value = 0;
			atkbd->last = 0;
		} else if (!atkbd->softrepeat && test_bit(keycode, dev->key)) {
			/* Workaround Toshiba laptop multiple keypress */
			value = time_before(jiffies, atkbd->time) && atkbd->last == code ? 1 : 2;
		} else {
			value = 1;
			atkbd->last = code;
			atkbd->time = jiffies + msecs_to_jiffies(dev->rep[REP_DELAY]) / 2;
		}

		input_event(dev, EV_KEY, keycode, value);
		input_sync(dev);

		if (value && test_bit(code, atkbd->force_release_mask)) {
			input_event(dev, EV_MSC, MSC_SCAN, code);
			input_report_key(dev, keycode, 0);
			input_sync(dev);
		}
	}

	if (atkbd->scroll) {
		if (click != -1)
			input_report_key(dev, BTN_MIDDLE, click);
		input_report_rel(dev, REL_WHEEL,
				 atkbd->release ? -scroll : scroll);
		input_report_rel(dev, REL_HWHEEL, hscroll);
		input_sync(dev);
	}

	atkbd->release = false;
out:
	return IRQ_HANDLED;
}

static int atkbd_set_repeat_rate(struct atkbd *atkbd)
{
	const short period[32] =
		{ 33,  37,  42,  46,  50,  54,  58,  63,  67,  75,  83,  92, 100, 109, 116, 125,
		 133, 149, 167, 182, 200, 217, 232, 250, 270, 303, 333, 370, 400, 435, 470, 500 };
	const short delay[4] =
		{ 250, 500, 750, 1000 };

	struct input_dev *dev = atkbd->dev;
	unsigned char param;
	int i = 0, j = 0;

	while (i < ARRAY_SIZE(period) - 1 && period[i] < dev->rep[REP_PERIOD])
		i++;
	dev->rep[REP_PERIOD] = period[i];

	while (j < ARRAY_SIZE(delay) - 1 && delay[j] < dev->rep[REP_DELAY])
		j++;
	dev->rep[REP_DELAY] = delay[j];

	param = i | (j << 5);
	return ps2_command(&atkbd->ps2dev, &param, ATKBD_CMD_SETREP);
}

static int atkbd_set_leds(struct atkbd *atkbd)
{
	struct input_dev *dev = atkbd->dev;
	unsigned char param[2];

	param[0] = (test_bit(LED_SCROLLL, dev->led) ? 1 : 0)
		 | (test_bit(LED_NUML,    dev->led) ? 2 : 0)
		 | (test_bit(LED_CAPSL,   dev->led) ? 4 : 0);
	if (ps2_command(&atkbd->ps2dev, param, ATKBD_CMD_SETLEDS))
		return -1;

	if (atkbd->extra) {
		param[0] = 0;
		param[1] = (test_bit(LED_COMPOSE, dev->led) ? 0x01 : 0)
			 | (test_bit(LED_SLEEP,   dev->led) ? 0x02 : 0)
			 | (test_bit(LED_SUSPEND, dev->led) ? 0x04 : 0)
			 | (test_bit(LED_MISC,    dev->led) ? 0x10 : 0)
			 | (test_bit(LED_MUTE,    dev->led) ? 0x20 : 0);
		if (ps2_command(&atkbd->ps2dev, param, ATKBD_CMD_EX_SETLEDS))
			return -1;
	}

	return 0;
}

/*
 * atkbd_event_work() is used to complete processing of events that
 * can not be processed by input_event() which is often called from
 * interrupt context.
 */

static void atkbd_event_work(struct work_struct *work)
{
	struct atkbd *atkbd = container_of(work, struct atkbd, event_work.work);

	mutex_lock(&atkbd->mutex);

	if (!atkbd->enabled) {
		/*
		 * Serio ports are resumed asynchronously so while driver core
		 * thinks that device is already fully operational in reality
		 * it may not be ready yet. In this case we need to keep
		 * rescheduling till reconnect completes.
		 */
		schedule_delayed_work(&atkbd->event_work,
					msecs_to_jiffies(100));
	} else {
		if (test_and_clear_bit(ATKBD_LED_EVENT_BIT, &atkbd->event_mask))
			atkbd_set_leds(atkbd);

		if (test_and_clear_bit(ATKBD_REP_EVENT_BIT, &atkbd->event_mask))
			atkbd_set_repeat_rate(atkbd);
	}

	mutex_unlock(&atkbd->mutex);
}

/*
 * Schedule switch for execution. We need to throttle requests,
 * otherwise keyboard may become unresponsive.
 */
static void atkbd_schedule_event_work(struct atkbd *atkbd, int event_bit)
{
	unsigned long delay = msecs_to_jiffies(50);

	if (time_after(jiffies, atkbd->event_jiffies + delay))
		delay = 0;

	atkbd->event_jiffies = jiffies;
	set_bit(event_bit, &atkbd->event_mask);
	mb();
	schedule_delayed_work(&atkbd->event_work, delay);
}

/*
 * Event callback from the input module. Events that change the state of
 * the hardware are processed here. If action can not be performed in
 * interrupt context it is offloaded to atkbd_event_work.
 */

static int atkbd_event(struct input_dev *dev,
			unsigned int type, unsigned int code, int value)
{
	struct atkbd *atkbd = input_get_drvdata(dev);

	if (!atkbd->write)
		return -1;

	switch (type) {

	case EV_LED:
		atkbd_schedule_event_work(atkbd, ATKBD_LED_EVENT_BIT);
		return 0;

	case EV_REP:
		if (!atkbd->softrepeat)
			atkbd_schedule_event_work(atkbd, ATKBD_REP_EVENT_BIT);
		return 0;

	default:
		return -1;
	}
}

/*
 * atkbd_enable() signals that interrupt handler is allowed to
 * generate input events.
 */

static inline void atkbd_enable(struct atkbd *atkbd)
{
	serio_pause_rx(atkbd->ps2dev.serio);
	atkbd->enabled = true;
	serio_continue_rx(atkbd->ps2dev.serio);
}

/*
 * atkbd_disable() tells input handler that all incoming data except
 * for ACKs and command response should be dropped.
 */

static inline void atkbd_disable(struct atkbd *atkbd)
{
	serio_pause_rx(atkbd->ps2dev.serio);
	atkbd->enabled = false;
	serio_continue_rx(atkbd->ps2dev.serio);
}

static int atkbd_activate(struct atkbd *atkbd)
{
	struct ps2dev *ps2dev = &atkbd->ps2dev;

/*
 * Enable the keyboard to receive keystrokes.
 */

	if (ps2_command(ps2dev, NULL, ATKBD_CMD_ENABLE)) {
		dev_err(&ps2dev->serio->dev,
			"Failed to enable keyboard on %s\n",
			ps2dev->serio->phys);
		return -1;
	}

	return 0;
}

/*
 * atkbd_deactivate() resets and disables the keyboard from sending
 * keystrokes.
 */

static void atkbd_deactivate(struct atkbd *atkbd)
{
	struct ps2dev *ps2dev = &atkbd->ps2dev;

	if (ps2_command(ps2dev, NULL, ATKBD_CMD_RESET_DIS))
		dev_err(&ps2dev->serio->dev,
			"Failed to deactivate keyboard on %s\n",
			ps2dev->serio->phys);
}

/*
 * atkbd_probe() probes for an AT keyboard on a serio port.
 */

static int atkbd_probe(struct atkbd *atkbd)
{
	struct ps2dev *ps2dev = &atkbd->ps2dev;
	unsigned char param[2];

/*
 * Some systems, where the bit-twiddling when testing the io-lines of the
 * controller may confuse the keyboard need a full reset of the keyboard. On
 * these systems the BIOS also usually doesn't do it for us.
 */

	if (atkbd_reset)
		if (ps2_command(ps2dev, NULL, ATKBD_CMD_RESET_BAT))
			dev_warn(&ps2dev->serio->dev,
				 "keyboard reset failed on %s\n",
				 ps2dev->serio->phys);

/*
 * Then we check the keyboard ID. We should get 0xab83 under normal conditions.
 * Some keyboards report different values, but the first byte is always 0xab or
 * 0xac. Some old AT keyboards don't report anything. If a mouse is connected, this
 * should make sure we don't try to set the LEDs on it.
 */

	param[0] = param[1] = 0xa5;	/* initialize with invalid values */
	if (ps2_command(ps2dev, param, ATKBD_CMD_GETID)) {

/*
 * If the get ID command failed, we check if we can at least set the LEDs on
 * the keyboard. This should work on every keyboard out there. It also turns
 * the LEDs off, which we want anyway.
 */
		param[0] = 0;
		if (ps2_command(ps2dev, param, ATKBD_CMD_SETLEDS))
			return -1;
		atkbd->id = 0xabba;
		return 0;
	}

	if (!ps2_is_keyboard_id(param[0]))
		return -1;

	atkbd->id = (param[0] << 8) | param[1];

	if (atkbd->id == 0xaca1 && atkbd->translated) {
		dev_err(&ps2dev->serio->dev,
			"NCD terminal keyboards are only supported on non-translating controllers. "
			"Use i8042.direct=1 to disable translation.\n");
		return -1;
	}

/*
 * Make sure nothing is coming from the keyboard and disturbs our
 * internal state.
 */
	if (!atkbd_skip_deactivate)
		atkbd_deactivate(atkbd);

	return 0;
}

/*
 * atkbd_select_set checks if a keyboard has a working Set 3 support, and
 * sets it into that. Unfortunately there are keyboards that can be switched
 * to Set 3, but don't work well in that (BTC Multimedia ...)
 */

static int atkbd_select_set(struct atkbd *atkbd, int target_set, int allow_extra)
{
	struct ps2dev *ps2dev = &atkbd->ps2dev;
	unsigned char param[2];

	atkbd->extra = false;
/*
 * For known special keyboards we can go ahead and set the correct set.
 * We check for NCD PS/2 Sun, NorthGate OmniKey 101 and
 * IBM RapidAccess / IBM EzButton / Chicony KBP-8993 keyboards.
 */

	if (atkbd->translated)
		return 2;

	if (atkbd->id == 0xaca1) {
		param[0] = 3;
		ps2_command(ps2dev, param, ATKBD_CMD_SSCANSET);
		return 3;
	}

	if (allow_extra) {
		param[0] = 0x71;
		if (!ps2_command(ps2dev, param, ATKBD_CMD_EX_ENABLE)) {
			atkbd->extra = true;
			return 2;
		}
	}

	if (atkbd_terminal) {
		ps2_command(ps2dev, param, ATKBD_CMD_SETALL_MB);
		return 3;
	}

	if (target_set != 3)
		return 2;

	if (!ps2_command(ps2dev, param, ATKBD_CMD_OK_GETID)) {
		atkbd->id = param[0] << 8 | param[1];
		return 2;
	}

	param[0] = 3;
	if (ps2_command(ps2dev, param, ATKBD_CMD_SSCANSET))
		return 2;

	param[0] = 0;
	if (ps2_command(ps2dev, param, ATKBD_CMD_GSCANSET))
		return 2;

	if (param[0] != 3) {
		param[0] = 2;
		if (ps2_command(ps2dev, param, ATKBD_CMD_SSCANSET))
			return 2;
	}

	ps2_command(ps2dev, param, ATKBD_CMD_SETALL_MBR);

	return 3;
}

static int atkbd_reset_state(struct atkbd *atkbd)
{
        struct ps2dev *ps2dev = &atkbd->ps2dev;
	unsigned char param[1];

/*
 * Set the LEDs to a predefined state (all off).
 */

	param[0] = 0;
	if (ps2_command(ps2dev, param, ATKBD_CMD_SETLEDS))
		return -1;

/*
 * Set autorepeat to fastest possible.
 */

	param[0] = 0;
	if (ps2_command(ps2dev, param, ATKBD_CMD_SETREP))
		return -1;

	return 0;
}

/*
 * atkbd_cleanup() restores the keyboard state so that BIOS is happy after a
 * reboot.
 */

static void atkbd_cleanup(struct serio *serio)
{
	struct atkbd *atkbd = serio_get_drvdata(serio);

	atkbd_disable(atkbd);
	ps2_command(&atkbd->ps2dev, NULL, ATKBD_CMD_RESET_DEF);
}


/*
 * atkbd_disconnect() closes and frees.
 */

static void atkbd_disconnect(struct serio *serio)
{
	struct atkbd *atkbd = serio_get_drvdata(serio);

	sysfs_remove_group(&serio->dev.kobj, &atkbd_attribute_group);

	atkbd_disable(atkbd);

	input_unregister_device(atkbd->dev);

	/*
	 * Make sure we don't have a command in flight.
	 * Note that since atkbd->enabled is false event work will keep
	 * rescheduling itself until it gets canceled and will not try
	 * accessing freed input device or serio port.
	 */
	cancel_delayed_work_sync(&atkbd->event_work);

	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	kfree(atkbd);
}

/*
 * generate release events for the keycodes given in data
 */
static void atkbd_apply_forced_release_keylist(struct atkbd* atkbd,
						const void *data)
{
	const unsigned int *keys = data;
	unsigned int i;

	if (atkbd->set == 2)
		for (i = 0; keys[i] != -1U; i++)
			__set_bit(keys[i], atkbd->force_release_mask);
}

/*
 * Most special keys (Fn+F?) on Dell laptops do not generate release
 * events so we have to do it ourselves.
 */
static unsigned int atkbd_dell_laptop_forced_release_keys[] = {
	0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8f, 0x93, -1U
};

/*
 * Perform fixup for HP system that doesn't generate release
 * for its video switch
 */
static unsigned int atkbd_hp_forced_release_keys[] = {
	0x94, -1U
};

/*
 * Samsung NC10,NC20 with Fn+F? key release not working
 */
static unsigned int atkbd_samsung_forced_release_keys[] = {
	0x82, 0x83, 0x84, 0x86, 0x88, 0x89, 0xb3, 0xf7, 0xf9, -1U
};

/*
 * Amilo Pi 3525 key release for Fn+Volume keys not working
 */
static unsigned int atkbd_amilo_pi3525_forced_release_keys[] = {
	0x20, 0xa0, 0x2e, 0xae, 0x30, 0xb0, -1U
};

/*
 * Amilo Xi 3650 key release for light touch bar not working
 */
static unsigned int atkbd_amilo_xi3650_forced_release_keys[] = {
	0x67, 0xed, 0x90, 0xa2, 0x99, 0xa4, 0xae, 0xb0, -1U
};

/*
 * Soltech TA12 system with broken key release on volume keys and mute key
 */
static unsigned int atkdb_soltech_ta12_forced_release_keys[] = {
	0xa0, 0xae, 0xb0, -1U
};

/*
 * Many notebooks don't send key release event for volume up/down
 * keys, with key list below common among them
 */
static unsigned int atkbd_volume_forced_release_keys[] = {
	0xae, 0xb0, -1U
};

/*
 * OQO 01+ multimedia keys (64--66) generate e0 6x upon release whereas
 * they should be generating e4-e6 (0x80 | code).
 */
static unsigned int atkbd_oqo_01plus_scancode_fixup(struct atkbd *atkbd,
						    unsigned int code)
{
	if (atkbd->translated && atkbd->emul == 1 &&
	    (code == 0x64 || code == 0x65 || code == 0x66)) {
		atkbd->emul = 0;
		code |= 0x80;
	}

	return code;
}

static int atkbd_get_keymap_from_fwnode(struct atkbd *atkbd)
{
	struct device *dev = &atkbd->ps2dev.serio->dev;
	int i, n;
	u32 *ptr;
	u16 scancode, keycode;

	/* Parse "linux,keymap" property */
	n = device_property_count_u32(dev, "linux,keymap");
	if (n <= 0 || n > ATKBD_KEYMAP_SIZE)
		return -ENXIO;

	ptr = kcalloc(n, sizeof(u32), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	if (device_property_read_u32_array(dev, "linux,keymap", ptr, n)) {
		dev_err(dev, "problem parsing FW keymap property\n");
		kfree(ptr);
		return -EINVAL;
	}

	memset(atkbd->keycode, 0, sizeof(atkbd->keycode));
	for (i = 0; i < n; i++) {
		scancode = SCANCODE(ptr[i]);
		keycode = KEYCODE(ptr[i]);
		atkbd->keycode[scancode] = keycode;
	}

	kfree(ptr);
	return 0;
}

/*
 * atkbd_set_keycode_table() initializes keyboard's keycode table
 * according to the selected scancode set
 */

static void atkbd_set_keycode_table(struct atkbd *atkbd)
{
	struct device *dev = &atkbd->ps2dev.serio->dev;
	unsigned int scancode;
	int i, j;

	memset(atkbd->keycode, 0, sizeof(atkbd->keycode));
	bitmap_zero(atkbd->force_release_mask, ATKBD_KEYMAP_SIZE);

	if (!atkbd_get_keymap_from_fwnode(atkbd)) {
		dev_dbg(dev, "Using FW keymap\n");
	} else if (atkbd->translated) {
		for (i = 0; i < 128; i++) {
			scancode = atkbd_unxlate_table[i];
			atkbd->keycode[i] = atkbd_set2_keycode[scancode];
			atkbd->keycode[i | 0x80] = atkbd_set2_keycode[scancode | 0x80];
			if (atkbd->scroll)
				for (j = 0; j < ARRAY_SIZE(atkbd_scroll_keys); j++)
					if ((scancode | 0x80) == atkbd_scroll_keys[j].set2)
						atkbd->keycode[i | 0x80] = atkbd_scroll_keys[j].keycode;
		}
	} else if (atkbd->set == 3) {
		memcpy(atkbd->keycode, atkbd_set3_keycode, sizeof(atkbd->keycode));
	} else {
		memcpy(atkbd->keycode, atkbd_set2_keycode, sizeof(atkbd->keycode));

		if (atkbd->scroll)
			for (i = 0; i < ARRAY_SIZE(atkbd_scroll_keys); i++) {
				scancode = atkbd_scroll_keys[i].set2;
				atkbd->keycode[scancode] = atkbd_scroll_keys[i].keycode;
		}
	}

/*
 * HANGEUL and HANJA keys do not send release events so we need to
 * generate such events ourselves
 */
	scancode = atkbd_compat_scancode(atkbd, ATKBD_RET_HANGEUL);
	atkbd->keycode[scancode] = KEY_HANGEUL;
	__set_bit(scancode, atkbd->force_release_mask);

	scancode = atkbd_compat_scancode(atkbd, ATKBD_RET_HANJA);
	atkbd->keycode[scancode] = KEY_HANJA;
	__set_bit(scancode, atkbd->force_release_mask);

/*
 * Perform additional fixups
 */
	if (atkbd_platform_fixup)
		atkbd_platform_fixup(atkbd, atkbd_platform_fixup_data);
}

/*
 * atkbd_set_device_attrs() sets up keyboard's input device structure
 */

static void atkbd_set_device_attrs(struct atkbd *atkbd)
{
	struct input_dev *input_dev = atkbd->dev;
	int i;

	if (atkbd->extra)
		snprintf(atkbd->name, sizeof(atkbd->name),
			 "AT Set 2 Extra keyboard");
	else
		snprintf(atkbd->name, sizeof(atkbd->name),
			 "AT %s Set %d keyboard",
			 atkbd->translated ? "Translated" : "Raw", atkbd->set);

	snprintf(atkbd->phys, sizeof(atkbd->phys),
		 "%s/input0", atkbd->ps2dev.serio->phys);

	input_dev->name = atkbd->name;
	input_dev->phys = atkbd->phys;
	input_dev->id.bustype = BUS_I8042;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = atkbd->translated ? 1 : atkbd->set;
	input_dev->id.version = atkbd->id;
	input_dev->event = atkbd_event;
	input_dev->dev.parent = &atkbd->ps2dev.serio->dev;

	input_set_drvdata(input_dev, atkbd);

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REP) |
		BIT_MASK(EV_MSC);

	if (atkbd->write) {
		input_dev->evbit[0] |= BIT_MASK(EV_LED);
		input_dev->ledbit[0] = BIT_MASK(LED_NUML) |
			BIT_MASK(LED_CAPSL) | BIT_MASK(LED_SCROLLL);
	}

	if (atkbd->extra)
		input_dev->ledbit[0] |= BIT_MASK(LED_COMPOSE) |
			BIT_MASK(LED_SUSPEND) | BIT_MASK(LED_SLEEP) |
			BIT_MASK(LED_MUTE) | BIT_MASK(LED_MISC);

	if (!atkbd->softrepeat) {
		input_dev->rep[REP_DELAY] = 250;
		input_dev->rep[REP_PERIOD] = 33;
	}

	input_dev->mscbit[0] = atkbd->softraw ? BIT_MASK(MSC_SCAN) :
		BIT_MASK(MSC_RAW) | BIT_MASK(MSC_SCAN);

	if (atkbd->scroll) {
		input_dev->evbit[0] |= BIT_MASK(EV_REL);
		input_dev->relbit[0] = BIT_MASK(REL_WHEEL) |
			BIT_MASK(REL_HWHEEL);
		__set_bit(BTN_MIDDLE, input_dev->keybit);
	}

	input_dev->keycode = atkbd->keycode;
	input_dev->keycodesize = sizeof(unsigned short);
	input_dev->keycodemax = ARRAY_SIZE(atkbd_set2_keycode);

	for (i = 0; i < ATKBD_KEYMAP_SIZE; i++) {
		if (atkbd->keycode[i] != KEY_RESERVED &&
		    atkbd->keycode[i] != ATKBD_KEY_NULL &&
		    atkbd->keycode[i] < ATKBD_SPECIAL) {
			__set_bit(atkbd->keycode[i], input_dev->keybit);
		}
	}
}

static void atkbd_parse_fwnode_data(struct serio *serio)
{
	struct atkbd *atkbd = serio_get_drvdata(serio);
	struct device *dev = &serio->dev;
	int n;

	/* Parse "function-row-physmap" property */
	n = device_property_count_u32(dev, "function-row-physmap");
	if (n > 0 && n <= MAX_FUNCTION_ROW_KEYS &&
	    !device_property_read_u32_array(dev, "function-row-physmap",
					    atkbd->function_row_physmap, n)) {
		atkbd->num_function_row_keys = n;
		dev_dbg(dev, "FW reported %d function-row key locations\n", n);
	}
}

/*
 * atkbd_connect() is called when the serio module finds an interface
 * that isn't handled yet by an appropriate device driver. We check if
 * there is an AT keyboard out there and if yes, we register ourselves
 * to the input module.
 */

static int atkbd_connect(struct serio *serio, struct serio_driver *drv)
{
	struct atkbd *atkbd;
	struct input_dev *dev;
	int err = -ENOMEM;

	atkbd = kzalloc(sizeof(struct atkbd), GFP_KERNEL);
	dev = input_allocate_device();
	if (!atkbd || !dev)
		goto fail1;

	atkbd->dev = dev;
	ps2_init(&atkbd->ps2dev, serio);
	INIT_DELAYED_WORK(&atkbd->event_work, atkbd_event_work);
	mutex_init(&atkbd->mutex);

	switch (serio->id.type) {

	case SERIO_8042_XL:
		atkbd->translated = true;
		/* Fall through */

	case SERIO_8042:
		if (serio->write)
			atkbd->write = true;
		break;
	}

	atkbd->softraw = atkbd_softraw;
	atkbd->softrepeat = atkbd_softrepeat;
	atkbd->scroll = atkbd_scroll;

	if (atkbd->softrepeat)
		atkbd->softraw = true;

	serio_set_drvdata(serio, atkbd);

	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	if (atkbd->write) {

		if (atkbd_probe(atkbd)) {
			err = -ENODEV;
			goto fail3;
		}

		atkbd->set = atkbd_select_set(atkbd, atkbd_set, atkbd_extra);
		atkbd_reset_state(atkbd);

	} else {
		atkbd->set = 2;
		atkbd->id = 0xab00;
	}

	atkbd_parse_fwnode_data(serio);

	atkbd_set_keycode_table(atkbd);
	atkbd_set_device_attrs(atkbd);

	err = sysfs_create_group(&serio->dev.kobj, &atkbd_attribute_group);
	if (err)
		goto fail3;

	atkbd_enable(atkbd);
	if (serio->write)
		atkbd_activate(atkbd);

	err = input_register_device(atkbd->dev);
	if (err)
		goto fail4;

	return 0;

 fail4: sysfs_remove_group(&serio->dev.kobj, &atkbd_attribute_group);
 fail3:	serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(dev);
	kfree(atkbd);
	return err;
}

/*
 * atkbd_reconnect() tries to restore keyboard into a sane state and is
 * most likely called on resume.
 */

static int atkbd_reconnect(struct serio *serio)
{
	struct atkbd *atkbd = serio_get_drvdata(serio);
	struct serio_driver *drv = serio->drv;
	int retval = -1;

	if (!atkbd || !drv) {
		dev_dbg(&serio->dev,
			"reconnect request, but serio is disconnected, ignoring...\n");
		return -1;
	}

	mutex_lock(&atkbd->mutex);

	atkbd_disable(atkbd);

	if (atkbd->write) {
		if (atkbd_probe(atkbd))
			goto out;

		if (atkbd->set != atkbd_select_set(atkbd, atkbd->set, atkbd->extra))
			goto out;

		/*
		 * Restore LED state and repeat rate. While input core
		 * will do this for us at resume time reconnect may happen
		 * because user requested it via sysfs or simply because
		 * keyboard was unplugged and plugged in again so we need
		 * to do it ourselves here.
		 */
		atkbd_set_leds(atkbd);
		if (!atkbd->softrepeat)
			atkbd_set_repeat_rate(atkbd);

	}

	/*
	 * Reset our state machine in case reconnect happened in the middle
	 * of multi-byte scancode.
	 */
	atkbd->xl_bit = 0;
	atkbd->emul = 0;

	atkbd_enable(atkbd);
	if (atkbd->write)
		atkbd_activate(atkbd);

	retval = 0;

 out:
	mutex_unlock(&atkbd->mutex);
	return retval;
}

static const struct serio_device_id atkbd_serio_ids[] = {
	{
		.type	= SERIO_8042,
		.proto	= SERIO_ANY,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{
		.type	= SERIO_8042_XL,
		.proto	= SERIO_ANY,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_PS2SER,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, atkbd_serio_ids);

static struct serio_driver atkbd_drv = {
	.driver		= {
		.name	= "atkbd",
	},
	.description	= DRIVER_DESC,
	.id_table	= atkbd_serio_ids,
	.interrupt	= atkbd_interrupt,
	.connect	= atkbd_connect,
	.reconnect	= atkbd_reconnect,
	.disconnect	= atkbd_disconnect,
	.cleanup	= atkbd_cleanup,
};

static ssize_t atkbd_attr_show_helper(struct device *dev, char *buf,
				ssize_t (*handler)(struct atkbd *, char *))
{
	struct serio *serio = to_serio_port(dev);
	struct atkbd *atkbd = serio_get_drvdata(serio);

	return handler(atkbd, buf);
}

static ssize_t atkbd_attr_set_helper(struct device *dev, const char *buf, size_t count,
				ssize_t (*handler)(struct atkbd *, const char *, size_t))
{
	struct serio *serio = to_serio_port(dev);
	struct atkbd *atkbd = serio_get_drvdata(serio);
	int retval;

	retval = mutex_lock_interruptible(&atkbd->mutex);
	if (retval)
		return retval;

	atkbd_disable(atkbd);
	retval = handler(atkbd, buf, count);
	atkbd_enable(atkbd);

	mutex_unlock(&atkbd->mutex);

	return retval;
}

static ssize_t atkbd_show_extra(struct atkbd *atkbd, char *buf)
{
	return sprintf(buf, "%d\n", atkbd->extra ? 1 : 0);
}

static ssize_t atkbd_set_extra(struct atkbd *atkbd, const char *buf, size_t count)
{
	struct input_dev *old_dev, *new_dev;
	unsigned int value;
	int err;
	bool old_extra;
	unsigned char old_set;

	if (!atkbd->write)
		return -EIO;

	err = kstrtouint(buf, 10, &value);
	if (err)
		return err;

	if (value > 1)
		return -EINVAL;

	if (atkbd->extra != value) {
		/*
		 * Since device's properties will change we need to
		 * unregister old device. But allocate and register
		 * new one first to make sure we have it.
		 */
		old_dev = atkbd->dev;
		old_extra = atkbd->extra;
		old_set = atkbd->set;

		new_dev = input_allocate_device();
		if (!new_dev)
			return -ENOMEM;

		atkbd->dev = new_dev;
		atkbd->set = atkbd_select_set(atkbd, atkbd->set, value);
		atkbd_reset_state(atkbd);
		atkbd_activate(atkbd);
		atkbd_set_keycode_table(atkbd);
		atkbd_set_device_attrs(atkbd);

		err = input_register_device(atkbd->dev);
		if (err) {
			input_free_device(new_dev);

			atkbd->dev = old_dev;
			atkbd->set = atkbd_select_set(atkbd, old_set, old_extra);
			atkbd_set_keycode_table(atkbd);
			atkbd_set_device_attrs(atkbd);

			return err;
		}
		input_unregister_device(old_dev);

	}
	return count;
}

static ssize_t atkbd_show_force_release(struct atkbd *atkbd, char *buf)
{
	size_t len = scnprintf(buf, PAGE_SIZE - 1, "%*pbl",
			       ATKBD_KEYMAP_SIZE, atkbd->force_release_mask);

	buf[len++] = '\n';
	buf[len] = '\0';

	return len;
}

static ssize_t atkbd_set_force_release(struct atkbd *atkbd,
					const char *buf, size_t count)
{
	/* 64 bytes on stack should be acceptable */
	DECLARE_BITMAP(new_mask, ATKBD_KEYMAP_SIZE);
	int err;

	err = bitmap_parselist(buf, new_mask, ATKBD_KEYMAP_SIZE);
	if (err)
		return err;

	memcpy(atkbd->force_release_mask, new_mask, sizeof(atkbd->force_release_mask));
	return count;
}


static ssize_t atkbd_show_scroll(struct atkbd *atkbd, char *buf)
{
	return sprintf(buf, "%d\n", atkbd->scroll ? 1 : 0);
}

static ssize_t atkbd_set_scroll(struct atkbd *atkbd, const char *buf, size_t count)
{
	struct input_dev *old_dev, *new_dev;
	unsigned int value;
	int err;
	bool old_scroll;

	err = kstrtouint(buf, 10, &value);
	if (err)
		return err;

	if (value > 1)
		return -EINVAL;

	if (atkbd->scroll != value) {
		old_dev = atkbd->dev;
		old_scroll = atkbd->scroll;

		new_dev = input_allocate_device();
		if (!new_dev)
			return -ENOMEM;

		atkbd->dev = new_dev;
		atkbd->scroll = value;
		atkbd_set_keycode_table(atkbd);
		atkbd_set_device_attrs(atkbd);

		err = input_register_device(atkbd->dev);
		if (err) {
			input_free_device(new_dev);

			atkbd->scroll = old_scroll;
			atkbd->dev = old_dev;
			atkbd_set_keycode_table(atkbd);
			atkbd_set_device_attrs(atkbd);

			return err;
		}
		input_unregister_device(old_dev);
	}
	return count;
}

static ssize_t atkbd_show_set(struct atkbd *atkbd, char *buf)
{
	return sprintf(buf, "%d\n", atkbd->set);
}

static ssize_t atkbd_set_set(struct atkbd *atkbd, const char *buf, size_t count)
{
	struct input_dev *old_dev, *new_dev;
	unsigned int value;
	int err;
	unsigned char old_set;
	bool old_extra;

	if (!atkbd->write)
		return -EIO;

	err = kstrtouint(buf, 10, &value);
	if (err)
		return err;

	if (value != 2 && value != 3)
		return -EINVAL;

	if (atkbd->set != value) {
		old_dev = atkbd->dev;
		old_extra = atkbd->extra;
		old_set = atkbd->set;

		new_dev = input_allocate_device();
		if (!new_dev)
			return -ENOMEM;

		atkbd->dev = new_dev;
		atkbd->set = atkbd_select_set(atkbd, value, atkbd->extra);
		atkbd_reset_state(atkbd);
		atkbd_activate(atkbd);
		atkbd_set_keycode_table(atkbd);
		atkbd_set_device_attrs(atkbd);

		err = input_register_device(atkbd->dev);
		if (err) {
			input_free_device(new_dev);

			atkbd->dev = old_dev;
			atkbd->set = atkbd_select_set(atkbd, old_set, old_extra);
			atkbd_set_keycode_table(atkbd);
			atkbd_set_device_attrs(atkbd);

			return err;
		}
		input_unregister_device(old_dev);
	}
	return count;
}

static ssize_t atkbd_show_softrepeat(struct atkbd *atkbd, char *buf)
{
	return sprintf(buf, "%d\n", atkbd->softrepeat ? 1 : 0);
}

static ssize_t atkbd_set_softrepeat(struct atkbd *atkbd, const char *buf, size_t count)
{
	struct input_dev *old_dev, *new_dev;
	unsigned int value;
	int err;
	bool old_softrepeat, old_softraw;

	if (!atkbd->write)
		return -EIO;

	err = kstrtouint(buf, 10, &value);
	if (err)
		return err;

	if (value > 1)
		return -EINVAL;

	if (atkbd->softrepeat != value) {
		old_dev = atkbd->dev;
		old_softrepeat = atkbd->softrepeat;
		old_softraw = atkbd->softraw;

		new_dev = input_allocate_device();
		if (!new_dev)
			return -ENOMEM;

		atkbd->dev = new_dev;
		atkbd->softrepeat = value;
		if (atkbd->softrepeat)
			atkbd->softraw = true;
		atkbd_set_device_attrs(atkbd);

		err = input_register_device(atkbd->dev);
		if (err) {
			input_free_device(new_dev);

			atkbd->dev = old_dev;
			atkbd->softrepeat = old_softrepeat;
			atkbd->softraw = old_softraw;
			atkbd_set_device_attrs(atkbd);

			return err;
		}
		input_unregister_device(old_dev);
	}
	return count;
}


static ssize_t atkbd_show_softraw(struct atkbd *atkbd, char *buf)
{
	return sprintf(buf, "%d\n", atkbd->softraw ? 1 : 0);
}

static ssize_t atkbd_set_softraw(struct atkbd *atkbd, const char *buf, size_t count)
{
	struct input_dev *old_dev, *new_dev;
	unsigned int value;
	int err;
	bool old_softraw;

	err = kstrtouint(buf, 10, &value);
	if (err)
		return err;

	if (value > 1)
		return -EINVAL;

	if (atkbd->softraw != value) {
		old_dev = atkbd->dev;
		old_softraw = atkbd->softraw;

		new_dev = input_allocate_device();
		if (!new_dev)
			return -ENOMEM;

		atkbd->dev = new_dev;
		atkbd->softraw = value;
		atkbd_set_device_attrs(atkbd);

		err = input_register_device(atkbd->dev);
		if (err) {
			input_free_device(new_dev);

			atkbd->dev = old_dev;
			atkbd->softraw = old_softraw;
			atkbd_set_device_attrs(atkbd);

			return err;
		}
		input_unregister_device(old_dev);
	}
	return count;
}

static ssize_t atkbd_show_err_count(struct atkbd *atkbd, char *buf)
{
	return sprintf(buf, "%lu\n", atkbd->err_count);
}

static int __init atkbd_setup_forced_release(const struct dmi_system_id *id)
{
	atkbd_platform_fixup = atkbd_apply_forced_release_keylist;
	atkbd_platform_fixup_data = id->driver_data;

	return 1;
}

static int __init atkbd_setup_scancode_fixup(const struct dmi_system_id *id)
{
	atkbd_platform_scancode_fixup = id->driver_data;

	return 1;
}

static int __init atkbd_deactivate_fixup(const struct dmi_system_id *id)
{
	atkbd_skip_deactivate = true;
	return 1;
}

/*
 * NOTE: do not add any more "force release" quirks to this table.  The
 * task of adjusting list of keys that should be "released" automatically
 * by the driver is now delegated to userspace tools, such as udev, so
 * submit such quirks there.
 */
static const struct dmi_system_id atkbd_dmi_quirk_table[] __initconst = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_CHASSIS_TYPE, "8"), /* Portable */
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_dell_laptop_forced_release_keys,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			DMI_MATCH(DMI_CHASSIS_TYPE, "8"), /* Portable */
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_dell_laptop_forced_release_keys,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP 2133"),
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_hp_forced_release_keys,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Pavilion ZV6100"),
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_volume_forced_release_keys,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Presario R4000"),
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_volume_forced_release_keys,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Presario R4100"),
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_volume_forced_release_keys,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Presario R4200"),
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_volume_forced_release_keys,
	},
	{
		/* Inventec Symphony */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "INVENTEC"),
			DMI_MATCH(DMI_PRODUCT_NAME, "SYMPHONY 6.0/7.0"),
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_volume_forced_release_keys,
	},
	{
		/* Samsung NC10 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "NC10"),
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_samsung_forced_release_keys,
	},
	{
		/* Samsung NC20 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "NC20"),
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_samsung_forced_release_keys,
	},
	{
		/* Samsung SQ45S70S */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
			DMI_MATCH(DMI_PRODUCT_NAME, "SQ45S70S"),
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_samsung_forced_release_keys,
	},
	{
		/* Fujitsu Amilo PA 1510 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Pa 1510"),
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_volume_forced_release_keys,
	},
	{
		/* Fujitsu Amilo Pi 3525 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Pi 3525"),
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_amilo_pi3525_forced_release_keys,
	},
	{
		/* Fujitsu Amilo Xi 3650 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU SIEMENS"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AMILO Xi 3650"),
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkbd_amilo_xi3650_forced_release_keys,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Soltech Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "TA12"),
		},
		.callback = atkbd_setup_forced_release,
		.driver_data = atkdb_soltech_ta12_forced_release_keys,
	},
	{
		/* OQO Model 01+ */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "OQO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ZEPTO"),
		},
		.callback = atkbd_setup_scancode_fixup,
		.driver_data = atkbd_oqo_01plus_scancode_fixup,
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LG Electronics"),
		},
		.callback = atkbd_deactivate_fixup,
	},
	{ }
};

static int __init atkbd_init(void)
{
	dmi_check_system(atkbd_dmi_quirk_table);

	return serio_register_driver(&atkbd_drv);
}

static void __exit atkbd_exit(void)
{
	serio_unregister_driver(&atkbd_drv);
}

module_init(atkbd_init);
module_exit(atkbd_exit);
