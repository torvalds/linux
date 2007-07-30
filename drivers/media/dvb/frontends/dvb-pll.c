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

struct dvb_pll_desc {
	char *name;
	u32  min;
	u32  max;
	u32  iffreq;
	void (*set)(u8 *buf, const struct dvb_frontend_parameters *params);
	u8   *initdata;
	u8   *sleepdata;
	int  count;
	struct {
		u32 limit;
		u32 stepsize;
		u8  config;
		u8  cb;
	} entries[12];
};

/* ----------------------------------------------------------- */
/* descriptions                                                */

/* Set AGC TOP value to 103 dBuV:
	0x80 = Control Byte
	0x40 = 250 uA charge pump (irrelevant)
	0x18 = Aux Byte to follow
	0x06 = 64.5 kHz divider (irrelevant)
	0x01 = Disable Vt (aka sleep)

	0x00 = AGC Time constant 2s Iagc = 300 nA (vs 0x80 = 9 nA)
	0x50 = AGC Take over point = 103 dBuV */
static u8 tua603x_agc103[] = { 2, 0x80|0x40|0x18|0x06|0x01, 0x00|0x50 };

/*	0x04 = 166.67 kHz divider

	0x80 = AGC Time constant 50ms Iagc = 9 uA
	0x20 = AGC Take over point = 112 dBuV */
static u8 tua603x_agc112[] = { 2, 0x80|0x40|0x18|0x04|0x01, 0x80|0x20 };

static struct dvb_pll_desc dvb_pll_thomson_dtt7579 = {
	.name  = "Thomson dtt7579",
	.min   = 177000000,
	.max   = 858000000,
	.iffreq= 36166667,
	.sleepdata = (u8[]){ 2, 0xb4, 0x03 },
	.count = 4,
	.entries = {
		{  443250000, 166667, 0xb4, 0x02 },
		{  542000000, 166667, 0xb4, 0x08 },
		{  771000000, 166667, 0xbc, 0x08 },
		{  999999999, 166667, 0xf4, 0x08 },
	},
};

static struct dvb_pll_desc dvb_pll_thomson_dtt7610 = {
	.name  = "Thomson dtt7610",
	.min   =  44000000,
	.max   = 958000000,
	.iffreq= 44000000,
	.count = 3,
	.entries = {
		{ 157250000, 62500, 0x8e, 0x39 },
		{ 454000000, 62500, 0x8e, 0x3a },
		{ 999999999, 62500, 0x8e, 0x3c },
	},
};

static void thomson_dtt759x_bw(u8 *buf,
			       const struct dvb_frontend_parameters *params)
{
	if (BANDWIDTH_7_MHZ == params->u.ofdm.bandwidth)
		buf[3] |= 0x10;
}

static struct dvb_pll_desc dvb_pll_thomson_dtt759x = {
	.name  = "Thomson dtt759x",
	.min   = 177000000,
	.max   = 896000000,
	.set   = thomson_dtt759x_bw,
	.iffreq= 36166667,
	.sleepdata = (u8[]){ 2, 0x84, 0x03 },
	.count = 5,
	.entries = {
		{  264000000, 166667, 0xb4, 0x02 },
		{  470000000, 166667, 0xbc, 0x02 },
		{  735000000, 166667, 0xbc, 0x08 },
		{  835000000, 166667, 0xf4, 0x08 },
		{  999999999, 166667, 0xfc, 0x08 },
	},
};

static struct dvb_pll_desc dvb_pll_lg_z201 = {
	.name  = "LG z201",
	.min   = 174000000,
	.max   = 862000000,
	.iffreq= 36166667,
	.sleepdata = (u8[]){ 2, 0xbc, 0x03 },
	.count = 5,
	.entries = {
		{  157500000, 166667, 0xbc, 0x01 },
		{  443250000, 166667, 0xbc, 0x02 },
		{  542000000, 166667, 0xbc, 0x04 },
		{  830000000, 166667, 0xf4, 0x04 },
		{  999999999, 166667, 0xfc, 0x04 },
	},
};

static struct dvb_pll_desc dvb_pll_microtune_4042 = {
	.name  = "Microtune 4042 FI5",
	.min   =  57000000,
	.max   = 858000000,
	.iffreq= 44000000,
	.count = 3,
	.entries = {
		{ 162000000, 62500, 0x8e, 0xa1 },
		{ 457000000, 62500, 0x8e, 0x91 },
		{ 999999999, 62500, 0x8e, 0x31 },
	},
};

static struct dvb_pll_desc dvb_pll_thomson_dtt761x = {
	/* DTT 7611 7611A 7612 7613 7613A 7614 7615 7615A */
	.name  = "Thomson dtt761x",
	.min   =  57000000,
	.max   = 863000000,
	.iffreq= 44000000,
	.count = 3,
	.initdata = tua603x_agc103,
	.entries = {
		{ 147000000, 62500, 0x8e, 0x39 },
		{ 417000000, 62500, 0x8e, 0x3a },
		{ 999999999, 62500, 0x8e, 0x3c },
	},
};

static struct dvb_pll_desc dvb_pll_unknown_1 = {
	.name  = "unknown 1", /* used by dntv live dvb-t */
	.min   = 174000000,
	.max   = 862000000,
	.iffreq= 36166667,
	.count = 9,
	.entries = {
		{  150000000, 166667, 0xb4, 0x01 },
		{  173000000, 166667, 0xbc, 0x01 },
		{  250000000, 166667, 0xb4, 0x02 },
		{  400000000, 166667, 0xbc, 0x02 },
		{  420000000, 166667, 0xf4, 0x02 },
		{  470000000, 166667, 0xfc, 0x02 },
		{  600000000, 166667, 0xbc, 0x08 },
		{  730000000, 166667, 0xf4, 0x08 },
		{  999999999, 166667, 0xfc, 0x08 },
	},
};

/* Infineon TUA6010XS
 * used in Thomson Cable Tuner
 */
static struct dvb_pll_desc dvb_pll_tua6010xs = {
	.name  = "Infineon TUA6010XS",
	.min   =  44250000,
	.max   = 858000000,
	.iffreq= 36125000,
	.count = 3,
	.entries = {
		{  115750000, 62500, 0x8e, 0x03 },
		{  403250000, 62500, 0x8e, 0x06 },
		{  999999999, 62500, 0x8e, 0x85 },
	},
};

/* Panasonic env57h1xd5 (some Philips PLL ?) */
static struct dvb_pll_desc dvb_pll_env57h1xd5 = {
	.name  = "Panasonic ENV57H1XD5",
	.min   =  44250000,
	.max   = 858000000,
	.iffreq= 36125000,
	.count = 4,
	.entries = {
		{  153000000, 166667, 0xc2, 0x41 },
		{  470000000, 166667, 0xc2, 0x42 },
		{  526000000, 166667, 0xc2, 0x84 },
		{  999999999, 166667, 0xc2, 0xa4 },
	},
};

/* Philips TDA6650/TDA6651
 * used in Panasonic ENV77H11D5
 */
static void tda665x_bw(u8 *buf, const struct dvb_frontend_parameters *params)
{
	if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ)
		buf[3] |= 0x08;
}

static struct dvb_pll_desc dvb_pll_tda665x = {
	.name  = "Philips TDA6650/TDA6651",
	.min   =  44250000,
	.max   = 858000000,
	.set   = tda665x_bw,
	.iffreq= 36166667,
	.initdata = (u8[]){ 4, 0x0b, 0xf5, 0x85, 0xab },
	.count = 12,
	.entries = {
		{   93834000, 166667, 0xca, 0x61 /* 011 0 0 0  01 */ },
		{  123834000, 166667, 0xca, 0xa1 /* 101 0 0 0  01 */ },
		{  161000000, 166667, 0xca, 0xa1 /* 101 0 0 0  01 */ },
		{  163834000, 166667, 0xca, 0xc2 /* 110 0 0 0  10 */ },
		{  253834000, 166667, 0xca, 0x62 /* 011 0 0 0  10 */ },
		{  383834000, 166667, 0xca, 0xa2 /* 101 0 0 0  10 */ },
		{  443834000, 166667, 0xca, 0xc2 /* 110 0 0 0  10 */ },
		{  444000000, 166667, 0xca, 0xc4 /* 110 0 0 1  00 */ },
		{  583834000, 166667, 0xca, 0x64 /* 011 0 0 1  00 */ },
		{  793834000, 166667, 0xca, 0xa4 /* 101 0 0 1  00 */ },
		{  444834000, 166667, 0xca, 0xc4 /* 110 0 0 1  00 */ },
		{  861000000, 166667, 0xca, 0xe4 /* 111 0 0 1  00 */ },
	}
};

/* Infineon TUA6034
 * used in LG TDTP E102P
 */
static void tua6034_bw(u8 *buf, const struct dvb_frontend_parameters *params)
{
	if (BANDWIDTH_7_MHZ != params->u.ofdm.bandwidth)
		buf[3] |= 0x08;
}

static struct dvb_pll_desc dvb_pll_tua6034 = {
	.name  = "Infineon TUA6034",
	.min   =  44250000,
	.max   = 858000000,
	.iffreq= 36166667,
	.count = 3,
	.set   = tua6034_bw,
	.entries = {
		{  174500000, 62500, 0xce, 0x01 },
		{  230000000, 62500, 0xce, 0x02 },
		{  999999999, 62500, 0xce, 0x04 },
	},
};

/* Infineon TUA6034
 * used in LG TDVS-H061F, LG TDVS-H062F and LG TDVS-H064F
 */
static struct dvb_pll_desc dvb_pll_lg_tdvs_h06xf = {
	.name  = "LG TDVS-H06xF",
	.min   =  54000000,
	.max   = 863000000,
	.iffreq= 44000000,
	.initdata = tua603x_agc103,
	.count = 3,
	.entries = {
		{  165000000, 62500, 0xce, 0x01 },
		{  450000000, 62500, 0xce, 0x02 },
		{  999999999, 62500, 0xce, 0x04 },
	},
};

/* Philips FMD1216ME
 * used in Medion Hybrid PCMCIA card and USB Box
 */
static void fmd1216me_bw(u8 *buf, const struct dvb_frontend_parameters *params)
{
	if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ &&
	    params->frequency >= 158870000)
		buf[3] |= 0x08;
}

static struct dvb_pll_desc dvb_pll_fmd1216me = {
	.name = "Philips FMD1216ME",
	.min = 50870000,
	.max = 858000000,
	.iffreq= 36125000,
	.set   = fmd1216me_bw,
	.initdata = tua603x_agc112,
	.sleepdata = (u8[]){ 4, 0x9c, 0x60, 0x85, 0x54 },
	.count = 7,
	.entries = {
		{ 143870000, 166667, 0xbc, 0x41 },
		{ 158870000, 166667, 0xf4, 0x41 },
		{ 329870000, 166667, 0xbc, 0x42 },
		{ 441870000, 166667, 0xf4, 0x42 },
		{ 625870000, 166667, 0xbc, 0x44 },
		{ 803870000, 166667, 0xf4, 0x44 },
		{ 999999999, 166667, 0xfc, 0x44 },
	}
};

/* ALPS TDED4
 * used in Nebula-Cards and USB boxes
 */
static void tded4_bw(u8 *buf, const struct dvb_frontend_parameters *params)
{
	if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ)
		buf[3] |= 0x04;
}

static struct dvb_pll_desc dvb_pll_tded4 = {
	.name = "ALPS TDED4",
	.min = 47000000,
	.max = 863000000,
	.iffreq= 36166667,
	.set   = tded4_bw,
	.count = 4,
	.entries = {
		{ 153000000, 166667, 0x85, 0x01 },
		{ 470000000, 166667, 0x85, 0x02 },
		{ 823000000, 166667, 0x85, 0x08 },
		{ 999999999, 166667, 0x85, 0x88 },
	}
};

/* ALPS TDHU2
 * used in AverTVHD MCE A180
 */
static struct dvb_pll_desc dvb_pll_tdhu2 = {
	.name = "ALPS TDHU2",
	.min = 54000000,
	.max = 864000000,
	.iffreq= 44000000,
	.count = 4,
	.entries = {
		{ 162000000, 62500, 0x85, 0x01 },
		{ 426000000, 62500, 0x85, 0x02 },
		{ 782000000, 62500, 0x85, 0x08 },
		{ 999999999, 62500, 0x85, 0x88 },
	}
};

/* Philips TUV1236D
 * used in ATI HDTV Wonder
 */
static void tuv1236d_rf(u8 *buf, const struct dvb_frontend_parameters *params)
{
	switch (params->u.vsb.modulation) {
		case QAM_64:
		case QAM_256:
			buf[3] |= 0x08;
			break;
		case VSB_8:
		default:
			buf[3] &= ~0x08;
	}
}

static struct dvb_pll_desc dvb_pll_tuv1236d = {
	.name  = "Philips TUV1236D",
	.min   =  54000000,
	.max   = 864000000,
	.iffreq= 44000000,
	.set   = tuv1236d_rf,
	.count = 3,
	.entries = {
		{ 157250000, 62500, 0xc6, 0x41 },
		{ 454000000, 62500, 0xc6, 0x42 },
		{ 999999999, 62500, 0xc6, 0x44 },
	},
};

/* Samsung TBMV30111IN / TBMV30712IN1
 * used in Air2PC ATSC - 2nd generation (nxt2002)
 */
static struct dvb_pll_desc dvb_pll_samsung_tbmv = {
	.name = "Samsung TBMV30111IN / TBMV30712IN1",
	.min = 54000000,
	.max = 860000000,
	.iffreq= 44000000,
	.count = 6,
	.entries = {
		{ 172000000, 166667, 0xb4, 0x01 },
		{ 214000000, 166667, 0xb4, 0x02 },
		{ 467000000, 166667, 0xbc, 0x02 },
		{ 721000000, 166667, 0xbc, 0x08 },
		{ 841000000, 166667, 0xf4, 0x08 },
		{ 999999999, 166667, 0xfc, 0x02 },
	}
};

/*
 * Philips SD1878 Tuner.
 */
static struct dvb_pll_desc dvb_pll_philips_sd1878_tda8261 = {
	.name  = "Philips SD1878",
	.min   =  950000,
	.max   = 2150000,
	.iffreq= 249, /* zero-IF, offset 249 is to round up */
	.count = 4,
	.entries = {
		{ 1250000, 500, 0xc4, 0x00},
		{ 1550000, 500, 0xc4, 0x40},
		{ 2050000, 500, 0xc4, 0x80},
		{ 2150000, 500, 0xc4, 0xc0},
	},
};

/*
 * Philips TD1316 Tuner.
 */
static void td1316_bw(u8 *buf, const struct dvb_frontend_parameters *params)
{
	u8 band;

	/* determine band */
	if (params->frequency < 161000000)
		band = 1;
	else if (params->frequency < 444000000)
		band = 2;
	else
		band = 4;

	buf[3] |= band;

	/* setup PLL filter */
	if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ)
		buf[3] |= 1 << 3;
}

static struct dvb_pll_desc dvb_pll_philips_td1316 = {
	.name  = "Philips TD1316",
	.min   =  87000000,
	.max   = 895000000,
	.iffreq= 36166667,
	.set   = td1316_bw,
	.count = 9,
	.entries = {
		{  93834000, 166667, 0xca, 0x60},
		{ 123834000, 166667, 0xca, 0xa0},
		{ 163834000, 166667, 0xca, 0xc0},
		{ 253834000, 166667, 0xca, 0x60},
		{ 383834000, 166667, 0xca, 0xa0},
		{ 443834000, 166667, 0xca, 0xc0},
		{ 583834000, 166667, 0xca, 0x60},
		{ 793834000, 166667, 0xca, 0xa0},
		{ 858834000, 166667, 0xca, 0xe0},
	},
};

/* FE6600 used on DViCO Hybrid */
static struct dvb_pll_desc dvb_pll_thomson_fe6600 = {
	.name = "Thomson FE6600",
	.min =  44250000,
	.max = 858000000,
	.iffreq= 36125000,
	.count = 4,
	.entries = {
		{ 250000000, 166667, 0xb4, 0x12 },
		{ 455000000, 166667, 0xfe, 0x11 },
		{ 775500000, 166667, 0xbc, 0x18 },
		{ 999999999, 166667, 0xf4, 0x18 },
	}
};

static void opera1_bw(u8 *buf, const struct dvb_frontend_parameters *params)
{
	if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ)
		buf[2] |= 0x08;
}

static struct dvb_pll_desc dvb_pll_opera1 = {
	.name  = "Opera Tuner",
	.min   =  900000,
	.max   = 2250000,
	.iffreq= 0,
	.set   = opera1_bw,
	.count = 8,
	.entries = {
		{ 1064000, 500, 0xe5, 0xc6 },
		{ 1169000, 500, 0xe5, 0xe6 },
		{ 1299000, 500, 0xe5, 0x24 },
		{ 1444000, 500, 0xe5, 0x44 },
		{ 1606000, 500, 0xe5, 0x64 },
		{ 1777000, 500, 0xe5, 0x84 },
		{ 1941000, 500, 0xe5, 0xa4 },
		{ 2250000, 500, 0xe5, 0xc4 },
	}
};

/* Philips FCV1236D
 */
static struct dvb_pll_desc dvb_pll_fcv1236d = {
/* Bit_0: RF Input select
 * Bit_1: 0=digital, 1=analog
 */
	.name  = "Philips FCV1236D",
	.min   =  53000000,
	.max   = 803000000,
	.iffreq= 44000000,
	.count = 3,
	.entries = {
		{ 159000000, 62500, 0x8e, 0xa0 },
		{ 453000000, 62500, 0x8e, 0x90 },
		{ 999999999, 62500, 0x8e, 0x30 },
	},
};

/* ----------------------------------------------------------- */

static struct dvb_pll_desc *pll_list[] = {
	[DVB_PLL_UNDEFINED]              = NULL,
	[DVB_PLL_THOMSON_DTT7579]        = &dvb_pll_thomson_dtt7579,
	[DVB_PLL_THOMSON_DTT759X]        = &dvb_pll_thomson_dtt759x,
	[DVB_PLL_THOMSON_DTT7610]        = &dvb_pll_thomson_dtt7610,
	[DVB_PLL_LG_Z201]                = &dvb_pll_lg_z201,
	[DVB_PLL_MICROTUNE_4042]         = &dvb_pll_microtune_4042,
	[DVB_PLL_THOMSON_DTT761X]        = &dvb_pll_thomson_dtt761x,
	[DVB_PLL_UNKNOWN_1]              = &dvb_pll_unknown_1,
	[DVB_PLL_TUA6010XS]              = &dvb_pll_tua6010xs,
	[DVB_PLL_ENV57H1XD5]             = &dvb_pll_env57h1xd5,
	[DVB_PLL_TUA6034]                = &dvb_pll_tua6034,
	[DVB_PLL_LG_TDVS_H06XF]          = &dvb_pll_lg_tdvs_h06xf,
	[DVB_PLL_TDA665X]                = &dvb_pll_tda665x,
	[DVB_PLL_FMD1216ME]              = &dvb_pll_fmd1216me,
	[DVB_PLL_TDED4]                  = &dvb_pll_tded4,
	[DVB_PLL_TUV1236D]               = &dvb_pll_tuv1236d,
	[DVB_PLL_TDHU2]                  = &dvb_pll_tdhu2,
	[DVB_PLL_SAMSUNG_TBMV]           = &dvb_pll_samsung_tbmv,
	[DVB_PLL_PHILIPS_SD1878_TDA8261] = &dvb_pll_philips_sd1878_tda8261,
	[DVB_PLL_PHILIPS_TD1316]         = &dvb_pll_philips_td1316,
	[DVB_PLL_THOMSON_FE6600]         = &dvb_pll_thomson_fe6600,
	[DVB_PLL_OPERA1]                 = &dvb_pll_opera1,
	[DVB_PLL_FCV1236D]               = &dvb_pll_fcv1236d,
};

/* ----------------------------------------------------------- */

struct dvb_pll_priv {
	/* i2c details */
	int pll_i2c_address;
	struct i2c_adapter *i2c;

	/* the PLL descriptor */
	struct dvb_pll_desc *pll_desc;

	/* cached frequency/bandwidth */
	u32 frequency;
	u32 bandwidth;
};

/* ----------------------------------------------------------- */
/* code                                                        */

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable verbose debug messages");

static int dvb_pll_configure(struct dvb_pll_desc *desc, u8 *buf,
			     const struct dvb_frontend_parameters *params)
{
	u32 div;
	int i;

	if (params->frequency != 0 && (params->frequency < desc->min ||
				       params->frequency > desc->max))
		return -EINVAL;

	for (i = 0; i < desc->count; i++) {
		if (params->frequency > desc->entries[i].limit)
			continue;
		break;
	}

	if (debug)
		printk("pll: %s: freq=%d | i=%d/%d\n", desc->name,
		       params->frequency, i, desc->count);
	if (i == desc->count)
		return -EINVAL;

	div = (params->frequency + desc->iffreq +
	       desc->entries[i].stepsize/2) / desc->entries[i].stepsize;
	buf[0] = div >> 8;
	buf[1] = div & 0xff;
	buf[2] = desc->entries[i].config;
	buf[3] = desc->entries[i].cb;

	if (desc->set)
		desc->set(buf, params);

	if (debug)
		printk("pll: %s: div=%d | buf=0x%02x,0x%02x,0x%02x,0x%02x\n",
		       desc->name, div, buf[0], buf[1], buf[2], buf[3]);

	// calculate the frequency we set it to
	return (div * desc->entries[i].stepsize) - desc->iffreq;
}

static int dvb_pll_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static int dvb_pll_sleep(struct dvb_frontend *fe)
{
	struct dvb_pll_priv *priv = fe->tuner_priv;

	if (priv->i2c == NULL)
		return -EINVAL;

	if (priv->pll_desc->sleepdata) {
		struct i2c_msg msg = { .flags = 0,
			.addr = priv->pll_i2c_address,
			.buf = priv->pll_desc->sleepdata + 1,
			.len = priv->pll_desc->sleepdata[0] };

		int result;

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);
		if ((result = i2c_transfer(priv->i2c, &msg, 1)) != 1) {
			return result;
		}
		return 0;
	}
	/* Shouldn't be called when initdata is NULL, maybe BUG()? */
	return -EINVAL;
}

static int dvb_pll_set_params(struct dvb_frontend *fe,
			      struct dvb_frontend_parameters *params)
{
	struct dvb_pll_priv *priv = fe->tuner_priv;
	u8 buf[4];
	struct i2c_msg msg =
		{ .addr = priv->pll_i2c_address, .flags = 0,
		  .buf = buf, .len = sizeof(buf) };
	int result;
	u32 frequency = 0;

	if (priv->i2c == NULL)
		return -EINVAL;

	if ((result = dvb_pll_configure(priv->pll_desc, buf, params)) < 0)
		return result;
	else
		frequency = result;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if ((result = i2c_transfer(priv->i2c, &msg, 1)) != 1) {
		return result;
	}

	priv->frequency = frequency;
	priv->bandwidth = (fe->ops.info.type == FE_OFDM) ? params->u.ofdm.bandwidth : 0;

	return 0;
}

static int dvb_pll_calc_regs(struct dvb_frontend *fe,
			     struct dvb_frontend_parameters *params,
			     u8 *buf, int buf_len)
{
	struct dvb_pll_priv *priv = fe->tuner_priv;
	int result;
	u32 frequency = 0;

	if (buf_len < 5)
		return -EINVAL;

	if ((result = dvb_pll_configure(priv->pll_desc, buf+1, params)) < 0)
		return result;
	else
		frequency = result;

	buf[0] = priv->pll_i2c_address;

	priv->frequency = frequency;
	priv->bandwidth = (fe->ops.info.type == FE_OFDM) ? params->u.ofdm.bandwidth : 0;

	return 5;
}

static int dvb_pll_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct dvb_pll_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static int dvb_pll_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct dvb_pll_priv *priv = fe->tuner_priv;
	*bandwidth = priv->bandwidth;
	return 0;
}

static int dvb_pll_init(struct dvb_frontend *fe)
{
	struct dvb_pll_priv *priv = fe->tuner_priv;

	if (priv->i2c == NULL)
		return -EINVAL;

	if (priv->pll_desc->initdata) {
		struct i2c_msg msg = { .flags = 0,
			.addr = priv->pll_i2c_address,
			.buf = priv->pll_desc->initdata + 1,
			.len = priv->pll_desc->initdata[0] };

		int result;
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);
		if ((result = i2c_transfer(priv->i2c, &msg, 1)) != 1) {
			return result;
		}
		return 0;
	}
	/* Shouldn't be called when initdata is NULL, maybe BUG()? */
	return -EINVAL;
}

static struct dvb_tuner_ops dvb_pll_tuner_ops = {
	.release = dvb_pll_release,
	.sleep = dvb_pll_sleep,
	.init = dvb_pll_init,
	.set_params = dvb_pll_set_params,
	.calc_regs = dvb_pll_calc_regs,
	.get_frequency = dvb_pll_get_frequency,
	.get_bandwidth = dvb_pll_get_bandwidth,
};

struct dvb_frontend *dvb_pll_attach(struct dvb_frontend *fe, int pll_addr,
				    struct i2c_adapter *i2c,
				    unsigned int pll_desc_id)
{
	u8 b1 [] = { 0 };
	struct i2c_msg msg = { .addr = pll_addr, .flags = I2C_M_RD,
			       .buf = b1, .len = 1 };
	struct dvb_pll_priv *priv = NULL;
	int ret;
	struct dvb_pll_desc *desc;

	BUG_ON(pll_desc_id < 1 || pll_desc_id >= ARRAY_SIZE(pll_list));

	desc = pll_list[pll_desc_id];

	if (i2c != NULL) {
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);

		ret = i2c_transfer (i2c, &msg, 1);
		if (ret != 1)
			return NULL;
		if (fe->ops.i2c_gate_ctrl)
			     fe->ops.i2c_gate_ctrl(fe, 0);
	}

	priv = kzalloc(sizeof(struct dvb_pll_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->pll_i2c_address = pll_addr;
	priv->i2c = i2c;
	priv->pll_desc = desc;

	memcpy(&fe->ops.tuner_ops, &dvb_pll_tuner_ops,
	       sizeof(struct dvb_tuner_ops));

	strncpy(fe->ops.tuner_ops.info.name, desc->name,
		sizeof(fe->ops.tuner_ops.info.name));
	fe->ops.tuner_ops.info.frequency_min = desc->min;
	fe->ops.tuner_ops.info.frequency_min = desc->max;
	if (!desc->initdata)
		fe->ops.tuner_ops.init = NULL;
	if (!desc->sleepdata)
		fe->ops.tuner_ops.sleep = NULL;

	fe->tuner_priv = priv;
	return fe;
}
EXPORT_SYMBOL(dvb_pll_attach);

MODULE_DESCRIPTION("dvb pll library");
MODULE_AUTHOR("Gerd Knorr");
MODULE_LICENSE("GPL");
