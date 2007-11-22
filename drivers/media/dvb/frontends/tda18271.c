/*
    tda18271.c - driver for the Philips / NXP TDA18271 silicon tuner

    Copyright (C) 2007 Michael Krufky (mkrufky@linuxtv.org)

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

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include "tuner-driver.h"

#include "tda18271.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

#define dprintk(level, fmt, arg...) do {\
	if (debug >= level) \
		printk(KERN_DEBUG "%s: " fmt, __FUNCTION__, ##arg); } while (0)

#define R_ID     0x00	/* ID byte                */
#define R_TM     0x01	/* Thermo byte            */
#define R_PL     0x02	/* Power level byte       */
#define R_EP1    0x03	/* Easy Prog byte 1       */
#define R_EP2    0x04	/* Easy Prog byte 2       */
#define R_EP3    0x05	/* Easy Prog byte 3       */
#define R_EP4    0x06	/* Easy Prog byte 4       */
#define R_EP5    0x07	/* Easy Prog byte 5       */
#define R_CPD    0x08	/* Cal Post-Divider byte  */
#define R_CD1    0x09	/* Cal Divider byte 1     */
#define R_CD2    0x0a	/* Cal Divider byte 2     */
#define R_CD3    0x0b	/* Cal Divider byte 3     */
#define R_MPD    0x0c	/* Main Post-Divider byte */
#define R_MD1    0x0d	/* Main Divider byte 1    */
#define R_MD2    0x0e	/* Main Divider byte 2    */
#define R_MD3    0x0f	/* Main Divider byte 3    */
#define R_EB1    0x10	/* Extended byte 1        */
#define R_EB2    0x11	/* Extended byte 2        */
#define R_EB3    0x12	/* Extended byte 3        */
#define R_EB4    0x13	/* Extended byte 4        */
#define R_EB5    0x14	/* Extended byte 5        */
#define R_EB6    0x15	/* Extended byte 6        */
#define R_EB7    0x16	/* Extended byte 7        */
#define R_EB8    0x17	/* Extended byte 8        */
#define R_EB9    0x18	/* Extended byte 9        */
#define R_EB10   0x19	/* Extended byte 10       */
#define R_EB11   0x1a	/* Extended byte 11       */
#define R_EB12   0x1b	/* Extended byte 12       */
#define R_EB13   0x1c	/* Extended byte 13       */
#define R_EB14   0x1d	/* Extended byte 14       */
#define R_EB15   0x1e	/* Extended byte 15       */
#define R_EB16   0x1f	/* Extended byte 16       */
#define R_EB17   0x20	/* Extended byte 17       */
#define R_EB18   0x21	/* Extended byte 18       */
#define R_EB19   0x22	/* Extended byte 19       */
#define R_EB20   0x23	/* Extended byte 20       */
#define R_EB21   0x24	/* Extended byte 21       */
#define R_EB22   0x25	/* Extended byte 22       */
#define R_EB23   0x26	/* Extended byte 23       */

struct tda18271_pll_map {
	u32 lomax;
	u8 pd; /* post div */
	u8 d;  /*      div */
};

static struct tda18271_pll_map tda18271_main_pll[] = {
	{ .lomax =  32000, .pd = 0x5f, .d = 0xf0 },
	{ .lomax =  35000, .pd = 0x5e, .d = 0xe0 },
	{ .lomax =  37000, .pd = 0x5d, .d = 0xd0 },
	{ .lomax =  41000, .pd = 0x5c, .d = 0xc0 },
	{ .lomax =  44000, .pd = 0x5b, .d = 0xb0 },
	{ .lomax =  49000, .pd = 0x5a, .d = 0xa0 },
	{ .lomax =  54000, .pd = 0x59, .d = 0x90 },
	{ .lomax =  61000, .pd = 0x58, .d = 0x80 },
	{ .lomax =  65000, .pd = 0x4f, .d = 0x78 },
	{ .lomax =  70000, .pd = 0x4e, .d = 0x70 },
	{ .lomax =  75000, .pd = 0x4d, .d = 0x68 },
	{ .lomax =  82000, .pd = 0x4c, .d = 0x60 },
	{ .lomax =  89000, .pd = 0x4b, .d = 0x58 },
	{ .lomax =  98000, .pd = 0x4a, .d = 0x50 },
	{ .lomax = 109000, .pd = 0x49, .d = 0x48 },
	{ .lomax = 123000, .pd = 0x48, .d = 0x40 },
	{ .lomax = 131000, .pd = 0x3f, .d = 0x3c },
	{ .lomax = 141000, .pd = 0x3e, .d = 0x38 },
	{ .lomax = 151000, .pd = 0x3d, .d = 0x34 },
	{ .lomax = 164000, .pd = 0x3c, .d = 0x30 },
	{ .lomax = 179000, .pd = 0x3b, .d = 0x2c },
	{ .lomax = 197000, .pd = 0x3a, .d = 0x28 },
	{ .lomax = 219000, .pd = 0x39, .d = 0x24 },
	{ .lomax = 246000, .pd = 0x38, .d = 0x20 },
	{ .lomax = 263000, .pd = 0x2f, .d = 0x1e },
	{ .lomax = 282000, .pd = 0x2e, .d = 0x1c },
	{ .lomax = 303000, .pd = 0x2d, .d = 0x1a },
	{ .lomax = 329000, .pd = 0x2c, .d = 0x18 },
	{ .lomax = 359000, .pd = 0x2b, .d = 0x16 },
	{ .lomax = 395000, .pd = 0x2a, .d = 0x14 },
	{ .lomax = 438000, .pd = 0x29, .d = 0x12 },
	{ .lomax = 493000, .pd = 0x28, .d = 0x10 },
	{ .lomax = 526000, .pd = 0x1f, .d = 0x0f },
	{ .lomax = 564000, .pd = 0x1e, .d = 0x0e },
	{ .lomax = 607000, .pd = 0x1d, .d = 0x0d },
	{ .lomax = 658000, .pd = 0x1c, .d = 0x0c },
	{ .lomax = 718000, .pd = 0x1b, .d = 0x0b },
	{ .lomax = 790000, .pd = 0x1a, .d = 0x0a },
	{ .lomax = 877000, .pd = 0x19, .d = 0x09 },
	{ .lomax = 987000, .pd = 0x18, .d = 0x08 },
	{ .lomax =      0, .pd = 0x00, .d = 0x00 }, /* end */
};

static struct tda18271_pll_map tda18271_cal_pll[] = {
	{ .lomax =   33000, .pd = 0xdd, .d = 0xd0 },
	{ .lomax =   36000, .pd = 0xdc, .d = 0xc0 },
	{ .lomax =   40000, .pd = 0xdb, .d = 0xb0 },
	{ .lomax =   44000, .pd = 0xda, .d = 0xa0 },
	{ .lomax =   49000, .pd = 0xd9, .d = 0x90 },
	{ .lomax =   55000, .pd = 0xd8, .d = 0x80 },
	{ .lomax =   63000, .pd = 0xd3, .d = 0x70 },
	{ .lomax =   67000, .pd = 0xcd, .d = 0x68 },
	{ .lomax =   73000, .pd = 0xcc, .d = 0x60 },
	{ .lomax =   80000, .pd = 0xcb, .d = 0x58 },
	{ .lomax =   88000, .pd = 0xca, .d = 0x50 },
	{ .lomax =   98000, .pd = 0xc9, .d = 0x48 },
	{ .lomax =  110000, .pd = 0xc8, .d = 0x40 },
	{ .lomax =  126000, .pd = 0xc3, .d = 0x38 },
	{ .lomax =  135000, .pd = 0xbd, .d = 0x34 },
	{ .lomax =  147000, .pd = 0xbc, .d = 0x30 },
	{ .lomax =  160000, .pd = 0xbb, .d = 0x2c },
	{ .lomax =  176000, .pd = 0xba, .d = 0x28 },
	{ .lomax =  196000, .pd = 0xb9, .d = 0x24 },
	{ .lomax =  220000, .pd = 0xb8, .d = 0x20 },
	{ .lomax =  252000, .pd = 0xb3, .d = 0x1c },
	{ .lomax =  271000, .pd = 0xad, .d = 0x1a },
	{ .lomax =  294000, .pd = 0xac, .d = 0x18 },
	{ .lomax =  321000, .pd = 0xab, .d = 0x16 },
	{ .lomax =  353000, .pd = 0xaa, .d = 0x14 },
	{ .lomax =  392000, .pd = 0xa9, .d = 0x12 },
	{ .lomax =  441000, .pd = 0xa8, .d = 0x10 },
	{ .lomax =  505000, .pd = 0xa3, .d = 0x0e },
	{ .lomax =  543000, .pd = 0x9d, .d = 0x0d },
	{ .lomax =  589000, .pd = 0x9c, .d = 0x0c },
	{ .lomax =  642000, .pd = 0x9b, .d = 0x0b },
	{ .lomax =  707000, .pd = 0x9a, .d = 0x0a },
	{ .lomax =  785000, .pd = 0x99, .d = 0x09 },
	{ .lomax =  883000, .pd = 0x98, .d = 0x08 },
	{ .lomax = 1010000, .pd = 0x93, .d = 0x07 },
	{ .lomax =       0, .pd = 0x00, .d = 0x00 }, /* end */
};

struct tda18271_map {
	u32 rfmax;
	u8  val;
};

static struct tda18271_map tda18271_bp_filter[] = {
	{ .rfmax =  62000, .val = 0x00 },
	{ .rfmax =  84000, .val = 0x01 },
	{ .rfmax = 100000, .val = 0x02 },
	{ .rfmax = 140000, .val = 0x03 },
	{ .rfmax = 170000, .val = 0x04 },
	{ .rfmax = 180000, .val = 0x05 },
	{ .rfmax = 865000, .val = 0x06 },
	{ .rfmax =      0, .val = 0x00 }, /* end */
};

static struct tda18271_map tda18271_km[] = {
	{ .rfmax =  61100, .val = 0x74 },
	{ .rfmax = 350000, .val = 0x40 },
	{ .rfmax = 720000, .val = 0x30 },
	{ .rfmax = 865000, .val = 0x40 },
	{ .rfmax =      0, .val = 0x00 }, /* end */
};

static struct tda18271_map tda18271_rf_band[] = {
	{ .rfmax =  47900, .val = 0x00 },
	{ .rfmax =  61100, .val = 0x01 },
/*	{ .rfmax = 152600, .val = 0x02 }, */
	{ .rfmax = 121200, .val = 0x02 },
	{ .rfmax = 164700, .val = 0x03 },
	{ .rfmax = 203500, .val = 0x04 },
	{ .rfmax = 457800, .val = 0x05 },
	{ .rfmax = 865000, .val = 0x06 },
	{ .rfmax =      0, .val = 0x00 }, /* end */
};

static struct tda18271_map tda18271_gain_taper[] = {
	{ .rfmax =  45400, .val = 0x1f },
	{ .rfmax =  45800, .val = 0x1e },
	{ .rfmax =  46200, .val = 0x1d },
	{ .rfmax =  46700, .val = 0x1c },
	{ .rfmax =  47100, .val = 0x1b },
	{ .rfmax =  47500, .val = 0x1a },
	{ .rfmax =  47900, .val = 0x19 },
	{ .rfmax =  49600, .val = 0x17 },
	{ .rfmax =  51200, .val = 0x16 },
	{ .rfmax =  52900, .val = 0x15 },
	{ .rfmax =  54500, .val = 0x14 },
	{ .rfmax =  56200, .val = 0x13 },
	{ .rfmax =  57800, .val = 0x12 },
	{ .rfmax =  59500, .val = 0x11 },
	{ .rfmax =  61100, .val = 0x10 },
	{ .rfmax =  67600, .val = 0x0d },
	{ .rfmax =  74200, .val = 0x0c },
	{ .rfmax =  80700, .val = 0x0b },
	{ .rfmax =  87200, .val = 0x0a },
	{ .rfmax =  93800, .val = 0x09 },
	{ .rfmax = 100300, .val = 0x08 },
	{ .rfmax = 106900, .val = 0x07 },
	{ .rfmax = 113400, .val = 0x06 },
	{ .rfmax = 119900, .val = 0x05 },
	{ .rfmax = 126500, .val = 0x04 },
	{ .rfmax = 133000, .val = 0x03 },
	{ .rfmax = 139500, .val = 0x02 },
	{ .rfmax = 146100, .val = 0x01 },
	{ .rfmax = 152600, .val = 0x00 },
	{ .rfmax = 154300, .val = 0x1f },
	{ .rfmax = 156100, .val = 0x1e },
	{ .rfmax = 157800, .val = 0x1d },
	{ .rfmax = 159500, .val = 0x1c },
	{ .rfmax = 161200, .val = 0x1b },
	{ .rfmax = 163000, .val = 0x1a },
	{ .rfmax = 164700, .val = 0x19 },
	{ .rfmax = 170200, .val = 0x17 },
	{ .rfmax = 175800, .val = 0x16 },
	{ .rfmax = 181300, .val = 0x15 },
	{ .rfmax = 186900, .val = 0x14 },
	{ .rfmax = 192400, .val = 0x13 },
	{ .rfmax = 198000, .val = 0x12 },
	{ .rfmax = 203500, .val = 0x11 },
	{ .rfmax = 216200, .val = 0x14 },
	{ .rfmax = 228900, .val = 0x13 },
	{ .rfmax = 241600, .val = 0x12 },
	{ .rfmax = 254400, .val = 0x11 },
	{ .rfmax = 267100, .val = 0x10 },
	{ .rfmax = 279800, .val = 0x0f },
	{ .rfmax = 292500, .val = 0x0e },
	{ .rfmax = 305200, .val = 0x0d },
	{ .rfmax = 317900, .val = 0x0c },
	{ .rfmax = 330700, .val = 0x0b },
	{ .rfmax = 343400, .val = 0x0a },
	{ .rfmax = 356100, .val = 0x09 },
	{ .rfmax = 368800, .val = 0x08 },
	{ .rfmax = 381500, .val = 0x07 },
	{ .rfmax = 394200, .val = 0x06 },
	{ .rfmax = 406900, .val = 0x05 },
	{ .rfmax = 419700, .val = 0x04 },
	{ .rfmax = 432400, .val = 0x03 },
	{ .rfmax = 445100, .val = 0x02 },
	{ .rfmax = 457800, .val = 0x01 },
	{ .rfmax = 476300, .val = 0x19 },
	{ .rfmax = 494800, .val = 0x18 },
	{ .rfmax = 513300, .val = 0x17 },
	{ .rfmax = 531800, .val = 0x16 },
	{ .rfmax = 550300, .val = 0x15 },
	{ .rfmax = 568900, .val = 0x14 },
	{ .rfmax = 587400, .val = 0x13 },
	{ .rfmax = 605900, .val = 0x12 },
	{ .rfmax = 624400, .val = 0x11 },
	{ .rfmax = 642900, .val = 0x10 },
	{ .rfmax = 661400, .val = 0x0f },
	{ .rfmax = 679900, .val = 0x0e },
	{ .rfmax = 698400, .val = 0x0d },
	{ .rfmax = 716900, .val = 0x0c },
	{ .rfmax = 735400, .val = 0x0b },
	{ .rfmax = 753900, .val = 0x0a },
	{ .rfmax = 772500, .val = 0x09 },
	{ .rfmax = 791000, .val = 0x08 },
	{ .rfmax = 809500, .val = 0x07 },
	{ .rfmax = 828000, .val = 0x06 },
	{ .rfmax = 846500, .val = 0x05 },
	{ .rfmax = 865000, .val = 0x04 },
	{ .rfmax =      0, .val = 0x00 }, /* end */
};

static struct tda18271_map tda18271_rf_cal[] = {
	{ .rfmax = 41000, .val = 0x1e },
	{ .rfmax = 43000, .val = 0x30 },
	{ .rfmax = 45000, .val = 0x43 },
	{ .rfmax = 46000, .val = 0x4d },
	{ .rfmax = 47000, .val = 0x54 },
	{ .rfmax = 47900, .val = 0x64 },
	{ .rfmax = 49100, .val = 0x20 },
	{ .rfmax = 50000, .val = 0x22 },
	{ .rfmax = 51000, .val = 0x2a },
	{ .rfmax = 53000, .val = 0x32 },
	{ .rfmax = 55000, .val = 0x35 },
	{ .rfmax = 56000, .val = 0x3c },
	{ .rfmax = 57000, .val = 0x3f },
	{ .rfmax = 58000, .val = 0x48 },
	{ .rfmax = 59000, .val = 0x4d },
	{ .rfmax = 60000, .val = 0x58 },
	{ .rfmax = 61100, .val = 0x5f },
	{ .rfmax =     0, .val = 0x00 }, /* end */
};

/*---------------------------------------------------------------------*/

#define TDA18271_NUM_REGS 39

#define TDA18271_ANALOG  0
#define TDA18271_DIGITAL 1

struct tda18271_priv {
	u8 i2c_addr;
	struct i2c_adapter *i2c_adap;
	unsigned char tda18271_regs[TDA18271_NUM_REGS];
	int mode;

	u32 frequency;
	u32 bandwidth;
};

static int tda18271_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	struct analog_tuner_ops *ops = fe->ops.analog_demod_ops;
	int ret = 0;

	switch (priv->mode) {
	case TDA18271_ANALOG:
		if (ops && ops->i2c_gate_ctrl)
			ret = ops->i2c_gate_ctrl(fe, enable);
		break;
	case TDA18271_DIGITAL:
		if (fe->ops.i2c_gate_ctrl)
			ret = fe->ops.i2c_gate_ctrl(fe, enable);
		break;
	}

	return ret;
};

/*---------------------------------------------------------------------*/

static void tda18271_dump_regs(struct dvb_frontend *fe)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;

	dprintk(1, "=== TDA18271 REG DUMP ===\n");
	dprintk(1, "ID_BYTE            = 0x%x\n", 0xff & regs[R_ID]);
	dprintk(1, "THERMO_BYTE        = 0x%x\n", 0xff & regs[R_TM]);
	dprintk(1, "POWER_LEVEL_BYTE   = 0x%x\n", 0xff & regs[R_PL]);
	dprintk(1, "EASY_PROG_BYTE_1   = 0x%x\n", 0xff & regs[R_EP1]);
	dprintk(1, "EASY_PROG_BYTE_2   = 0x%x\n", 0xff & regs[R_EP2]);
	dprintk(1, "EASY_PROG_BYTE_3   = 0x%x\n", 0xff & regs[R_EP3]);
	dprintk(1, "EASY_PROG_BYTE_4   = 0x%x\n", 0xff & regs[R_EP4]);
	dprintk(1, "EASY_PROG_BYTE_5   = 0x%x\n", 0xff & regs[R_EP5]);
	dprintk(1, "CAL_POST_DIV_BYTE  = 0x%x\n", 0xff & regs[R_CPD]);
	dprintk(1, "CAL_DIV_BYTE_1     = 0x%x\n", 0xff & regs[R_CD1]);
	dprintk(1, "CAL_DIV_BYTE_2     = 0x%x\n", 0xff & regs[R_CD2]);
	dprintk(1, "CAL_DIV_BYTE_3     = 0x%x\n", 0xff & regs[R_CD3]);
	dprintk(1, "MAIN_POST_DIV_BYTE = 0x%x\n", 0xff & regs[R_MPD]);
	dprintk(1, "MAIN_DIV_BYTE_1    = 0x%x\n", 0xff & regs[R_MD1]);
	dprintk(1, "MAIN_DIV_BYTE_2    = 0x%x\n", 0xff & regs[R_MD2]);
	dprintk(1, "MAIN_DIV_BYTE_3    = 0x%x\n", 0xff & regs[R_MD3]);
}

static void tda18271_read_regs(struct dvb_frontend *fe)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	unsigned char buf = 0x00;
	int ret;
	struct i2c_msg msg[] = {
		{ .addr = priv->i2c_addr, .flags = 0,
		  .buf = &buf, .len = 1 },
		{ .addr = priv->i2c_addr, .flags = I2C_M_RD,
		  .buf = regs, .len = 16 }
	};

	tda18271_i2c_gate_ctrl(fe, 1);

	/* read all registers */
	ret = i2c_transfer(priv->i2c_adap, msg, 2);

	tda18271_i2c_gate_ctrl(fe, 0);

	if (ret != 2)
		printk("ERROR: %s: i2c_transfer returned: %d\n",
		       __FUNCTION__, ret);

	if (debug > 1)
		tda18271_dump_regs(fe);
}

static void tda18271_write_regs(struct dvb_frontend *fe, int idx, int len)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	unsigned char buf[TDA18271_NUM_REGS+1];
	struct i2c_msg msg = { .addr = priv->i2c_addr, .flags = 0,
			       .buf = buf, .len = len+1 };
	int i, ret;

	BUG_ON((len == 0) || (idx+len > sizeof(buf)));

	buf[0] = idx;
	for (i = 1; i <= len; i++) {
		buf[i] = regs[idx-1+i];
	}

	tda18271_i2c_gate_ctrl(fe, 1);

	/* write registers */
	ret = i2c_transfer(priv->i2c_adap, &msg, 1);

	tda18271_i2c_gate_ctrl(fe, 0);

	if (ret != 1)
		printk(KERN_WARNING "ERROR: %s: i2c_transfer returned: %d\n",
		       __FUNCTION__, ret);
}

/*---------------------------------------------------------------------*/

static int tda18271_init_regs(struct dvb_frontend *fe)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;

	printk(KERN_INFO "tda18271: initializing registers\n");

	/* initialize registers */
	regs[R_ID]   = 0x83;
	regs[R_TM]   = 0x08;
	regs[R_PL]   = 0x80;
	regs[R_EP1]  = 0xc6;
	regs[R_EP2]  = 0xdf;
	regs[R_EP3]  = 0x16;
	regs[R_EP4]  = 0x60;
	regs[R_EP5]  = 0x80;
	regs[R_CPD]  = 0x80;
	regs[R_CD1]  = 0x00;
	regs[R_CD2]  = 0x00;
	regs[R_CD3]  = 0x00;
	regs[R_MPD]  = 0x00;
	regs[R_MD1]  = 0x00;
	regs[R_MD2]  = 0x00;
	regs[R_MD3]  = 0x00;
	regs[R_EB1]  = 0xff;
	regs[R_EB2]  = 0x01;
	regs[R_EB3]  = 0x84;
	regs[R_EB4]  = 0x41;
	regs[R_EB5]  = 0x01;
	regs[R_EB6]  = 0x84;
	regs[R_EB7]  = 0x40;
	regs[R_EB8]  = 0x07;
	regs[R_EB9]  = 0x00;
	regs[R_EB10] = 0x00;
	regs[R_EB11] = 0x96;
	regs[R_EB12] = 0x0f;
	regs[R_EB13] = 0xc1;
	regs[R_EB14] = 0x00;
	regs[R_EB15] = 0x8f;
	regs[R_EB16] = 0x00;
	regs[R_EB17] = 0x00;
	regs[R_EB18] = 0x00;
	regs[R_EB19] = 0x00;
	regs[R_EB20] = 0x20;
	regs[R_EB21] = 0x33;
	regs[R_EB22] = 0x48;
	regs[R_EB23] = 0xb0;

	tda18271_write_regs(fe, 0x00, TDA18271_NUM_REGS);
	/* setup AGC1 & AGC2 */
	regs[R_EB17] = 0x00;
	tda18271_write_regs(fe, R_EB17, 1);
	regs[R_EB17] = 0x03;
	tda18271_write_regs(fe, R_EB17, 1);
	regs[R_EB17] = 0x43;
	tda18271_write_regs(fe, R_EB17, 1);
	regs[R_EB17] = 0x4c;
	tda18271_write_regs(fe, R_EB17, 1);

	regs[R_EB20] = 0xa0;
	tda18271_write_regs(fe, R_EB20, 1);
	regs[R_EB20] = 0xa7;
	tda18271_write_regs(fe, R_EB20, 1);
	regs[R_EB20] = 0xe7;
	tda18271_write_regs(fe, R_EB20, 1);
	regs[R_EB20] = 0xec;
	tda18271_write_regs(fe, R_EB20, 1);

	/* image rejection calibration */

	/* low-band */
	regs[R_EP3] = 0x1f;
	regs[R_EP4] = 0x66;
	regs[R_EP5] = 0x81;
	regs[R_CPD] = 0xcc;
	regs[R_CD1] = 0x6c;
	regs[R_CD2] = 0x00;
	regs[R_CD3] = 0x00;
	regs[R_MPD] = 0xcd;
	regs[R_MD1] = 0x77;
	regs[R_MD2] = 0x08;
	regs[R_MD3] = 0x00;

	tda18271_write_regs(fe, R_EP3, 11);
	msleep(5); /* pll locking */

	regs[R_EP1] = 0xc6;
	tda18271_write_regs(fe, R_EP1, 1);
	msleep(5); /* wanted low measurement */

	regs[R_EP3] = 0x1f;
	regs[R_EP4] = 0x66;
	regs[R_EP5] = 0x85;
	regs[R_CPD] = 0xcb;
	regs[R_CD1] = 0x66;
	regs[R_CD2] = 0x70;
	regs[R_CD3] = 0x00;

	tda18271_write_regs(fe, R_EP3, 7);
	msleep(5); /* pll locking */

	regs[R_EP2] = 0xdf;
	tda18271_write_regs(fe, R_EP2, 1);
	msleep(30); /* image low optimization completion */

	/* mid-band */
	regs[R_EP3] = 0x1f;
	regs[R_EP4] = 0x66;
	regs[R_EP5] = 0x82;
	regs[R_CPD] = 0xa8;
	regs[R_CD1] = 0x66;
	regs[R_CD2] = 0x00;
	regs[R_CD3] = 0x00;
	regs[R_MPD] = 0xa9;
	regs[R_MD1] = 0x73;
	regs[R_MD2] = 0x1a;
	regs[R_MD3] = 0x00;

	tda18271_write_regs(fe, R_EP3, 11);
	msleep(5); /* pll locking */

	regs[R_EP1] = 0xc6;
	tda18271_write_regs(fe, R_EP1, 1);
	msleep(5); /* wanted mid measurement */

	regs[R_EP3] = 0x1f;
	regs[R_EP4] = 0x66;
	regs[R_EP5] = 0x86;
	regs[R_CPD] = 0xa8;
	regs[R_CD1] = 0x66;
	regs[R_CD2] = 0xa0;
	regs[R_CD3] = 0x00;

	tda18271_write_regs(fe, R_EP3, 7);
	msleep(5); /* pll locking */

	regs[R_EP2] = 0xdf;
	tda18271_write_regs(fe, R_EP2, 1);
	msleep(30); /* image mid optimization completion */

	/* high-band */
	regs[R_EP3] = 0x1f;
	regs[R_EP4] = 0x66;
	regs[R_EP5] = 0x83;
	regs[R_CPD] = 0x98;
	regs[R_CD1] = 0x65;
	regs[R_CD2] = 0x00;
	regs[R_CD3] = 0x00;
	regs[R_MPD] = 0x99;
	regs[R_MD1] = 0x71;
	regs[R_MD2] = 0xcd;
	regs[R_MD3] = 0x00;

	tda18271_write_regs(fe, R_EP3, 11);
	msleep(5); /* pll locking */

	regs[R_EP1] = 0xc6;
	tda18271_write_regs(fe, R_EP1, 1);
	msleep(5); /* wanted high measurement */

	regs[R_EP3] = 0x1f;
	regs[R_EP4] = 0x66;
	regs[R_EP5] = 0x87;
	regs[R_CPD] = 0x98;
	regs[R_CD1] = 0x65;
	regs[R_CD2] = 0x50;
	regs[R_CD3] = 0x00;

	tda18271_write_regs(fe, R_EP3, 7);
	msleep(5); /* pll locking */

	regs[R_EP2] = 0xdf;

	tda18271_write_regs(fe, R_EP2, 1);
	msleep(30); /* image high optimization completion */

	regs[R_EP4] = 0x64;
	tda18271_write_regs(fe, R_EP4, 1);

	regs[R_EP1] = 0xc6;
	tda18271_write_regs(fe, R_EP1, 1);

	return 0;
}

static int tda18271_tune(struct dvb_frontend *fe,
			 u32 ifc, u32 freq, u32 bw, u8 std)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	unsigned char *regs = priv->tda18271_regs;
	u32 div, N = 0;
	int i;

	tda18271_read_regs(fe);

	/* test IR_CAL_OK to see if we need init */
	if ((regs[R_EP1] & 0x08) == 0)
		tda18271_init_regs(fe);


	dprintk(1, "freq = %d, ifc = %d\n", freq, ifc);

	/* RF tracking filter calibration */

	/* calculate BP_Filter */
	i = 0;
	while ((tda18271_bp_filter[i].rfmax * 1000) < freq) {
		if (tda18271_bp_filter[i + 1].rfmax == 0)
			break;
		i++;
	}
	dprintk(2, "bp filter = 0x%x, i = %d\n", tda18271_bp_filter[i].val, i);

	regs[R_EP1]  &= ~0x07; /* clear bp filter bits */
	regs[R_EP1]  |= tda18271_bp_filter[i].val;
	tda18271_write_regs(fe, R_EP1, 1);

	regs[R_EB4]  &= 0x07;
	regs[R_EB4]  |= 0x60;
	tda18271_write_regs(fe, R_EB4, 1);

	regs[R_EB7]   = 0x60;
	tda18271_write_regs(fe, R_EB7, 1);

	regs[R_EB14]  = 0x00;
	tda18271_write_regs(fe, R_EB14, 1);

	regs[R_EB20]  = 0xcc;
	tda18271_write_regs(fe, R_EB20, 1);

	/* set CAL mode to RF tracking filter calibration */
	regs[R_EB4]  |= 0x03;

	/* calculate CAL PLL */

	switch (priv->mode) {
	case TDA18271_ANALOG:
		N = freq - 1250000;
		break;
	case TDA18271_DIGITAL:
		N = freq + bw / 2;
		break;
	}

	i = 0;
	while ((tda18271_cal_pll[i].lomax * 1000) < N) {
		if (tda18271_cal_pll[i + 1].lomax == 0)
			break;
		i++;
	}
	dprintk(2, "cal pll, pd = 0x%x, d = 0x%x, i = %d\n",
		tda18271_cal_pll[i].pd, tda18271_cal_pll[i].d, i);

	regs[R_CPD]   = tda18271_cal_pll[i].pd;

	div =  ((tda18271_cal_pll[i].d * (N / 1000)) << 7) / 125;
	regs[R_CD1]   = 0xff & (div >> 16);
	regs[R_CD2]   = 0xff & (div >> 8);
	regs[R_CD3]   = 0xff & div;

	/* calculate MAIN PLL */

	switch (priv->mode) {
	case TDA18271_ANALOG:
		N = freq - 250000;
		break;
	case TDA18271_DIGITAL:
		N = freq + bw / 2 + 1000000;
		break;
	}

	i = 0;
	while ((tda18271_main_pll[i].lomax * 1000) < N) {
		if (tda18271_main_pll[i + 1].lomax == 0)
			break;
		i++;
	}
	dprintk(2, "main pll, pd = 0x%x, d = 0x%x, i = %d\n",
		tda18271_main_pll[i].pd, tda18271_main_pll[i].d, i);

	regs[R_MPD]   = (0x7f & tda18271_main_pll[i].pd);

	switch (priv->mode) {
	case TDA18271_ANALOG:
		regs[R_MPD]  &= ~0x08;
		break;
	case TDA18271_DIGITAL:
		regs[R_MPD]  |=  0x08;
		break;
	}

	div =  ((tda18271_main_pll[i].d * (N / 1000)) << 7) / 125;
	regs[R_MD1]   = 0xff & (div >> 16);
	regs[R_MD2]   = 0xff & (div >> 8);
	regs[R_MD3]   = 0xff & div;

	tda18271_write_regs(fe, R_EP3, 11);
	msleep(5); /* RF tracking filter calibration initialization */

	/* search for K,M,CO for RF Calibration */
	i = 0;
	while ((tda18271_km[i].rfmax * 1000) < freq) {
		if (tda18271_km[i + 1].rfmax == 0)
			break;
		i++;
	}
	dprintk(2, "km = 0x%x, i = %d\n", tda18271_km[i].val, i);

	regs[R_EB13] &= 0x83;
	regs[R_EB13] |= tda18271_km[i].val;
	tda18271_write_regs(fe, R_EB13, 1);

	/* search for RF_BAND */
	i = 0;
	while ((tda18271_rf_band[i].rfmax * 1000) < freq) {
		if (tda18271_rf_band[i + 1].rfmax == 0)
			break;
		i++;
	}
	dprintk(2, "rf band = 0x%x, i = %d\n", tda18271_rf_band[i].val, i);

	regs[R_EP2]  &= ~0xe0; /* clear rf band bits */
	regs[R_EP2]  |= (tda18271_rf_band[i].val << 5);

	/* search for Gain_Taper */
	i = 0;
	while ((tda18271_gain_taper[i].rfmax * 1000) < freq) {
		if (tda18271_gain_taper[i + 1].rfmax == 0)
			break;
		i++;
	}
	dprintk(2, "gain taper = 0x%x, i = %d\n",
		tda18271_gain_taper[i].val, i);

	regs[R_EP2]  &= ~0x1f; /* clear gain taper bits */
	regs[R_EP2]  |= tda18271_gain_taper[i].val;

	tda18271_write_regs(fe, R_EP2, 1);
	tda18271_write_regs(fe, R_EP1, 1);
	tda18271_write_regs(fe, R_EP2, 1);
	tda18271_write_regs(fe, R_EP1, 1);

	regs[R_EB4]  &= 0x07;
	regs[R_EB4]  |= 0x40;
	tda18271_write_regs(fe, R_EB4, 1);

	regs[R_EB7]   = 0x40;
	tda18271_write_regs(fe, R_EB7, 1);
	msleep(10);

	regs[R_EB20]  = 0xec;
	tda18271_write_regs(fe, R_EB20, 1);
	msleep(60); /* RF tracking filter calibration completion */

	regs[R_EP4]  &= ~0x03; /* set cal mode to normal */
	tda18271_write_regs(fe, R_EP4, 1);

	tda18271_write_regs(fe, R_EP1, 1);

	/* RF tracking filer correction for VHF_Low band */
	i = 0;
	while ((tda18271_rf_cal[i].rfmax * 1000) < freq) {
		if (tda18271_rf_cal[i].rfmax == 0)
			break;
		i++;
	}
	dprintk(2, "rf cal = 0x%x, i = %d\n", tda18271_rf_cal[i].val, i);

	/* VHF_Low band only */
	if (tda18271_rf_cal[i].rfmax != 0) {
		regs[R_EB14]   = tda18271_rf_cal[i].val;
		tda18271_write_regs(fe, R_EB14, 1);
	}

	/* Channel Configuration */

	switch (priv->mode) {
	case TDA18271_ANALOG:
		regs[R_EB22]  = 0x2c;
		break;
	case TDA18271_DIGITAL:
		regs[R_EB22]  = 0x37;
		break;
	}
	tda18271_write_regs(fe, R_EB22, 1);

	regs[R_EP1]  |= 0x40; /* set dis power level on */

	/* set standard */
	regs[R_EP3]  &= ~0x1f; /* clear std bits */

	/* see table 22 */
	regs[R_EP3]  |= std;

	/* TO DO:           *
	 * ================ *
	 * FM radio,   0x18 *
	 * ATSC  6MHz, 0x1c *
	 * DVB-T 6MHz, 0x1c *
	 * DVB-T 7MHz, 0x1d *
	 * DVB-T 8MHz, 0x1e *
	 * QAM   6MHz, 0x1d *
	 * QAM   8MHz, 0x1f */

	regs[R_EP4]  &= ~0x03; /* set cal mode to normal */

	regs[R_EP4]  &= ~0x1c; /* clear if level bits */
	switch (priv->mode) {
	case TDA18271_ANALOG:
		regs[R_MPD]  &= ~0x80; /* IF notch = 0 */
		break;
	case TDA18271_DIGITAL:
		regs[R_EP4]  |= 0x04;
		regs[R_MPD]  |= 0x80;
		break;
	}

	regs[R_EP4]  &= ~0x80; /* turn this bit on only for fm */

	/* FIXME: image rejection validity EP5[2:0] */

	/* calculate MAIN PLL */
	N = freq + ifc;

	i = 0;
	while ((tda18271_main_pll[i].lomax * 1000) < N) {
		if (tda18271_main_pll[i + 1].lomax == 0)
			break;
		i++;
	}
	dprintk(2, "main pll, pd = 0x%x, d = 0x%x, i = %d\n",
		tda18271_main_pll[i].pd, tda18271_main_pll[i].d, i);

	regs[R_MPD]   = (0x7f & tda18271_main_pll[i].pd);
	switch (priv->mode) {
	case TDA18271_ANALOG:
		regs[R_MPD]  &= ~0x08;
		break;
	case TDA18271_DIGITAL:
		regs[R_MPD]  |= 0x08;
		break;
	}

	div =  ((tda18271_main_pll[i].d * (N / 1000)) << 7) / 125;
	regs[R_MD1]   = 0xff & (div >> 16);
	regs[R_MD2]   = 0xff & (div >> 8);
	regs[R_MD3]   = 0xff & div;

	tda18271_write_regs(fe, R_TM, 15);
	msleep(5);
	return 0;
}

/* ------------------------------------------------------------------ */

static int tda18271_set_params(struct dvb_frontend *fe,
			       struct dvb_frontend_parameters *params)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	u8 std;
	u32 bw, sgIF = 0;

	u32 freq = params->frequency;

	priv->mode = TDA18271_DIGITAL;

	/* see table 22 */
	if (fe->ops.info.type == FE_ATSC) {
		switch (params->u.vsb.modulation) {
		case VSB_8:
		case VSB_16:
			std = 0x1b; /* device-specific (spec says 0x1c) */
			sgIF = 5380000;
			break;
		case QAM_64:
		case QAM_256:
			std = 0x18; /* device-specific (spec says 0x1d) */
			sgIF = 4000000;
			break;
		default:
			printk(KERN_WARNING "%s: modulation not set!\n",
			       __FUNCTION__);
			return -EINVAL;
		}
		freq += 1750000; /* Adjust to center (+1.75MHZ) */
		bw = 6000000;
	} else if (fe->ops.info.type == FE_OFDM) {
		switch (params->u.ofdm.bandwidth) {
		case BANDWIDTH_6_MHZ:
			std = 0x1c;
			bw = 6000000;
			break;
		case BANDWIDTH_7_MHZ:
			std = 0x1d;
			bw = 7000000;
			break;
		case BANDWIDTH_8_MHZ:
			std = 0x1e;
			bw = 8000000;
			break;
		default:
			printk(KERN_WARNING "%s: bandwidth not set!\n",
			       __FUNCTION__);
			return -EINVAL;
		}
	} else {
		printk(KERN_WARNING "%s: modulation type not supported!\n",
		       __FUNCTION__);
		return -EINVAL;
	}

	return tda18271_tune(fe, sgIF, freq, bw, std);
}

static int tda18271_set_analog_params(struct dvb_frontend *fe,
				      struct analog_parameters *params)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	u8 std;
	unsigned int sgIF;
	char *mode;

	priv->mode = TDA18271_ANALOG;

	/* see table 22 */
	if (params->std & V4L2_STD_MN) {
		std = 0x0d;
		sgIF =  92;
		mode = "MN";
	} else if (params->std & V4L2_STD_B) {
		std = 0x0e;
		sgIF =  108;
		mode = "B";
	} else if (params->std & V4L2_STD_GH) {
		std = 0x0f;
		sgIF =  124;
		mode = "GH";
	} else if (params->std & V4L2_STD_PAL_I) {
		std = 0x0f;
		sgIF =  124;
		mode = "I";
	} else if (params->std & V4L2_STD_DK) {
		std = 0x0f;
		sgIF =  124;
		mode = "DK";
	} else if (params->std & V4L2_STD_SECAM_L) {
		std = 0x0f;
		sgIF =  124;
		mode = "L";
	} else if (params->std & V4L2_STD_SECAM_LC) {
		std = 0x0f;
		sgIF =  20;
		mode = "LC";
	} else {
		std = 0x0f;
		sgIF =  124;
		mode = "xx";
	}

	if (params->mode == V4L2_TUNER_RADIO)
		sgIF =  88; /* if frequency is 5.5 MHz */

	dprintk(1, "setting tda18271 to system %s\n", mode);

	return tda18271_tune(fe, sgIF * 62500, params->frequency * 62500,
			     0, std);
}

static int tda18271_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static int tda18271_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static int tda18271_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct tda18271_priv *priv = fe->tuner_priv;
	*bandwidth = priv->bandwidth;
	return 0;
}

static struct dvb_tuner_ops tda18271_tuner_ops = {
	.info = {
		.name = "NXP TDA18271HD",
		.frequency_min  =  45000000,
		.frequency_max  = 864000000,
		.frequency_step =     62500
	},
	.init              = tda18271_init_regs,
	.set_params        = tda18271_set_params,
	.set_analog_params = tda18271_set_analog_params,
	.release           = tda18271_release,
	.get_frequency     = tda18271_get_frequency,
	.get_bandwidth     = tda18271_get_bandwidth,
};

struct dvb_frontend *tda18271_attach(struct dvb_frontend *fe, u8 addr,
				     struct i2c_adapter *i2c)
{
	struct tda18271_priv *priv = NULL;

	dprintk(1, "@ 0x%x\n", addr);
	priv = kzalloc(sizeof(struct tda18271_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;

	priv->i2c_addr = addr;
	priv->i2c_adap = i2c;

	memcpy(&fe->ops.tuner_ops, &tda18271_tuner_ops,
	       sizeof(struct dvb_tuner_ops));

	fe->tuner_priv = priv;

	return fe;
}
EXPORT_SYMBOL_GPL(tda18271_attach);
MODULE_DESCRIPTION("NXP TDA18271HD analog / digital tuner driver");
MODULE_AUTHOR("Michael Krufky <mkrufky@linuxtv.org>");
MODULE_LICENSE("GPL");

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
