/*
 *
 * Copyright (c) 2003 Gerd Knorr
 * Copyright (c) 2003 Pavel Machek
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/pci.h>

#include <media/ir-common.h>

#include "bttv.h"

/* ---------------------------------------------------------------------- */

static IR_KEYTAB_TYPE ir_codes_avermedia[IR_KEYTAB_SIZE] = {
	[ 34 ] = KEY_KP0,
	[ 40 ] = KEY_KP1,
	[ 24 ] = KEY_KP2,
	[ 56 ] = KEY_KP3,
	[ 36 ] = KEY_KP4,
	[ 20 ] = KEY_KP5,
	[ 52 ] = KEY_KP6,
	[ 44 ] = KEY_KP7,
	[ 28 ] = KEY_KP8,
	[ 60 ] = KEY_KP9,

	[ 48 ] = KEY_EJECTCD,     // Unmarked on my controller
	[  0 ] = KEY_POWER,
	[ 18 ] = BTN_LEFT,        // DISPLAY/L
	[ 50 ] = BTN_RIGHT,       // LOOP/R
	[ 10 ] = KEY_MUTE,
	[ 38 ] = KEY_RECORD,
	[ 22 ] = KEY_PAUSE,
	[ 54 ] = KEY_STOP,
	[ 30 ] = KEY_VOLUMEDOWN,
	[ 62 ] = KEY_VOLUMEUP,

	[ 32 ] = KEY_TUNER,       // TV/FM
	[ 16 ] = KEY_CD,
	[  8 ] = KEY_VIDEO,
	[  4 ] = KEY_AUDIO,
	[ 12 ] = KEY_ZOOM,        // full screen
	[  2 ] = KEY_INFO,        // preview
	[ 42 ] = KEY_SEARCH,      // autoscan
	[ 26 ] = KEY_STOP,        // freeze
	[ 58 ] = KEY_RECORD,      // capture
	[  6 ] = KEY_PLAY,        // unmarked
	[ 46 ] = KEY_RED,         // unmarked
	[ 14 ] = KEY_GREEN,       // unmarked

	[ 33 ] = KEY_YELLOW,      // unmarked
	[ 17 ] = KEY_CHANNELDOWN,
	[ 49 ] = KEY_CHANNELUP,
	[  1 ] = KEY_BLUE,        // unmarked
};

/* Matt Jesson <dvb@jesson.eclipse.co.uk */
static IR_KEYTAB_TYPE ir_codes_avermedia_dvbt[IR_KEYTAB_SIZE] = {
	[ 0x28 ] = KEY_KP0,         //'0' / 'enter'
	[ 0x22 ] = KEY_KP1,         //'1'
	[ 0x12 ] = KEY_KP2,         //'2' / 'up arrow'
	[ 0x32 ] = KEY_KP3,         //'3'
	[ 0x24 ] = KEY_KP4,         //'4' / 'left arrow'
	[ 0x14 ] = KEY_KP5,         //'5'
	[ 0x34 ] = KEY_KP6,         //'6' / 'right arrow'
	[ 0x26 ] = KEY_KP7,         //'7'
	[ 0x16 ] = KEY_KP8,         //'8' / 'down arrow'
	[ 0x36 ] = KEY_KP9,         //'9'

	[ 0x20 ] = KEY_LIST,        // 'source'
	[ 0x10 ] = KEY_TEXT,        // 'teletext'
	[ 0x00 ] = KEY_POWER,       // 'power'
	[ 0x04 ] = KEY_AUDIO,       // 'audio'
	[ 0x06 ] = KEY_ZOOM,        // 'full screen'
	[ 0x18 ] = KEY_VIDEO,       // 'display'
	[ 0x38 ] = KEY_SEARCH,      // 'loop'
	[ 0x08 ] = KEY_INFO,        // 'preview'
	[ 0x2a ] = KEY_REWIND,      // 'backward <<'
	[ 0x1a ] = KEY_FASTFORWARD, // 'forward >>'
	[ 0x3a ] = KEY_RECORD,      // 'capture'
	[ 0x0a ] = KEY_MUTE,        // 'mute'
	[ 0x2c ] = KEY_RECORD,      // 'record'
	[ 0x1c ] = KEY_PAUSE,       // 'pause'
	[ 0x3c ] = KEY_STOP,        // 'stop'
	[ 0x0c ] = KEY_PLAY,        // 'play'
	[ 0x2e ] = KEY_RED,         // 'red'
	[ 0x01 ] = KEY_BLUE,        // 'blue' / 'cancel'
	[ 0x0e ] = KEY_YELLOW,      // 'yellow' / 'ok'
	[ 0x21 ] = KEY_GREEN,       // 'green'
	[ 0x11 ] = KEY_CHANNELDOWN, // 'channel -'
	[ 0x31 ] = KEY_CHANNELUP,   // 'channel +'
	[ 0x1e ] = KEY_VOLUMEDOWN,  // 'volume -'
	[ 0x3e ] = KEY_VOLUMEUP,    // 'volume +'
};

/* Attila Kondoros <attila.kondoros@chello.hu> */
static IR_KEYTAB_TYPE ir_codes_apac_viewcomp[IR_KEYTAB_SIZE] = {

	[  1 ] = KEY_KP1,
	[  2 ] = KEY_KP2,
	[  3 ] = KEY_KP3,
	[  4 ] = KEY_KP4,
	[  5 ] = KEY_KP5,
	[  6 ] = KEY_KP6,
	[  7 ] = KEY_KP7,
	[  8 ] = KEY_KP8,
	[  9 ] = KEY_KP9,
	[  0 ] = KEY_KP0,
	[ 23 ] = KEY_LAST,        // +100
	[ 10 ] = KEY_LIST,        // recall


	[ 28 ] = KEY_TUNER,       // TV/FM
	[ 21 ] = KEY_SEARCH,      // scan
	[ 18 ] = KEY_POWER,       // power
	[ 31 ] = KEY_VOLUMEDOWN,  // vol up
	[ 27 ] = KEY_VOLUMEUP,    // vol down
	[ 30 ] = KEY_CHANNELDOWN, // chn up
	[ 26 ] = KEY_CHANNELUP,   // chn down

	[ 17 ] = KEY_VIDEO,       // video
	[ 15 ] = KEY_ZOOM,        // full screen
	[ 19 ] = KEY_MUTE,        // mute/unmute
	[ 16 ] = KEY_TEXT,        // min

	[ 13 ] = KEY_STOP,        // freeze
	[ 14 ] = KEY_RECORD,      // record
	[ 29 ] = KEY_PLAYPAUSE,   // stop
	[ 25 ] = KEY_PLAY,        // play

	[ 22 ] = KEY_GOTO,        // osd
	[ 20 ] = KEY_REFRESH,     // default
	[ 12 ] = KEY_KPPLUS,      // fine tune >>>>
	[ 24 ] = KEY_KPMINUS      // fine tune <<<<
};

/* ---------------------------------------------------------------------- */

/* Ricardo Cerqueira <v4l@cerqueira.org> */
/* Weird matching, since the remote has "uncommon" keys */

static IR_KEYTAB_TYPE ir_codes_conceptronic[IR_KEYTAB_SIZE] = {

	[ 30 ] = KEY_POWER,       // power
	[ 7  ] = KEY_MEDIA,       // source
	[ 28 ] = KEY_SEARCH,      // scan

/* FIXME: duplicate keycodes?
 *
 * These four keys seem to share the same GPIO as CH+, CH-, <<< and >>>
 * The GPIO values are
 * 6397fb for both "Scan <" and "CH -",
 * 639ffb for "Scan >" and "CH+",
 * 6384fb for "Tune <" and "<<<",
 * 638cfb for "Tune >" and ">>>", regardless of the mask.
 *
 *	[ 23 ] = KEY_BACK,        // fm scan <<
 *	[ 31 ] = KEY_FORWARD,     // fm scan >>
 *
 *	[ 4  ] = KEY_LEFT,        // fm tuning <
 *	[ 12 ] = KEY_RIGHT,       // fm tuning >
 *
 * For now, these four keys are disabled. Pressing them will generate
 * the CH+/CH-/<<</>>> events
 */

	[ 3  ] = KEY_TUNER,       // TV/FM

	[ 0  ] = KEY_RECORD,
	[ 8  ] = KEY_STOP,
	[ 17 ] = KEY_PLAY,

	[ 26 ] = KEY_PLAYPAUSE,   // freeze
	[ 25 ] = KEY_ZOOM,        // zoom
	[ 15 ] = KEY_TEXT,        // min

	[ 1  ] = KEY_KP1,
	[ 11 ] = KEY_KP2,
	[ 27 ] = KEY_KP3,
	[ 5  ] = KEY_KP4,
	[ 9  ] = KEY_KP5,
	[ 21 ] = KEY_KP6,
	[ 6  ] = KEY_KP7,
	[ 10 ] = KEY_KP8,
	[ 18 ] = KEY_KP9,
	[ 2  ] = KEY_KP0,
	[ 16 ] = KEY_LAST,        // +100
	[ 19 ] = KEY_LIST,        // recall

	[ 31 ] = KEY_CHANNELUP,   // chn down
	[ 23 ] = KEY_CHANNELDOWN, // chn up
	[ 22 ] = KEY_VOLUMEUP,    // vol down
	[ 20 ] = KEY_VOLUMEDOWN,  // vol up

	[ 4  ] = KEY_KPMINUS,     // <<<
	[ 14 ] = KEY_SETUP,       // function
	[ 12 ] = KEY_KPPLUS,      // >>>

	[ 13 ] = KEY_GOTO,        // mts
	[ 29 ] = KEY_REFRESH,     // reset
	[ 24 ] = KEY_MUTE         // mute/unmute
};

struct IR {
	struct bttv_sub_device  *sub;
	struct input_dev        *input;
	struct ir_input_state   ir;
	char                    name[32];
	char                    phys[32];
	u32                     mask_keycode;
	u32                     mask_keydown;
	u32                     mask_keyup;

	int                     polling;
	u32                     last_gpio;
	struct work_struct      work;
	struct timer_list       timer;
};

static int debug;
module_param(debug, int, 0644);    /* debug level (0,1,2) */

#define DEVNAME "ir-kbd-gpio"
#define dprintk(fmt, arg...)	if (debug) \
	printk(KERN_DEBUG DEVNAME ": " fmt , ## arg)

static void ir_irq(struct bttv_sub_device *sub);
static int ir_probe(struct device *dev);
static int ir_remove(struct device *dev);

static struct bttv_sub_driver driver = {
	.drv = {
		.name	= DEVNAME,
		.probe	= ir_probe,
		.remove	= ir_remove,
	},
	.gpio_irq       = ir_irq,
};

/* ---------------------------------------------------------------------- */

static void ir_handle_key(struct IR *ir)
{
	u32 gpio,data;

	/* read gpio value */
	gpio = bttv_gpio_read(ir->sub->core);
	if (ir->polling) {
		if (ir->last_gpio == gpio)
			return;
		ir->last_gpio = gpio;
	}

	/* extract data */
	data = ir_extract_bits(gpio, ir->mask_keycode);
	dprintk(DEVNAME ": irq gpio=0x%x code=%d | %s%s%s\n",
		gpio, data,
		ir->polling               ? "poll"  : "irq",
		(gpio & ir->mask_keydown) ? " down" : "",
		(gpio & ir->mask_keyup)   ? " up"   : "");

	if (ir->mask_keydown) {
		/* bit set on keydown */
		if (gpio & ir->mask_keydown) {
			ir_input_keydown(ir->input, &ir->ir, data, data);
		} else {
			ir_input_nokey(ir->input, &ir->ir);
		}

	} else if (ir->mask_keyup) {
		/* bit cleared on keydown */
		if (0 == (gpio & ir->mask_keyup)) {
			ir_input_keydown(ir->input, &ir->ir, data, data);
		} else {
			ir_input_nokey(ir->input, &ir->ir);
		}

	} else {
		/* can't disturgissh keydown/up :-/ */
		ir_input_keydown(ir->input, &ir->ir, data, data);
		ir_input_nokey(ir->input, &ir->ir);
	}
}

static void ir_irq(struct bttv_sub_device *sub)
{
	struct IR *ir = dev_get_drvdata(&sub->dev);

	if (!ir->polling)
		ir_handle_key(ir);
}

static void ir_timer(unsigned long data)
{
	struct IR *ir = (struct IR*)data;

	schedule_work(&ir->work);
}

static void ir_work(void *data)
{
	struct IR *ir = data;
	unsigned long timeout;

	ir_handle_key(ir);
	timeout = jiffies + (ir->polling * HZ / 1000);
	mod_timer(&ir->timer, timeout);
}

/* ---------------------------------------------------------------------- */

static int ir_probe(struct device *dev)
{
	struct bttv_sub_device *sub = to_bttv_sub_dev(dev);
	struct IR *ir;
	struct input_dev *input_dev;
	IR_KEYTAB_TYPE *ir_codes = NULL;
	int ir_type = IR_TYPE_OTHER;

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ir || !input_dev) {
		kfree(ir);
		input_free_device(input_dev);
		return -ENOMEM;
	}

	/* detect & configure */
	switch (sub->core->type) {
	case BTTV_BOARD_AVERMEDIA:
	case BTTV_BOARD_AVPHONE98:
	case BTTV_BOARD_AVERMEDIA98:
		ir_codes         = ir_codes_avermedia;
		ir->mask_keycode = 0xf88000;
		ir->mask_keydown = 0x010000;
		ir->polling      = 50; // ms
		break;

	case BTTV_BOARD_AVDVBT_761:
	case BTTV_BOARD_AVDVBT_771:
		ir_codes         = ir_codes_avermedia_dvbt;
		ir->mask_keycode = 0x0f00c0;
		ir->mask_keydown = 0x000020;
		ir->polling      = 50; // ms
		break;

	case BTTV_BOARD_PXELVWPLTVPAK:
		ir_codes         = ir_codes_pixelview;
		ir->mask_keycode = 0x003e00;
		ir->mask_keyup   = 0x010000;
		ir->polling      = 50; // ms
		break;
	case BTTV_BOARD_PV_BT878P_9B:
	case BTTV_BOARD_PV_BT878P_PLUS:
		ir_codes         = ir_codes_pixelview;
		ir->mask_keycode = 0x001f00;
		ir->mask_keyup   = 0x008000;
		ir->polling      = 50; // ms
		break;

	case BTTV_BOARD_WINFAST2000:
		ir_codes         = ir_codes_winfast;
		ir->mask_keycode = 0x1f8;
		break;
	case BTTV_BOARD_MAGICTVIEW061:
	case BTTV_BOARD_MAGICTVIEW063:
		ir_codes         = ir_codes_winfast;
		ir->mask_keycode = 0x0008e000;
		ir->mask_keydown = 0x00200000;
		break;
	case BTTV_BOARD_APAC_VIEWCOMP:
		ir_codes         = ir_codes_apac_viewcomp;
		ir->mask_keycode = 0x001f00;
		ir->mask_keyup   = 0x008000;
		ir->polling      = 50; // ms
		break;
	case BTTV_BOARD_CONCEPTRONIC_CTVFMI2:
		ir_codes         = ir_codes_conceptronic;
		ir->mask_keycode = 0x001F00;
		ir->mask_keyup   = 0x006000;
		ir->polling      = 50; // ms
		break;
	}
	if (NULL == ir_codes) {
		kfree(ir);
		input_free_device(input_dev);
		return -ENODEV;
	}

	/* init hardware-specific stuff */
	bttv_gpio_inout(sub->core, ir->mask_keycode | ir->mask_keydown, 0);
	ir->sub = sub;

	/* init input device */
	snprintf(ir->name, sizeof(ir->name), "bttv IR (card=%d)",
		 sub->core->type);
	snprintf(ir->phys, sizeof(ir->phys), "pci-%s/ir0",
		 pci_name(sub->core->pci));

	ir_input_init(input_dev, &ir->ir, ir_type, ir_codes);
	input_dev->name = ir->name;
	input_dev->phys = ir->phys;
	input_dev->id.bustype = BUS_PCI;
	input_dev->id.version = 1;
	if (sub->core->pci->subsystem_vendor) {
		input_dev->id.vendor  = sub->core->pci->subsystem_vendor;
		input_dev->id.product = sub->core->pci->subsystem_device;
	} else {
		input_dev->id.vendor  = sub->core->pci->vendor;
		input_dev->id.product = sub->core->pci->device;
	}
	input_dev->cdev.dev = &sub->core->pci->dev;

	if (ir->polling) {
		INIT_WORK(&ir->work, ir_work, ir);
		init_timer(&ir->timer);
		ir->timer.function = ir_timer;
		ir->timer.data     = (unsigned long)ir;
		schedule_work(&ir->work);
	}

	/* all done */
	dev_set_drvdata(dev, ir);
	input_register_device(ir->input);

	return 0;
}

static int ir_remove(struct device *dev)
{
	struct IR *ir = dev_get_drvdata(dev);

	if (ir->polling) {
		del_timer(&ir->timer);
		flush_scheduled_work();
	}

	input_unregister_device(ir->input);
	kfree(ir);
	return 0;
}

/* ---------------------------------------------------------------------- */

MODULE_AUTHOR("Gerd Knorr, Pavel Machek");
MODULE_DESCRIPTION("input driver for bt8x8 gpio IR remote controls");
MODULE_LICENSE("GPL");

static int ir_init(void)
{
	return bttv_sub_register(&driver, "remote");
}

static void ir_fini(void)
{
	bttv_sub_unregister(&driver);
}

module_init(ir_init);
module_exit(ir_fini);


/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
