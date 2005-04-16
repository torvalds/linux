/*
 * $Id: dvb-pll.c,v 1.7 2005/02/10 11:52:02 kraxel Exp $
 *
 * descriptions + helper functions for simple dvb plls.
 *
 * (c) 2004 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
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
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/dvb/frontend.h>
#include <asm/types.h>

#include "dvb-pll.h"

/* ----------------------------------------------------------- */
/* descriptions                                                */

struct dvb_pll_desc dvb_pll_thomson_dtt7579 = {
	.name  = "Thomson dtt7579",
	.min   = 177000000,
	.max   = 858000000,
	.count = 5,
	.entries = {
		{          0, 36166667, 166666, 0xb4, 0x03 }, /* go sleep */
		{  443250000, 36166667, 166666, 0xb4, 0x02 },
		{  542000000, 36166667, 166666, 0xb4, 0x08 },
		{  771000000, 36166667, 166666, 0xbc, 0x08 },
		{  999999999, 36166667, 166666, 0xf4, 0x08 },
	},
};
EXPORT_SYMBOL(dvb_pll_thomson_dtt7579);

struct dvb_pll_desc dvb_pll_thomson_dtt7610 = {
	.name  = "Thomson dtt7610",
	.min   =  44000000,
	.max   = 958000000,
	.count = 3,
	.entries = {
		{ 157250000, 44000000, 62500, 0x8e, 0x39 },
		{ 454000000, 44000000, 62500, 0x8e, 0x3a },
		{ 999999999, 44000000, 62500, 0x8e, 0x3c },
	},
};
EXPORT_SYMBOL(dvb_pll_thomson_dtt7610);

static void thomson_dtt759x_bw(u8 *buf, int bandwidth)
{
	if (BANDWIDTH_7_MHZ == bandwidth)
		buf[3] |= 0x10;
}

struct dvb_pll_desc dvb_pll_thomson_dtt759x = {
	.name  = "Thomson dtt759x",
	.min   = 177000000,
	.max   = 896000000,
	.setbw = thomson_dtt759x_bw,
	.count = 6,
	.entries = {
		{          0, 36166667, 166666, 0x84, 0x03 },
		{  264000000, 36166667, 166666, 0xb4, 0x02 },
		{  470000000, 36166667, 166666, 0xbc, 0x02 },
		{  735000000, 36166667, 166666, 0xbc, 0x08 },
		{  835000000, 36166667, 166666, 0xf4, 0x08 },
		{  999999999, 36166667, 166666, 0xfc, 0x08 },
	},
};
EXPORT_SYMBOL(dvb_pll_thomson_dtt759x);

struct dvb_pll_desc dvb_pll_lg_z201 = {
	.name  = "LG z201",
	.min   = 174000000,
	.max   = 862000000,
	.count = 5,
	.entries = {
		{          0, 36166667, 166666, 0xbc, 0x03 },
		{  443250000, 36166667, 166666, 0xbc, 0x01 },
		{  542000000, 36166667, 166666, 0xbc, 0x02 },
		{  830000000, 36166667, 166666, 0xf4, 0x02 },
		{  999999999, 36166667, 166666, 0xfc, 0x02 },
	},
};
EXPORT_SYMBOL(dvb_pll_lg_z201);

struct dvb_pll_desc dvb_pll_unknown_1 = {
	.name  = "unknown 1", /* used by dntv live dvb-t */
	.min   = 174000000,
	.max   = 862000000,
	.count = 9,
	.entries = {
		{  150000000, 36166667, 166666, 0xb4, 0x01 },
		{  173000000, 36166667, 166666, 0xbc, 0x01 },
		{  250000000, 36166667, 166666, 0xb4, 0x02 },
		{  400000000, 36166667, 166666, 0xbc, 0x02 },
		{  420000000, 36166667, 166666, 0xf4, 0x02 },
		{  470000000, 36166667, 166666, 0xfc, 0x02 },
		{  600000000, 36166667, 166666, 0xbc, 0x08 },
		{  730000000, 36166667, 166666, 0xf4, 0x08 },
		{  999999999, 36166667, 166666, 0xfc, 0x08 },
	},
};
EXPORT_SYMBOL(dvb_pll_unknown_1);

/* ----------------------------------------------------------- */
/* code                                                        */

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable verbose debug messages");

int dvb_pll_configure(struct dvb_pll_desc *desc, u8 *buf,
		      u32 freq, int bandwidth)
{
	u32 div;
	int i;

	if (freq != 0 && (freq < desc->min || freq > desc->max))
	    return -EINVAL;

	for (i = 0; i < desc->count; i++) {
		if (freq > desc->entries[i].limit)
			continue;
		break;
	}
	if (debug)
		printk("pll: %s: freq=%d bw=%d | i=%d/%d\n",
		       desc->name, freq, bandwidth, i, desc->count);
	BUG_ON(i == desc->count);

	div = (freq + desc->entries[i].offset) / desc->entries[i].stepsize;
	buf[0] = div >> 8;
	buf[1] = div & 0xff;
	buf[2] = desc->entries[i].cb1;
	buf[3] = desc->entries[i].cb2;

	if (desc->setbw)
		desc->setbw(buf, bandwidth);

	if (debug)
		printk("pll: %s: div=%d | buf=0x%02x,0x%02x,0x%02x,0x%02x\n",
		       desc->name, div, buf[0], buf[1], buf[2], buf[3]);

	return 0;
}
EXPORT_SYMBOL(dvb_pll_configure);

MODULE_DESCRIPTION("dvb pll library");
MODULE_AUTHOR("Gerd Knorr");
MODULE_LICENSE("GPL");

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
