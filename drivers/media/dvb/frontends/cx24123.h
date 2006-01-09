/*
    Conexant cx24123/cx24109 - DVB QPSK Satellite demod/tuner driver

    Copyright (C) 2005 Steven Toth <stoth@hauppauge.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef CX24123_H
#define CX24123_H

#include <linux/dvb/frontend.h>

struct cx24123_config
{
	/* the demodulator's i2c address */
	u8 demod_address;

	/* PLL maintenance */
	int (*pll_init)(struct dvb_frontend* fe);
	int (*pll_set)(struct dvb_frontend* fe, struct dvb_frontend_parameters* params);

	/* Need to set device param for start_dma */
	int (*set_ts_params)(struct dvb_frontend* fe, int is_punctured);
};

/* Various tuner defaults need to be established for a given symbol rate Sps */
struct
{
	u32 symbolrate_low;
	u32 symbolrate_high;
	u32 VCAslope;
	u32 VCAoffset;
	u32 VGA1offset;
	u32 VGA2offset;
	u32 VCAprogdata;
	u32 VGAprogdata;
} cx24123_AGC_vals[] =
{
	{
		.symbolrate_low		= 1000000,
		.symbolrate_high	= 4999999,
		.VCAslope		= 0x07,
		.VCAoffset		= 0x0f,
		.VGA1offset		= 0x1f8,
		.VGA2offset		= 0x1f8,
		.VGAprogdata		= (2 << 18) | (0x1f8 << 9) | 0x1f8,
		.VCAprogdata		= (4 << 18) | (0x07 << 9) | 0x07,
	},
	{
		.symbolrate_low		=  5000000,
		.symbolrate_high	= 14999999,
		.VCAslope		= 0x1f,
		.VCAoffset		= 0x1f,
		.VGA1offset		= 0x1e0,
		.VGA2offset		= 0x180,
		.VGAprogdata		= (2 << 18) | (0x180 << 9) | 0x1e0,
		.VCAprogdata		= (4 << 18) | (0x07 << 9) | 0x1f,
	},
	{
		.symbolrate_low		= 15000000,
		.symbolrate_high	= 45000000,
		.VCAslope		= 0x3f,
		.VCAoffset		= 0x3f,
		.VGA1offset		= 0x180,
		.VGA2offset		= 0x100,
		.VGAprogdata		= (2 << 18) | (0x100 << 9) | 0x180,
		.VCAprogdata		= (4 << 18) | (0x07 << 9) | 0x3f,
	},
};

/*
 * Various tuner defaults need to be established for a given frequency kHz.
 * fixme: The bounds on the bands do not match the doc in real life.
 * fixme: Some of them have been moved, other might need adjustment.
 */
struct
{
	u32 freq_low;
	u32 freq_high;
	u32 bandselect;
	u32 VCOdivider;
	u32 VCOnumber;
	u32 progdata;
} cx24123_bandselect_vals[] =
{
	{
		.freq_low	= 950000,
		.freq_high	= 1018999,
		.bandselect	= 0x40,
		.VCOdivider	= 4,
		.VCOnumber	= 7,
		.progdata	= (0 << 18) | (0 << 9) | 0x40,
	},
	{
		.freq_low	= 1019000,
		.freq_high	= 1074999,
		.bandselect	= 0x80,
		.VCOdivider	= 4,
		.VCOnumber	= 8,
		.progdata	= (0 << 18) | (0 << 9) | 0x80,
	},
	{
		.freq_low	= 1075000,
		.freq_high	= 1227999,
		.bandselect	= 0x01,
		.VCOdivider	= 2,
		.VCOnumber	= 1,
		.progdata	= (0 << 18) | (1 << 9) | 0x01,
	},
	{
		.freq_low	= 1228000,
		.freq_high	= 1349999,
		.bandselect	= 0x02,
		.VCOdivider	= 2,
		.VCOnumber	= 2,
		.progdata	= (0 << 18) | (1 << 9) | 0x02,
	},
	{
		.freq_low	= 1350000,
		.freq_high	= 1481999,
		.bandselect	= 0x04,
		.VCOdivider	= 2,
		.VCOnumber	= 3,
		.progdata	= (0 << 18) | (1 << 9) | 0x04,
	},
	{
		.freq_low	= 1482000,
		.freq_high	= 1595999,
		.bandselect	= 0x08,
		.VCOdivider	= 2,
		.VCOnumber	= 4,
		.progdata	= (0 << 18) | (1 << 9) | 0x08,
	},
	{
		.freq_low	= 1596000,
		.freq_high	= 1717999,
		.bandselect	= 0x10,
		.VCOdivider	= 2,
		.VCOnumber	= 5,
		.progdata	= (0 << 18) | (1 << 9) | 0x10,
	},
	{
		.freq_low	= 1718000,
		.freq_high	= 1855999,
		.bandselect	= 0x20,
		.VCOdivider	= 2,
		.VCOnumber	= 6,
		.progdata	= (0 << 18) | (1 << 9) | 0x20,
	},
	{
		.freq_low	= 1856000,
		.freq_high	= 2035999,
		.bandselect	= 0x40,
		.VCOdivider	= 2,
		.VCOnumber	= 7,
		.progdata	= (0 << 18) | (1 << 9) | 0x40,
	},
	{
		.freq_low	= 2036000,
		.freq_high	= 2149999,
		.bandselect	= 0x80,
		.VCOdivider	= 2,
		.VCOnumber	= 8,
		.progdata	= (0 << 18) | (1 << 9) | 0x80,
	},
};

extern struct dvb_frontend* cx24123_attach(const struct cx24123_config* config,
					   struct i2c_adapter* i2c);

#endif /* CX24123_H */

