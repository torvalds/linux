/*
 * Driver for the ST STV0910 DVB-S/S2 demodulator.
 *
 * Copyright (C) 2014-2015 Ralph Metzler <rjkm@metzlerbros.de>
 *                         Marcus Metzler <mocm@metzlerbros.de>
 *                         developed for Digital Devices GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <asm/div64.h>

#include "dvb_math.h"
#include "dvb_frontend.h"
#include "stv0910.h"
#include "stv0910_regs.h"

#define EXT_CLOCK    30000000
#define TUNING_DELAY 200
#define BER_SRC_S    0x20
#define BER_SRC_S2   0x20

LIST_HEAD(stvlist);

enum receive_mode { RCVMODE_NONE, RCVMODE_DVBS, RCVMODE_DVBS2, RCVMODE_AUTO };

enum dvbs2_fectype { DVBS2_64K, DVBS2_16K };

enum dvbs2_mod_cod {
	DVBS2_DUMMY_PLF, DVBS2_QPSK_1_4, DVBS2_QPSK_1_3, DVBS2_QPSK_2_5,
	DVBS2_QPSK_1_2, DVBS2_QPSK_3_5, DVBS2_QPSK_2_3,	DVBS2_QPSK_3_4,
	DVBS2_QPSK_4_5,	DVBS2_QPSK_5_6,	DVBS2_QPSK_8_9,	DVBS2_QPSK_9_10,
	DVBS2_8PSK_3_5,	DVBS2_8PSK_2_3,	DVBS2_8PSK_3_4,	DVBS2_8PSK_5_6,
	DVBS2_8PSK_8_9,	DVBS2_8PSK_9_10, DVBS2_16APSK_2_3, DVBS2_16APSK_3_4,
	DVBS2_16APSK_4_5, DVBS2_16APSK_5_6, DVBS2_16APSK_8_9, DVBS2_16APSK_9_10,
	DVBS2_32APSK_3_4, DVBS2_32APSK_4_5, DVBS2_32APSK_5_6, DVBS2_32APSK_8_9,
	DVBS2_32APSK_9_10
};

enum fe_stv0910_mod_cod {
	FE_DUMMY_PLF, FE_QPSK_14, FE_QPSK_13, FE_QPSK_25,
	FE_QPSK_12, FE_QPSK_35, FE_QPSK_23, FE_QPSK_34,
	FE_QPSK_45, FE_QPSK_56, FE_QPSK_89, FE_QPSK_910,
	FE_8PSK_35, FE_8PSK_23, FE_8PSK_34, FE_8PSK_56,
	FE_8PSK_89, FE_8PSK_910, FE_16APSK_23, FE_16APSK_34,
	FE_16APSK_45, FE_16APSK_56, FE_16APSK_89, FE_16APSK_910,
	FE_32APSK_34, FE_32APSK_45, FE_32APSK_56, FE_32APSK_89,
	FE_32APSK_910
};

enum fe_stv0910_roll_off { FE_SAT_35, FE_SAT_25, FE_SAT_20, FE_SAT_15 };

static inline u32 muldiv32(u32 a, u32 b, u32 c)
{
	u64 tmp64;

	tmp64 = (u64)a * (u64)b;
	do_div(tmp64, c);

	return (u32) tmp64;
}

struct stv_base {
	struct list_head     stvlist;

	u8                   adr;
	struct i2c_adapter  *i2c;
	struct mutex         i2c_lock;
	struct mutex         reg_lock;
	int                  count;

	u32                  extclk;
	u32                  mclk;
};

struct stv {
	struct stv_base     *base;
	struct dvb_frontend  fe;
	int                  nr;
	u16                  regoff;
	u8                   i2crpt;
	u8                   tscfgh;
	u8                   tsgeneral;
	u8                   tsspeed;
	u8                   single;
	unsigned long        tune_time;

	s32                  search_range;
	u32                  started;
	u32                  demod_lock_time;
	enum receive_mode    receive_mode;
	u32                  demod_timeout;
	u32                  fec_timeout;
	u32                  first_time_lock;
	u8                   demod_bits;
	u32                  symbol_rate;

	u8                       last_viterbi_rate;
	enum fe_code_rate        puncture_rate;
	enum fe_stv0910_mod_cod  mod_cod;
	enum dvbs2_fectype       fectype;
	u32                      pilots;
	enum fe_stv0910_roll_off feroll_off;

	int   is_standard_broadcast;
	int   is_vcm;

	u32   cur_scrambling_code;

	u32   last_bernumerator;
	u32   last_berdenominator;
	u8    berscale;

	u8    vth[6];
};

struct sinit_table {
	u16  address;
	u8   data;
};

struct slookup {
	s16  value;
	u32  reg_value;
};

static inline int i2c_write(struct i2c_adapter *adap, u8 adr,
			    u8 *data, int len)
{
	struct i2c_msg msg = {.addr = adr, .flags = 0,
			      .buf = data, .len = len};

	if (i2c_transfer(adap, &msg, 1) != 1) {
		dev_warn(&adap->dev, "i2c write error ([%02x] %04x: %02x)\n",
			adr, (data[0] << 8) | data[1],
			(len > 2 ? data[2] : 0));
		return -EREMOTEIO;
	}
	return 0;
}

static int i2c_write_reg16(struct i2c_adapter *adap, u8 adr, u16 reg, u8 val)
{
	u8 msg[3] = {reg >> 8, reg & 0xff, val};

	return i2c_write(adap, adr, msg, 3);
}

static int write_reg(struct stv *state, u16 reg, u8 val)
{
	return i2c_write_reg16(state->base->i2c, state->base->adr, reg, val);
}

static inline int i2c_read_regs16(struct i2c_adapter *adapter, u8 adr,
				 u16 reg, u8 *val, int count)
{
	u8 msg[2] = {reg >> 8, reg & 0xff};
	struct i2c_msg msgs[2] = {{.addr = adr, .flags = 0,
				   .buf  = msg, .len   = 2},
				  {.addr = adr, .flags = I2C_M_RD,
				   .buf  = val, .len   = count } };

	if (i2c_transfer(adapter, msgs, 2) != 2) {
		dev_warn(&adapter->dev, "i2c read error ([%02x] %04x)\n",
			adr, reg);
		return -EREMOTEIO;
	}
	return 0;
}

static int read_reg(struct stv *state, u16 reg, u8 *val)
{
	return i2c_read_regs16(state->base->i2c, state->base->adr,
		reg, val, 1);
}

static int read_regs(struct stv *state, u16 reg, u8 *val, int len)
{
	return i2c_read_regs16(state->base->i2c, state->base->adr,
			       reg, val, len);
}

static int write_shared_reg(struct stv *state, u16 reg, u8 mask, u8 val)
{
	int status;
	u8 tmp;

	mutex_lock(&state->base->reg_lock);
	status = read_reg(state, reg, &tmp);
	if (!status)
		status = write_reg(state, reg, (tmp & ~mask) | (val & mask));
	mutex_unlock(&state->base->reg_lock);
	return status;
}

struct slookup s1_sn_lookup[] = {
	{   0,    9242  },  /*C/N=  0dB*/
	{   5,    9105  },  /*C/N=0.5dB*/
	{  10,    8950  },  /*C/N=1.0dB*/
	{  15,    8780  },  /*C/N=1.5dB*/
	{  20,    8566  },  /*C/N=2.0dB*/
	{  25,    8366  },  /*C/N=2.5dB*/
	{  30,    8146  },  /*C/N=3.0dB*/
	{  35,    7908  },  /*C/N=3.5dB*/
	{  40,    7666  },  /*C/N=4.0dB*/
	{  45,    7405  },  /*C/N=4.5dB*/
	{  50,    7136  },  /*C/N=5.0dB*/
	{  55,    6861  },  /*C/N=5.5dB*/
	{  60,    6576  },  /*C/N=6.0dB*/
	{  65,    6330  },  /*C/N=6.5dB*/
	{  70,    6048  },  /*C/N=7.0dB*/
	{  75,    5768  },  /*C/N=7.5dB*/
	{  80,    5492  },  /*C/N=8.0dB*/
	{  85,    5224  },  /*C/N=8.5dB*/
	{  90,    4959  },  /*C/N=9.0dB*/
	{  95,    4709  },  /*C/N=9.5dB*/
	{  100,   4467  },  /*C/N=10.0dB*/
	{  105,   4236  },  /*C/N=10.5dB*/
	{  110,   4013  },  /*C/N=11.0dB*/
	{  115,   3800  },  /*C/N=11.5dB*/
	{  120,   3598  },  /*C/N=12.0dB*/
	{  125,   3406  },  /*C/N=12.5dB*/
	{  130,   3225  },  /*C/N=13.0dB*/
	{  135,   3052  },  /*C/N=13.5dB*/
	{  140,   2889  },  /*C/N=14.0dB*/
	{  145,   2733  },  /*C/N=14.5dB*/
	{  150,   2587  },  /*C/N=15.0dB*/
	{  160,   2318  },  /*C/N=16.0dB*/
	{  170,   2077  },  /*C/N=17.0dB*/
	{  180,   1862  },  /*C/N=18.0dB*/
	{  190,   1670  },  /*C/N=19.0dB*/
	{  200,   1499  },  /*C/N=20.0dB*/
	{  210,   1347  },  /*C/N=21.0dB*/
	{  220,   1213  },  /*C/N=22.0dB*/
	{  230,   1095  },  /*C/N=23.0dB*/
	{  240,    992  },  /*C/N=24.0dB*/
	{  250,    900  },  /*C/N=25.0dB*/
	{  260,    826  },  /*C/N=26.0dB*/
	{  270,    758  },  /*C/N=27.0dB*/
	{  280,    702  },  /*C/N=28.0dB*/
	{  290,    653  },  /*C/N=29.0dB*/
	{  300,    613  },  /*C/N=30.0dB*/
	{  310,    579  },  /*C/N=31.0dB*/
	{  320,    550  },  /*C/N=32.0dB*/
	{  330,    526  },  /*C/N=33.0dB*/
	{  350,    490  },  /*C/N=33.0dB*/
	{  400,    445  },  /*C/N=40.0dB*/
	{  450,    430  },  /*C/N=45.0dB*/
	{  500,    426  },  /*C/N=50.0dB*/
	{  510,    425  }   /*C/N=51.0dB*/
};

struct slookup s2_sn_lookup[] = {
	{  -30,  13950  },  /*C/N=-2.5dB*/
	{  -25,  13580  },  /*C/N=-2.5dB*/
	{  -20,  13150  },  /*C/N=-2.0dB*/
	{  -15,  12760  },  /*C/N=-1.5dB*/
	{  -10,  12345  },  /*C/N=-1.0dB*/
	{   -5,  11900  },  /*C/N=-0.5dB*/
	{    0,  11520  },  /*C/N=   0dB*/
	{    5,  11080  },  /*C/N= 0.5dB*/
	{   10,  10630  },  /*C/N= 1.0dB*/
	{   15,  10210  },  /*C/N= 1.5dB*/
	{   20,   9790  },  /*C/N= 2.0dB*/
	{   25,   9390  },  /*C/N= 2.5dB*/
	{   30,   8970  },  /*C/N= 3.0dB*/
	{   35,   8575  },  /*C/N= 3.5dB*/
	{   40,   8180  },  /*C/N= 4.0dB*/
	{   45,   7800  },  /*C/N= 4.5dB*/
	{   50,   7430  },  /*C/N= 5.0dB*/
	{   55,   7080  },  /*C/N= 5.5dB*/
	{   60,   6720  },  /*C/N= 6.0dB*/
	{   65,   6320  },  /*C/N= 6.5dB*/
	{   70,   6060  },  /*C/N= 7.0dB*/
	{   75,   5760  },  /*C/N= 7.5dB*/
	{   80,   5480  },  /*C/N= 8.0dB*/
	{   85,   5200  },  /*C/N= 8.5dB*/
	{   90,   4930  },  /*C/N= 9.0dB*/
	{   95,   4680  },  /*C/N= 9.5dB*/
	{  100,   4425  },  /*C/N=10.0dB*/
	{  105,   4210  },  /*C/N=10.5dB*/
	{  110,   3980  },  /*C/N=11.0dB*/
	{  115,   3765  },  /*C/N=11.5dB*/
	{  120,   3570  },  /*C/N=12.0dB*/
	{  125,   3315  },  /*C/N=12.5dB*/
	{  130,   3140  },  /*C/N=13.0dB*/
	{  135,   2980  },  /*C/N=13.5dB*/
	{  140,   2820  },  /*C/N=14.0dB*/
	{  145,   2670  },  /*C/N=14.5dB*/
	{  150,   2535  },  /*C/N=15.0dB*/
	{  160,   2270  },  /*C/N=16.0dB*/
	{  170,   2035  },  /*C/N=17.0dB*/
	{  180,   1825  },  /*C/N=18.0dB*/
	{  190,   1650  },  /*C/N=19.0dB*/
	{  200,   1485  },  /*C/N=20.0dB*/
	{  210,   1340  },  /*C/N=21.0dB*/
	{  220,   1212  },  /*C/N=22.0dB*/
	{  230,   1100  },  /*C/N=23.0dB*/
	{  240,   1000  },  /*C/N=24.0dB*/
	{  250,    910  },  /*C/N=25.0dB*/
	{  260,    836  },  /*C/N=26.0dB*/
	{  270,    772  },  /*C/N=27.0dB*/
	{  280,    718  },  /*C/N=28.0dB*/
	{  290,    671  },  /*C/N=29.0dB*/
	{  300,    635  },  /*C/N=30.0dB*/
	{  310,    602  },  /*C/N=31.0dB*/
	{  320,    575  },  /*C/N=32.0dB*/
	{  330,    550  },  /*C/N=33.0dB*/
	{  350,    517  },  /*C/N=35.0dB*/
	{  400,    480  },  /*C/N=40.0dB*/
	{  450,    466  },  /*C/N=45.0dB*/
	{  500,    464  },  /*C/N=50.0dB*/
	{  510,    463  },  /*C/N=51.0dB*/
};

struct slookup padc_lookup[] = {
	{    0,  118000 }, /* PADC=+0dBm  */
	{ -100,  93600  }, /* PADC=-1dBm  */
	{ -200,  74500  }, /* PADC=-2dBm  */
	{ -300,  59100  }, /* PADC=-3dBm  */
	{ -400,  47000  }, /* PADC=-4dBm  */
	{ -500,  37300  }, /* PADC=-5dBm  */
	{ -600,  29650  }, /* PADC=-6dBm  */
	{ -700,  23520  }, /* PADC=-7dBm  */
	{ -900,  14850  }, /* PADC=-9dBm  */
	{ -1100, 9380   }, /* PADC=-11dBm */
	{ -1300, 5910   }, /* PADC=-13dBm */
	{ -1500, 3730   }, /* PADC=-15dBm */
	{ -1700, 2354   }, /* PADC=-17dBm */
	{ -1900, 1485   }, /* PADC=-19dBm */
	{ -2000, 1179   }, /* PADC=-20dBm */
	{ -2100, 1000   }, /* PADC=-21dBm */
};

/*********************************************************************
 * Tracking carrier loop carrier QPSK 1/4 to 8PSK 9/10 long Frame
 *********************************************************************/
static u8 s2car_loop[] =	{
	/* Modcod  2MPon 2MPoff 5MPon 5MPoff 10MPon 10MPoff
	 * 20MPon 20MPoff 30MPon 30MPoff
	 */

	/* FE_QPSK_14  */
	0x0C,  0x3C,  0x0B,  0x3C,  0x2A,  0x2C,  0x2A,  0x1C,  0x3A,  0x3B,
	/* FE_QPSK_13  */
	0x0C,  0x3C,  0x0B,  0x3C,  0x2A,  0x2C,  0x3A,  0x0C,  0x3A,  0x2B,
	/* FE_QPSK_25  */
	0x1C,  0x3C,  0x1B,  0x3C,  0x3A,  0x1C,  0x3A,  0x3B,  0x3A,  0x2B,
	/* FE_QPSK_12  */
	0x0C,  0x1C,  0x2B,  0x1C,  0x0B,  0x2C,  0x0B,  0x0C,  0x2A,  0x2B,
	/* FE_QPSK_35  */
	0x1C,  0x1C,  0x2B,  0x1C,  0x0B,  0x2C,  0x0B,  0x0C,  0x2A,  0x2B,
	/* FE_QPSK_23  */
	0x2C,  0x2C,  0x2B,  0x1C,  0x0B,  0x2C,  0x0B,  0x0C,  0x2A,  0x2B,
	/* FE_QPSK_34  */
	0x3C,  0x2C,  0x3B,  0x2C,  0x1B,  0x1C,  0x1B,  0x3B,  0x3A,  0x1B,
	/* FE_QPSK_45  */
	0x0D,  0x3C,  0x3B,  0x2C,  0x1B,  0x1C,  0x1B,  0x3B,  0x3A,  0x1B,
	/* FE_QPSK_56  */
	0x1D,  0x3C,  0x0C,  0x2C,  0x2B,  0x1C,  0x1B,  0x3B,  0x0B,  0x1B,
	/* FE_QPSK_89  */
	0x3D,  0x0D,  0x0C,  0x2C,  0x2B,  0x0C,  0x2B,  0x2B,  0x0B,  0x0B,
	/* FE_QPSK_910 */
	0x1E,  0x0D,  0x1C,  0x2C,  0x3B,  0x0C,  0x2B,  0x2B,  0x1B,  0x0B,
	/* FE_8PSK_35  */
	0x28,  0x09,  0x28,  0x09,  0x28,  0x09,  0x28,  0x08,  0x28,  0x27,
	/* FE_8PSK_23  */
	0x19,  0x29,  0x19,  0x29,  0x19,  0x29,  0x38,  0x19,  0x28,  0x09,
	/* FE_8PSK_34  */
	0x1A,  0x0B,  0x1A,  0x3A,  0x0A,  0x2A,  0x39,  0x2A,  0x39,  0x1A,
	/* FE_8PSK_56  */
	0x2B,  0x2B,  0x1B,  0x1B,  0x0B,  0x1B,  0x1A,  0x0B,  0x1A,  0x1A,
	/* FE_8PSK_89  */
	0x0C,  0x0C,  0x3B,  0x3B,  0x1B,  0x1B,  0x2A,  0x0B,  0x2A,  0x2A,
	/* FE_8PSK_910 */
	0x0C,  0x1C,  0x0C,  0x3B,  0x2B,  0x1B,  0x3A,  0x0B,  0x2A,  0x2A,

	/**********************************************************************
	 * Tracking carrier loop carrier 16APSK 2/3 to 32APSK 9/10 long Frame
	 **********************************************************************/

	/* Modcod 2MPon  2MPoff 5MPon 5MPoff 10MPon 10MPoff 20MPon
	 * 20MPoff 30MPon 30MPoff
	 */

	/* FE_16APSK_23  */
	0x0A,  0x0A,  0x0A,  0x0A,  0x1A,  0x0A,  0x39,  0x0A,  0x29,  0x0A,
	/* FE_16APSK_34  */
	0x0A,  0x0A,  0x0A,  0x0A,  0x0B,  0x0A,  0x2A,  0x0A,  0x1A,  0x0A,
	/* FE_16APSK_45  */
	0x0A,  0x0A,  0x0A,  0x0A,  0x1B,  0x0A,  0x3A,  0x0A,  0x2A,  0x0A,
	/* FE_16APSK_56  */
	0x0A,  0x0A,  0x0A,  0x0A,  0x1B,  0x0A,  0x3A,  0x0A,  0x2A,  0x0A,
	/* FE_16APSK_89  */
	0x0A,  0x0A,  0x0A,  0x0A,  0x2B,  0x0A,  0x0B,  0x0A,  0x3A,  0x0A,
	/* FE_16APSK_910 */
	0x0A,  0x0A,  0x0A,  0x0A,  0x2B,  0x0A,  0x0B,  0x0A,  0x3A,  0x0A,
	/* FE_32APSK_34  */
	0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,
	/* FE_32APSK_45  */
	0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,
	/* FE_32APSK_56  */
	0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,
	/* FE_32APSK_89  */
	0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,
	/* FE_32APSK_910 */
	0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,  0x09,
};

static u8 get_optim_cloop(struct stv *state,
			  enum fe_stv0910_mod_cod mod_cod, u32 pilots)
{
	int i = 0;

	if (mod_cod >= FE_32APSK_910)
		i = ((int)FE_32APSK_910 - (int)FE_QPSK_14) * 10;
	else if (mod_cod >= FE_QPSK_14)
		i = ((int)mod_cod - (int)FE_QPSK_14) * 10;

	if (state->symbol_rate <= 3000000)
		i += 0;
	else if (state->symbol_rate <=  7000000)
		i += 2;
	else if (state->symbol_rate <= 15000000)
		i += 4;
	else if (state->symbol_rate <= 25000000)
		i += 6;
	else
		i += 8;

	if (!pilots)
		i += 1;

	return s2car_loop[i];
}

static int get_cur_symbol_rate(struct stv *state, u32 *p_symbol_rate)
{
	int status = 0;
	u8 symb_freq0;
	u8 symb_freq1;
	u8 symb_freq2;
	u8 symb_freq3;
	u8 tim_offs0;
	u8 tim_offs1;
	u8 tim_offs2;
	u32 symbol_rate;
	s32 timing_offset;

	*p_symbol_rate = 0;
	if (!state->started)
		return status;

	read_reg(state, RSTV0910_P2_SFR3 + state->regoff, &symb_freq3);
	read_reg(state, RSTV0910_P2_SFR2 + state->regoff, &symb_freq2);
	read_reg(state, RSTV0910_P2_SFR1 + state->regoff, &symb_freq1);
	read_reg(state, RSTV0910_P2_SFR0 + state->regoff, &symb_freq0);
	read_reg(state, RSTV0910_P2_TMGREG2 + state->regoff, &tim_offs2);
	read_reg(state, RSTV0910_P2_TMGREG1 + state->regoff, &tim_offs1);
	read_reg(state, RSTV0910_P2_TMGREG0 + state->regoff, &tim_offs0);

	symbol_rate = ((u32) symb_freq3 << 24) | ((u32) symb_freq2 << 16) |
		((u32) symb_freq1 << 8) | (u32) symb_freq0;
	timing_offset = ((u32) tim_offs2 << 16) | ((u32) tim_offs1 << 8) |
		(u32) tim_offs0;

	if ((timing_offset & (1<<23)) != 0)
		timing_offset |= 0xFF000000; /* Sign extent */

	symbol_rate = (u32) (((u64) symbol_rate * state->base->mclk) >> 32);
	timing_offset = (s32) (((s64) symbol_rate * (s64) timing_offset) >> 29);

	*p_symbol_rate = symbol_rate + timing_offset;

	return 0;
}

static int get_signal_parameters(struct stv *state)
{
	u8 tmp;

	if (!state->started)
		return -EINVAL;

	if (state->receive_mode == RCVMODE_DVBS2) {
		read_reg(state, RSTV0910_P2_DMDMODCOD + state->regoff, &tmp);
		state->mod_cod = (enum fe_stv0910_mod_cod) ((tmp & 0x7c) >> 2);
		state->pilots = (tmp & 0x01) != 0;
		state->fectype = (enum dvbs2_fectype) ((tmp & 0x02) >> 1);

	} else if (state->receive_mode == RCVMODE_DVBS) {
		read_reg(state, RSTV0910_P2_VITCURPUN + state->regoff, &tmp);
		state->puncture_rate = FEC_NONE;
		switch (tmp & 0x1F) {
		case 0x0d:
			state->puncture_rate = FEC_1_2;
			break;
		case 0x12:
			state->puncture_rate = FEC_2_3;
			break;
		case 0x15:
			state->puncture_rate = FEC_3_4;
			break;
		case 0x18:
			state->puncture_rate = FEC_5_6;
			break;
		case 0x1a:
			state->puncture_rate = FEC_7_8;
			break;
		}
		state->is_vcm = 0;
		state->is_standard_broadcast = 1;
		state->feroll_off = FE_SAT_35;
	}
	return 0;
}

static int tracking_optimization(struct stv *state)
{
	u32 symbol_rate = 0;
	u8 tmp;

	get_cur_symbol_rate(state, &symbol_rate);
	read_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff, &tmp);
	tmp &= ~0xC0;

	switch (state->receive_mode) {
	case RCVMODE_DVBS:
		tmp |= 0x40;
		break;
	case RCVMODE_DVBS2:
		tmp |= 0x80;
		break;
	default:
		tmp |= 0xC0;
		break;
	}
	write_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff, tmp);

	if (state->receive_mode == RCVMODE_DVBS2) {
		/* Disable Reed-Solomon */
		write_shared_reg(state,
				 RSTV0910_TSTTSRS, state->nr ? 0x02 : 0x01,
				 0x03);

		if (state->fectype == DVBS2_64K) {
			u8 aclc = get_optim_cloop(state, state->mod_cod,
						  state->pilots);

			if (state->mod_cod <= FE_QPSK_910) {
				write_reg(state, RSTV0910_P2_ACLC2S2Q +
					  state->regoff, aclc);
			} else if (state->mod_cod <= FE_8PSK_910) {
				write_reg(state, RSTV0910_P2_ACLC2S2Q +
					  state->regoff, 0x2a);
				write_reg(state, RSTV0910_P2_ACLC2S28 +
					  state->regoff, aclc);
			} else if (state->mod_cod <= FE_16APSK_910) {
				write_reg(state, RSTV0910_P2_ACLC2S2Q +
					  state->regoff, 0x2a);
				write_reg(state, RSTV0910_P2_ACLC2S216A +
					  state->regoff, aclc);
			} else if (state->mod_cod <= FE_32APSK_910) {
				write_reg(state, RSTV0910_P2_ACLC2S2Q +
					  state->regoff, 0x2a);
				write_reg(state, RSTV0910_P2_ACLC2S232A +
					  state->regoff, aclc);
			}
		}
	}
	return 0;
}

static s32 table_lookup(struct slookup *table,
		       int table_size, u32 reg_value)
{
	s32 value;
	int imin = 0;
	int imax = table_size - 1;
	int i;
	s32 reg_diff;

	/* Assumes Table[0].RegValue > Table[imax].RegValue */
	if (reg_value >= table[0].reg_value)
		value = table[0].value;
	else if (reg_value <= table[imax].reg_value)
		value = table[imax].value;
	else {
		while (imax-imin > 1) {
			i = (imax + imin) / 2;
			if ((table[imin].reg_value >= reg_value) &&
				(reg_value >= table[i].reg_value))
				imax = i;
			else
				imin = i;
		}

		reg_diff = table[imax].reg_value - table[imin].reg_value;
		value = table[imin].value;
		if (reg_diff != 0)
			value += ((s32)(reg_value - table[imin].reg_value) *
				  (s32)(table[imax].value
					- table[imin].value))
					/ (reg_diff);
	}

	return value;
}

static int get_signal_to_noise(struct stv *state, s32 *signal_to_noise)
{
	u8 data0;
	u8 data1;
	u16 data;
	int n_lookup;
	struct slookup *lookup;

	*signal_to_noise = 0;

	if (!state->started)
		return -EINVAL;

	if (state->receive_mode == RCVMODE_DVBS2) {
		read_reg(state, RSTV0910_P2_NNOSPLHT1 + state->regoff,
			 &data1);
		read_reg(state, RSTV0910_P2_NNOSPLHT0 + state->regoff,
			 &data0);
		n_lookup = ARRAY_SIZE(s2_sn_lookup);
		lookup = s2_sn_lookup;
	} else {
		read_reg(state, RSTV0910_P2_NNOSDATAT1 + state->regoff,
			 &data1);
		read_reg(state, RSTV0910_P2_NNOSDATAT0 + state->regoff,
			 &data0);
		n_lookup = ARRAY_SIZE(s1_sn_lookup);
		lookup = s1_sn_lookup;
	}
	data = (((u16)data1) << 8) | (u16) data0;
	*signal_to_noise = table_lookup(lookup, n_lookup, data);
	return 0;
}

static int get_bit_error_rate_s(struct stv *state, u32 *bernumerator,
			    u32 *berdenominator)
{
	u8 regs[3];

	int status = read_regs(state,
			       RSTV0910_P2_ERRCNT12 + state->regoff,
			       regs, 3);

	if (status)
		return -EINVAL;

	if ((regs[0] & 0x80) == 0) {
		state->last_berdenominator = 1 << ((state->berscale * 2) +
						  10 + 3);
		state->last_bernumerator = ((u32) (regs[0] & 0x7F) << 16) |
			((u32) regs[1] << 8) | regs[2];
		if (state->last_bernumerator < 256 && state->berscale < 6) {
			state->berscale += 1;
			status = write_reg(state, RSTV0910_P2_ERRCTRL1 +
					   state->regoff,
					   0x20 | state->berscale);
		} else if (state->last_bernumerator > 1024 &&
			   state->berscale > 2) {
			state->berscale -= 1;
			status = write_reg(state, RSTV0910_P2_ERRCTRL1 +
					   state->regoff, 0x20 |
					   state->berscale);
		}
	}
	*bernumerator = state->last_bernumerator;
	*berdenominator = state->last_berdenominator;
	return 0;
}

static u32 dvbs2_nbch(enum dvbs2_mod_cod mod_cod, enum dvbs2_fectype fectype)
{
	static u32 nbch[][2] = {
		{    0,     0}, /* DUMMY_PLF */
		{16200,  3240}, /* QPSK_1_4, */
		{21600,  5400}, /* QPSK_1_3, */
		{25920,  6480}, /* QPSK_2_5, */
		{32400,  7200}, /* QPSK_1_2, */
		{38880,  9720}, /* QPSK_3_5, */
		{43200, 10800}, /* QPSK_2_3, */
		{48600, 11880}, /* QPSK_3_4, */
		{51840, 12600}, /* QPSK_4_5, */
		{54000, 13320}, /* QPSK_5_6, */
		{57600, 14400}, /* QPSK_8_9, */
		{58320, 16000}, /* QPSK_9_10, */
		{43200,  9720}, /* 8PSK_3_5, */
		{48600, 10800}, /* 8PSK_2_3, */
		{51840, 11880}, /* 8PSK_3_4, */
		{54000, 13320}, /* 8PSK_5_6, */
		{57600, 14400}, /* 8PSK_8_9, */
		{58320, 16000}, /* 8PSK_9_10, */
		{43200, 10800}, /* 16APSK_2_3, */
		{48600, 11880}, /* 16APSK_3_4, */
		{51840, 12600}, /* 16APSK_4_5, */
		{54000, 13320}, /* 16APSK_5_6, */
		{57600, 14400}, /* 16APSK_8_9, */
		{58320, 16000}, /* 16APSK_9_10 */
		{48600, 11880}, /* 32APSK_3_4, */
		{51840, 12600}, /* 32APSK_4_5, */
		{54000, 13320}, /* 32APSK_5_6, */
		{57600, 14400}, /* 32APSK_8_9, */
		{58320, 16000}, /* 32APSK_9_10 */
	};

	if (mod_cod >= DVBS2_QPSK_1_4 &&
	    mod_cod <= DVBS2_32APSK_9_10 && fectype <= DVBS2_16K)
		return nbch[mod_cod][fectype];
	return 64800;
}

static int get_bit_error_rate_s2(struct stv *state, u32 *bernumerator,
			     u32 *berdenominator)
{
	u8 regs[3];

	int status = read_regs(state, RSTV0910_P2_ERRCNT12 + state->regoff,
			       regs, 3);

	if (status)
		return -EINVAL;

	if ((regs[0] & 0x80) == 0) {
		state->last_berdenominator =
			dvbs2_nbch((enum dvbs2_mod_cod) state->mod_cod,
				   state->fectype) <<
			(state->berscale * 2);
		state->last_bernumerator = (((u32) regs[0] & 0x7F) << 16) |
			((u32) regs[1] << 8) | regs[2];
		if (state->last_bernumerator < 256 && state->berscale < 6) {
			state->berscale += 1;
			write_reg(state, RSTV0910_P2_ERRCTRL1 + state->regoff,
				  0x20 | state->berscale);
		} else if (state->last_bernumerator > 1024 &&
			   state->berscale > 2) {
			state->berscale -= 1;
			write_reg(state, RSTV0910_P2_ERRCTRL1 + state->regoff,
				  0x20 | state->berscale);
		}
	}
	*bernumerator = state->last_bernumerator;
	*berdenominator = state->last_berdenominator;
	return status;
}

static int get_bit_error_rate(struct stv *state, u32 *bernumerator,
			   u32 *berdenominator)
{
	*bernumerator = 0;
	*berdenominator = 1;

	switch (state->receive_mode) {
	case RCVMODE_DVBS:
		return get_bit_error_rate_s(state,
					    bernumerator, berdenominator);
	case RCVMODE_DVBS2:
		return get_bit_error_rate_s2(state,
					     bernumerator, berdenominator);
	default:
		break;
	}
	return 0;
}

static int set_mclock(struct stv *state, u32 master_clock)
{
	u32 idf = 1;
	u32 odf = 4;
	u32 quartz = state->base->extclk / 1000000;
	u32 fphi = master_clock / 1000000;
	u32 ndiv = (fphi * odf * idf) / quartz;
	u32 cp = 7;
	u32 fvco;

	if (ndiv >= 7 && ndiv <= 71)
		cp = 7;
	else if (ndiv >=  72 && ndiv <=  79)
		cp = 8;
	else if (ndiv >=  80 && ndiv <=  87)
		cp = 9;
	else if (ndiv >=  88 && ndiv <=  95)
		cp = 10;
	else if (ndiv >=  96 && ndiv <= 103)
		cp = 11;
	else if (ndiv >= 104 && ndiv <= 111)
		cp = 12;
	else if (ndiv >= 112 && ndiv <= 119)
		cp = 13;
	else if (ndiv >= 120 && ndiv <= 127)
		cp = 14;
	else if (ndiv >= 128 && ndiv <= 135)
		cp = 15;
	else if (ndiv >= 136 && ndiv <= 143)
		cp = 16;
	else if (ndiv >= 144 && ndiv <= 151)
		cp = 17;
	else if (ndiv >= 152 && ndiv <= 159)
		cp = 18;
	else if (ndiv >= 160 && ndiv <= 167)
		cp = 19;
	else if (ndiv >= 168 && ndiv <= 175)
		cp = 20;
	else if (ndiv >= 176 && ndiv <= 183)
		cp = 21;
	else if (ndiv >= 184 && ndiv <= 191)
		cp = 22;
	else if (ndiv >= 192 && ndiv <= 199)
		cp = 23;
	else if (ndiv >= 200 && ndiv <= 207)
		cp = 24;
	else if (ndiv >= 208 && ndiv <= 215)
		cp = 25;
	else if (ndiv >= 216 && ndiv <= 223)
		cp = 26;
	else if (ndiv >= 224 && ndiv <= 225)
		cp = 27;

	write_reg(state, RSTV0910_NCOARSE, (cp << 3) | idf);
	write_reg(state, RSTV0910_NCOARSE2, odf);
	write_reg(state, RSTV0910_NCOARSE1, ndiv);

	fvco = (quartz * 2 * ndiv) / idf;
	state->base->mclk = fvco / (2 * odf) * 1000000;

	return 0;
}

static int stop(struct stv *state)
{
	if (state->started) {
		u8 tmp;

		write_reg(state, RSTV0910_P2_TSCFGH + state->regoff,
			  state->tscfgh | 0x01);
		read_reg(state, RSTV0910_P2_PDELCTRL1 + state->regoff, &tmp);
		tmp &= ~0x01; /*release reset DVBS2 packet delin*/
		write_reg(state, RSTV0910_P2_PDELCTRL1 + state->regoff, tmp);
		/* Blind optim*/
		write_reg(state, RSTV0910_P2_AGC2O + state->regoff, 0x5B);
		/* Stop the demod */
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x5c);
		state->started = 0;
	}
	state->receive_mode = RCVMODE_NONE;
	return 0;
}

static int init_search_param(struct stv *state)
{
	u8 tmp;

	read_reg(state, RSTV0910_P2_PDELCTRL1 + state->regoff, &tmp);
	tmp |= 0x20; // Filter_en (no effect if SIS=non-MIS
	write_reg(state, RSTV0910_P2_PDELCTRL1 + state->regoff, tmp);

	read_reg(state, RSTV0910_P2_PDELCTRL2 + state->regoff, &tmp);
	tmp &= ~0x02; // frame mode = 0
	write_reg(state, RSTV0910_P2_PDELCTRL2 + state->regoff, tmp);

	write_reg(state, RSTV0910_P2_UPLCCST0 + state->regoff, 0xe0);
	write_reg(state, RSTV0910_P2_ISIBITENA + state->regoff, 0x00);

	read_reg(state, RSTV0910_P2_TSSTATEM + state->regoff, &tmp);
	tmp &= ~0x01; // nosync = 0, in case next signal is standard TS
	write_reg(state, RSTV0910_P2_TSSTATEM + state->regoff, tmp);

	read_reg(state, RSTV0910_P2_TSCFGL + state->regoff, &tmp);
	tmp &= ~0x04; // embindvb = 0
	write_reg(state, RSTV0910_P2_TSCFGL + state->regoff, tmp);

	read_reg(state, RSTV0910_P2_TSINSDELH + state->regoff, &tmp);
	tmp &= ~0x80; // syncbyte = 0
	write_reg(state, RSTV0910_P2_TSINSDELH + state->regoff, tmp);

	read_reg(state, RSTV0910_P2_TSINSDELM + state->regoff, &tmp);
	tmp &= ~0x08; // token = 0
	write_reg(state, RSTV0910_P2_TSINSDELM + state->regoff, tmp);

	read_reg(state, RSTV0910_P2_TSDLYSET2 + state->regoff, &tmp);
	tmp &= ~0x30; // hysteresis threshold = 0
	write_reg(state, RSTV0910_P2_TSDLYSET2 + state->regoff, tmp);

	read_reg(state, RSTV0910_P2_PDELCTRL0 + state->regoff, &tmp);
	tmp = (tmp & ~0x30) | 0x10; // isi obs mode = 1, observe min ISI
	write_reg(state, RSTV0910_P2_PDELCTRL0 + state->regoff, tmp);

	return 0;
}

static int enable_puncture_rate(struct stv *state, enum fe_code_rate rate)
{
	switch (rate) {
	case FEC_1_2:
		return write_reg(state,
				 RSTV0910_P2_PRVIT + state->regoff, 0x01);
	case FEC_2_3:
		return write_reg(state,
				 RSTV0910_P2_PRVIT + state->regoff, 0x02);
	case FEC_3_4:
		return write_reg(state,
				 RSTV0910_P2_PRVIT + state->regoff, 0x04);
	case FEC_5_6:
		return write_reg(state,
				 RSTV0910_P2_PRVIT + state->regoff, 0x08);
	case FEC_7_8:
		return write_reg(state,
				 RSTV0910_P2_PRVIT + state->regoff, 0x20);
	case FEC_NONE:
	default:
		return write_reg(state,
				 RSTV0910_P2_PRVIT + state->regoff, 0x2f);
	}
}

static int set_vth_default(struct stv *state)
{
	state->vth[0] = 0xd7;
	state->vth[1] = 0x85;
	state->vth[2] = 0x58;
	state->vth[3] = 0x3a;
	state->vth[4] = 0x34;
	state->vth[5] = 0x28;
	write_reg(state, RSTV0910_P2_VTH12 + state->regoff + 0, state->vth[0]);
	write_reg(state, RSTV0910_P2_VTH12 + state->regoff + 1, state->vth[1]);
	write_reg(state, RSTV0910_P2_VTH12 + state->regoff + 2, state->vth[2]);
	write_reg(state, RSTV0910_P2_VTH12 + state->regoff + 3, state->vth[3]);
	write_reg(state, RSTV0910_P2_VTH12 + state->regoff + 4, state->vth[4]);
	write_reg(state, RSTV0910_P2_VTH12 + state->regoff + 5, state->vth[5]);
	return 0;
}

static int set_vth(struct stv *state)
{
	static struct slookup vthlookup_table[] = {
		{250,	8780}, /*C/N=1.5dB*/
		{100,	7405}, /*C/N=4.5dB*/
		{40,	6330}, /*C/N=6.5dB*/
		{12,	5224}, /*C/N=8.5dB*/
		{5,	4236} /*C/N=10.5dB*/
	};

	int i;
	u8 tmp[2];
	int status = read_regs(state,
			       RSTV0910_P2_NNOSDATAT1 + state->regoff,
			       tmp, 2);
	u16 reg_value = (tmp[0] << 8) | tmp[1];
	s32 vth = table_lookup(vthlookup_table, ARRAY_SIZE(vthlookup_table),
			      reg_value);

	for (i = 0; i < 6; i += 1)
		if (state->vth[i] > vth)
			state->vth[i] = vth;

	write_reg(state, RSTV0910_P2_VTH12 + state->regoff + 0, state->vth[0]);
	write_reg(state, RSTV0910_P2_VTH12 + state->regoff + 1, state->vth[1]);
	write_reg(state, RSTV0910_P2_VTH12 + state->regoff + 2, state->vth[2]);
	write_reg(state, RSTV0910_P2_VTH12 + state->regoff + 3, state->vth[3]);
	write_reg(state, RSTV0910_P2_VTH12 + state->regoff + 4, state->vth[4]);
	write_reg(state, RSTV0910_P2_VTH12 + state->regoff + 5, state->vth[5]);
	return status;
}

static int start(struct stv *state, struct dtv_frontend_properties *p)
{
	s32 freq;
	u8  reg_dmdcfgmd;
	u16 symb;
	u32 scrambling_code = 1;

	if (p->symbol_rate < 100000 || p->symbol_rate > 70000000)
		return -EINVAL;

	state->receive_mode = RCVMODE_NONE;
	state->demod_lock_time = 0;

	/* Demod Stop */
	if (state->started)
		write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x5C);

	init_search_param(state);

	if (p->stream_id != NO_STREAM_ID_FILTER) {
		/* Backwards compatibility to "crazy" API.
		 * PRBS X root cannot be 0, so this should always work.
		 */
		if (p->stream_id & 0xffffff00)
			scrambling_code = p->stream_id >> 8;
		write_reg(state, RSTV0910_P2_ISIENTRY + state->regoff,
			  p->stream_id & 0xff);
		write_reg(state, RSTV0910_P2_ISIBITENA + state->regoff,
			  0xff);
	}

	if (scrambling_code != state->cur_scrambling_code) {
		write_reg(state, RSTV0910_P2_PLROOT0 + state->regoff,
			  scrambling_code & 0xff);
		write_reg(state, RSTV0910_P2_PLROOT1 + state->regoff,
			  (scrambling_code >> 8) & 0xff);
		write_reg(state, RSTV0910_P2_PLROOT2 + state->regoff,
			  (scrambling_code >> 16) & 0x07);
		state->cur_scrambling_code = scrambling_code;
	}

	if (p->symbol_rate <= 1000000) {  /* SR <=1Msps */
		state->demod_timeout = 3000;
		state->fec_timeout = 2000;
	} else if (p->symbol_rate <= 2000000) {  /* 1Msps < SR <=2Msps */
		state->demod_timeout = 2500;
		state->fec_timeout = 1300;
	} else if (p->symbol_rate <= 5000000) {  /* 2Msps< SR <=5Msps */
		state->demod_timeout = 1000;
		state->fec_timeout = 650;
	} else if (p->symbol_rate <= 10000000) {  /* 5Msps< SR <=10Msps */
		state->demod_timeout = 700;
		state->fec_timeout = 350;
	} else if (p->symbol_rate < 20000000) {  /* 10Msps< SR <=20Msps */
		state->demod_timeout = 400;
		state->fec_timeout = 200;
	} else {  /* SR >=20Msps */
		state->demod_timeout = 300;
		state->fec_timeout = 200;
	}

	/* Set the Init Symbol rate */
	symb = muldiv32(p->symbol_rate, 65536, state->base->mclk);
	write_reg(state, RSTV0910_P2_SFRINIT1 + state->regoff,
		  ((symb >> 8) & 0x7F));
	write_reg(state, RSTV0910_P2_SFRINIT0 + state->regoff, (symb & 0xFF));

	state->demod_bits |= 0x80;
	write_reg(state, RSTV0910_P2_DEMOD + state->regoff, state->demod_bits);

	/* FE_STV0910_SetSearchStandard */
	read_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff, &reg_dmdcfgmd);
	write_reg(state, RSTV0910_P2_DMDCFGMD + state->regoff,
		  reg_dmdcfgmd |= 0xC0);

	write_shared_reg(state,
			 RSTV0910_TSTTSRS, state->nr ? 0x02 : 0x01, 0x00);

	/* Disable DSS */
	write_reg(state, RSTV0910_P2_FECM  + state->regoff, 0x00);
	write_reg(state, RSTV0910_P2_PRVIT + state->regoff, 0x2F);

	enable_puncture_rate(state, FEC_NONE);

	/* 8PSK 3/5, 8PSK 2/3 Poff tracking optimization WA*/
	write_reg(state, RSTV0910_P2_ACLC2S2Q + state->regoff, 0x0B);
	write_reg(state, RSTV0910_P2_ACLC2S28 + state->regoff, 0x0A);
	write_reg(state, RSTV0910_P2_BCLC2S2Q + state->regoff, 0x84);
	write_reg(state, RSTV0910_P2_BCLC2S28 + state->regoff, 0x84);
	write_reg(state, RSTV0910_P2_CARHDR + state->regoff, 0x1C);
	write_reg(state, RSTV0910_P2_CARFREQ + state->regoff, 0x79);

	write_reg(state, RSTV0910_P2_ACLC2S216A + state->regoff, 0x29);
	write_reg(state, RSTV0910_P2_ACLC2S232A + state->regoff, 0x09);
	write_reg(state, RSTV0910_P2_BCLC2S216A + state->regoff, 0x84);
	write_reg(state, RSTV0910_P2_BCLC2S232A + state->regoff, 0x84);

	/* Reset CAR3, bug DVBS2->DVBS1 lock*/
	/* Note: The bit is only pulsed -> no lock on shared register needed */
	write_reg(state, RSTV0910_TSTRES0, state->nr ? 0x04 : 0x08);
	write_reg(state, RSTV0910_TSTRES0, 0);

	set_vth_default(state);
	/* Reset demod */
	write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x1F);

	write_reg(state, RSTV0910_P2_CARCFG + state->regoff, 0x46);

	if (p->symbol_rate <= 5000000)
		freq = (state->search_range / 2000) + 80;
	else
		freq = (state->search_range / 2000) + 1600;
	freq = (freq << 16) / (state->base->mclk / 1000);

	write_reg(state, RSTV0910_P2_CFRUP1 + state->regoff,
		  (freq >> 8) & 0xff);
	write_reg(state, RSTV0910_P2_CFRUP0 + state->regoff, (freq & 0xff));
	/*CFR Low Setting*/
	freq = -freq;
	write_reg(state, RSTV0910_P2_CFRLOW1 + state->regoff,
		  (freq >> 8) & 0xff);
	write_reg(state, RSTV0910_P2_CFRLOW0 + state->regoff, (freq & 0xff));

	/* init the demod frequency offset to 0 */
	write_reg(state, RSTV0910_P2_CFRINIT1 + state->regoff, 0);
	write_reg(state, RSTV0910_P2_CFRINIT0 + state->regoff, 0);

	write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x1F);
	/* Trigger acq */
	write_reg(state, RSTV0910_P2_DMDISTATE + state->regoff, 0x15);

	state->demod_lock_time += TUNING_DELAY;
	state->started = 1;

	return 0;
}

static int init_diseqc(struct stv *state)
{
	u16 offs = state->nr ? 0x40 : 0;  /* Address offset */
	u8 freq = ((state->base->mclk + 11000 * 32) / (22000 * 32));

	/* Disable receiver */
	write_reg(state, RSTV0910_P1_DISRXCFG + offs, 0x00);
	write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0xBA); /* Reset = 1 */
	write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x3A); /* Reset = 0 */
	write_reg(state, RSTV0910_P1_DISTXF22 + offs, freq);
	return 0;
}

static int probe(struct stv *state)
{
	u8 id;

	state->receive_mode = RCVMODE_NONE;
	state->started = 0;

	if (read_reg(state, RSTV0910_MID, &id) < 0)
		return -ENODEV;

	if (id != 0x51)
		return -EINVAL;

	 /* Configure the I2C repeater to off */
	write_reg(state, RSTV0910_P1_I2CRPT, 0x24);
	/* Configure the I2C repeater to off */
	write_reg(state, RSTV0910_P2_I2CRPT, 0x24);
	/* Set the I2C to oversampling ratio */
	write_reg(state, RSTV0910_I2CCFG, 0x88); /* state->i2ccfg */

	write_reg(state, RSTV0910_OUTCFG,    0x00);  /* OUTCFG */
	write_reg(state, RSTV0910_PADCFG,    0x05);  /* RFAGC Pads Dev = 05 */
	write_reg(state, RSTV0910_SYNTCTRL,  0x02);  /* SYNTCTRL */
	write_reg(state, RSTV0910_TSGENERAL, state->tsgeneral);  /* TSGENERAL */
	write_reg(state, RSTV0910_CFGEXT,    0x02);  /* CFGEXT */

	if (state->single)
		write_reg(state, RSTV0910_GENCFG, 0x14);  /* GENCFG */
	else
		write_reg(state, RSTV0910_GENCFG, 0x15);  /* GENCFG */

	write_reg(state, RSTV0910_P1_TNRCFG2, 0x02);  /* IQSWAP = 0 */
	write_reg(state, RSTV0910_P2_TNRCFG2, 0x82);  /* IQSWAP = 1 */

	write_reg(state, RSTV0910_P1_CAR3CFG, 0x02);
	write_reg(state, RSTV0910_P2_CAR3CFG, 0x02);
	write_reg(state, RSTV0910_P1_DMDCFG4, 0x04);
	write_reg(state, RSTV0910_P2_DMDCFG4, 0x04);

	write_reg(state, RSTV0910_TSTRES0, 0x80); /* LDPC Reset */
	write_reg(state, RSTV0910_TSTRES0, 0x00);

	write_reg(state, RSTV0910_P1_TSPIDFLT1, 0x00);
	write_reg(state, RSTV0910_P2_TSPIDFLT1, 0x00);

	write_reg(state, RSTV0910_P1_TMGCFG2, 0x80);
	write_reg(state, RSTV0910_P2_TMGCFG2, 0x80);

	set_mclock(state, 135000000);

	/* TS output */
	write_reg(state, RSTV0910_P1_TSCFGH, state->tscfgh | 0x01);
	write_reg(state, RSTV0910_P1_TSCFGH, state->tscfgh);
	write_reg(state, RSTV0910_P1_TSCFGM, 0xC0);  /* Manual speed */
	write_reg(state, RSTV0910_P1_TSCFGL, 0x20);

	/* Speed = 67.5 MHz */
	write_reg(state, RSTV0910_P1_TSSPEED, state->tsspeed);

	write_reg(state, RSTV0910_P2_TSCFGH, state->tscfgh | 0x01);
	write_reg(state, RSTV0910_P2_TSCFGH, state->tscfgh);
	write_reg(state, RSTV0910_P2_TSCFGM, 0xC0);  /* Manual speed */
	write_reg(state, RSTV0910_P2_TSCFGL, 0x20);

	/* Speed = 67.5 MHz */
	write_reg(state, RSTV0910_P2_TSSPEED, state->tsspeed);

	/* Reset stream merger */
	write_reg(state, RSTV0910_P1_TSCFGH, state->tscfgh | 0x01);
	write_reg(state, RSTV0910_P2_TSCFGH, state->tscfgh | 0x01);
	write_reg(state, RSTV0910_P1_TSCFGH, state->tscfgh);
	write_reg(state, RSTV0910_P2_TSCFGH, state->tscfgh);

	write_reg(state, RSTV0910_P1_I2CRPT, state->i2crpt);
	write_reg(state, RSTV0910_P2_I2CRPT, state->i2crpt);

	init_diseqc(state);
	return 0;
}


static int gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct stv *state = fe->demodulator_priv;
	u8 i2crpt = state->i2crpt & ~0x86;

	if (enable)
		mutex_lock(&state->base->i2c_lock);

	if (enable)
		i2crpt |= 0x80;
	else
		i2crpt |= 0x02;

	if (write_reg(state, state->nr ? RSTV0910_P2_I2CRPT :
		      RSTV0910_P1_I2CRPT, i2crpt) < 0)
		return -EIO;

	state->i2crpt = i2crpt;

	if (!enable)
		mutex_unlock(&state->base->i2c_lock);
	return 0;
}

static void release(struct dvb_frontend *fe)
{
	struct stv *state = fe->demodulator_priv;

	state->base->count--;
	if (state->base->count == 0) {
		list_del(&state->base->stvlist);
		kfree(state->base);
	}
	kfree(state);
}

static int set_parameters(struct dvb_frontend *fe)
{
	int stat = 0;
	struct stv *state = fe->demodulator_priv;
	u32 iffreq;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	stop(state);
	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);
	if (fe->ops.tuner_ops.get_if_frequency)
		fe->ops.tuner_ops.get_if_frequency(fe, &iffreq);
	state->symbol_rate = p->symbol_rate;
	stat = start(state, p);
	return stat;
}

static int manage_matype_info(struct stv *state)
{
	if (!state->started)
		return -EINVAL;
	if (state->receive_mode == RCVMODE_DVBS2) {
		u8 bbheader[2];

		read_regs(state, RSTV0910_P2_MATSTR1 + state->regoff,
			bbheader, 2);
		state->feroll_off =
			(enum fe_stv0910_roll_off) (bbheader[0] & 0x03);
		state->is_vcm = (bbheader[0] & 0x10) == 0;
		state->is_standard_broadcast = (bbheader[0] & 0xFC) == 0xF0;
	} else if (state->receive_mode == RCVMODE_DVBS) {
		state->is_vcm = 0;
		state->is_standard_broadcast = 1;
		state->feroll_off = FE_SAT_35;
	}
	return 0;
}

static int read_snr(struct dvb_frontend *fe)
{
	struct stv *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	s32 snrval;

	if (!get_signal_to_noise(state, &snrval)) {
		p->cnr.stat[0].scale = FE_SCALE_DECIBEL;
		p->cnr.stat[0].uvalue = 100 * snrval; /* fix scale */
	} else
		p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	return 0;
}

static int read_ber(struct dvb_frontend *fe)
{
	struct stv *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 n, d;

	get_bit_error_rate(state, &n, &d);

	p->pre_bit_error.stat[0].scale = FE_SCALE_COUNTER;
	p->pre_bit_error.stat[0].uvalue = n;
	p->pre_bit_count.stat[0].scale = FE_SCALE_COUNTER;
	p->pre_bit_count.stat[0].uvalue = d;

	return 0;
}

static void read_signal_strength(struct dvb_frontend *fe)
{
	struct stv *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &state->fe.dtv_property_cache;
	s64 strength;
	u8 reg[2];
	u16 agc;
	s32 padc, power = 0;
	int i;

	read_regs(state, RSTV0910_P2_AGCIQIN1 + state->regoff, reg, 2);

	agc = (((u32) reg[0]) << 8) | reg[1];

	for (i = 0; i < 5; i += 1) {
		read_regs(state, RSTV0910_P2_POWERI + state->regoff, reg, 2);
		power += (u32) reg[0] * (u32) reg[0]
			+ (u32) reg[1] * (u32) reg[1];
		usleep_range(3000, 4000);
	}
	power /= 5;

	padc = table_lookup(padc_lookup, ARRAY_SIZE(padc_lookup), power) + 352;

	strength = (padc - agc);

	p->strength.stat[0].scale = FE_SCALE_DECIBEL;
	p->strength.stat[0].uvalue = strength;
}

static int read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct stv *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u8 dmd_state = 0;
	u8 dstatus  = 0;
	enum receive_mode cur_receive_mode = RCVMODE_NONE;
	u32 feclock = 0;

	*status = 0;

	read_reg(state, RSTV0910_P2_DMDSTATE + state->regoff, &dmd_state);

	if (dmd_state & 0x40) {
		read_reg(state, RSTV0910_P2_DSTATUS + state->regoff, &dstatus);
		if (dstatus & 0x08)
			cur_receive_mode = (dmd_state & 0x20) ?
				RCVMODE_DVBS : RCVMODE_DVBS2;
	}
	if (cur_receive_mode == RCVMODE_NONE) {
		set_vth(state);

		/* reset signal statistics */
		p->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		p->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		p->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

		return 0;
	}

	*status |= (FE_HAS_SIGNAL
		| FE_HAS_CARRIER
		| FE_HAS_VITERBI
		| FE_HAS_SYNC);

	if (state->receive_mode == RCVMODE_NONE) {
		state->receive_mode = cur_receive_mode;
		state->demod_lock_time = jiffies;
		state->first_time_lock = 1;

		get_signal_parameters(state);
		tracking_optimization(state);

		write_reg(state, RSTV0910_P2_TSCFGH + state->regoff,
			  state->tscfgh);
		usleep_range(3000, 4000);
		write_reg(state, RSTV0910_P2_TSCFGH + state->regoff,
			  state->tscfgh | 0x01);
		write_reg(state, RSTV0910_P2_TSCFGH + state->regoff,
			  state->tscfgh);
	}
	if (dmd_state & 0x40) {
		if (state->receive_mode == RCVMODE_DVBS2) {
			u8 pdelstatus;

			read_reg(state,
				 RSTV0910_P2_PDELSTATUS1 + state->regoff,
				 &pdelstatus);
			feclock = (pdelstatus & 0x02) != 0;
		} else {
			u8 vstatus;

			read_reg(state,
				 RSTV0910_P2_VSTATUSVIT + state->regoff,
				 &vstatus);
			feclock = (vstatus & 0x08) != 0;
		}
	}

	if (feclock) {
		*status |= FE_HAS_LOCK;

		if (state->first_time_lock) {
			u8 tmp;

			state->first_time_lock = 0;

			manage_matype_info(state);

			if (state->receive_mode == RCVMODE_DVBS2) {
				/* FSTV0910_P2_MANUALSX_ROLLOFF,
				 * FSTV0910_P2_MANUALS2_ROLLOFF = 0
				 */
				state->demod_bits &= ~0x84;
				write_reg(state,
					  RSTV0910_P2_DEMOD + state->regoff,
					  state->demod_bits);
				read_reg(state,
					 RSTV0910_P2_PDELCTRL2 + state->regoff,
					 &tmp);
				/*reset DVBS2 packet delinator error counter */
				tmp |= 0x40;
				write_reg(state,
					  RSTV0910_P2_PDELCTRL2 + state->regoff,
					  tmp);
				/*reset DVBS2 packet delinator error counter */
				tmp &= ~0x40;
				write_reg(state,
					  RSTV0910_P2_PDELCTRL2 + state->regoff,
					  tmp);

				state->berscale = 2;
				state->last_bernumerator = 0;
				state->last_berdenominator = 1;
				/* force to PRE BCH Rate */
				write_reg(state,
					  RSTV0910_P2_ERRCTRL1 + state->regoff,
					  BER_SRC_S2 | state->berscale);
			} else {
				state->berscale = 2;
				state->last_bernumerator = 0;
				state->last_berdenominator = 1;
				/* force to PRE RS Rate */
				write_reg(state,
					  RSTV0910_P2_ERRCTRL1 + state->regoff,
					  BER_SRC_S | state->berscale);
			}
			/*Reset the Total packet counter */
			write_reg(state,
				  RSTV0910_P2_FBERCPT4 + state->regoff, 0x00);
			/* Reset the packet Error counter2 (and Set it to
			 * infinit error count mode )
			 */
			write_reg(state,
				  RSTV0910_P2_ERRCTRL2 + state->regoff, 0xc1);

			set_vth_default(state);
			if (state->receive_mode == RCVMODE_DVBS)
				enable_puncture_rate(state,
						     state->puncture_rate);
		}
	}

	/* read signal statistics */

	/* read signal strength */
	read_signal_strength(fe);

	/* read carrier/noise on FE_HAS_CARRIER */
	if (*status & FE_HAS_CARRIER)
		read_snr(fe);
	else
		p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	/* read ber */
	if (*status & FE_HAS_VITERBI)
		read_ber(fe);
	else {
		p->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		p->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	return 0;
}

static int get_frontend(struct dvb_frontend *fe,
			struct dtv_frontend_properties *p)
{
	struct stv *state = fe->demodulator_priv;
	u8 tmp;

	if (state->receive_mode == RCVMODE_DVBS2) {
		u32 mc;
		enum fe_modulation modcod2mod[0x20] = {
			QPSK, QPSK, QPSK, QPSK,
			QPSK, QPSK, QPSK, QPSK,
			QPSK, QPSK, QPSK, QPSK,
			PSK_8, PSK_8, PSK_8, PSK_8,
			PSK_8, PSK_8, APSK_16, APSK_16,
			APSK_16, APSK_16, APSK_16, APSK_16,
			APSK_32, APSK_32, APSK_32, APSK_32,
			APSK_32,
		};
		enum fe_code_rate modcod2fec[0x20] = {
			FEC_NONE, FEC_NONE, FEC_NONE, FEC_2_5,
			FEC_1_2, FEC_3_5, FEC_2_3, FEC_3_4,
			FEC_4_5, FEC_5_6, FEC_8_9, FEC_9_10,
			FEC_3_5, FEC_2_3, FEC_3_4, FEC_5_6,
			FEC_8_9, FEC_9_10, FEC_2_3, FEC_3_4,
			FEC_4_5, FEC_5_6, FEC_8_9, FEC_9_10,
			FEC_3_4, FEC_4_5, FEC_5_6, FEC_8_9,
			FEC_9_10
		};
		read_reg(state, RSTV0910_P2_DMDMODCOD + state->regoff, &tmp);
		mc = ((tmp & 0x7c) >> 2);
		p->pilot = (tmp & 0x01) ? PILOT_ON : PILOT_OFF;
		p->modulation = modcod2mod[mc];
		p->fec_inner = modcod2fec[mc];
	} else if (state->receive_mode == RCVMODE_DVBS) {
		read_reg(state, RSTV0910_P2_VITCURPUN + state->regoff, &tmp);
		switch (tmp & 0x1F) {
		case 0x0d:
			p->fec_inner = FEC_1_2;
			break;
		case 0x12:
			p->fec_inner = FEC_2_3;
			break;
		case 0x15:
			p->fec_inner = FEC_3_4;
			break;
		case 0x18:
			p->fec_inner = FEC_5_6;
			break;
		case 0x1a:
			p->fec_inner = FEC_7_8;
			break;
		default:
			p->fec_inner = FEC_NONE;
			break;
		}
		p->rolloff = ROLLOFF_35;
	}

	return 0;
}

static int tune(struct dvb_frontend *fe, bool re_tune,
		unsigned int mode_flags,
		unsigned int *delay, enum fe_status *status)
{
	struct stv *state = fe->demodulator_priv;
	int r;

	if (re_tune) {
		r = set_parameters(fe);
		if (r)
			return r;
		state->tune_time = jiffies;
	}
	if (*status & FE_HAS_LOCK)
		return 0;
	*delay = HZ;

	r = read_status(fe, status);
	if (r)
		return r;
	return 0;
}


static int get_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static int set_tone(struct dvb_frontend *fe, enum fe_sec_tone_mode tone)
{
	struct stv *state = fe->demodulator_priv;
	u16 offs = state->nr ? 0x40 : 0;

	switch (tone) {
	case SEC_TONE_ON:
		return write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x38);
	case SEC_TONE_OFF:
		return write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x3a);
	default:
		break;
	}
	return -EINVAL;
}

static int wait_dis(struct stv *state, u8 flag, u8 val)
{
	int i;
	u8 stat;
	u16 offs = state->nr ? 0x40 : 0;

	for (i = 0; i < 10; i++) {
		read_reg(state, RSTV0910_P1_DISTXSTATUS + offs, &stat);
		if ((stat & flag) == val)
			return 0;
		usleep_range(10000, 11000);
	}
	return -ETIMEDOUT;
}

static int send_master_cmd(struct dvb_frontend *fe,
			   struct dvb_diseqc_master_cmd *cmd)
{
	struct stv *state = fe->demodulator_priv;
	u16 offs = state->nr ? 0x40 : 0;
	int i;

	write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x3E);
	for (i = 0; i < cmd->msg_len; i++) {
		wait_dis(state, 0x40, 0x00);
		write_reg(state, RSTV0910_P1_DISTXFIFO + offs, cmd->msg[i]);
	}
	write_reg(state, RSTV0910_P1_DISTXCFG + offs, 0x3A);
	wait_dis(state, 0x20, 0x20);
	return 0;
}

static int sleep(struct dvb_frontend *fe)
{
	struct stv *state = fe->demodulator_priv;

	stop(state);
	return 0;
}

static struct dvb_frontend_ops stv0910_ops = {
	.delsys = { SYS_DVBS, SYS_DVBS2, SYS_DSS },
	.info = {
		.name			= "STV0910",
		.frequency_min		= 950000,
		.frequency_max		= 2150000,
		.frequency_stepsize	= 0,
		.frequency_tolerance	= 0,
		.symbol_rate_min	= 1000000,
		.symbol_rate_max	= 70000000,
		.caps			= FE_CAN_INVERSION_AUTO |
					  FE_CAN_FEC_AUTO       |
					  FE_CAN_QPSK           |
					  FE_CAN_2G_MODULATION  |
					  FE_CAN_MULTISTREAM
	},
	.sleep				= sleep,
	.release                        = release,
	.i2c_gate_ctrl                  = gate_ctrl,
	.get_frontend_algo              = get_algo,
	.get_frontend                   = get_frontend,
	.tune                           = tune,
	.read_status			= read_status,
	.set_tone			= set_tone,

	.diseqc_send_master_cmd		= send_master_cmd,
};

static struct stv_base *match_base(struct i2c_adapter  *i2c, u8 adr)
{
	struct stv_base *p;

	list_for_each_entry(p, &stvlist, stvlist)
		if (p->i2c == i2c && p->adr == adr)
			return p;
	return NULL;
}

static void stv0910_init_stats(struct stv *state)
{
	struct dtv_frontend_properties *p = &state->fe.dtv_property_cache;

	p->strength.len = 1;
	p->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->cnr.len = 1;
	p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->pre_bit_error.len = 1;
	p->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->pre_bit_count.len = 1;
	p->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
}

struct dvb_frontend *stv0910_attach(struct i2c_adapter *i2c,
				    struct stv0910_cfg *cfg,
				    int nr)
{
	struct stv *state;
	struct stv_base *base;

	state = kzalloc(sizeof(struct stv), GFP_KERNEL);
	if (!state)
		return NULL;

	state->tscfgh = 0x20 | (cfg->parallel ? 0 : 0x40);
	state->tsgeneral = (cfg->parallel == 2) ? 0x02 : 0x00;
	state->i2crpt = 0x0A | ((cfg->rptlvl & 0x07) << 4);
	state->tsspeed = 0x28;
	state->nr = nr;
	state->regoff = state->nr ? 0 : 0x200;
	state->search_range = 16000000;
	state->demod_bits = 0x10;     /* Inversion : Auto with reset to 0 */
	state->receive_mode   = RCVMODE_NONE;
	state->cur_scrambling_code = (~0U);
	state->single = cfg->single ? 1 : 0;

	base = match_base(i2c, cfg->adr);
	if (base) {
		base->count++;
		state->base = base;
	} else {
		base = kzalloc(sizeof(struct stv_base), GFP_KERNEL);
		if (!base)
			goto fail;
		base->i2c = i2c;
		base->adr = cfg->adr;
		base->count = 1;
		base->extclk = cfg->clk ? cfg->clk : 30000000;

		mutex_init(&base->i2c_lock);
		mutex_init(&base->reg_lock);
		state->base = base;
		if (probe(state) < 0) {
			dev_info(&i2c->dev, "No demod found at adr %02X on %s\n",
				cfg->adr, dev_name(&i2c->dev));
			kfree(base);
			goto fail;
		}
		list_add(&base->stvlist, &stvlist);
	}
	state->fe.ops               = stv0910_ops;
	state->fe.demodulator_priv  = state;
	state->nr = nr;

	dev_info(&i2c->dev, "%s demod found at adr %02X on %s\n",
		state->fe.ops.info.name, cfg->adr, dev_name(&i2c->dev));

	stv0910_init_stats(state);

	return &state->fe;

fail:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL_GPL(stv0910_attach);

MODULE_DESCRIPTION("ST STV0910 multistandard frontend driver");
MODULE_AUTHOR("Ralph and Marcus Metzler, Manfred Voelkel");
MODULE_LICENSE("GPL");
