/*
 *
 * handle saa7134 IR remotes via linux kernel input layer.
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
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/input.h>

#include "saa7134-reg.h"
#include "saa7134.h"

static unsigned int disable_ir = 0;
module_param(disable_ir, int, 0444);
MODULE_PARM_DESC(disable_ir,"disable infrared remote support");

static unsigned int ir_debug = 0;
module_param(ir_debug, int, 0644);
MODULE_PARM_DESC(ir_debug,"enable debug messages [IR]");

#define dprintk(fmt, arg...)	if (ir_debug) \
	printk(KERN_DEBUG "%s/ir: " fmt, dev->name , ## arg)
#define i2cdprintk(fmt, arg...)    if (ir_debug) \
	printk(KERN_DEBUG "%s/ir: " fmt, ir->c.name , ## arg)

/* ---------------------------------------------------------------------- */

static IR_KEYTAB_TYPE flyvideo_codes[IR_KEYTAB_SIZE] = {
	[   15 ] = KEY_KP0,
	[    3 ] = KEY_KP1,
	[    4 ] = KEY_KP2,
	[    5 ] = KEY_KP3,
	[    7 ] = KEY_KP4,
	[    8 ] = KEY_KP5,
	[    9 ] = KEY_KP6,
	[   11 ] = KEY_KP7,
	[   12 ] = KEY_KP8,
	[   13 ] = KEY_KP9,

	[   14 ] = KEY_TUNER,        // Air/Cable
	[   17 ] = KEY_VIDEO,        // Video
	[   21 ] = KEY_AUDIO,        // Audio
	[    0 ] = KEY_POWER,        // Pover
	[    2 ] = KEY_ZOOM,         // Fullscreen
	[   27 ] = KEY_MUTE,         // Mute
	[   20 ] = KEY_VOLUMEUP,
	[   23 ] = KEY_VOLUMEDOWN,
	[   18 ] = KEY_CHANNELUP,    // Channel +
	[   19 ] = KEY_CHANNELDOWN,  // Channel -
	[    6 ] = KEY_AGAIN,        // Recal
	[   16 ] = KEY_KPENTER,      // Enter

	[   26 ] = KEY_F22,          // Stereo
	[   24 ] = KEY_EDIT,         // AV Source
};

static IR_KEYTAB_TYPE cinergy_codes[IR_KEYTAB_SIZE] = {
	[    0 ] = KEY_KP0,
	[    1 ] = KEY_KP1,
	[    2 ] = KEY_KP2,
	[    3 ] = KEY_KP3,
	[    4 ] = KEY_KP4,
	[    5 ] = KEY_KP5,
	[    6 ] = KEY_KP6,
	[    7 ] = KEY_KP7,
	[    8 ] = KEY_KP8,
	[    9 ] = KEY_KP9,

	[ 0x0a ] = KEY_POWER,
	[ 0x0b ] = KEY_PROG1,           // app
	[ 0x0c ] = KEY_ZOOM,            // zoom/fullscreen
	[ 0x0d ] = KEY_CHANNELUP,       // channel
	[ 0x0e ] = KEY_CHANNELDOWN,     // channel-
	[ 0x0f ] = KEY_VOLUMEUP,
	[ 0x10 ] = KEY_VOLUMEDOWN,
	[ 0x11 ] = KEY_TUNER,           // AV
	[ 0x12 ] = KEY_NUMLOCK,         // -/--
	[ 0x13 ] = KEY_AUDIO,           // audio
	[ 0x14 ] = KEY_MUTE,
	[ 0x15 ] = KEY_UP,
	[ 0x16 ] = KEY_DOWN,
	[ 0x17 ] = KEY_LEFT,
	[ 0x18 ] = KEY_RIGHT,
	[ 0x19 ] = BTN_LEFT,
	[ 0x1a ] = BTN_RIGHT,
	[ 0x1b ] = KEY_WWW,             // text
	[ 0x1c ] = KEY_REWIND,
	[ 0x1d ] = KEY_FORWARD,
	[ 0x1e ] = KEY_RECORD,
	[ 0x1f ] = KEY_PLAY,
	[ 0x20 ] = KEY_PREVIOUSSONG,
	[ 0x21 ] = KEY_NEXTSONG,
	[ 0x22 ] = KEY_PAUSE,
	[ 0x23 ] = KEY_STOP,
};

/* Alfons Geser <a.geser@cox.net>
 * updates from Job D. R. Borges <jobdrb@ig.com.br> */
static IR_KEYTAB_TYPE eztv_codes[IR_KEYTAB_SIZE] = {
	[ 18 ] = KEY_POWER,
	[  1 ] = KEY_TV,             // DVR
	[ 21 ] = KEY_DVD,            // DVD
	[ 23 ] = KEY_AUDIO,          // music
				     // DVR mode / DVD mode / music mode

	[ 27 ] = KEY_MUTE,           // mute
	[  2 ] = KEY_LANGUAGE,       // MTS/SAP / audio / autoseek
	[ 30 ] = KEY_SUBTITLE,       // closed captioning / subtitle / seek
	[ 22 ] = KEY_ZOOM,           // full screen
	[ 28 ] = KEY_VIDEO,          // video source / eject / delall
	[ 29 ] = KEY_RESTART,        // playback / angle / del
	[ 47 ] = KEY_SEARCH,         // scan / menu / playlist
	[ 48 ] = KEY_CHANNEL,        // CH surfing / bookmark / memo

	[ 49 ] = KEY_HELP,           // help
	[ 50 ] = KEY_MODE,           // num/memo
	[ 51 ] = KEY_ESC,            // cancel

	[ 12 ] = KEY_UP,             // up
	[ 16 ] = KEY_DOWN,           // down
	[  8 ] = KEY_LEFT,           // left
	[  4 ] = KEY_RIGHT,          // right
	[  3 ] = KEY_SELECT,         // select

	[ 31 ] = KEY_REWIND,         // rewind
	[ 32 ] = KEY_PLAYPAUSE,      // play/pause
	[ 41 ] = KEY_FORWARD,        // forward
	[ 20 ] = KEY_AGAIN,          // repeat
	[ 43 ] = KEY_RECORD,         // recording
	[ 44 ] = KEY_STOP,           // stop
	[ 45 ] = KEY_PLAY,           // play
	[ 46 ] = KEY_SHUFFLE,        // snapshot / shuffle

	[  0 ] = KEY_KP0,
	[  5 ] = KEY_KP1,
	[  6 ] = KEY_KP2,
	[  7 ] = KEY_KP3,
	[  9 ] = KEY_KP4,
	[ 10 ] = KEY_KP5,
	[ 11 ] = KEY_KP6,
	[ 13 ] = KEY_KP7,
	[ 14 ] = KEY_KP8,
	[ 15 ] = KEY_KP9,

	[ 42 ] = KEY_VOLUMEUP,
	[ 17 ] = KEY_VOLUMEDOWN,
	[ 24 ] = KEY_CHANNELUP,      // CH.tracking up
	[ 25 ] = KEY_CHANNELDOWN,    // CH.tracking down

	[ 19 ] = KEY_KPENTER,        // enter
	[ 33 ] = KEY_KPDOT,          // . (decimal dot)
};

static IR_KEYTAB_TYPE avacssmart_codes[IR_KEYTAB_SIZE] = {
	[ 30 ] = KEY_POWER,		// power
	[ 28 ] = KEY_SEARCH,		// scan
	[  7 ] = KEY_SELECT,		// source

	[ 22 ] = KEY_VOLUMEUP,
	[ 20 ] = KEY_VOLUMEDOWN,
	[ 31 ] = KEY_CHANNELUP,
	[ 23 ] = KEY_CHANNELDOWN,
	[ 24 ] = KEY_MUTE,

	[  2 ] = KEY_KP0,
	[  1 ] = KEY_KP1,
	[ 11 ] = KEY_KP2,
	[ 27 ] = KEY_KP3,
	[  5 ] = KEY_KP4,
	[  9 ] = KEY_KP5,
	[ 21 ] = KEY_KP6,
	[  6 ] = KEY_KP7,
	[ 10 ] = KEY_KP8,
	[ 18 ] = KEY_KP9,
	[ 16 ] = KEY_KPDOT,

	[  3 ] = KEY_TUNER,		// tv/fm
	[  4 ] = KEY_REWIND,		// fm tuning left or function left
	[ 12 ] = KEY_FORWARD,		// fm tuning right or function right

	[  0 ] = KEY_RECORD,
	[  8 ] = KEY_STOP,
	[ 17 ] = KEY_PLAY,

	[ 25 ] = KEY_ZOOM,
	[ 14 ] = KEY_MENU,		// function
	[ 19 ] = KEY_AGAIN,		// recall
	[ 29 ] = KEY_RESTART,		// reset
	[ 26 ] = KEY_SHUFFLE,		// snapshot/shuffle

// FIXME
	[ 13 ] = KEY_F21,		// mts
	[ 15 ] = KEY_F22,		// min
};

/* Alex Hermann <gaaf@gmx.net> */
static IR_KEYTAB_TYPE md2819_codes[IR_KEYTAB_SIZE] = {
	[ 40 ] = KEY_KP1,
	[ 24 ] = KEY_KP2,
	[ 56 ] = KEY_KP3,
	[ 36 ] = KEY_KP4,
	[ 20 ] = KEY_KP5,
	[ 52 ] = KEY_KP6,
	[ 44 ] = KEY_KP7,
	[ 28 ] = KEY_KP8,
	[ 60 ] = KEY_KP9,
	[ 34 ] = KEY_KP0,

	[ 32 ] = KEY_TV,		// TV/FM
	[ 16 ] = KEY_CD,		// CD
	[ 48 ] = KEY_TEXT,		// TELETEXT
	[  0 ] = KEY_POWER,		// POWER

	[  8 ] = KEY_VIDEO,		// VIDEO
	[  4 ] = KEY_AUDIO,		// AUDIO
	[ 12 ] = KEY_ZOOM,		// FULL SCREEN

	[ 18 ] = KEY_SUBTITLE,		// DISPLAY	- ???
	[ 50 ] = KEY_REWIND,		// LOOP		- ???
	[  2 ] = KEY_PRINT,		// PREVIEW	- ???

	[ 42 ] = KEY_SEARCH,		// AUTOSCAN
	[ 26 ] = KEY_SLEEP,		// FREEZE	- ???
	[ 58 ] = KEY_SHUFFLE,		// SNAPSHOT	- ???
	[ 10 ] = KEY_MUTE,		// MUTE

	[ 38 ] = KEY_RECORD,		// RECORD
	[ 22 ] = KEY_PAUSE,		// PAUSE
	[ 54 ] = KEY_STOP,		// STOP
	[  6 ] = KEY_PLAY,		// PLAY

	[ 46 ] = KEY_RED,		// <RED>
	[ 33 ] = KEY_GREEN,		// <GREEN>
	[ 14 ] = KEY_YELLOW,		// <YELLOW>
	[  1 ] = KEY_BLUE,		// <BLUE>

	[ 30 ] = KEY_VOLUMEDOWN,	// VOLUME-
	[ 62 ] = KEY_VOLUMEUP,		// VOLUME+
	[ 17 ] = KEY_CHANNELDOWN,	// CHANNEL/PAGE-
	[ 49 ] = KEY_CHANNELUP		// CHANNEL/PAGE+
};

static IR_KEYTAB_TYPE videomate_tv_pvr_codes[IR_KEYTAB_SIZE] = {
	[ 20 ] = KEY_MUTE,
	[ 36 ] = KEY_ZOOM,

	[  1 ] = KEY_DVD,
	[ 35 ] = KEY_RADIO,
	[  0 ] = KEY_TV,

	[ 10 ] = KEY_REWIND,
	[  8 ] = KEY_PLAYPAUSE,
	[ 15 ] = KEY_FORWARD,

	[  2 ] = KEY_PREVIOUS,
	[  7 ] = KEY_STOP,
	[  6 ] = KEY_NEXT,

	[ 12 ] = KEY_UP,
	[ 14 ] = KEY_DOWN,
	[ 11 ] = KEY_LEFT,
	[ 13 ] = KEY_RIGHT,
	[ 17 ] = KEY_OK,

	[  3 ] = KEY_MENU,
	[  9 ] = KEY_SETUP,
	[  5 ] = KEY_VIDEO,
	[ 34 ] = KEY_CHANNEL,

	[ 18 ] = KEY_VOLUMEUP,
	[ 21 ] = KEY_VOLUMEDOWN,
	[ 16 ] = KEY_CHANNELUP,
	[ 19 ] = KEY_CHANNELDOWN,

	[  4 ] = KEY_RECORD,

	[ 22 ] = KEY_KP1,
	[ 23 ] = KEY_KP2,
	[ 24 ] = KEY_KP3,
	[ 25 ] = KEY_KP4,
	[ 26 ] = KEY_KP5,
	[ 27 ] = KEY_KP6,
	[ 28 ] = KEY_KP7,
	[ 29 ] = KEY_KP8,
	[ 30 ] = KEY_KP9,
	[ 31 ] = KEY_KP0,

	[ 32 ] = KEY_LANGUAGE,
	[ 33 ] = KEY_SLEEP,
};

/* Michael Tokarev <mjt@tls.msk.ru>
   http://www.corpit.ru/mjt/beholdTV/remote_control.jpg
   keytable is used by MANLI MTV00[12] and BeholdTV 40[13] at
   least, and probably other cards too.
   The "ascii-art picture" below (in comments, first row
   is the keycode in hex, and subsequent row(s) shows
   the button labels (several variants when appropriate)
   helps to descide which keycodes to assign to the buttons.
 */
static IR_KEYTAB_TYPE manli_codes[IR_KEYTAB_SIZE] = {

	/*  0x1c            0x12  *
	 * FUNCTION         POWER *
	 *   FM              (|)  *
	 *                        */
	[ 0x1c ] = KEY_RADIO,	/*XXX*/
	[ 0x12 ] = KEY_POWER,

	/*  0x01    0x02    0x03  *
	 *   1       2       3    *
	 *                        *
	 *  0x04    0x05    0x06  *
	 *   4       5       6    *
	 *                        *
	 *  0x07    0x08    0x09  *
	 *   7       8       9    *
	 *                        */
	[ 0x01 ] = KEY_KP1,
	[ 0x02 ] = KEY_KP2,
	[ 0x03 ] = KEY_KP3,
	[ 0x04 ] = KEY_KP4,
	[ 0x05 ] = KEY_KP5,
	[ 0x06 ] = KEY_KP6,
	[ 0x07 ] = KEY_KP7,
	[ 0x08 ] = KEY_KP8,
	[ 0x09 ] = KEY_KP9,

	/*  0x0a    0x00    0x17  *
	 * RECALL    0      +100  *
	 *                  PLUS  *
	 *                        */
	[ 0x0a ] = KEY_AGAIN,	/*XXX KEY_REWIND? */
	[ 0x00 ] = KEY_KP0,
	[ 0x17 ] = KEY_DIGITS,	/*XXX*/

	/*  0x14            0x10  *
	 *  MENU            INFO  *
	 *  OSD                   */
	[ 0x14 ] = KEY_MENU,
	[ 0x10 ] = KEY_INFO,

	/*          0x0b          *
	 *           Up           *
	 *                        *
	 *  0x18    0x16    0x0c  *
	 *  Left     Ok     Right *
	 *                        *
	 *         0x015          *
	 *         Down           *
	 *                        */
	[ 0x0b ] = KEY_UP,	/*XXX KEY_SCROLLUP? */
	[ 0x18 ] = KEY_LEFT,	/*XXX KEY_BACK? */
	[ 0x16 ] = KEY_OK,	/*XXX KEY_SELECT? KEY_ENTER? */
	[ 0x0c ] = KEY_RIGHT,	/*XXX KEY_FORWARD? */
	[ 0x15 ] = KEY_DOWN,	/*XXX KEY_SCROLLDOWN? */

	/*  0x11            0x0d  *
	 *  TV/AV           MODE  *
	 *  SOURCE         STEREO *
	 *                        */
	[ 0x11 ] = KEY_TV,	/*XXX*/
	[ 0x0d ] = KEY_MODE,	/*XXX there's no KEY_STEREO */

	/*  0x0f    0x1b    0x1a  *
	 *  AUDIO   Vol+    Chan+ *
	 *        TIMESHIFT???    *
	 *                        *
	 *  0x0e    0x1f    0x1e  *
	 *  SLEEP   Vol-    Chan- *
	 *                        */
	[ 0x0f ] = KEY_AUDIO,
	[ 0x1b ] = KEY_VOLUMEUP,
	[ 0x1a ] = KEY_CHANNELUP,
	[ 0x0e ] = KEY_SLEEP,	/*XXX maybe KEY_PAUSE */
	[ 0x1f ] = KEY_VOLUMEDOWN,
	[ 0x1e ] = KEY_CHANNELDOWN,

	/*         0x13     0x19  *
	 *         MUTE   SNAPSHOT*
	 *                        */
	[ 0x13 ] = KEY_MUTE,
	[ 0x19 ] = KEY_RECORD,	/*XXX*/

	// 0x1d unused ?
};


/* Mike Baikov <mike@baikov.com> */
static IR_KEYTAB_TYPE gotview7135_codes[IR_KEYTAB_SIZE] = {

	[ 33 ] = KEY_POWER,
	[ 105] = KEY_TV,
	[ 51 ] = KEY_KP0,
	[ 81 ] = KEY_KP1,
	[ 49 ] = KEY_KP2,
	[ 113] = KEY_KP3,
	[ 59 ] = KEY_KP4,
	[ 88 ] = KEY_KP5,
	[ 65 ] = KEY_KP6,
	[ 72 ] = KEY_KP7,
	[ 48 ] = KEY_KP8,
	[ 83 ] = KEY_KP9,
	[ 115] = KEY_AGAIN, /* LOOP */
	[ 10 ] = KEY_AUDIO,
	[ 97 ] = KEY_PRINT, /* PREVIEW */
	[ 122] = KEY_VIDEO,
	[ 32 ] = KEY_CHANNELUP,
	[ 64 ] = KEY_CHANNELDOWN,
	[ 24 ] = KEY_VOLUMEDOWN,
	[ 80 ] = KEY_VOLUMEUP,
	[ 16 ] = KEY_MUTE,
	[ 74 ] = KEY_SEARCH,
	[ 123] = KEY_SHUFFLE, /* SNAPSHOT */
	[ 34 ] = KEY_RECORD,
	[ 98 ] = KEY_STOP,
	[ 120] = KEY_PLAY,
	[ 57 ] = KEY_REWIND,
	[ 89 ] = KEY_PAUSE,
	[ 25 ] = KEY_FORWARD,
	[  9 ] = KEY_ZOOM,

	[ 82 ] = KEY_F21, /* LIVE TIMESHIFT */
	[ 26 ] = KEY_F22, /* MIN TIMESHIFT */
	[ 58 ] = KEY_F23, /* TIMESHIFT */
	[ 112] = KEY_F24, /* NORMAL TIMESHIFT */
};

static IR_KEYTAB_TYPE ir_codes_purpletv[IR_KEYTAB_SIZE] = {
	[ 0x3  ] = KEY_POWER,
	[ 0x6f ] = KEY_MUTE,
	[ 0x10 ] = KEY_BACKSPACE,       /* Recall */

	[ 0x11 ] = KEY_KP0,
	[ 0x4  ] = KEY_KP1,
	[ 0x5  ] = KEY_KP2,
	[ 0x6  ] = KEY_KP3,
	[ 0x8  ] = KEY_KP4,
	[ 0x9  ] = KEY_KP5,
	[ 0xa  ] = KEY_KP6,
	[ 0xc  ] = KEY_KP7,
	[ 0xd  ] = KEY_KP8,
	[ 0xe  ] = KEY_KP9,
	[ 0x12 ] = KEY_KPDOT,           /* 100+ */

	[ 0x7  ] = KEY_VOLUMEUP,
	[ 0xb  ] = KEY_VOLUMEDOWN,
	[ 0x1a ] = KEY_KPPLUS,
	[ 0x18 ] = KEY_KPMINUS,
	[ 0x15 ] = KEY_UP,
	[ 0x1d ] = KEY_DOWN,
	[ 0xf  ] = KEY_CHANNELUP,
	[ 0x13 ] = KEY_CHANNELDOWN,
	[ 0x48 ] = KEY_ZOOM,

	[ 0x1b ] = KEY_VIDEO,           /* Video source */
	[ 0x49 ] = KEY_LANGUAGE,        /* MTS Select */
	[ 0x19 ] = KEY_SEARCH,          /* Auto Scan */

	[ 0x4b ] = KEY_RECORD,
	[ 0x46 ] = KEY_PLAY,
	[ 0x45 ] = KEY_PAUSE,           /* Pause */
	[ 0x44 ] = KEY_STOP,
	[ 0x40 ] = KEY_FORWARD,         /* Forward ? */
	[ 0x42 ] = KEY_REWIND,          /* Backward ? */

};

/* Mapping for the 28 key remote control as seen at
   http://www.sednacomputer.com/photo/cardbus-tv.jpg
   Pavel Mihaylov <bin@bash.info> */
static IR_KEYTAB_TYPE pctv_sedna_codes[IR_KEYTAB_SIZE] = {
	[    0 ] = KEY_KP0,
	[    1 ] = KEY_KP1,
	[    2 ] = KEY_KP2,
	[    3 ] = KEY_KP3,
	[    4 ] = KEY_KP4,
	[    5 ] = KEY_KP5,
	[    6 ] = KEY_KP6,
	[    7 ] = KEY_KP7,
	[    8 ] = KEY_KP8,
	[    9 ] = KEY_KP9,

	[ 0x0a ] = KEY_AGAIN,          /* Recall */
	[ 0x0b ] = KEY_CHANNELUP,
	[ 0x0c ] = KEY_VOLUMEUP,
	[ 0x0d ] = KEY_MODE,           /* Stereo */
	[ 0x0e ] = KEY_STOP,
	[ 0x0f ] = KEY_PREVIOUSSONG,
	[ 0x10 ] = KEY_ZOOM,
	[ 0x11 ] = KEY_TUNER,          /* Source */
	[ 0x12 ] = KEY_POWER,
	[ 0x13 ] = KEY_MUTE,
	[ 0x15 ] = KEY_CHANNELDOWN,
	[ 0x18 ] = KEY_VOLUMEDOWN,
	[ 0x19 ] = KEY_SHUFFLE,        /* Snapshot */
	[ 0x1a ] = KEY_NEXTSONG,
	[ 0x1b ] = KEY_TEXT,           /* Time Shift */
	[ 0x1c ] = KEY_RADIO,          /* FM Radio */
	[ 0x1d ] = KEY_RECORD,
	[ 0x1e ] = KEY_PAUSE,
};


/* -------------------- GPIO generic keycode builder -------------------- */

static int build_key(struct saa7134_dev *dev)
{
	struct saa7134_ir *ir = dev->remote;
	u32 gpio, data;

	/* rising SAA7134_GPIO_GPRESCAN reads the status */
	saa_clearb(SAA7134_GPIO_GPMODE3,SAA7134_GPIO_GPRESCAN);
	saa_setb(SAA7134_GPIO_GPMODE3,SAA7134_GPIO_GPRESCAN);

	gpio = saa_readl(SAA7134_GPIO_GPSTATUS0 >> 2);
	if (ir->polling) {
		if (ir->last_gpio == gpio)
			return 0;
		ir->last_gpio = gpio;
	}

	data = ir_extract_bits(gpio, ir->mask_keycode);
	dprintk("build_key gpio=0x%x mask=0x%x data=%d\n",
		gpio, ir->mask_keycode, data);

	if ((ir->mask_keydown  &&  (0 != (gpio & ir->mask_keydown))) ||
	    (ir->mask_keyup    &&  (0 == (gpio & ir->mask_keyup)))) {
		ir_input_keydown(ir->dev, &ir->ir, data, data);
	} else {
		ir_input_nokey(ir->dev, &ir->ir);
	}
	return 0;
}

/* --------------------- Chip specific I2C key builders ----------------- */

static int get_key_purpletv(struct IR_i2c *ir, u32 *ir_key, u32 *ir_raw)
{
	unsigned char b;

	/* poll IR chip */
	if (1 != i2c_master_recv(&ir->c,&b,1)) {
		i2cdprintk("read error\n");
		return -EIO;
	}

	/* no button press */
	if (b==0)
		return 0;

	/* repeating */
	if (b & 0x80)
		return 1;

	*ir_key = b;
	*ir_raw = b;
	return 1;
}

void saa7134_input_irq(struct saa7134_dev *dev)
{
	struct saa7134_ir *ir = dev->remote;

	if (!ir->polling)
		build_key(dev);
}

static void saa7134_input_timer(unsigned long data)
{
	struct saa7134_dev *dev = (struct saa7134_dev*)data;
	struct saa7134_ir *ir = dev->remote;
	unsigned long timeout;

	build_key(dev);
	timeout = jiffies + (ir->polling * HZ / 1000);
	mod_timer(&ir->timer, timeout);
}

int saa7134_input_init1(struct saa7134_dev *dev)
{
	struct saa7134_ir *ir;
	struct input_dev *input_dev;
	IR_KEYTAB_TYPE *ir_codes = NULL;
	u32 mask_keycode = 0;
	u32 mask_keydown = 0;
	u32 mask_keyup   = 0;
	int polling      = 0;
	int ir_type      = IR_TYPE_OTHER;

	if (dev->has_remote != SAA7134_REMOTE_GPIO)
		return -ENODEV;
	if (disable_ir)
		return -ENODEV;

	/* detect & configure */
	switch (dev->board) {
	case SAA7134_BOARD_FLYVIDEO2000:
	case SAA7134_BOARD_FLYVIDEO3000:
	case SAA7134_BOARD_FLYTVPLATINUM_FM:
	case SAA7134_BOARD_FLYTVPLATINUM_MINI2:
		ir_codes     = flyvideo_codes;
		mask_keycode = 0xEC00000;
		mask_keydown = 0x0040000;
		break;
	case SAA7134_BOARD_CINERGY400:
	case SAA7134_BOARD_CINERGY600:
	case SAA7134_BOARD_CINERGY600_MK3:
		ir_codes     = cinergy_codes;
		mask_keycode = 0x00003f;
		mask_keyup   = 0x040000;
		break;
	case SAA7134_BOARD_ECS_TVP3XP:
	case SAA7134_BOARD_ECS_TVP3XP_4CB5:
		ir_codes     = eztv_codes;
		mask_keycode = 0x00017c;
		mask_keyup   = 0x000002;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_KWORLD_XPERT:
	case SAA7134_BOARD_AVACSSMARTTV:
		ir_codes     = avacssmart_codes;
		mask_keycode = 0x00001F;
		mask_keyup   = 0x000020;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_MD2819:
	case SAA7134_BOARD_KWORLD_VSTREAM_XPERT:
	case SAA7134_BOARD_AVERMEDIA_305:
	case SAA7134_BOARD_AVERMEDIA_307:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_305:
	case SAA7134_BOARD_AVERMEDIA_STUDIO_307:
	case SAA7134_BOARD_AVERMEDIA_GO_007_FM:
		ir_codes     = md2819_codes;
		mask_keycode = 0x0007C8;
		mask_keydown = 0x000010;
		polling      = 50; // ms
		/* Set GPIO pin2 to high to enable the IR controller */
		saa_setb(SAA7134_GPIO_GPMODE0, 0x4);
		saa_setb(SAA7134_GPIO_GPSTATUS0, 0x4);
		break;
	case SAA7134_BOARD_KWORLD_TERMINATOR:
		ir_codes     = avacssmart_codes;
		mask_keycode = 0x00001f;
		mask_keyup   = 0x000060;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_MANLI_MTV001:
	case SAA7134_BOARD_MANLI_MTV002:
	case SAA7134_BOARD_BEHOLD_409FM:
		ir_codes     = manli_codes;
		mask_keycode = 0x001f00;
		mask_keyup   = 0x004000;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_SEDNA_PC_TV_CARDBUS:
		ir_codes     = pctv_sedna_codes;
		mask_keycode = 0x001f00;
		mask_keyup   = 0x004000;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_GOTVIEW_7135:
		ir_codes     = gotview7135_codes;
		mask_keycode = 0x0003EC;
		mask_keyup   = 0x008000;
		mask_keydown = 0x000010;
		polling	     = 50; // ms
		break;
	case SAA7134_BOARD_VIDEOMATE_TV_PVR:
	case SAA7134_BOARD_VIDEOMATE_TV_GOLD_PLUSII:
		ir_codes     = videomate_tv_pvr_codes;
		mask_keycode = 0x00003F;
		mask_keyup   = 0x400000;
		polling      = 50; // ms
		break;
	case SAA7134_BOARD_VIDEOMATE_DVBT_300:
	case SAA7134_BOARD_VIDEOMATE_DVBT_200:
		ir_codes     = videomate_tv_pvr_codes;
		mask_keycode = 0x003F00;
		mask_keyup   = 0x040000;
		break;
	}
	if (NULL == ir_codes) {
		printk("%s: Oops: IR config error [card=%d]\n",
		       dev->name, dev->board);
		return -ENODEV;
	}

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!ir || !input_dev) {
		kfree(ir);
		input_free_device(input_dev);
		return -ENOMEM;
	}

	/* init hardware-specific stuff */
	ir->mask_keycode = mask_keycode;
	ir->mask_keydown = mask_keydown;
	ir->mask_keyup   = mask_keyup;
	ir->polling      = polling;

	/* init input device */
	snprintf(ir->name, sizeof(ir->name), "saa7134 IR (%s)",
		 saa7134_boards[dev->board].name);
	snprintf(ir->phys, sizeof(ir->phys), "pci-%s/ir0",
		 pci_name(dev->pci));

	ir_input_init(input_dev, &ir->ir, ir_type, ir_codes);
	input_dev->name = ir->name;
	input_dev->phys = ir->phys;
	input_dev->id.bustype = BUS_PCI;
	input_dev->id.version = 1;
	if (dev->pci->subsystem_vendor) {
		input_dev->id.vendor  = dev->pci->subsystem_vendor;
		input_dev->id.product = dev->pci->subsystem_device;
	} else {
		input_dev->id.vendor  = dev->pci->vendor;
		input_dev->id.product = dev->pci->device;
	}
	input_dev->cdev.dev = &dev->pci->dev;

	/* all done */
	dev->remote = ir;
	if (ir->polling) {
		init_timer(&ir->timer);
		ir->timer.function = saa7134_input_timer;
		ir->timer.data     = (unsigned long)dev;
		ir->timer.expires  = jiffies + HZ;
		add_timer(&ir->timer);
	}

	input_register_device(ir->dev);
	return 0;
}

void saa7134_input_fini(struct saa7134_dev *dev)
{
	if (NULL == dev->remote)
		return;

	if (dev->remote->polling)
		del_timer_sync(&dev->remote->timer);
	input_unregister_device(dev->remote->dev);
	kfree(dev->remote);
	dev->remote = NULL;
}

void saa7134_set_i2c_ir(struct saa7134_dev *dev, struct IR_i2c *ir)
{
	if (disable_ir) {
		dprintk("Found supported i2c remote, but IR has been disabled\n");
		ir->get_key=NULL;
		return;
	}

	switch (dev->board) {
	case SAA7134_BOARD_PINNACLE_PCTV_110i:
		snprintf(ir->c.name, sizeof(ir->c.name), "Pinnacle PCTV");
		ir->get_key   = get_key_pinnacle;
		ir->ir_codes  = ir_codes_pinnacle;
		break;
	case SAA7134_BOARD_UPMOST_PURPLE_TV:
		snprintf(ir->c.name, sizeof(ir->c.name), "Purple TV");
		ir->get_key   = get_key_purpletv;
		ir->ir_codes  = ir_codes_purpletv;
		break;
	default:
		dprintk("Shouldn't get here: Unknown board %x for I2C IR?\n",dev->board);
		break;
	}

}
/* ----------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
