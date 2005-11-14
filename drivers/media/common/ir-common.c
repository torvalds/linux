/*
 *
 * some common structs and functions to handle infrared remotes via
 * input layer ...
 *
 * (c) 2003 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <media/ir-common.h>

/* -------------------------------------------------------------------------- */

MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

static int repeat = 1;
module_param(repeat, int, 0444);
MODULE_PARM_DESC(repeat,"auto-repeat for IR keys (default: on)");

static int debug = 0;    /* debug level (0,1,2) */
module_param(debug, int, 0644);

#define dprintk(level, fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG fmt , ## arg)

/* -------------------------------------------------------------------------- */

/* generic RC5 keytable                                          */
/* see http://users.pandora.be/nenya/electronics/rc5/codes00.htm */
/* used by old (black) Hauppauge remotes                         */
IR_KEYTAB_TYPE ir_codes_rc5_tv[IR_KEYTAB_SIZE] = {
	/* Keys 0 to 9 */
	[ 0x00 ] = KEY_KP0,
	[ 0x01 ] = KEY_KP1,
	[ 0x02 ] = KEY_KP2,
	[ 0x03 ] = KEY_KP3,
	[ 0x04 ] = KEY_KP4,
	[ 0x05 ] = KEY_KP5,
	[ 0x06 ] = KEY_KP6,
	[ 0x07 ] = KEY_KP7,
	[ 0x08 ] = KEY_KP8,
	[ 0x09 ] = KEY_KP9,

	[ 0x0b ] = KEY_CHANNEL,		/* channel / program (japan: 11) */
	[ 0x0c ] = KEY_POWER,		/* standby */
	[ 0x0d ] = KEY_MUTE,		/* mute / demute */
	[ 0x0f ] = KEY_TV,		/* display */
	[ 0x10 ] = KEY_VOLUMEUP,
	[ 0x11 ] = KEY_VOLUMEDOWN,
	[ 0x12 ] = KEY_BRIGHTNESSUP,
	[ 0x13 ] = KEY_BRIGHTNESSDOWN,
	[ 0x1e ] = KEY_SEARCH,		/* search + */
	[ 0x20 ] = KEY_CHANNELUP,	/* channel / program + */
	[ 0x21 ] = KEY_CHANNELDOWN,	/* channel / program - */
	[ 0x22 ] = KEY_CHANNEL,		/* alt / channel */
	[ 0x23 ] = KEY_LANGUAGE,	/* 1st / 2nd language */
	[ 0x26 ] = KEY_SLEEP,		/* sleeptimer */
	[ 0x2e ] = KEY_MENU,		/* 2nd controls (USA: menu) */
	[ 0x30 ] = KEY_PAUSE,
	[ 0x32 ] = KEY_REWIND,
	[ 0x33 ] = KEY_GOTO,
	[ 0x35 ] = KEY_PLAY,
	[ 0x36 ] = KEY_STOP,
	[ 0x37 ] = KEY_RECORD,		/* recording */
	[ 0x3c ] = KEY_TEXT,    	/* teletext submode (Japan: 12) */
	[ 0x3d ] = KEY_SUSPEND,		/* system standby */

};
EXPORT_SYMBOL_GPL(ir_codes_rc5_tv);

/* Table for Leadtek Winfast Remote Controls - used by both bttv and cx88 */
IR_KEYTAB_TYPE ir_codes_winfast[IR_KEYTAB_SIZE] = {
	/* Keys 0 to 9 */
	[ 18 ] = KEY_KP0,
	[  5 ] = KEY_KP1,
	[  6 ] = KEY_KP2,
	[  7 ] = KEY_KP3,
	[  9 ] = KEY_KP4,
	[ 10 ] = KEY_KP5,
	[ 11 ] = KEY_KP6,
	[ 13 ] = KEY_KP7,
	[ 14 ] = KEY_KP8,
	[ 15 ] = KEY_KP9,

	[  0 ] = KEY_POWER,
	[  2 ] = KEY_TUNER,		/* TV/FM */
	[ 30 ] = KEY_VIDEO,
	[  4 ] = KEY_VOLUMEUP,
	[  8 ] = KEY_VOLUMEDOWN,
	[ 12 ] = KEY_CHANNELUP,
	[ 16 ] = KEY_CHANNELDOWN,
	[  3 ] = KEY_ZOOM,		/* fullscreen */
	[ 31 ] = KEY_SUBTITLE,		/* closed caption/teletext */
	[ 32 ] = KEY_SLEEP,
	[ 20 ] = KEY_MUTE,
	[ 43 ] = KEY_RED,
	[ 44 ] = KEY_GREEN,
	[ 45 ] = KEY_YELLOW,
	[ 46 ] = KEY_BLUE,
	[ 24 ] = KEY_KPPLUS,		/* fine tune + */
	[ 25 ] = KEY_KPMINUS,		/* fine tune - */
	[ 33 ] = KEY_KPDOT,
	[ 19 ] = KEY_KPENTER,
	[ 34 ] = KEY_BACK,
	[ 35 ] = KEY_PLAYPAUSE,
	[ 36 ] = KEY_NEXT,
	[ 38 ] = KEY_STOP,
	[ 39 ] = KEY_RECORD
};
EXPORT_SYMBOL_GPL(ir_codes_winfast);

IR_KEYTAB_TYPE ir_codes_pinnacle[IR_KEYTAB_SIZE] = {
	[ 0x59 ] = KEY_MUTE,
	[ 0x4a ] = KEY_POWER,

	[ 0x18 ] = KEY_TEXT,
	[ 0x26 ] = KEY_TV,
	[ 0x3d ] = KEY_PRINT,

	[ 0x48 ] = KEY_RED,
	[ 0x04 ] = KEY_GREEN,
	[ 0x11 ] = KEY_YELLOW,
	[ 0x00 ] = KEY_BLUE,

	[ 0x2d ] = KEY_VOLUMEUP,
	[ 0x1e ] = KEY_VOLUMEDOWN,

	[ 0x49 ] = KEY_MENU,

	[ 0x16 ] = KEY_CHANNELUP,
	[ 0x17 ] = KEY_CHANNELDOWN,

	[ 0x20 ] = KEY_UP,
	[ 0x21 ] = KEY_DOWN,
	[ 0x22 ] = KEY_LEFT,
	[ 0x23 ] = KEY_RIGHT,
	[ 0x0d ] = KEY_SELECT,



	[ 0x08 ] = KEY_BACK,
	[ 0x07 ] = KEY_REFRESH,

	[ 0x2f ] = KEY_ZOOM,
	[ 0x29 ] = KEY_RECORD,

	[ 0x4b ] = KEY_PAUSE,
	[ 0x4d ] = KEY_REWIND,
	[ 0x2e ] = KEY_PLAY,
	[ 0x4e ] = KEY_FORWARD,
	[ 0x53 ] = KEY_PREVIOUS,
	[ 0x4c ] = KEY_STOP,
	[ 0x54 ] = KEY_NEXT,

	[ 0x69 ] = KEY_KP0,
	[ 0x6a ] = KEY_KP1,
	[ 0x6b ] = KEY_KP2,
	[ 0x6c ] = KEY_KP3,
	[ 0x6d ] = KEY_KP4,
	[ 0x6e ] = KEY_KP5,
	[ 0x6f ] = KEY_KP6,
	[ 0x70 ] = KEY_KP7,
	[ 0x71 ] = KEY_KP8,
	[ 0x72 ] = KEY_KP9,

	[ 0x74 ] = KEY_CHANNEL,
	[ 0x0a ] = KEY_BACKSPACE,
};

EXPORT_SYMBOL_GPL(ir_codes_pinnacle);

/* empty keytable, can be used as placeholder for not-yet created keytables */
IR_KEYTAB_TYPE ir_codes_empty[IR_KEYTAB_SIZE] = {
	[ 42 ] = KEY_COFFEE,
};
EXPORT_SYMBOL_GPL(ir_codes_empty);

/* Hauppauge: the newer, gray remotes (seems there are multiple
 * slightly different versions), shipped with cx88+ivtv cards.
 * almost rc5 coding, but some non-standard keys */
IR_KEYTAB_TYPE ir_codes_hauppauge_new[IR_KEYTAB_SIZE] = {
	/* Keys 0 to 9 */
	[ 0x00 ] = KEY_KP0,
	[ 0x01 ] = KEY_KP1,
	[ 0x02 ] = KEY_KP2,
	[ 0x03 ] = KEY_KP3,
	[ 0x04 ] = KEY_KP4,
	[ 0x05 ] = KEY_KP5,
	[ 0x06 ] = KEY_KP6,
	[ 0x07 ] = KEY_KP7,
	[ 0x08 ] = KEY_KP8,
	[ 0x09 ] = KEY_KP9,

	[ 0x0a ] = KEY_TEXT,      	/* keypad asterisk as well */
	[ 0x0b ] = KEY_RED,		/* red button */
	[ 0x0c ] = KEY_RADIO,
	[ 0x0d ] = KEY_MENU,
	[ 0x0e ] = KEY_SUBTITLE,	/* also the # key */
	[ 0x0f ] = KEY_MUTE,
	[ 0x10 ] = KEY_VOLUMEUP,
	[ 0x11 ] = KEY_VOLUMEDOWN,
	[ 0x12 ] = KEY_PREVIOUS,	/* previous channel */
	[ 0x14 ] = KEY_UP,
	[ 0x15 ] = KEY_DOWN,
	[ 0x16 ] = KEY_LEFT,
	[ 0x17 ] = KEY_RIGHT,
	[ 0x18 ] = KEY_VIDEO,		/* Videos */
	[ 0x19 ] = KEY_AUDIO,		/* Music */
	/* 0x1a: Pictures - presume this means
	   "Multimedia Home Platform" -
	   no "PICTURES" key in input.h
	 */
	[ 0x1a ] = KEY_MHP,

	[ 0x1b ] = KEY_EPG,		/* Guide */
	[ 0x1c ] = KEY_TV,
	[ 0x1e ] = KEY_NEXTSONG,	/* skip >| */
	[ 0x1f ] = KEY_EXIT,		/* back/exit */
	[ 0x20 ] = KEY_CHANNELUP,	/* channel / program + */
	[ 0x21 ] = KEY_CHANNELDOWN,	/* channel / program - */
	[ 0x22 ] = KEY_CHANNEL,		/* source (old black remote) */
	[ 0x24 ] = KEY_PREVIOUSSONG,	/* replay |< */
	[ 0x25 ] = KEY_ENTER,		/* OK */
	[ 0x26 ] = KEY_SLEEP,		/* minimize (old black remote) */
	[ 0x29 ] = KEY_BLUE,		/* blue key */
	[ 0x2e ] = KEY_GREEN,		/* green button */
	[ 0x30 ] = KEY_PAUSE,		/* pause */
	[ 0x32 ] = KEY_REWIND,		/* backward << */
	[ 0x34 ] = KEY_FASTFORWARD,	/* forward >> */
	[ 0x35 ] = KEY_PLAY,
	[ 0x36 ] = KEY_STOP,
	[ 0x37 ] = KEY_RECORD,		/* recording */
	[ 0x38 ] = KEY_YELLOW,		/* yellow key */
	[ 0x3b ] = KEY_SELECT,		/* top right button */
	[ 0x3c ] = KEY_ZOOM,		/* full */
	[ 0x3d ] = KEY_POWER,		/* system power (green button) */
};
EXPORT_SYMBOL(ir_codes_hauppauge_new);

IR_KEYTAB_TYPE ir_codes_pixelview[IR_KEYTAB_SIZE] = {
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

	[  3 ] = KEY_TUNER,		/* TV/FM */
	[  7 ] = KEY_SEARCH,		/* scan */
	[ 28 ] = KEY_ZOOM,		/* full screen */
	[ 30 ] = KEY_POWER,
	[ 23 ] = KEY_VOLUMEDOWN,
	[ 31 ] = KEY_VOLUMEUP,
	[ 20 ] = KEY_CHANNELDOWN,
	[ 22 ] = KEY_CHANNELUP,
	[ 24 ] = KEY_MUTE,

	[  0 ] = KEY_LIST,		/* source */
	[ 19 ] = KEY_INFO,		/* loop */
	[ 16 ] = KEY_LAST,		/* +100 */
	[ 13 ] = KEY_CLEAR,		/* reset */
	[ 12 ] = BTN_RIGHT,		/* fun++ */
	[  4 ] = BTN_LEFT,		/* fun-- */
	[ 14 ] = KEY_GOTO,		/* function */
	[ 15 ] = KEY_STOP,		/* freeze */
};
EXPORT_SYMBOL(ir_codes_pixelview);

/* -------------------------------------------------------------------------- */

static void ir_input_key_event(struct input_dev *dev, struct ir_input_state *ir)
{
	if (KEY_RESERVED == ir->keycode) {
		printk(KERN_INFO "%s: unknown key: key=0x%02x raw=0x%02x down=%d\n",
		       dev->name,ir->ir_key,ir->ir_raw,ir->keypressed);
		return;
	}
	dprintk(1,"%s: key event code=%d down=%d\n",
		dev->name,ir->keycode,ir->keypressed);
	input_report_key(dev,ir->keycode,ir->keypressed);
	input_sync(dev);
}

/* -------------------------------------------------------------------------- */

void ir_input_init(struct input_dev *dev, struct ir_input_state *ir,
		   int ir_type, IR_KEYTAB_TYPE *ir_codes)
{
	int i;

	ir->ir_type = ir_type;
	if (ir_codes)
		memcpy(ir->ir_codes, ir_codes, sizeof(ir->ir_codes));

	dev->keycode     = ir->ir_codes;
	dev->keycodesize = sizeof(IR_KEYTAB_TYPE);
	dev->keycodemax  = IR_KEYTAB_SIZE;
	for (i = 0; i < IR_KEYTAB_SIZE; i++)
		set_bit(ir->ir_codes[i], dev->keybit);
	clear_bit(0, dev->keybit);

	set_bit(EV_KEY, dev->evbit);
	if (repeat)
		set_bit(EV_REP, dev->evbit);
}

void ir_input_nokey(struct input_dev *dev, struct ir_input_state *ir)
{
	if (ir->keypressed) {
		ir->keypressed = 0;
		ir_input_key_event(dev,ir);
	}
}

void ir_input_keydown(struct input_dev *dev, struct ir_input_state *ir,
		      u32 ir_key, u32 ir_raw)
{
	u32 keycode = IR_KEYCODE(ir->ir_codes, ir_key);

	if (ir->keypressed && ir->keycode != keycode) {
		ir->keypressed = 0;
		ir_input_key_event(dev,ir);
	}
	if (!ir->keypressed) {
		ir->ir_key  = ir_key;
		ir->ir_raw  = ir_raw;
		ir->keycode = keycode;
		ir->keypressed = 1;
		ir_input_key_event(dev,ir);
	}
}

/* -------------------------------------------------------------------------- */

u32 ir_extract_bits(u32 data, u32 mask)
{
	int mbit, vbit;
	u32 value;

	value = 0;
	vbit  = 0;
	for (mbit = 0; mbit < 32; mbit++) {
		if (!(mask & ((u32)1 << mbit)))
			continue;
		if (data & ((u32)1 << mbit))
			value |= (1 << vbit);
		vbit++;
	}
	return value;
}

static int inline getbit(u32 *samples, int bit)
{
	return (samples[bit/32] & (1 << (31-(bit%32)))) ? 1 : 0;
}

/* sump raw samples for visual debugging ;) */
int ir_dump_samples(u32 *samples, int count)
{
	int i, bit, start;

	printk(KERN_DEBUG "ir samples: ");
	start = 0;
	for (i = 0; i < count * 32; i++) {
		bit = getbit(samples,i);
		if (bit)
			start = 1;
		if (0 == start)
			continue;
		printk("%s", bit ? "#" : "_");
	}
	printk("\n");
	return 0;
}

/* decode raw samples, pulse distance coding used by NEC remotes */
int ir_decode_pulsedistance(u32 *samples, int count, int low, int high)
{
	int i,last,bit,len;
	u32 curBit;
	u32 value;

	/* find start burst */
	for (i = len = 0; i < count * 32; i++) {
		bit = getbit(samples,i);
		if (bit) {
			len++;
		} else {
			if (len >= 29)
				break;
			len = 0;
		}
	}

	/* start burst to short */
	if (len < 29)
		return 0xffffffff;

	/* find start silence */
	for (len = 0; i < count * 32; i++) {
		bit = getbit(samples,i);
		if (bit) {
			break;
		} else {
			len++;
		}
	}

	/* silence to short */
	if (len < 7)
		return 0xffffffff;

	/* go decoding */
	len   = 0;
	last = 1;
	value = 0; curBit = 1;
	for (; i < count * 32; i++) {
		bit  = getbit(samples,i);
		if (last) {
			if(bit) {
				continue;
			} else {
				len = 1;
			}
		} else {
			if (bit) {
				if (len > (low + high) /2)
					value |= curBit;
				curBit <<= 1;
				if (curBit == 1)
					break;
			} else {
				len++;
			}
		}
		last = bit;
	}

	return value;
}

/* decode raw samples, biphase coding, used by rc5 for example */
int ir_decode_biphase(u32 *samples, int count, int low, int high)
{
	int i,last,bit,len,flips;
	u32 value;

	/* find start bit (1) */
	for (i = 0; i < 32; i++) {
		bit = getbit(samples,i);
		if (bit)
			break;
	}

	/* go decoding */
	len   = 0;
	flips = 0;
	value = 1;
	for (; i < count * 32; i++) {
		if (len > high)
			break;
		if (flips > 1)
			break;
		last = bit;
		bit  = getbit(samples,i);
		if (last == bit) {
			len++;
			continue;
		}
		if (len < low) {
			len++;
			flips++;
			continue;
		}
		value <<= 1;
		value |= bit;
		flips = 0;
		len   = 1;
	}
	return value;
}

EXPORT_SYMBOL_GPL(ir_input_init);
EXPORT_SYMBOL_GPL(ir_input_nokey);
EXPORT_SYMBOL_GPL(ir_input_keydown);

EXPORT_SYMBOL_GPL(ir_extract_bits);
EXPORT_SYMBOL_GPL(ir_dump_samples);
EXPORT_SYMBOL_GPL(ir_decode_biphase);
EXPORT_SYMBOL_GPL(ir_decode_pulsedistance);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

