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
