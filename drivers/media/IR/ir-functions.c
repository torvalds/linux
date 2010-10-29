/*
 * some common functions to handle infrared remote protocol decoding for
 * drivers which have not yet been (or can't be) converted to use the
 * regular protocol decoders...
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
#include <linux/string.h>
#include <linux/jiffies.h>
#include <media/ir-common.h>
#include "ir-core-priv.h"

/* -------------------------------------------------------------------------- */

MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

/* -------------------------------------------------------------------------- */
/* extract mask bits out of data and pack them into the result */
u32 ir_extract_bits(u32 data, u32 mask)
{
	u32 vbit = 1, value = 0;

	do {
	    if (mask&1) {
		if (data&1)
			value |= vbit;
		vbit<<=1;
	    }
	    data>>=1;
	} while (mask>>=1);

	return value;
}
EXPORT_SYMBOL_GPL(ir_extract_bits);

/* RC5 decoding stuff, moved from bttv-input.c to share it with
 * saa7134 */

/* decode raw bit pattern to RC5 code */
static u32 ir_rc5_decode(unsigned int code)
{
	unsigned int org_code = code;
	unsigned int pair;
	unsigned int rc5 = 0;
	int i;

	for (i = 0; i < 14; ++i) {
		pair = code & 0x3;
		code >>= 2;

		rc5 <<= 1;
		switch (pair) {
		case 0:
		case 2:
			break;
		case 1:
			rc5 |= 1;
			break;
		case 3:
			IR_dprintk(1, "ir-common: ir_rc5_decode(%x) bad code\n", org_code);
			return 0;
		}
	}
	IR_dprintk(1, "ir-common: code=%x, rc5=%x, start=%x, toggle=%x, address=%x, "
		"instr=%x\n", rc5, org_code, RC5_START(rc5),
		RC5_TOGGLE(rc5), RC5_ADDR(rc5), RC5_INSTR(rc5));
	return rc5;
}

void ir_rc5_timer_end(unsigned long data)
{
	struct card_ir *ir = (struct card_ir *)data;
	struct timeval tv;
	unsigned long current_jiffies;
	u32 gap;
	u32 rc5 = 0;

	/* get time */
	current_jiffies = jiffies;
	do_gettimeofday(&tv);

	/* avoid overflow with gap >1s */
	if (tv.tv_sec - ir->base_time.tv_sec > 1) {
		gap = 200000;
	} else {
		gap = 1000000 * (tv.tv_sec - ir->base_time.tv_sec) +
		    tv.tv_usec - ir->base_time.tv_usec;
	}

	/* signal we're ready to start a new code */
	ir->active = 0;

	/* Allow some timer jitter (RC5 is ~24ms anyway so this is ok) */
	if (gap < 28000) {
		IR_dprintk(1, "ir-common: spurious timer_end\n");
		return;
	}

	if (ir->last_bit < 20) {
		/* ignore spurious codes (caused by light/other remotes) */
		IR_dprintk(1, "ir-common: short code: %x\n", ir->code);
	} else {
		ir->code = (ir->code << ir->shift_by) | 1;
		rc5 = ir_rc5_decode(ir->code);

		/* two start bits? */
		if (RC5_START(rc5) != ir->start) {
			IR_dprintk(1, "ir-common: rc5 start bits invalid: %u\n", RC5_START(rc5));

			/* right address? */
		} else if (RC5_ADDR(rc5) == ir->addr) {
			u32 toggle = RC5_TOGGLE(rc5);
			u32 instr = RC5_INSTR(rc5);

			/* Good code */
			ir_keydown(ir->dev, instr, toggle);
			IR_dprintk(1, "ir-common: instruction %x, toggle %x\n",
				   instr, toggle);
		}
	}
}
EXPORT_SYMBOL_GPL(ir_rc5_timer_end);
