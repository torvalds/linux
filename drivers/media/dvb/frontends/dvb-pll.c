/*
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

static void thomson_dtt759x_bw(u8 *buf, u32 freq, int bandwidth)
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
	.count = 6,
	.entries = {
		{          0, 36166667, 166666, 0xbc, 0x03 },
		{  157500000, 36166667, 166666, 0xbc, 0x01 },
		{  443250000, 36166667, 166666, 0xbc, 0x02 },
		{  542000000, 36166667, 166666, 0xbc, 0x04 },
		{  830000000, 36166667, 166666, 0xf4, 0x04 },
		{  999999999, 36166667, 166666, 0xfc, 0x04 },
	},
};
EXPORT_SYMBOL(dvb_pll_lg_z201);

struct dvb_pll_desc dvb_pll_microtune_4042 = {
	.name  = "Microtune 4042 FI5",
	.min   =  57000000,
	.max   = 858000000,
	.count = 3,
	.entries = {
		{ 162000000, 44000000, 62500, 0x8e, 0xa1 },
		{ 457000000, 44000000, 62500, 0x8e, 0x91 },
		{ 999999999, 44000000, 62500, 0x8e, 0x31 },
	},
};
EXPORT_SYMBOL(dvb_pll_microtune_4042);

struct dvb_pll_desc dvb_pll_thomson_dtt761x = {
	/* DTT 7611 7611A 7612 7613 7613A 7614 7615 7615A */
	.name  = "Thomson dtt761x",
	.min   =  57000000,
	.max   = 863000000,
	.count = 3,
	.entries = {
		{ 147000000, 44000000, 62500, 0x8e, 0x39 },
		{ 417000000, 44000000, 62500, 0x8e, 0x3a },
		{ 999999999, 44000000, 62500, 0x8e, 0x3c },
	},
};
EXPORT_SYMBOL(dvb_pll_thomson_dtt761x);

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

/* Infineon TUA6010XS
 * used in Thomson Cable Tuner
 */
struct dvb_pll_desc dvb_pll_tua6010xs = {
	.name  = "Infineon TUA6010XS",
	.min   =  44250000,
	.max   = 858000000,
	.count = 3,
	.entries = {
		{  115750000, 36125000, 62500, 0x8e, 0x03 },
		{  403250000, 36125000, 62500, 0x8e, 0x06 },
		{  999999999, 36125000, 62500, 0x8e, 0x85 },
	},
};
EXPORT_SYMBOL(dvb_pll_tua6010xs);

/* Panasonic env57h1xd5 (some Philips PLL ?) */
struct dvb_pll_desc dvb_pll_env57h1xd5 = {
	.name  = "Panasonic ENV57H1XD5",
	.min   =  44250000,
	.max   = 858000000,
	.count = 4,
	.entries = {
		{  153000000, 36291666, 166666, 0xc2, 0x41 },
		{  470000000, 36291666, 166666, 0xc2, 0x42 },
		{  526000000, 36291666, 166666, 0xc2, 0x84 },
		{  999999999, 36291666, 166666, 0xc2, 0xa4 },
	},
};
EXPORT_SYMBOL(dvb_pll_env57h1xd5);

/* Philips TDA6650/TDA6651
 * used in Panasonic ENV77H11D5
 */
static void tda665x_bw(u8 *buf, u32 freq, int bandwidth)
{
	if (bandwidth == BANDWIDTH_8_MHZ)
		buf[3] |= 0x08;
}

struct dvb_pll_desc dvb_pll_tda665x = {
	.name  = "Philips TDA6650/TDA6651",
	.min   =  44250000,
	.max   = 858000000,
	.setbw = tda665x_bw,
	.count = 12,
	.entries = {
		{   93834000, 36249333, 166667, 0xca, 0x61 /* 011 0 0 0  01 */ },
		{  123834000, 36249333, 166667, 0xca, 0xa1 /* 101 0 0 0  01 */ },
		{  161000000, 36249333, 166667, 0xca, 0xa1 /* 101 0 0 0  01 */ },
		{  163834000, 36249333, 166667, 0xca, 0xc2 /* 110 0 0 0  10 */ },
		{  253834000, 36249333, 166667, 0xca, 0x62 /* 011 0 0 0  10 */ },
		{  383834000, 36249333, 166667, 0xca, 0xa2 /* 101 0 0 0  10 */ },
		{  443834000, 36249333, 166667, 0xca, 0xc2 /* 110 0 0 0  10 */ },
		{  444000000, 36249333, 166667, 0xca, 0xc3 /* 110 0 0 0  11 */ },
		{  583834000, 36249333, 166667, 0xca, 0x63 /* 011 0 0 0  11 */ },
		{  793834000, 36249333, 166667, 0xca, 0xa3 /* 101 0 0 0  11 */ },
		{  444834000, 36249333, 166667, 0xca, 0xc3 /* 110 0 0 0  11 */ },
		{  861000000, 36249333, 166667, 0xca, 0xe3 /* 111 0 0 0  11 */ },
	}
};
EXPORT_SYMBOL(dvb_pll_tda665x);

/* Infineon TUA6034
 * used in LG TDTP E102P
 */
static void tua6034_bw(u8 *buf, u32 freq, int bandwidth)
{
	if (BANDWIDTH_7_MHZ != bandwidth)
		buf[3] |= 0x08;
}

struct dvb_pll_desc dvb_pll_tua6034 = {
	.name  = "Infineon TUA6034",
	.min   =  44250000,
	.max   = 858000000,
	.count = 3,
	.setbw = tua6034_bw,
	.entries = {
		{  174500000, 36166667, 62500, 0xce, 0x01 },
		{  230000000, 36166667, 62500, 0xce, 0x02 },
		{  999999999, 36166667, 62500, 0xce, 0x04 },
	},
};
EXPORT_SYMBOL(dvb_pll_tua6034);

/* Infineon TUA6034
 * used in LG TDVS H061F and LG TDVS H062F
 */
struct dvb_pll_desc dvb_pll_tdvs_tua6034 = {
	.name  = "LG/Infineon TUA6034",
	.min   =  54000000,
	.max   = 863000000,
	.count = 3,
	.entries = {
		{  160000000, 44000000, 62500, 0xce, 0x01 },
		{  455000000, 44000000, 62500, 0xce, 0x02 },
		{  999999999, 44000000, 62500, 0xce, 0x04 },
	},
};
EXPORT_SYMBOL(dvb_pll_tdvs_tua6034);

/* Philips FMD1216ME
 * used in Medion Hybrid PCMCIA card and USB Box
 */
static void fmd1216me_bw(u8 *buf, u32 freq, int bandwidth)
{
	if (bandwidth == BANDWIDTH_8_MHZ && freq >= 158870000)
		buf[3] |= 0x08;
}

struct dvb_pll_desc dvb_pll_fmd1216me = {
	.name = "Philips FMD1216ME",
	.min = 50870000,
	.max = 858000000,
	.setbw = fmd1216me_bw,
	.count = 7,
	.entries = {
		{ 143870000, 36213333, 166667, 0xbc, 0x41 },
		{ 158870000, 36213333, 166667, 0xf4, 0x41 },
		{ 329870000, 36213333, 166667, 0xbc, 0x42 },
		{ 441870000, 36213333, 166667, 0xf4, 0x42 },
		{ 625870000, 36213333, 166667, 0xbc, 0x44 },
		{ 803870000, 36213333, 166667, 0xf4, 0x44 },
		{ 999999999, 36213333, 166667, 0xfc, 0x44 },
	}
};
EXPORT_SYMBOL(dvb_pll_fmd1216me);

/* ALPS TDED4
 * used in Nebula-Cards and USB boxes
 */
static void tded4_bw(u8 *buf, u32 freq, int bandwidth)
{
	if (bandwidth == BANDWIDTH_8_MHZ)
		buf[3] |= 0x04;
}

struct dvb_pll_desc dvb_pll_tded4 = {
	.name = "ALPS TDED4",
	.min = 47000000,
	.max = 863000000,
	.setbw = tded4_bw,
	.count = 4,
	.entries = {
		{ 153000000, 36166667, 166667, 0x85, 0x01 },
		{ 470000000, 36166667, 166667, 0x85, 0x02 },
		{ 823000000, 36166667, 166667, 0x85, 0x08 },
		{ 999999999, 36166667, 166667, 0x85, 0x88 },
	}
};
EXPORT_SYMBOL(dvb_pll_tded4);

/* ALPS TDHU2
 * used in AverTVHD MCE A180
 */
struct dvb_pll_desc dvb_pll_tdhu2 = {
	.name = "ALPS TDHU2",
	.min = 54000000,
	.max = 864000000,
	.count = 4,
	.entries = {
		{ 162000000, 44000000, 62500, 0x85, 0x01 },
		{ 426000000, 44000000, 62500, 0x85, 0x02 },
		{ 782000000, 44000000, 62500, 0x85, 0x08 },
		{ 999999999, 44000000, 62500, 0x85, 0x88 },
	}
};
EXPORT_SYMBOL(dvb_pll_tdhu2);

/* Philips TUV1236D
 * used in ATI HDTV Wonder
 */
struct dvb_pll_desc dvb_pll_tuv1236d = {
	.name  = "Philips TUV1236D",
	.min   =  54000000,
	.max   = 864000000,
	.count = 3,
	.entries = {
		{ 157250000, 44000000, 62500, 0xc6, 0x41 },
		{ 454000000, 44000000, 62500, 0xc6, 0x42 },
		{ 999999999, 44000000, 62500, 0xc6, 0x44 },
	},
};
EXPORT_SYMBOL(dvb_pll_tuv1236d);

/* Samsung TBMV30111IN
 * used in Air2PC ATSC - 2nd generation (nxt2002)
 */
struct dvb_pll_desc dvb_pll_tbmv30111in = {
	.name = "Samsung TBMV30111IN",
	.min = 54000000,
	.max = 860000000,
	.count = 6,
	.entries = {
		{ 172000000, 44000000, 166666, 0xb4, 0x01 },
		{ 214000000, 44000000, 166666, 0xb4, 0x02 },
		{ 467000000, 44000000, 166666, 0xbc, 0x02 },
		{ 721000000, 44000000, 166666, 0xbc, 0x08 },
		{ 841000000, 44000000, 166666, 0xf4, 0x08 },
		{ 999999999, 44000000, 166666, 0xfc, 0x02 },
	}
};
EXPORT_SYMBOL(dvb_pll_tbmv30111in);

/*
 * Philips SD1878 Tuner.
 */
struct dvb_pll_desc dvb_pll_philips_sd1878_tda8261 = {
	.name  = "Philips SD1878",
	.min   =  950000,
	.max   = 2150000,
	.count = 4,
	.entries = {
		{ 1250000, 499, 500, 0xc4, 0x00},
		{ 1550000, 499, 500, 0xc4, 0x40},
		{ 2050000, 499, 500, 0xc4, 0x80},
		{ 2150000, 499, 500, 0xc4, 0xc0},
	},
};
EXPORT_SYMBOL(dvb_pll_philips_sd1878_tda8261);

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
		desc->setbw(buf, freq, bandwidth);

	if (debug)
		printk("pll: %s: div=%d | buf=0x%02x,0x%02x,0x%02x,0x%02x\n",
		       desc->name, div, buf[0], buf[1], buf[2], buf[3]);

	return 0;
}
EXPORT_SYMBOL(dvb_pll_configure);

MODULE_DESCRIPTION("dvb pll library");
MODULE_AUTHOR("Gerd Knorr");
MODULE_LICENSE("GPL");
