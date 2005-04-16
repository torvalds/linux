/*
 * b2c2-common.c - common methods for the B2C2/Technisat SkyStar2 PCI DVB card and
 *                 for the B2C2/Technisat Sky/Cable/AirStar USB devices
 *                 based on the FlexCopII/FlexCopIII by B2C2, Inc.
 *
 * Copyright (C) 2003  Vadim Catana, skystar@moldova.cc
 *
 * FIX: DISEQC Tone Burst in flexcop_diseqc_ioctl()
 * FIX: FULL soft DiSEqC for skystar2 (FlexCopII rev 130) VP310 equipped
 *     Vincenzo Di Massa, hawk.it at tiscalinet.it
 *
 * Converted to Linux coding style
 * Misc reorganization, polishing, restyling
 *     Roberto Ragusa, r.ragusa at libero.it
 *
 * Added hardware filtering support,
 *     Niklas Peinecke, peinecke at gdv.uni-hannover.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
#include "stv0299.h"
#include "mt352.h"
#include "mt312.h"

static int samsung_tbmu24112_set_symbol_rate(struct dvb_frontend* fe, u32 srate, u32 ratio)
{
	u8 aclk = 0;
	u8 bclk = 0;

	if (srate < 1500000) { aclk = 0xb7; bclk = 0x47; }
	else if (srate < 3000000) { aclk = 0xb7; bclk = 0x4b; }
	else if (srate < 7000000) { aclk = 0xb7; bclk = 0x4f; }
	else if (srate < 14000000) { aclk = 0xb7; bclk = 0x53; }
	else if (srate < 30000000) { aclk = 0xb6; bclk = 0x53; }
	else if (srate < 45000000) { aclk = 0xb4; bclk = 0x51; }

	stv0299_writereg (fe, 0x13, aclk);
	stv0299_writereg (fe, 0x14, bclk);
	stv0299_writereg (fe, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg (fe, 0x20, (ratio >>  8) & 0xff);
	stv0299_writereg (fe, 0x21, (ratio      ) & 0xf0);

	return 0;
}

static int samsung_tbmu24112_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	u8 buf[4];
	u32 div;
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = buf, .len = sizeof(buf) };
//	struct adapter* adapter = (struct adapter*) fe->dvb->priv;

	div = params->frequency / 125;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x84;  // 0xC4
	buf[3] = 0x08;

	if (params->frequency < 1500000) buf[3] |= 0x10;

//	if (i2c_transfer (&adapter->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

static u8 samsung_tbmu24112_inittab[] = {
	     0x01, 0x15,
	     0x02, 0x30,
	     0x03, 0x00,
	     0x04, 0x7D,
	     0x05, 0x35,
	     0x06, 0x02,
	     0x07, 0x00,
	     0x08, 0xC3,
	     0x0C, 0x00,
	     0x0D, 0x81,
	     0x0E, 0x23,
	     0x0F, 0x12,
	     0x10, 0x7E,
	     0x11, 0x84,
	     0x12, 0xB9,
	     0x13, 0x88,
	     0x14, 0x89,
	     0x15, 0xC9,
	     0x16, 0x00,
	     0x17, 0x5C,
	     0x18, 0x00,
	     0x19, 0x00,
	     0x1A, 0x00,
	     0x1C, 0x00,
	     0x1D, 0x00,
	     0x1E, 0x00,
	     0x1F, 0x3A,
	     0x20, 0x2E,
	     0x21, 0x80,
	     0x22, 0xFF,
	     0x23, 0xC1,
	     0x28, 0x00,
	     0x29, 0x1E,
	     0x2A, 0x14,
	     0x2B, 0x0F,
	     0x2C, 0x09,
	     0x2D, 0x05,
	     0x31, 0x1F,
	     0x32, 0x19,
	     0x33, 0xFE,
	     0x34, 0x93,
	     0xff, 0xff,
};

static struct stv0299_config samsung_tbmu24112_config = {
	.demod_address = 0x68,
	.inittab = samsung_tbmu24112_inittab,
	.mclk = 88000000UL,
	.invert = 0,
	.enhanced_tuning = 0,
	.skip_reinit = 0,
	.lock_output = STV0229_LOCKOUTPUT_LK,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 100,
	.set_symbol_rate = samsung_tbmu24112_set_symbol_rate,
   	.pll_set = samsung_tbmu24112_pll_set,
};





static int samsung_tdtc9251dh0_demod_init(struct dvb_frontend* fe)
{
	static u8 mt352_clock_config [] = { 0x89, 0x10, 0x2d };
	static u8 mt352_reset [] = { 0x50, 0x80 };
	static u8 mt352_adc_ctl_1_cfg [] = { 0x8E, 0x40 };
	static u8 mt352_agc_cfg [] = { 0x67, 0x28, 0xa1 };
	static u8 mt352_capt_range_cfg[] = { 0x75, 0x32 };

	mt352_write(fe, mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(fe, mt352_reset, sizeof(mt352_reset));
	mt352_write(fe, mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));

	mt352_write(fe, mt352_agc_cfg, sizeof(mt352_agc_cfg));
	mt352_write(fe, mt352_capt_range_cfg, sizeof(mt352_capt_range_cfg));

	return 0;
}

static int samsung_tdtc9251dh0_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params, u8* pllbuf)
{
	u32 div;
	unsigned char bs = 0;

	#define IF_FREQUENCYx6 217    /* 6 * 36.16666666667MHz */
	div = (((params->frequency + 83333) * 3) / 500000) + IF_FREQUENCYx6;

	if (params->frequency >= 48000000 && params->frequency <= 154000000) bs = 0x09;
	if (params->frequency >= 161000000 && params->frequency <= 439000000) bs = 0x0a;
	if (params->frequency >= 447000000 && params->frequency <= 863000000) bs = 0x08;

	pllbuf[0] = 0xc2; // Note: non-linux standard PLL i2c address
	pllbuf[1] = div >> 8;
   	pllbuf[2] = div & 0xff;
   	pllbuf[3] = 0xcc;
   	pllbuf[4] = bs;

	return 0;
}

static struct mt352_config samsung_tdtc9251dh0_config = {

	.demod_address = 0x0f,
	.demod_init = samsung_tdtc9251dh0_demod_init,
   	.pll_set = samsung_tdtc9251dh0_pll_set,
};

static int skystar23_samsung_tbdu18132_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	u8 buf[4];
	u32 div;
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = buf, .len = sizeof(buf) };
//	struct adapter* adapter = (struct adapter*) fe->dvb->priv;

	div = (params->frequency + (125/2)) / 125;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = (div >> 0) & 0xff;
	buf[2] = 0x84 | ((div >> 10) & 0x60);
	buf[3] = 0x80;

	if (params->frequency < 1550000)
		buf[3] |= 0x02;

	//if (i2c_transfer (&adapter->i2c_adap, &msg, 1) != 1) return -EIO;
	return 0;
}

static struct mt312_config skystar23_samsung_tbdu18132_config = {

	.demod_address = 0x0e,
   	.pll_set = skystar23_samsung_tbdu18132_pll_set,
};
