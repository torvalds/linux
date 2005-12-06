/*

   i2c tv tuner chip device driver
   controls the philips tda8290+75 tuner chip combo.

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
#include <linux/videodev.h>
#include <linux/delay.h>
#include <media/tuner.h>

/* ---------------------------------------------------------------------- */

struct tda827x_data {
	u32 lomax;
	u8  spd;
	u8  bs;
	u8  bp;
	u8  cp;
	u8  gc3;
	u8 div1p5;
};

     /* Note lomax entry is lo / 62500 */

static struct tda827x_data tda827x_analog[] = {
	{ .lomax =   992, .spd = 3, .bs = 2, .bp = 0, .cp = 0, .gc3 = 3, .div1p5 = 1}, /*  62 MHz */
	{ .lomax =  1056, .spd = 3, .bs = 3, .bp = 0, .cp = 0, .gc3 = 3, .div1p5 = 1}, /*  66 MHz */
	{ .lomax =  1216, .spd = 3, .bs = 1, .bp = 0, .cp = 0, .gc3 = 3, .div1p5 = 0}, /*  76 MHz */
	{ .lomax =  1344, .spd = 3, .bs = 2, .bp = 0, .cp = 0, .gc3 = 3, .div1p5 = 0}, /*  84 MHz */
	{ .lomax =  1488, .spd = 3, .bs = 2, .bp = 0, .cp = 0, .gc3 = 1, .div1p5 = 0}, /*  93 MHz */
	{ .lomax =  1568, .spd = 3, .bs = 3, .bp = 0, .cp = 0, .gc3 = 1, .div1p5 = 0}, /*  98 MHz */
	{ .lomax =  1744, .spd = 3, .bs = 3, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 0}, /* 109 MHz */
	{ .lomax =  1968, .spd = 2, .bs = 2, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 1}, /* 123 MHz */
	{ .lomax =  2128, .spd = 2, .bs = 3, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 1}, /* 133 MHz */
	{ .lomax =  2416, .spd = 2, .bs = 1, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 0}, /* 151 MHz */
	{ .lomax =  2464, .spd = 2, .bs = 2, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 0}, /* 154 MHz */
	{ .lomax =  2896, .spd = 2, .bs = 2, .bp = 1, .cp = 0, .gc3 = 0, .div1p5 = 0}, /* 181 MHz */
	{ .lomax =  2960, .spd = 2, .bs = 2, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 0}, /* 185 MHz */
	{ .lomax =  3472, .spd = 2, .bs = 3, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 0}, /* 217 MHz */
	{ .lomax =  3904, .spd = 1, .bs = 2, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 1}, /* 244 MHz */
	{ .lomax =  4240, .spd = 1, .bs = 3, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 1}, /* 265 MHz */
	{ .lomax =  4832, .spd = 1, .bs = 1, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 0}, /* 302 MHz */
	{ .lomax =  5184, .spd = 1, .bs = 2, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 0}, /* 324 MHz */
	{ .lomax =  5920, .spd = 1, .bs = 2, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 0}, /* 370 MHz */
	{ .lomax =  7264, .spd = 1, .bs = 3, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 0}, /* 454 MHz */
	{ .lomax =  7888, .spd = 0, .bs = 2, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 1}, /* 493 MHz */
	{ .lomax =  8480, .spd = 0, .bs = 3, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 1}, /* 530 MHz */
	{ .lomax =  8864, .spd = 0, .bs = 1, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 0}, /* 554 MHz */
	{ .lomax =  9664, .spd = 0, .bs = 1, .bp = 4, .cp = 0, .gc3 = 0, .div1p5 = 0}, /* 604 MHz */
	{ .lomax = 11088, .spd = 0, .bs = 2, .bp = 4, .cp = 0, .gc3 = 0, .div1p5 = 0}, /* 696 MHz */
	{ .lomax = 11840, .spd = 0, .bs = 2, .bp = 4, .cp = 1, .gc3 = 0, .div1p5 = 0}, /* 740 MHz */
	{ .lomax = 13120, .spd = 0, .bs = 3, .bp = 4, .cp = 0, .gc3 = 0, .div1p5 = 0}, /* 820 MHz */
	{ .lomax = 13840, .spd = 0, .bs = 3, .bp = 4, .cp = 1, .gc3 = 0, .div1p5 = 0}, /* 865 MHz */
	{ .lomax =     0, .spd = 0, .bs = 0, .bp = 0, .cp = 0, .gc3 = 0, .div1p5 = 0}  /* End      */
};

static void tda827x_tune(struct i2c_client *c, u16 ifc, unsigned int freq)
{
	unsigned char tuner_reg[8];
	unsigned char reg2[2];
	u32 N;
	int i;
	struct tuner *t = i2c_get_clientdata(c);
	struct i2c_msg msg = {.addr = t->tda827x_addr, .flags = 0};

	if (t->mode == V4L2_TUNER_RADIO)
		freq = freq / 1000;

	N = freq + ifc;
	i = 0;
	while (tda827x_analog[i].lomax < N) {
		if(tda827x_analog[i + 1].lomax == 0)
			break;
		i++;
	}

	N = N << tda827x_analog[i].spd;

	tuner_reg[0] = 0;
	tuner_reg[1] = (unsigned char)(N>>8);
	tuner_reg[2] = (unsigned char) N;
	tuner_reg[3] = 0x40;
	tuner_reg[4] = 0x52 + (t->tda827x_lpsel << 5);
	tuner_reg[5] = (tda827x_analog[i].spd   << 6) + (tda827x_analog[i].div1p5 <<5) +
		       (tda827x_analog[i].bs     <<3) +  tda827x_analog[i].bp;
	tuner_reg[6] = 0x8f + (tda827x_analog[i].gc3 << 4);
	tuner_reg[7] = 0x8f;

	msg.buf = tuner_reg;
	msg.len = 8;
	i2c_transfer(c->adapter, &msg, 1);

	msg.buf= reg2;
	msg.len = 2;
	reg2[0] = 0x80;
	reg2[1] = 0;
	i2c_transfer(c->adapter, &msg, 1);

	reg2[0] = 0x60;
	reg2[1] = 0xbf;
	i2c_transfer(c->adapter, &msg, 1);

	reg2[0] = 0x30;
	reg2[1] = tuner_reg[4] + 0x80;
	i2c_transfer(c->adapter, &msg, 1);

	msleep(1);
	reg2[0] = 0x30;
	reg2[1] = tuner_reg[4] + 4;
	i2c_transfer(c->adapter, &msg, 1);

	msleep(1);
	reg2[0] = 0x30;
	reg2[1] = tuner_reg[4];
	i2c_transfer(c->adapter, &msg, 1);

	msleep(550);
	reg2[0] = 0x30;
	reg2[1] = (tuner_reg[4] & 0xfc) + tda827x_analog[i].cp ;
	i2c_transfer(c->adapter, &msg, 1);

	reg2[0] = 0x60;
	reg2[1] = 0x3f;
	i2c_transfer(c->adapter, &msg, 1);

	reg2[0] = 0x80;
	reg2[1] = 0x08;   // Vsync en
	i2c_transfer(c->adapter, &msg, 1);
}

static void tda827x_agcf(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);
	unsigned char data[] = {0x80, 0x0c};
	struct i2c_msg msg = {.addr = t->tda827x_addr, .buf = data,
			      .flags = 0, .len = 2};
	i2c_transfer(c->adapter, &msg, 1);
}

/* ---------------------------------------------------------------------- */

struct tda827xa_data {
	u32 lomax;
	u8  svco;
	u8  spd;
	u8  scr;
	u8  sbs;
	u8  gc3;
};

static struct tda827xa_data tda827xa_analog[] = {
	{ .lomax =   910, .svco = 3, .spd = 4, .scr = 0, .sbs = 0, .gc3 = 3},  /*  56.875 MHz */
	{ .lomax =  1076, .svco = 0, .spd = 3, .scr = 0, .sbs = 0, .gc3 = 3},  /*  67.25 MHz */
	{ .lomax =  1300, .svco = 1, .spd = 3, .scr = 0, .sbs = 0, .gc3 = 3},  /*  81.25 MHz */
	{ .lomax =  1560, .svco = 2, .spd = 3, .scr = 0, .sbs = 0, .gc3 = 3},  /*  97.5  MHz */
	{ .lomax =  1820, .svco = 3, .spd = 3, .scr = 0, .sbs = 1, .gc3 = 1},  /* 113.75 MHz */
	{ .lomax =  2152, .svco = 0, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},  /* 134.5 MHz */
	{ .lomax =  2464, .svco = 1, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},  /* 154   MHz */
	{ .lomax =  2600, .svco = 1, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},  /* 162.5 MHz */
	{ .lomax =  2928, .svco = 2, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},  /* 183   MHz */
	{ .lomax =  3120, .svco = 2, .spd = 2, .scr = 0, .sbs = 2, .gc3 = 1},  /* 195   MHz */
	{ .lomax =  3640, .svco = 3, .spd = 2, .scr = 0, .sbs = 2, .gc3 = 3},  /* 227.5 MHz */
	{ .lomax =  4304, .svco = 0, .spd = 1, .scr = 0, .sbs = 2, .gc3 = 3},  /* 269   MHz */
	{ .lomax =  5200, .svco = 1, .spd = 1, .scr = 0, .sbs = 2, .gc3 = 1},  /* 325   MHz */
	{ .lomax =  6240, .svco = 2, .spd = 1, .scr = 0, .sbs = 3, .gc3 = 3},  /* 390   MHz */
	{ .lomax =  7280, .svco = 3, .spd = 1, .scr = 0, .sbs = 3, .gc3 = 3},  /* 455   MHz */
	{ .lomax =  8320, .svco = 0, .spd = 0, .scr = 0, .sbs = 3, .gc3 = 1},  /* 520   MHz */
	{ .lomax =  8608, .svco = 0, .spd = 0, .scr = 1, .sbs = 3, .gc3 = 1},  /* 538   MHz */
	{ .lomax =  8864, .svco = 1, .spd = 0, .scr = 0, .sbs = 3, .gc3 = 1},  /* 554   MHz */
	{ .lomax =  9920, .svco = 1, .spd = 0, .scr = 0, .sbs = 4, .gc3 = 0},  /* 620   MHz */
	{ .lomax = 10400, .svco = 1, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 0},  /* 650   MHz */
	{ .lomax = 11200, .svco = 2, .spd = 0, .scr = 0, .sbs = 4, .gc3 = 0},  /* 700   MHz */
	{ .lomax = 12480, .svco = 2, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 0},  /* 780   MHz */
	{ .lomax = 13120, .svco = 3, .spd = 0, .scr = 0, .sbs = 4, .gc3 = 0},  /* 820   MHz */
	{ .lomax = 13920, .svco = 3, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 0},  /* 870   MHz */
	{ .lomax = 14576, .svco = 3, .spd = 0, .scr = 2, .sbs = 4, .gc3 = 0},  /* 911   MHz */
	{ .lomax =     0, .svco = 0, .spd = 0, .scr = 0, .sbs = 0, .gc3 = 0}   /* End */
};

static void tda827xa_tune(struct i2c_client *c, u16 ifc, unsigned int freq)
{
	unsigned char tuner_reg[14];
	unsigned char reg2[2];
	u32 N;
	int i;
	struct tuner *t = i2c_get_clientdata(c);
	struct i2c_msg msg = {.addr = t->tda827x_addr, .flags = 0};

	if (t->mode == V4L2_TUNER_RADIO)
		freq = freq / 1000;

	N = freq + ifc;
	i = 0;
	while (tda827xa_analog[i].lomax < N) {
		if(tda827xa_analog[i + 1].lomax == 0)
			break;
		i++;
	}

	N = N << tda827xa_analog[i].spd;

	tuner_reg[0] = 0;
	tuner_reg[1] = (unsigned char)(N>>8);
	tuner_reg[2] = (unsigned char) N;
	tuner_reg[3] = 0;
	tuner_reg[4] = 0x16;
	tuner_reg[5] = (tda827xa_analog[i].spd << 5) + (tda827xa_analog[i].svco << 3) +
			tda827xa_analog[i].sbs;
	tuner_reg[6] = 0x8b + (tda827xa_analog[i].gc3 << 4);
	tuner_reg[7] = 0x0c;
	tuner_reg[8] = 4;
	tuner_reg[9] = 0x20;
	tuner_reg[10] = 0xff;
	tuner_reg[11] = 0xe0;
	tuner_reg[12] = 0;
	tuner_reg[13] = 0x39 + (t->tda827x_lpsel << 1);

	msg.buf = tuner_reg;
	msg.len = 14;
	i2c_transfer(c->adapter, &msg, 1);

	msg.buf= reg2;
	msg.len = 2;
	reg2[0] = 0x60;
	reg2[1] = 0x3c;
	i2c_transfer(c->adapter, &msg, 1);

	reg2[0] = 0xa0;
	reg2[1] = 0xc0;
	i2c_transfer(c->adapter, &msg, 1);

	msleep(2);
	reg2[0] = 0x30;
	reg2[1] = 0x10 + tda827xa_analog[i].scr;
	i2c_transfer(c->adapter, &msg, 1);

	msleep(550);
	reg2[0] = 0x50;
	reg2[1] = 0x8f + (tda827xa_analog[i].gc3 << 4);
	i2c_transfer(c->adapter, &msg, 1);

	reg2[0] = 0x80;
	reg2[1] = 0x28;
	i2c_transfer(c->adapter, &msg, 1);

	reg2[0] = 0xb0;
	reg2[1] = 0x01;
	i2c_transfer(c->adapter, &msg, 1);

	reg2[0] = 0xc0;
	reg2[1] = 0x19 + (t->tda827x_lpsel << 1);
	i2c_transfer(c->adapter, &msg, 1);
}

static void tda827xa_agcf(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);
	unsigned char data[] = {0x80, 0x2c};
	struct i2c_msg msg = {.addr = t->tda827x_addr, .buf = data,
			      .flags = 0, .len = 2};
	i2c_transfer(c->adapter, &msg, 1);
}

/*---------------------------------------------------------------------*/

static void tda8290_i2c_bridge(struct i2c_client *c, int close)
{
	unsigned char  enable[2] = { 0x21, 0xC0 };
	unsigned char disable[2] = { 0x21, 0x80 };
	unsigned char *msg;
	if(close) {
		msg = enable;
		i2c_master_send(c, msg, 2);
		/* let the bridge stabilize */
		msleep(20);
	} else {
		msg = disable;
		i2c_master_send(c, msg, 2);
	}
}

/*---------------------------------------------------------------------*/

static int tda8290_tune(struct i2c_client *c, u16 ifc, unsigned int freq)
{
	struct tuner *t = i2c_get_clientdata(c);
	unsigned char soft_reset[]  = { 0x00, 0x00 };
	unsigned char easy_mode[]   = { 0x01, t->tda8290_easy_mode };
	unsigned char expert_mode[] = { 0x01, 0x80 };
	unsigned char gainset_off[] = { 0x28, 0x14 };
	unsigned char if_agc_spd[]  = { 0x0f, 0x88 };
	unsigned char adc_head_6[]  = { 0x05, 0x04 };
	unsigned char adc_head_9[]  = { 0x05, 0x02 };
	unsigned char adc_head_12[] = { 0x05, 0x01 };
	unsigned char pll_bw_nom[]  = { 0x0d, 0x47 };
	unsigned char pll_bw_low[]  = { 0x0d, 0x27 };
	unsigned char gainset_2[]   = { 0x28, 0x64 };
	unsigned char agc_rst_on[]  = { 0x0e, 0x0b };
	unsigned char agc_rst_off[] = { 0x0e, 0x09 };
	unsigned char if_agc_set[]  = { 0x0f, 0x81 };
	unsigned char addr_adc_sat  = 0x1a;
	unsigned char addr_agc_stat = 0x1d;
	unsigned char addr_pll_stat = 0x1b;
	unsigned char adc_sat, agc_stat,
		      pll_stat;

	i2c_master_send(c, easy_mode, 2);
	i2c_master_send(c, soft_reset, 2);
	msleep(1);

	expert_mode[1] = t->tda8290_easy_mode + 0x80;
	i2c_master_send(c, expert_mode, 2);
	i2c_master_send(c, gainset_off, 2);
	i2c_master_send(c, if_agc_spd, 2);
	if (t->tda8290_easy_mode & 0x60)
		i2c_master_send(c, adc_head_9, 2);
	else
		i2c_master_send(c, adc_head_6, 2);
	i2c_master_send(c, pll_bw_nom, 2);

	tda8290_i2c_bridge(c, 1);
	if (t->tda827x_ver != 0)
		tda827xa_tune(c, ifc, freq);
	else
		tda827x_tune(c, ifc, freq);
	/* adjust headroom resp. gain */
	i2c_master_send(c, &addr_adc_sat, 1);
	i2c_master_recv(c, &adc_sat, 1);
	i2c_master_send(c, &addr_agc_stat, 1);
	i2c_master_recv(c, &agc_stat, 1);
	i2c_master_send(c, &addr_pll_stat, 1);
	i2c_master_recv(c, &pll_stat, 1);
	if (pll_stat & 0x80)
		tuner_dbg("tda8290 is locked, AGC: %d\n", agc_stat);
	else
		tuner_dbg("tda8290 not locked, no signal?\n");
	if ((agc_stat > 115) || (!(pll_stat & 0x80) && (adc_sat < 20))) {
		tuner_dbg("adjust gain, step 1. Agc: %d, ADC stat: %d, lock: %d\n",
			   agc_stat, adc_sat, pll_stat & 0x80);
		i2c_master_send(c, gainset_2, 2);
		msleep(100);
		i2c_master_send(c, &addr_agc_stat, 1);
		i2c_master_recv(c, &agc_stat, 1);
		i2c_master_send(c, &addr_pll_stat, 1);
		i2c_master_recv(c, &pll_stat, 1);
		if ((agc_stat > 115) || !(pll_stat & 0x80)) {
			tuner_dbg("adjust gain, step 2. Agc: %d, lock: %d\n",
				   agc_stat, pll_stat & 0x80);
			if (t->tda827x_ver != 0)
				tda827xa_agcf(c);
			else
				tda827x_agcf(c);
			msleep(100);
			i2c_master_send(c, &addr_agc_stat, 1);
			i2c_master_recv(c, &agc_stat, 1);
			i2c_master_send(c, &addr_pll_stat, 1);
			i2c_master_recv(c, &pll_stat, 1);
			if((agc_stat > 115) || !(pll_stat & 0x80)) {
				tuner_dbg("adjust gain, step 3. Agc: %d\n", agc_stat);
				i2c_master_send(c, adc_head_12, 2);
				i2c_master_send(c, pll_bw_low, 2);
				msleep(100);
			}
		}
	}

	/* l/ l' deadlock? */
	if(t->tda8290_easy_mode & 0x60) {
		i2c_master_send(c, &addr_adc_sat, 1);
		i2c_master_recv(c, &adc_sat, 1);
		i2c_master_send(c, &addr_pll_stat, 1);
		i2c_master_recv(c, &pll_stat, 1);
		if ((adc_sat > 20) || !(pll_stat & 0x80)) {
			tuner_dbg("trying to resolve SECAM L deadlock\n");
			i2c_master_send(c, agc_rst_on, 2);
			msleep(40);
			i2c_master_send(c, agc_rst_off, 2);
		}
	}

	tda8290_i2c_bridge(c, 0);
	i2c_master_send(c, if_agc_set, 2);
	return 0;
}


/*---------------------------------------------------------------------*/

#define V4L2_STD_MN	(V4L2_STD_PAL_M|V4L2_STD_PAL_N|V4L2_STD_PAL_Nc|V4L2_STD_NTSC)
#define V4L2_STD_B	(V4L2_STD_PAL_B|V4L2_STD_PAL_B1|V4L2_STD_SECAM_B)
#define V4L2_STD_GH	(V4L2_STD_PAL_G|V4L2_STD_PAL_H|V4L2_STD_SECAM_G|V4L2_STD_SECAM_H)
#define V4L2_STD_DK	(V4L2_STD_PAL_DK|V4L2_STD_SECAM_DK)

static void set_audio(struct tuner *t)
{
	char* mode;

	t->tda827x_lpsel = 0;
	mode = "xx";
	if (t->std & V4L2_STD_MN) {
		t->sgIF = 92;
		t->tda8290_easy_mode = 0x01;
		t->tda827x_lpsel = 1;
		mode = "MN";
	} else if (t->std & V4L2_STD_B) {
		t->sgIF = 108;
		t->tda8290_easy_mode = 0x02;
		mode = "B";
	} else if (t->std & V4L2_STD_GH) {
		t->sgIF = 124;
		t->tda8290_easy_mode = 0x04;
		mode = "GH";
	} else if (t->std & V4L2_STD_PAL_I) {
		t->sgIF = 124;
		t->tda8290_easy_mode = 0x08;
		mode = "I";
	} else if (t->std & V4L2_STD_DK) {
		t->sgIF = 124;
		t->tda8290_easy_mode = 0x10;
		mode = "DK";
	} else if (t->std & V4L2_STD_SECAM_L) {
		t->sgIF = 124;
		t->tda8290_easy_mode = 0x20;
		mode = "L";
	} else if (t->std & V4L2_STD_SECAM_LC) {
		t->sgIF = 20;
		t->tda8290_easy_mode = 0x40;
		mode = "LC";
	}
    tuner_dbg("setting tda8290 to system %s\n", mode);
}

static void set_tv_freq(struct i2c_client *c, unsigned int freq)
{
	struct tuner *t = i2c_get_clientdata(c);

	set_audio(t);
	tda8290_tune(c, t->sgIF, freq);
}

static void set_radio_freq(struct i2c_client *c, unsigned int freq)
{
	/* if frequency is 5.5 MHz */
	tda8290_tune(c, 88, freq);
}

static int has_signal(struct i2c_client *c)
{
	unsigned char i2c_get_afc[1] = { 0x1B };
	unsigned char afc = 0;

	i2c_master_send(c, i2c_get_afc, ARRAY_SIZE(i2c_get_afc));
	i2c_master_recv(c, &afc, 1);
	return (afc & 0x80)? 65535:0;
}

/*---------------------------------------------------------------------*/

static void standby(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);
	unsigned char cb1[] = { 0x30, 0xD0 };
	unsigned char tda8290_standby[] = { 0x00, 0x02 };
	struct i2c_msg msg = {.addr = t->tda827x_addr, .flags=0, .buf=cb1, .len = 2};

	tda8290_i2c_bridge(c, 1);
	if (t->tda827x_ver != 0)
		cb1[1] = 0x90;
	i2c_transfer(c->adapter, &msg, 1);
	tda8290_i2c_bridge(c, 0);
	i2c_master_send(c, tda8290_standby, 2);
}


static void tda8290_init_if(struct i2c_client *c)
{
	unsigned char set_VS[] = { 0x30, 0x6F };
	unsigned char set_GP01_CF[] = { 0x20, 0x0B };

	i2c_master_send(c, set_VS, 2);
	i2c_master_send(c, set_GP01_CF, 2);
}

static void tda8290_init_tuner(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);
	unsigned char tda8275_init[]  = { 0x00, 0x00, 0x00, 0x40, 0xdC, 0x04, 0xAf,
					  0x3F, 0x2A, 0x04, 0xFF, 0x00, 0x00, 0x40 };
	unsigned char tda8275a_init[] = { 0x00, 0x00, 0x00, 0x00, 0xdC, 0x05, 0x8b,
					  0x0c, 0x04, 0x20, 0xFF, 0x00, 0x00, 0x4b };
	struct i2c_msg msg = {.addr = t->tda827x_addr, .flags=0,
			      .buf=tda8275_init, .len = 14};
	if (t->tda827x_ver != 0)
		msg.buf = tda8275a_init;

	tda8290_i2c_bridge(c, 1);
	i2c_transfer(c->adapter, &msg, 1);
	tda8290_i2c_bridge(c, 0);
}

/*---------------------------------------------------------------------*/

int tda8290_init(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);
	u8 data;
	int i, ret, tuners_found;
	u32 tuner_addrs;
	struct i2c_msg msg = {.flags=I2C_M_RD, .buf=&data, .len = 1};

	tda8290_i2c_bridge(c, 1);
	/* probe for tuner chip */
	tuners_found = 0;
	tuner_addrs = 0;
	for (i=0x60; i<= 0x63; i++) {
		msg.addr = i;
		ret = i2c_transfer(c->adapter, &msg, 1);
		if (ret == 1) {
			tuners_found++;
			tuner_addrs = (tuner_addrs << 8) + i;
		}
	}
	/* if there is more than one tuner, we expect the right one is
	   behind the bridge and we choose the highest address that doesn't
	   give a response now
	 */
	tda8290_i2c_bridge(c, 0);
	if(tuners_found > 1)
		for (i = 0; i < tuners_found; i++) {
			msg.addr = tuner_addrs  & 0xff;
			ret = i2c_transfer(c->adapter, &msg, 1);
			if(ret == 1)
				tuner_addrs = tuner_addrs >> 8;
			else
				break;
		}
	if (tuner_addrs == 0) {
		tuner_addrs = 0x61;
		tuner_info ("could not clearly identify tuner address, defaulting to %x\n",
			     tuner_addrs);
	} else {
		tuner_addrs = tuner_addrs & 0xff;
		tuner_info ("setting tuner address to %x\n", tuner_addrs);
	}
	t->tda827x_addr = tuner_addrs;
	msg.addr = tuner_addrs;

	tda8290_i2c_bridge(c, 1);
	ret = i2c_transfer(c->adapter, &msg, 1);
	if( ret != 1)
		tuner_warn ("TDA827x access failed!\n");
	if ((data & 0x3c) == 0) {
		strlcpy(c->name, "tda8290+75", sizeof(c->name));
		t->tda827x_ver = 0;
	} else {
		strlcpy(c->name, "tda8290+75a", sizeof(c->name));
		t->tda827x_ver = 2;
	}
	tuner_info("tuner: type set to %s\n", c->name);

	t->tv_freq    = set_tv_freq;
	t->radio_freq = set_radio_freq;
	t->has_signal = has_signal;
	t->standby = standby;
	t->tda827x_lpsel = 0;

	tda8290_init_tuner(c);
	tda8290_init_if(c);
	return 0;
}

int tda8290_probe(struct i2c_client *c)
{
	unsigned char soft_reset[]  = { 0x00, 0x00 };
	unsigned char easy_mode_b[] = { 0x01, 0x02 };
	unsigned char easy_mode_g[] = { 0x01, 0x04 };
	unsigned char addr_dto_lsb = 0x07;
	unsigned char data;

	i2c_master_send(c, easy_mode_b, 2);
	i2c_master_send(c, soft_reset, 2);
	i2c_master_send(c, &addr_dto_lsb, 1);
	i2c_master_recv(c, &data, 1);
	if (data == 0) {
		i2c_master_send(c, easy_mode_g, 2);
		i2c_master_send(c, soft_reset, 2);
		i2c_master_send(c, &addr_dto_lsb, 1);
		i2c_master_recv(c, &data, 1);
		if (data == 0x7b) {
			return 0;
		}
	}
	return -1;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
