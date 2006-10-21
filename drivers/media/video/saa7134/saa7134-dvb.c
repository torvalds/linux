/*
 *
 * (c) 2004 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  Extended 3 / 2005 by Hartmut Hackmann to support various
 *  cards with the tda10046 DVB-T channel decoder
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

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/suspend.h>

#include "saa7134-reg.h"
#include "saa7134.h"
#include <media/v4l2-common.h>
#include "dvb-pll.h"

#include "mt352.h"
#include "mt352_priv.h" /* FIXME */
#include "tda1004x.h"
#include "nxt200x.h"

#include "tda10086.h"
#include "tda826x.h"
#include "isl6421.h"
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

static unsigned int antenna_pwr = 0;

module_param(antenna_pwr, int, 0444);
MODULE_PARM_DESC(antenna_pwr,"enable antenna power (Pinnacle 300i)");

/* ------------------------------------------------------------------ */
static int pinnacle_antenna_pwr(struct saa7134_dev *dev, int on)
{
	u32 ok;

	if (!on) {
		saa_setl(SAA7134_GPIO_GPMODE0 >> 2,     (1 << 26));
		saa_clearl(SAA7134_GPIO_GPSTATUS0 >> 2, (1 << 26));
		return 0;
	}

	saa_setl(SAA7134_GPIO_GPMODE0 >> 2,     (1 << 26));
	saa_setl(SAA7134_GPIO_GPSTATUS0 >> 2,   (1 << 26));
	udelay(10);

	saa_setl(SAA7134_GPIO_GPMODE0 >> 2,     (1 << 28));
	saa_clearl(SAA7134_GPIO_GPSTATUS0 >> 2, (1 << 28));
	udelay(10);
	saa_setl(SAA7134_GPIO_GPSTATUS0 >> 2,   (1 << 28));
	udelay(10);
	ok = saa_readl(SAA7134_GPIO_GPSTATUS0) & (1 << 27);
	printk("%s: %s %s\n", dev->name, __FUNCTION__,
	       ok ? "on" : "off");

	if (!ok)
		saa_clearl(SAA7134_GPIO_GPSTATUS0 >> 2,   (1 << 26));
	return ok;
}

static int mt352_pinnacle_init(struct dvb_frontend* fe)
{
	static u8 clock_config []  = { CLOCK_CTL,  0x3d, 0x28 };
	static u8 reset []         = { RESET,      0x80 };
	static u8 adc_ctl_1_cfg [] = { ADC_CTL_1,  0x40 };
	static u8 agc_cfg []       = { AGC_TARGET, 0x28, 0xa0 };
	static u8 capt_range_cfg[] = { CAPT_RANGE, 0x31 };
	static u8 fsm_ctl_cfg[]    = { 0x7b,       0x04 };
	static u8 gpp_ctl_cfg []   = { GPP_CTL,    0x0f };
	static u8 scan_ctl_cfg []  = { SCAN_CTL,   0x0d };
	static u8 irq_cfg []       = { INTERRUPT_EN_0, 0x00, 0x00, 0x00, 0x00 };
	struct saa7134_dev *dev= fe->dvb->priv;

	printk("%s: %s called\n",dev->name,__FUNCTION__);

	mt352_write(fe, clock_config,   sizeof(clock_config));
	udelay(200);
	mt352_write(fe, reset,          sizeof(reset));
	mt352_write(fe, adc_ctl_1_cfg,  sizeof(adc_ctl_1_cfg));
	mt352_write(fe, agc_cfg,        sizeof(agc_cfg));
	mt352_write(fe, capt_range_cfg, sizeof(capt_range_cfg));
	mt352_write(fe, gpp_ctl_cfg,    sizeof(gpp_ctl_cfg));

	mt352_write(fe, fsm_ctl_cfg,    sizeof(fsm_ctl_cfg));
	mt352_write(fe, scan_ctl_cfg,   sizeof(scan_ctl_cfg));
	mt352_write(fe, irq_cfg,        sizeof(irq_cfg));

	return 0;
}

static int mt352_aver777_init(struct dvb_frontend* fe)
{
	static u8 clock_config []  = { CLOCK_CTL,  0x38, 0x2d };
	static u8 reset []         = { RESET,      0x80 };
	static u8 adc_ctl_1_cfg [] = { ADC_CTL_1,  0x40 };
	static u8 agc_cfg []       = { AGC_TARGET, 0x28, 0xa0 };
	static u8 capt_range_cfg[] = { CAPT_RANGE, 0x33 };

	mt352_write(fe, clock_config,   sizeof(clock_config));
	udelay(200);
	mt352_write(fe, reset,          sizeof(reset));
	mt352_write(fe, adc_ctl_1_cfg,  sizeof(adc_ctl_1_cfg));
	mt352_write(fe, agc_cfg,        sizeof(agc_cfg));
	mt352_write(fe, capt_range_cfg, sizeof(capt_range_cfg));

	return 0;
}

static int mt352_pinnacle_tuner_set_params(struct dvb_frontend* fe,
					   struct dvb_frontend_parameters* params)
{
	u8 off[] = { 0x00, 0xf1};
	u8 on[]  = { 0x00, 0x71};
	struct i2c_msg msg = {.addr=0x43, .flags=0, .buf=off, .len = sizeof(off)};

	struct saa7134_dev *dev = fe->dvb->priv;
	struct v4l2_frequency f;

	/* set frequency (mt2050) */
	f.tuner     = 0;
	f.type      = V4L2_TUNER_DIGITAL_TV;
	f.frequency = params->frequency / 1000 * 16 / 1000;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(&dev->i2c_adap, &msg, 1);
	saa7134_i2c_call_clients(dev,VIDIOC_S_FREQUENCY,&f);
	msg.buf = on;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(&dev->i2c_adap, &msg, 1);

	pinnacle_antenna_pwr(dev, antenna_pwr);

	/* mt352 setup */
	return mt352_pinnacle_init(fe);
}

static int mt352_aver777_tuner_calc_regs(struct dvb_frontend *fe, struct dvb_frontend_parameters *params, u8* pllbuf, int buf_len)
{
	if (buf_len < 5)
		return -EINVAL;

	pllbuf[0] = 0x61;
	dvb_pll_configure(&dvb_pll_philips_td1316, pllbuf+1,
			  params->frequency,
			  params->u.ofdm.bandwidth);
	return 5;
}

static struct mt352_config pinnacle_300i = {
	.demod_address = 0x3c >> 1,
	.adc_clock     = 20333,
	.if2           = 36150,
	.no_tuner      = 1,
	.demod_init    = mt352_pinnacle_init,
};

static struct mt352_config avermedia_777 = {
	.demod_address = 0xf,
	.demod_init    = mt352_aver777_init,
};

/* ------------------------------------------------------------------ */
static int philips_tda6651_pll_set(u8 addr, struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	u8 tuner_buf[4];
	struct i2c_msg tuner_msg = {.addr = addr,.flags = 0,.buf = tuner_buf,.len =
			sizeof(tuner_buf) };
	int tuner_frequency = 0;
	u8 band, cp, filter;

	/* determine charge pump */
	tuner_frequency = params->frequency + 36166000;
	if (tuner_frequency < 87000000)
		return -EINVAL;
	else if (tuner_frequency < 130000000)
		cp = 3;
	else if (tuner_frequency < 160000000)
		cp = 5;
	else if (tuner_frequency < 200000000)
		cp = 6;
	else if (tuner_frequency < 290000000)
		cp = 3;
	else if (tuner_frequency < 420000000)
		cp = 5;
	else if (tuner_frequency < 480000000)
		cp = 6;
	else if (tuner_frequency < 620000000)
		cp = 3;
	else if (tuner_frequency < 830000000)
		cp = 5;
	else if (tuner_frequency < 895000000)
		cp = 7;
	else
		return -EINVAL;

	/* determine band */
	if (params->frequency < 49000000)
		return -EINVAL;
	else if (params->frequency < 161000000)
		band = 1;
	else if (params->frequency < 444000000)
		band = 2;
	else if (params->frequency < 861000000)
		band = 4;
	else
		return -EINVAL;

	/* setup PLL filter */
	switch (params->u.ofdm.bandwidth) {
	case BANDWIDTH_6_MHZ:
		filter = 0;
		break;

	case BANDWIDTH_7_MHZ:
		filter = 0;
		break;

	case BANDWIDTH_8_MHZ:
		filter = 1;
		break;

	default:
		return -EINVAL;
	}

	/* calculate divisor
	 * ((36166000+((1000000/6)/2)) + Finput)/(1000000/6)
	 */
	tuner_frequency = (((params->frequency / 1000) * 6) + 217496) / 1000;

	/* setup tuner buffer */
	tuner_buf[0] = (tuner_frequency >> 8) & 0x7f;
	tuner_buf[1] = tuner_frequency & 0xff;
	tuner_buf[2] = 0xca;
	tuner_buf[3] = (cp << 5) | (filter << 3) | band;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&dev->i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;
	msleep(1);
	return 0;
}

static int philips_tda6651_pll_init(u8 addr, struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	static u8 tu1216_init[] = { 0x0b, 0xf5, 0x85, 0xab };
	struct i2c_msg tuner_msg = {.addr = addr,.flags = 0,.buf = tu1216_init,.len = sizeof(tu1216_init) };

	/* setup PLL configuration */
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&dev->i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;
	msleep(1);

	return 0;
}

/* ------------------------------------------------------------------ */

static int philips_tu1216_tuner_60_init(struct dvb_frontend *fe)
{
	return philips_tda6651_pll_init(0x60, fe);
}

static int philips_tu1216_tuner_60_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	return philips_tda6651_pll_set(0x60, fe, params);
}

static int philips_tu1216_request_firmware(struct dvb_frontend *fe,
					   const struct firmware **fw, char *name)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	return request_firmware(fw, name, &dev->pci->dev);
}

static struct tda1004x_config philips_tu1216_60_config = {

	.demod_address = 0x8,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_4M,
	.agc_config    = TDA10046_AGC_DEFAULT,
	.if_freq       = TDA10046_FREQ_3617,
	.request_firmware = philips_tu1216_request_firmware,
};

/* ------------------------------------------------------------------ */

static int philips_tu1216_tuner_61_init(struct dvb_frontend *fe)
{
	return philips_tda6651_pll_init(0x61, fe);
}

static int philips_tu1216_tuner_61_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	return philips_tda6651_pll_set(0x61, fe, params);
}

static struct tda1004x_config philips_tu1216_61_config = {

	.demod_address = 0x8,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_4M,
	.agc_config    = TDA10046_AGC_DEFAULT,
	.if_freq       = TDA10046_FREQ_3617,
	.request_firmware = philips_tu1216_request_firmware,
};

/* ------------------------------------------------------------------ */

static int philips_europa_tuner_init(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	static u8 msg[] = { 0x0b, 0xf5, 0x86, 0xab };
	struct i2c_msg init_msg = {.addr = 0x61,.flags = 0,.buf = msg,.len = sizeof(msg) };

	/* setup PLL configuration */
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&dev->i2c_adap, &init_msg, 1) != 1)
		return -EIO;
	msleep(1);

	/* switch the board to dvb mode */
	init_msg.addr = 0x43;
	init_msg.len  = 0x02;
	msg[0] = 0x00;
	msg[1] = 0x40;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&dev->i2c_adap, &init_msg, 1) != 1)
		return -EIO;

	return 0;
}

static int philips_td1316_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	return philips_tda6651_pll_set(0x61, fe, params);
}

static int philips_europa_tuner_sleep(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	/* this message actually turns the tuner back to analog mode */
	static u8 msg[] = { 0x0b, 0xdc, 0x86, 0xa4 };
	struct i2c_msg analog_msg = {.addr = 0x61,.flags = 0,.buf = msg,.len = sizeof(msg) };

	i2c_transfer(&dev->i2c_adap, &analog_msg, 1);
	msleep(1);

	/* switch the board to analog mode */
	analog_msg.addr = 0x43;
	analog_msg.len  = 0x02;
	msg[0] = 0x00;
	msg[1] = 0x14;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(&dev->i2c_adap, &analog_msg, 1);
	return 0;
}

static int philips_europa_demod_sleep(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;

	if (dev->original_demod_sleep)
		dev->original_demod_sleep(fe);
	fe->ops.i2c_gate_ctrl(fe, 1);
	return 0;
}

static struct tda1004x_config philips_europa_config = {

	.demod_address = 0x8,
	.invert        = 0,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_4M,
	.agc_config    = TDA10046_AGC_IFO_AUTO_POS,
	.if_freq       = TDA10046_FREQ_052,
	.request_firmware = NULL,
};

/* ------------------------------------------------------------------ */

static int philips_fmd1216_tuner_init(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	/* this message is to set up ATC and ALC */
	static u8 fmd1216_init[] = { 0x0b, 0xdc, 0x9c, 0xa0 };
	struct i2c_msg tuner_msg = {.addr = 0x61,.flags = 0,.buf = fmd1216_init,.len = sizeof(fmd1216_init) };

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&dev->i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;
	msleep(1);

	return 0;
}

static int philips_fmd1216_tuner_sleep(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	/* this message actually turns the tuner back to analog mode */
	static u8 fmd1216_init[] = { 0x0b, 0xdc, 0x9c, 0x60 };
	struct i2c_msg tuner_msg = {.addr = 0x61,.flags = 0,.buf = fmd1216_init,.len = sizeof(fmd1216_init) };

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(&dev->i2c_adap, &tuner_msg, 1);
	msleep(1);
	fmd1216_init[2] = 0x86;
	fmd1216_init[3] = 0x54;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(&dev->i2c_adap, &tuner_msg, 1);
	msleep(1);
	return 0;
}

static int philips_fmd1216_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	u8 tuner_buf[4];
	struct i2c_msg tuner_msg = {.addr = 0x61,.flags = 0,.buf = tuner_buf,.len =
			sizeof(tuner_buf) };
	int tuner_frequency = 0;
	int divider = 0;
	u8 band, mode, cp;

	/* determine charge pump */
	tuner_frequency = params->frequency + 36130000;
	if (tuner_frequency < 87000000)
		return -EINVAL;
	/* low band */
	else if (tuner_frequency < 180000000) {
		band = 1;
		mode = 7;
		cp   = 0;
	} else if (tuner_frequency < 195000000) {
		band = 1;
		mode = 6;
		cp   = 1;
	/* mid band	*/
	} else if (tuner_frequency < 366000000) {
		if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ) {
			band = 10;
		} else {
			band = 2;
		}
		mode = 7;
		cp   = 0;
	} else if (tuner_frequency < 478000000) {
		if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ) {
			band = 10;
		} else {
			band = 2;
		}
		mode = 6;
		cp   = 1;
	/* high band */
	} else if (tuner_frequency < 662000000) {
		if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ) {
			band = 12;
		} else {
			band = 4;
		}
		mode = 7;
		cp   = 0;
	} else if (tuner_frequency < 840000000) {
		if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ) {
			band = 12;
		} else {
			band = 4;
		}
		mode = 6;
		cp   = 1;
	} else {
		if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ) {
			band = 12;
		} else {
			band = 4;
		}
		mode = 7;
		cp   = 1;

	}
	/* calculate divisor */
	/* ((36166000 + Finput) / 166666) rounded! */
	divider = (tuner_frequency + 83333) / 166667;

	/* setup tuner buffer */
	tuner_buf[0] = (divider >> 8) & 0x7f;
	tuner_buf[1] = divider & 0xff;
	tuner_buf[2] = 0x80 | (cp << 6) | (mode  << 3) | 4;
	tuner_buf[3] = 0x40 | band;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&dev->i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct tda1004x_config medion_cardbus = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_IFO_AUTO_NEG,
	.if_freq       = TDA10046_FREQ_3613,
	.request_firmware = NULL,
};

/* ------------------------------------------------------------------ */

struct tda827x_data {
	u32 lomax;
	u8  spd;
	u8  bs;
	u8  bp;
	u8  cp;
	u8  gc3;
	u8 div1p5;
};

static struct tda827x_data tda827x_dvbt[] = {
	{ .lomax =  62000000, .spd = 3, .bs = 2, .bp = 0, .cp = 0, .gc3 = 3, .div1p5 = 1},
	{ .lomax =  66000000, .spd = 3, .bs = 3, .bp = 0, .cp = 0, .gc3 = 3, .div1p5 = 1},
	{ .lomax =  76000000, .spd = 3, .bs = 1, .bp = 0, .cp = 0, .gc3 = 3, .div1p5 = 0},
	{ .lomax =  84000000, .spd = 3, .bs = 2, .bp = 0, .cp = 0, .gc3 = 3, .div1p5 = 0},
	{ .lomax =  93000000, .spd = 3, .bs = 2, .bp = 0, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax =  98000000, .spd = 3, .bs = 3, .bp = 0, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 109000000, .spd = 3, .bs = 3, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 123000000, .spd = 2, .bs = 2, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 1},
	{ .lomax = 133000000, .spd = 2, .bs = 3, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 1},
	{ .lomax = 151000000, .spd = 2, .bs = 1, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 154000000, .spd = 2, .bs = 2, .bp = 1, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 181000000, .spd = 2, .bs = 2, .bp = 1, .cp = 0, .gc3 = 0, .div1p5 = 0},
	{ .lomax = 185000000, .spd = 2, .bs = 2, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 217000000, .spd = 2, .bs = 3, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 244000000, .spd = 1, .bs = 2, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 1},
	{ .lomax = 265000000, .spd = 1, .bs = 3, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 1},
	{ .lomax = 302000000, .spd = 1, .bs = 1, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 324000000, .spd = 1, .bs = 2, .bp = 2, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 370000000, .spd = 1, .bs = 2, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 454000000, .spd = 1, .bs = 3, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 493000000, .spd = 0, .bs = 2, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 1},
	{ .lomax = 530000000, .spd = 0, .bs = 3, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 1},
	{ .lomax = 554000000, .spd = 0, .bs = 1, .bp = 3, .cp = 0, .gc3 = 1, .div1p5 = 0},
	{ .lomax = 604000000, .spd = 0, .bs = 1, .bp = 4, .cp = 0, .gc3 = 0, .div1p5 = 0},
	{ .lomax = 696000000, .spd = 0, .bs = 2, .bp = 4, .cp = 0, .gc3 = 0, .div1p5 = 0},
	{ .lomax = 740000000, .spd = 0, .bs = 2, .bp = 4, .cp = 1, .gc3 = 0, .div1p5 = 0},
	{ .lomax = 820000000, .spd = 0, .bs = 3, .bp = 4, .cp = 0, .gc3 = 0, .div1p5 = 0},
	{ .lomax = 865000000, .spd = 0, .bs = 3, .bp = 4, .cp = 1, .gc3 = 0, .div1p5 = 0},
	{ .lomax =         0, .spd = 0, .bs = 0, .bp = 0, .cp = 0, .gc3 = 0, .div1p5 = 0}
};

static int philips_tda827x_tuner_init(struct dvb_frontend *fe)
{
	return 0;
}

static int philips_tda827x_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	u8 tuner_buf[14];

	struct i2c_msg tuner_msg = {.addr = 0x60,.flags = 0,.buf = tuner_buf,
					.len = sizeof(tuner_buf) };
	int i, tuner_freq, if_freq;
	u32 N;
	switch (params->u.ofdm.bandwidth) {
	case BANDWIDTH_6_MHZ:
		if_freq = 4000000;
		break;
	case BANDWIDTH_7_MHZ:
		if_freq = 4500000;
		break;
	default:		   /* 8 MHz or Auto */
		if_freq = 5000000;
		break;
	}
	tuner_freq = params->frequency + if_freq;

	i = 0;
	while (tda827x_dvbt[i].lomax < tuner_freq) {
		if(tda827x_dvbt[i + 1].lomax == 0)
			break;
		i++;
	}

	N = ((tuner_freq + 125000) / 250000) << (tda827x_dvbt[i].spd + 2);
	tuner_buf[0] = 0;
	tuner_buf[1] = (N>>8) | 0x40;
	tuner_buf[2] = N & 0xff;
	tuner_buf[3] = 0;
	tuner_buf[4] = 0x52;
	tuner_buf[5] = (tda827x_dvbt[i].spd << 6) + (tda827x_dvbt[i].div1p5 << 5) +
				   (tda827x_dvbt[i].bs << 3) + tda827x_dvbt[i].bp;
	tuner_buf[6] = (tda827x_dvbt[i].gc3 << 4) + 0x8f;
	tuner_buf[7] = 0xbf;
	tuner_buf[8] = 0x2a;
	tuner_buf[9] = 0x05;
	tuner_buf[10] = 0xff;
	tuner_buf[11] = 0x00;
	tuner_buf[12] = 0x00;
	tuner_buf[13] = 0x40;

	tuner_msg.len = 14;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&dev->i2c_adap, &tuner_msg, 1) != 1)
		return -EIO;

	msleep(500);
	/* correct CP value */
	tuner_buf[0] = 0x30;
	tuner_buf[1] = 0x50 + tda827x_dvbt[i].cp;
	tuner_msg.len = 2;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(&dev->i2c_adap, &tuner_msg, 1);

	return 0;
}

static int philips_tda827x_tuner_sleep(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	static u8 tda827x_sleep[] = { 0x30, 0xd0};
	struct i2c_msg tuner_msg = {.addr = 0x60,.flags = 0,.buf = tda827x_sleep,
				    .len = sizeof(tda827x_sleep) };
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(&dev->i2c_adap, &tuner_msg, 1);
	return 0;
}

static struct tda1004x_config tda827x_lifeview_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.if_freq       = TDA10046_FREQ_045,
	.request_firmware = NULL,
};

/* ------------------------------------------------------------------ */

struct tda827xa_data {
	u32 lomax;
	u8  svco;
	u8  spd;
	u8  scr;
	u8  sbs;
	u8  gc3;
};

static struct tda827xa_data tda827xa_dvbt[] = {
	{ .lomax =  56875000, .svco = 3, .spd = 4, .scr = 0, .sbs = 0, .gc3 = 1},
	{ .lomax =  67250000, .svco = 0, .spd = 3, .scr = 0, .sbs = 0, .gc3 = 1},
	{ .lomax =  81250000, .svco = 1, .spd = 3, .scr = 0, .sbs = 0, .gc3 = 1},
	{ .lomax =  97500000, .svco = 2, .spd = 3, .scr = 0, .sbs = 0, .gc3 = 1},
	{ .lomax = 113750000, .svco = 3, .spd = 3, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 134500000, .svco = 0, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 154000000, .svco = 1, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 162500000, .svco = 1, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 183000000, .svco = 2, .spd = 2, .scr = 0, .sbs = 1, .gc3 = 1},
	{ .lomax = 195000000, .svco = 2, .spd = 2, .scr = 0, .sbs = 2, .gc3 = 1},
	{ .lomax = 227500000, .svco = 3, .spd = 2, .scr = 0, .sbs = 2, .gc3 = 1},
	{ .lomax = 269000000, .svco = 0, .spd = 1, .scr = 0, .sbs = 2, .gc3 = 1},
	{ .lomax = 290000000, .svco = 1, .spd = 1, .scr = 0, .sbs = 2, .gc3 = 1},
	{ .lomax = 325000000, .svco = 1, .spd = 1, .scr = 0, .sbs = 3, .gc3 = 1},
	{ .lomax = 390000000, .svco = 2, .spd = 1, .scr = 0, .sbs = 3, .gc3 = 1},
	{ .lomax = 455000000, .svco = 3, .spd = 1, .scr = 0, .sbs = 3, .gc3 = 1},
	{ .lomax = 520000000, .svco = 0, .spd = 0, .scr = 0, .sbs = 3, .gc3 = 1},
	{ .lomax = 538000000, .svco = 0, .spd = 0, .scr = 1, .sbs = 3, .gc3 = 1},
	{ .lomax = 550000000, .svco = 1, .spd = 0, .scr = 0, .sbs = 3, .gc3 = 1},
	{ .lomax = 620000000, .svco = 1, .spd = 0, .scr = 0, .sbs = 4, .gc3 = 0},
	{ .lomax = 650000000, .svco = 1, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 0},
	{ .lomax = 700000000, .svco = 2, .spd = 0, .scr = 0, .sbs = 4, .gc3 = 0},
	{ .lomax = 780000000, .svco = 2, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 0},
	{ .lomax = 820000000, .svco = 3, .spd = 0, .scr = 0, .sbs = 4, .gc3 = 0},
	{ .lomax = 870000000, .svco = 3, .spd = 0, .scr = 1, .sbs = 4, .gc3 = 0},
	{ .lomax = 911000000, .svco = 3, .spd = 0, .scr = 2, .sbs = 4, .gc3 = 0},
	{ .lomax =         0, .svco = 0, .spd = 0, .scr = 0, .sbs = 0, .gc3 = 0}};


static int philips_tda827xa_pll_set(u8 addr, struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	u8 tuner_buf[14];
	unsigned char reg2[2];

	struct i2c_msg msg = {.addr = addr,.flags = 0,.buf = tuner_buf};
	int i, tuner_freq, if_freq;
	u32 N;

	switch (params->u.ofdm.bandwidth) {
	case BANDWIDTH_6_MHZ:
		if_freq = 4000000;
		break;
	case BANDWIDTH_7_MHZ:
		if_freq = 4500000;
		break;
	default:		   /* 8 MHz or Auto */
		if_freq = 5000000;
		break;
	}
	tuner_freq = params->frequency + if_freq;

	i = 0;
	while (tda827xa_dvbt[i].lomax < tuner_freq) {
		if(tda827xa_dvbt[i + 1].lomax == 0)
			break;
		i++;
	}

	N = ((tuner_freq + 31250) / 62500) << tda827xa_dvbt[i].spd;
	tuner_buf[0] = 0;            // subaddress
	tuner_buf[1] = N >> 8;
	tuner_buf[2] = N & 0xff;
	tuner_buf[3] = 0;
	tuner_buf[4] = 0x16;
	tuner_buf[5] = (tda827xa_dvbt[i].spd << 5) + (tda827xa_dvbt[i].svco << 3) +
			tda827xa_dvbt[i].sbs;
	tuner_buf[6] = 0x4b + (tda827xa_dvbt[i].gc3 << 4);
	tuner_buf[7] = 0x0c;
	tuner_buf[8] = 0x06;
	tuner_buf[9] = 0x24;
	tuner_buf[10] = 0xff;
	tuner_buf[11] = 0x60;
	tuner_buf[12] = 0x00;
	tuner_buf[13] = 0x39;  // lpsel
	msg.len = 14;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&dev->i2c_adap, &msg, 1) != 1)
		return -EIO;

	msg.buf= reg2;
	msg.len = 2;
	reg2[0] = 0x60;
	reg2[1] = 0x3c;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(&dev->i2c_adap, &msg, 1);

	reg2[0] = 0xa0;
	reg2[1] = 0x40;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(&dev->i2c_adap, &msg, 1);

	msleep(2);
	/* correct CP value */
	reg2[0] = 0x30;
	reg2[1] = 0x10 + tda827xa_dvbt[i].scr;
	msg.len = 2;
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(&dev->i2c_adap, &msg, 1);

	msleep(550);
	reg2[0] = 0x50;
	reg2[1] = 0x4f + (tda827xa_dvbt[i].gc3 << 4);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(&dev->i2c_adap, &msg, 1);

	return 0;

}

static int philips_tda827xa_tuner_sleep(u8 addr, struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	static u8 tda827xa_sleep[] = { 0x30, 0x90};
	struct i2c_msg tuner_msg = {.addr = addr,.flags = 0,.buf = tda827xa_sleep,
				    .len = sizeof(tda827xa_sleep) };
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(&dev->i2c_adap, &tuner_msg, 1);
	return 0;
}

/* ------------------------------------------------------------------ */

static int philips_tiger_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	int ret;
	struct saa7134_dev *dev = fe->dvb->priv;
	static u8 tda8290_close[] = { 0x21, 0xc0};
	static u8 tda8290_open[]  = { 0x21, 0x80};
	struct i2c_msg tda8290_msg = {.addr = 0x4b,.flags = 0, .len = 2};

	/* close tda8290 i2c bridge */
	tda8290_msg.buf = tda8290_close;
	ret = i2c_transfer(&dev->i2c_adap, &tda8290_msg, 1);
	if (ret != 1)
		return -EIO;
	msleep(20);
	ret = philips_tda827xa_pll_set(0x61, fe, params);
	if (ret != 0)
		return ret;
	/* open tda8290 i2c bridge */
	tda8290_msg.buf = tda8290_open;
	i2c_transfer(&dev->i2c_adap, &tda8290_msg, 1);
	return ret;
}

static int philips_tiger_tuner_init(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	static u8 data[] = { 0x3c, 0x33, 0x6a};
	struct i2c_msg msg = {.addr=0x08, .flags=0, .buf=data, .len = sizeof(data)};

	if (i2c_transfer(&dev->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static int philips_tiger_tuner_sleep(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	static u8 data[] = { 0x3c, 0x33, 0x68};
	struct i2c_msg msg = {.addr=0x08, .flags=0, .buf=data, .len = sizeof(data)};

	i2c_transfer(&dev->i2c_adap, &msg, 1);
	philips_tda827xa_tuner_sleep( 0x61, fe);
	return 0;
}

static struct tda1004x_config philips_tiger_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.if_freq       = TDA10046_FREQ_045,
	.request_firmware = NULL,
};

/* ------------------------------------------------------------------ */

static int asus_p7131_dual_tuner_init(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	static u8 data[] = { 0x3c, 0x33, 0x6a};
	struct i2c_msg msg = {.addr=0x08, .flags=0, .buf=data, .len = sizeof(data)};

	if (i2c_transfer(&dev->i2c_adap, &msg, 1) != 1)
		return -EIO;
	/* make sure the DVB-T antenna input is set */
	saa_setl(SAA7134_GPIO_GPSTATUS0 >> 2, 0x0200000);
	return 0;
}

static int asus_p7131_dual_tuner_sleep(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	static u8 data[] = { 0x3c, 0x33, 0x68};
	struct i2c_msg msg = {.addr=0x08, .flags=0, .buf=data, .len = sizeof(data)};

	i2c_transfer(&dev->i2c_adap, &msg, 1);
	philips_tda827xa_tuner_sleep( 0x61, fe);
	/* reset antenna inputs for analog usage */
	saa_clearl(SAA7134_GPIO_GPSTATUS0 >> 2, 0x0200000);
	return 0;
}

/* ------------------------------------------------------------------ */

static int lifeview_trio_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	int ret;

	ret = philips_tda827xa_pll_set(0x60, fe, params);
	return ret;
}

static int lifeview_trio_tuner_sleep(struct dvb_frontend *fe)
{
	philips_tda827xa_tuner_sleep(0x60, fe);
	return 0;
}

static struct tda1004x_config lifeview_trio_config = {
	.demod_address = 0x09,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X_GPL,
	.if_freq       = TDA10046_FREQ_045,
	.request_firmware = NULL,
};

/* ------------------------------------------------------------------ */

static int ads_duo_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	int ret;

	ret = philips_tda827xa_pll_set(0x61, fe, params);
	return ret;
}

static int ads_duo_tuner_init(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	/* route TDA8275a AGC input to the channel decoder */
	saa_writeb(SAA7134_GPIO_GPSTATUS2, 0x60);
	return 0;
}

static int ads_duo_tuner_sleep(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	/* route TDA8275a AGC input to the analog IF chip*/
	saa_writeb(SAA7134_GPIO_GPSTATUS2, 0x20);
	philips_tda827xa_tuner_sleep( 0x61, fe);
	return 0;
}

static struct tda1004x_config ads_tech_duo_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X_GPL,
	.if_freq       = TDA10046_FREQ_045,
	.request_firmware = NULL,
};

/* ------------------------------------------------------------------ */

static int tevion_dvb220rf_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	int ret;
	ret = philips_tda827xa_pll_set(0x60, fe, params);
	return ret;
}

static int tevion_dvb220rf_tuner_sleep(struct dvb_frontend *fe)
{
	philips_tda827xa_tuner_sleep( 0x61, fe);
	return 0;
}

static struct tda1004x_config tevion_dvbt220rf_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.if_freq       = TDA10046_FREQ_045,
	.request_firmware = NULL,
};

/* ------------------------------------------------------------------ */

static int md8800_dvbt_analog_mode(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	static u8 data[] = { 0x3c, 0x33, 0x68};
	struct i2c_msg msg = {.addr=0x08, .flags=0, .buf=data, .len = sizeof(data)};

	i2c_transfer(&dev->i2c_adap, &msg, 1);
	philips_tda827xa_tuner_sleep( 0x61, fe);
	return 0;
}

static int md8800_dvbt_pll_set(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	int ret;
	struct saa7134_dev *dev = fe->dvb->priv;
	static u8 tda8290_close[] = { 0x21, 0xc0};
	static u8 tda8290_open[]  = { 0x21, 0x80};
	struct i2c_msg tda8290_msg = {.addr = 0x4b,.flags = 0, .len = 2};
	/* close tda8290 i2c bridge */
	tda8290_msg.buf = tda8290_close;
	ret = i2c_transfer(&dev->i2c_adap, &tda8290_msg, 1);
	if (ret != 1)
		return -EIO;
	msleep(20);
	ret = philips_tda827xa_pll_set(0x60, fe, params);
	if (ret != 0)
		return ret;
	/* open tda8290 i2c bridge */
	tda8290_msg.buf = tda8290_open;
	i2c_transfer(&dev->i2c_adap, &tda8290_msg, 1);
	return ret;
}

static struct tda1004x_config md8800_dvbt_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.if_freq       = TDA10046_FREQ_045,
	.request_firmware = NULL,
};

static struct tda10086_config flydvbs = {
	.demod_address = 0x0e,
	.invert = 0,
};

/* ------------------------------------------------------------------ */

static struct nxt200x_config avertvhda180 = {
	.demod_address    = 0x0a,
};

static int nxt200x_set_pll_input(u8 *buf, int input)
{
	if (input)
		buf[3] |= 0x08;
	else
		buf[3] &= ~0x08;
	return 0;
}

static struct nxt200x_config kworldatsc110 = {
	.demod_address    = 0x0a,
	.set_pll_input    = nxt200x_set_pll_input,
};

/* ------------------------------------------------------------------ */

static int dvb_init(struct saa7134_dev *dev)
{
	/* init struct videobuf_dvb */
	dev->ts.nr_bufs    = 32;
	dev->ts.nr_packets = 32*4;
	dev->dvb.name = dev->name;
	videobuf_queue_init(&dev->dvb.dvbq, &saa7134_ts_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_ALTERNATE,
			    sizeof(struct saa7134_buf),
			    dev);

	switch (dev->board) {
	case SAA7134_BOARD_PINNACLE_300I_DVBT_PAL:
		printk("%s: pinnacle 300i dvb setup\n",dev->name);
		dev->dvb.frontend = dvb_attach(mt352_attach, &pinnacle_300i,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.set_params = mt352_pinnacle_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_AVERMEDIA_777:
	case SAA7134_BOARD_AVERMEDIA_A16AR:
		printk("%s: avertv 777 dvb setup\n",dev->name);
		dev->dvb.frontend = dvb_attach(mt352_attach, &avermedia_777,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.calc_regs = mt352_aver777_tuner_calc_regs;
		}
		break;
	case SAA7134_BOARD_MD7134:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &medion_cardbus,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = philips_fmd1216_tuner_init;
			dev->dvb.frontend->ops.tuner_ops.sleep = philips_fmd1216_tuner_sleep;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_fmd1216_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_PHILIPS_TOUGH:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &philips_tu1216_60_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = philips_tu1216_tuner_60_init;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_tu1216_tuner_60_set_params;
		}
		break;
	case SAA7134_BOARD_FLYDVBTDUO:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &tda827x_lifeview_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = philips_tda827x_tuner_init;
			dev->dvb.frontend->ops.tuner_ops.sleep = philips_tda827x_tuner_sleep;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_tda827x_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_FLYDVBT_DUO_CARDBUS:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &tda827x_lifeview_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = philips_tda827x_tuner_init;
			dev->dvb.frontend->ops.tuner_ops.sleep = philips_tda827x_tuner_sleep;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_tda827x_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_PHILIPS_EUROPA:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &philips_europa_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->original_demod_sleep = dev->dvb.frontend->ops.sleep;
			dev->dvb.frontend->ops.sleep = philips_europa_demod_sleep;
			dev->dvb.frontend->ops.tuner_ops.init = philips_europa_tuner_init;
			dev->dvb.frontend->ops.tuner_ops.sleep = philips_europa_tuner_sleep;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_td1316_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_VIDEOMATE_DVBT_300:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &philips_europa_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = philips_europa_tuner_init;
			dev->dvb.frontend->ops.tuner_ops.sleep = philips_europa_tuner_sleep;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_td1316_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_VIDEOMATE_DVBT_200:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &philips_tu1216_61_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = philips_tu1216_tuner_61_init;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_tu1216_tuner_61_set_params;
		}
		break;
	case SAA7134_BOARD_PHILIPS_TIGER:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &philips_tiger_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = philips_tiger_tuner_init;
			dev->dvb.frontend->ops.tuner_ops.sleep = philips_tiger_tuner_sleep;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_tiger_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_ASUSTeK_P7131_DUAL:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &philips_tiger_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = asus_p7131_dual_tuner_init;
			dev->dvb.frontend->ops.tuner_ops.sleep = asus_p7131_dual_tuner_sleep;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_tiger_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_FLYDVBT_LR301:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &tda827x_lifeview_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = philips_tda827x_tuner_init;
			dev->dvb.frontend->ops.tuner_ops.sleep = philips_tda827x_tuner_sleep;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_tda827x_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_FLYDVB_TRIO:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &lifeview_trio_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.sleep = lifeview_trio_tuner_sleep;
			dev->dvb.frontend->ops.tuner_ops.set_params = lifeview_trio_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_ADS_DUO_CARDBUS_PTV331:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &ads_tech_duo_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = ads_duo_tuner_init;
			dev->dvb.frontend->ops.tuner_ops.sleep = ads_duo_tuner_sleep;
			dev->dvb.frontend->ops.tuner_ops.set_params = ads_duo_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_TEVION_DVBT_220RF:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &tevion_dvbt220rf_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.sleep = tevion_dvb220rf_tuner_sleep;
			dev->dvb.frontend->ops.tuner_ops.set_params = tevion_dvb220rf_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_FLYDVBT_HYBRID_CARDBUS:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &ads_tech_duo_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = ads_duo_tuner_init;
			dev->dvb.frontend->ops.tuner_ops.sleep = ads_duo_tuner_sleep;
			dev->dvb.frontend->ops.tuner_ops.set_params = ads_duo_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_MEDION_MD8800_QUADRO:
		dev->dvb.frontend = tda10046_attach(&md8800_dvbt_config,
						    &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = philips_tiger_tuner_init;
			dev->dvb.frontend->ops.tuner_ops.sleep = md8800_dvbt_analog_mode;
			dev->dvb.frontend->ops.tuner_ops.set_params = md8800_dvbt_pll_set;
		}
		break;
	case SAA7134_BOARD_AVERMEDIA_AVERTVHD_A180:
		dev->dvb.frontend = dvb_attach(nxt200x_attach, &avertvhda180,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   NULL, &dvb_pll_tdhu2);
		}
		break;
	case SAA7134_BOARD_KWORLD_ATSC110:
		dev->dvb.frontend = dvb_attach(nxt200x_attach, &kworldatsc110,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   NULL, &dvb_pll_tuv1236d);
		}
		break;
	case SAA7134_BOARD_FLYDVBS_LR300:
		dev->dvb.frontend = dvb_attach(tda10086_attach, &flydvbs,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			if (dvb_attach(tda826x_attach, dev->dvb.frontend, 0x60,
				       &dev->i2c_adap, 0) == NULL) {
				printk("%s: No tda826x found!\n", __FUNCTION__);
			}
			if (dvb_attach(isl6421_attach, dev->dvb.frontend,
				       &dev->i2c_adap, 0x08, 0, 0) == NULL) {
				printk("%s: No ISL6421 found!\n", __FUNCTION__);
			}
		}
		break;
	case SAA7134_BOARD_ASUS_EUROPA2_HYBRID:
		dev->dvb.frontend = tda10046_attach(&medion_cardbus,
						    &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->original_demod_sleep = dev->dvb.frontend->ops.sleep;
			dev->dvb.frontend->ops.sleep = philips_europa_demod_sleep;
			dev->dvb.frontend->ops.tuner_ops.init = philips_fmd1216_tuner_init;
			dev->dvb.frontend->ops.tuner_ops.sleep = philips_fmd1216_tuner_sleep;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_fmd1216_tuner_set_params;
		}
		break;

	default:
		printk("%s: Huh? unknown DVB card?\n",dev->name);
		break;
	}

	if (NULL == dev->dvb.frontend) {
		printk("%s: frontend initialization failed\n",dev->name);
		return -1;
	}

	/* register everything else */
	return videobuf_dvb_register(&dev->dvb, THIS_MODULE, dev, &dev->pci->dev);
}

static int dvb_fini(struct saa7134_dev *dev)
{
	static int on  = TDA9887_PRESENT | TDA9887_PORT2_INACTIVE;

	switch (dev->board) {
	case SAA7134_BOARD_PINNACLE_300I_DVBT_PAL:
		/* otherwise we don't detect the tuner on next insmod */
		saa7134_i2c_call_clients(dev,TDA9887_SET_CONFIG,&on);
		break;
	};
	videobuf_dvb_unregister(&dev->dvb);
	return 0;
}

static struct saa7134_mpeg_ops dvb_ops = {
	.type          = SAA7134_MPEG_DVB,
	.init          = dvb_init,
	.fini          = dvb_fini,
};

static int __init dvb_register(void)
{
	return saa7134_ts_register(&dvb_ops);
}

static void __exit dvb_unregister(void)
{
	saa7134_ts_unregister(&dvb_ops);
}

module_init(dvb_register);
module_exit(dvb_unregister);

/* ------------------------------------------------------------------ */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
