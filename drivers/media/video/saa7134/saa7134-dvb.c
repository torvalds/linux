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
#include "tda827x.h"
#include "isl6421.h"

MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

static unsigned int antenna_pwr = 0;

module_param(antenna_pwr, int, 0444);
MODULE_PARM_DESC(antenna_pwr,"enable antenna power (Pinnacle 300i)");

static int use_frontend = 0;
module_param(use_frontend, int, 0644);
MODULE_PARM_DESC(use_frontend,"for cards with multiple frontends (0: terrestrial, 1: satellite)");

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off module debugging (default:off).");

#define dprintk(fmt, arg...)	do { if (debug) \
	printk(KERN_DEBUG "%s/dvb: " fmt, dev->name , ## arg); } while(0)

/* Print a warning */
#define wprintk(fmt, arg...) \
	printk(KERN_WARNING "%s/dvb: " fmt, dev->name, ## arg)

/* ------------------------------------------------------------------
 * mt352 based DVB-T cards
 */

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
	dprintk("%s %s\n", __FUNCTION__, ok ? "on" : "off");

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

	dprintk("%s called\n", __FUNCTION__);

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

/* ==================================================================
 * tda1004x based DVB-T cards, helper functions
 */

static int philips_tda1004x_request_firmware(struct dvb_frontend *fe,
					   const struct firmware **fw, char *name)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	return request_firmware(fw, name, &dev->pci->dev);
}

/* ------------------------------------------------------------------
 * these tuners are tu1216, td1316(a)
 */

static int philips_tda6651_pll_set(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	struct tda1004x_state *state = fe->demodulator_priv;
	u8 addr = state->config->tuner_address;
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
	if (i2c_transfer(&dev->i2c_adap, &tuner_msg, 1) != 1) {
		wprintk("could not write to tuner at addr: 0x%02x\n",
			addr << 1);
		return -EIO;
	}
	msleep(1);
	return 0;
}

static int philips_tu1216_init(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	struct tda1004x_state *state = fe->demodulator_priv;
	u8 addr = state->config->tuner_address;
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

static struct tda1004x_config philips_tu1216_60_config = {
	.demod_address = 0x8,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_4M,
	.agc_config    = TDA10046_AGC_DEFAULT,
	.if_freq       = TDA10046_FREQ_3617,
	.tuner_address = 0x60,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config philips_tu1216_61_config = {

	.demod_address = 0x8,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_4M,
	.agc_config    = TDA10046_AGC_DEFAULT,
	.if_freq       = TDA10046_FREQ_3617,
	.tuner_address = 0x61,
	.request_firmware = philips_tda1004x_request_firmware
};

/* ------------------------------------------------------------------ */

static int philips_td1316_tuner_init(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	struct tda1004x_state *state = fe->demodulator_priv;
	u8 addr = state->config->tuner_address;
	static u8 msg[] = { 0x0b, 0xf5, 0x86, 0xab };
	struct i2c_msg init_msg = {.addr = addr,.flags = 0,.buf = msg,.len = sizeof(msg) };

	/* setup PLL configuration */
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&dev->i2c_adap, &init_msg, 1) != 1)
		return -EIO;
	return 0;
}

static int philips_td1316_tuner_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	return philips_tda6651_pll_set(fe, params);
}

static int philips_td1316_tuner_sleep(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	struct tda1004x_state *state = fe->demodulator_priv;
	u8 addr = state->config->tuner_address;
	static u8 msg[] = { 0x0b, 0xdc, 0x86, 0xa4 };
	struct i2c_msg analog_msg = {.addr = addr,.flags = 0,.buf = msg,.len = sizeof(msg) };

	/* switch the tuner to analog mode */
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (i2c_transfer(&dev->i2c_adap, &analog_msg, 1) != 1)
		return -EIO;
	return 0;
}

/* ------------------------------------------------------------------ */

static int philips_europa_tuner_init(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	static u8 msg[] = { 0x00, 0x40};
	struct i2c_msg init_msg = {.addr = 0x43,.flags = 0,.buf = msg,.len = sizeof(msg) };


	if (philips_td1316_tuner_init(fe))
		return -EIO;
	msleep(1);
	if (i2c_transfer(&dev->i2c_adap, &init_msg, 1) != 1)
		return -EIO;

	return 0;
}

static int philips_europa_tuner_sleep(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;

	static u8 msg[] = { 0x00, 0x14 };
	struct i2c_msg analog_msg = {.addr = 0x43,.flags = 0,.buf = msg,.len = sizeof(msg) };

	if (philips_td1316_tuner_sleep(fe))
		return -EIO;

	/* switch the board to analog mode */
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
	.tuner_address = 0x61,
	.request_firmware = philips_tda1004x_request_firmware
};

/* ------------------------------------------------------------------ */

static struct tda1004x_config medion_cardbus = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_IFO_AUTO_NEG,
	.if_freq       = TDA10046_FREQ_3613,
	.tuner_address = 0x61,
	.request_firmware = philips_tda1004x_request_firmware
};

/* ------------------------------------------------------------------
 * tda 1004x based cards with philips silicon tuner
 */

static void philips_tda827x_lna_gain(struct dvb_frontend *fe, int high)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	struct tda1004x_state *state = fe->demodulator_priv;
	u8 addr = state->config->i2c_gate;
	u8 config = state->config->tuner_config;
	u8 GP00_CF[] = {0x20, 0x01};
	u8 GP00_LEV[] = {0x22, 0x00};

	struct i2c_msg msg = {.addr = addr,.flags = 0,.buf = GP00_CF, .len = 2};
	if (config) {
		if (high) {
			dprintk("setting LNA to high gain\n");
		} else {
			dprintk("setting LNA to low gain\n");
		}
	}
	switch (config) {
	case 0: /* no LNA */
		break;
	case 1: /* switch is GPIO 0 of tda8290 */
	case 2:
		/* turn Vsync off */
		saa7134_set_gpio(dev, 22, 0);
		GP00_LEV[1] = high ? 0 : 1;
		if (i2c_transfer(&dev->i2c_adap, &msg, 1) != 1) {
			wprintk("could not access tda8290 at addr: 0x%02x\n",
				addr << 1);
			return;
		}
		msg.buf = GP00_LEV;
		if (config == 2)
			GP00_LEV[1] = high ? 1 : 0;
		i2c_transfer(&dev->i2c_adap, &msg, 1);
		break;
	case 3: /* switch with GPIO of saa713x */
		saa7134_set_gpio(dev, 22, high);
		break;
	}
}

static int tda8290_i2c_gate_ctrl( struct dvb_frontend* fe, int enable)
{
	struct tda1004x_state *state = fe->demodulator_priv;

	u8 addr = state->config->i2c_gate;
	static u8 tda8290_close[] = { 0x21, 0xc0};
	static u8 tda8290_open[]  = { 0x21, 0x80};
	struct i2c_msg tda8290_msg = {.addr = addr,.flags = 0, .len = 2};
	if (enable) {
		tda8290_msg.buf = tda8290_close;
	} else {
		tda8290_msg.buf = tda8290_open;
	}
	if (i2c_transfer(state->i2c, &tda8290_msg, 1) != 1) {
		struct saa7134_dev *dev = fe->dvb->priv;
		wprintk("could not access tda8290 I2C gate\n");
		return -EIO;
	}
	msleep(20);
	return 0;
}

/* ------------------------------------------------------------------ */

static int philips_tda827x_tuner_init(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	struct tda1004x_state *state = fe->demodulator_priv;

	switch (state->config->antenna_switch) {
	case 0: break;
	case 1:	dprintk("setting GPIO21 to 0 (TV antenna?)\n");
		saa7134_set_gpio(dev, 21, 0);
		break;
	case 2: dprintk("setting GPIO21 to 1 (Radio antenna?)\n");
		saa7134_set_gpio(dev, 21, 1);
		break;
	}
	return 0;
}

static int philips_tda827x_tuner_sleep(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	struct tda1004x_state *state = fe->demodulator_priv;

	switch (state->config->antenna_switch) {
	case 0: break;
	case 1: dprintk("setting GPIO21 to 1 (Radio antenna?)\n");
		saa7134_set_gpio(dev, 21, 1);
		break;
	case 2:	dprintk("setting GPIO21 to 0 (TV antenna?)\n");
		saa7134_set_gpio(dev, 21, 0);
		break;
	}
	return 0;
}

static struct tda827x_config tda827x_cfg = {
	.lna_gain = philips_tda827x_lna_gain,
	.init = philips_tda827x_tuner_init,
	.sleep = philips_tda827x_tuner_sleep
};

static void configure_tda827x_fe(struct saa7134_dev *dev, struct tda1004x_config *tda_conf)
{
	dev->dvb.frontend = dvb_attach(tda10046_attach, tda_conf, &dev->i2c_adap);
	if (dev->dvb.frontend) {
		if (tda_conf->i2c_gate)
			dev->dvb.frontend->ops.i2c_gate_ctrl = tda8290_i2c_gate_ctrl;
		if (dvb_attach(tda827x_attach, dev->dvb.frontend, tda_conf->tuner_address,
						&dev->i2c_adap,&tda827x_cfg) == NULL) {
			wprintk("no tda827x tuner found at addr: %02x\n",
				tda_conf->tuner_address);
		}
	}
}

/* ------------------------------------------------------------------ */

static struct tda1004x_config tda827x_lifeview_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP11_I,
	.if_freq       = TDA10046_FREQ_045,
	.tuner_address = 0x60,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config philips_tiger_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP11_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x4b,
	.tuner_address = 0x61,
	.tuner_config  = 0,
	.antenna_switch= 1,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config cinergy_ht_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP01_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x4b,
	.tuner_address = 0x61,
	.tuner_config  = 0,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config cinergy_ht_pci_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP01_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x4b,
	.tuner_address = 0x60,
	.tuner_config  = 0,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config philips_tiger_s_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP01_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x4b,
	.tuner_address = 0x61,
	.tuner_config  = 2,
	.antenna_switch= 1,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config pinnacle_pctv_310i_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP11_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x4b,
	.tuner_address = 0x61,
	.tuner_config  = 1,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config hauppauge_hvr_1110_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP11_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x4b,
	.tuner_address = 0x61,
	.tuner_config  = 1,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config asus_p7131_dual_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP11_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x4b,
	.tuner_address = 0x61,
	.tuner_config  = 0,
	.antenna_switch= 2,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config lifeview_trio_config = {
	.demod_address = 0x09,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP00_I,
	.if_freq       = TDA10046_FREQ_045,
	.tuner_address = 0x60,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config tevion_dvbt220rf_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP11_I,
	.if_freq       = TDA10046_FREQ_045,
	.tuner_address = 0x60,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config md8800_dvbt_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP01_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x4b,
	.tuner_address = 0x60,
	.tuner_config  = 0,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config asus_p7131_4871_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP01_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x4b,
	.tuner_address = 0x61,
	.tuner_config  = 2,
	.antenna_switch= 2,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config asus_p7131_hybrid_lna_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP11_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x4b,
	.tuner_address = 0x61,
	.tuner_config  = 2,
	.antenna_switch= 2,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config kworld_dvb_t_210_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP11_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x4b,
	.tuner_address = 0x61,
	.tuner_config  = 2,
	.antenna_switch= 1,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config avermedia_super_007_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP01_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x4b,
	.tuner_address = 0x60,
	.tuner_config  = 0,
	.antenna_switch= 1,
	.request_firmware = philips_tda1004x_request_firmware
};

/* ------------------------------------------------------------------
 * special case: this card uses saa713x GPIO22 for the mode switch
 */

static int ads_duo_tuner_init(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	philips_tda827x_tuner_init(fe);
	/* route TDA8275a AGC input to the channel decoder */
	saa7134_set_gpio(dev, 22, 1);
	return 0;
}

static int ads_duo_tuner_sleep(struct dvb_frontend *fe)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	/* route TDA8275a AGC input to the analog IF chip*/
	saa7134_set_gpio(dev, 22, 0);
	philips_tda827x_tuner_sleep(fe);
	return 0;
}

static struct tda827x_config ads_duo_cfg = {
	.lna_gain = philips_tda827x_lna_gain,
	.init = ads_duo_tuner_init,
	.sleep = ads_duo_tuner_sleep
};

static struct tda1004x_config ads_tech_duo_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP00_I,
	.if_freq       = TDA10046_FREQ_045,
	.tuner_address = 0x61,
	.request_firmware = philips_tda1004x_request_firmware
};

/* ==================================================================
 * tda10086 based DVB-S cards, helper functions
 */

static struct tda10086_config flydvbs = {
	.demod_address = 0x0e,
	.invert = 0,
};

/* ==================================================================
 * nxt200x based ATSC cards, helper functions
 */

static struct nxt200x_config avertvhda180 = {
	.demod_address    = 0x0a,
};

static struct nxt200x_config kworldatsc110 = {
	.demod_address    = 0x0a,
};

/* ==================================================================
 * Core code
 */

static int dvb_init(struct saa7134_dev *dev)
{
	int ret;
	/* init struct videobuf_dvb */
	dev->ts.nr_bufs    = 32;
	dev->ts.nr_packets = 32*4;
	dev->dvb.name = dev->name;
	videobuf_queue_pci_init(&dev->dvb.dvbq, &saa7134_ts_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_ALTERNATE,
			    sizeof(struct saa7134_buf),
			    dev);

	switch (dev->board) {
	case SAA7134_BOARD_PINNACLE_300I_DVBT_PAL:
		dprintk("pinnacle 300i dvb setup\n");
		dev->dvb.frontend = dvb_attach(mt352_attach, &pinnacle_300i,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.set_params = mt352_pinnacle_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_AVERMEDIA_777:
	case SAA7134_BOARD_AVERMEDIA_A16AR:
		dprintk("avertv 777 dvb setup\n");
		dev->dvb.frontend = dvb_attach(mt352_attach, &avermedia_777,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   NULL, DVB_PLL_PHILIPS_TD1316);
		}
		break;
	case SAA7134_BOARD_MD7134:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &medion_cardbus,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, medion_cardbus.tuner_address,
				   &dev->i2c_adap, DVB_PLL_FMD1216ME);
		}
		break;
	case SAA7134_BOARD_PHILIPS_TOUGH:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &philips_tu1216_60_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = philips_tu1216_init;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_tda6651_pll_set;
		}
		break;
	case SAA7134_BOARD_FLYDVBTDUO:
	case SAA7134_BOARD_FLYDVBT_DUO_CARDBUS:
		configure_tda827x_fe(dev, &tda827x_lifeview_config);
		break;
	case SAA7134_BOARD_PHILIPS_EUROPA:
	case SAA7134_BOARD_VIDEOMATE_DVBT_300:
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
	case SAA7134_BOARD_VIDEOMATE_DVBT_200:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &philips_tu1216_61_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = philips_tu1216_init;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_tda6651_pll_set;
		}
		break;
	case SAA7134_BOARD_KWORLD_DVBT_210:
		configure_tda827x_fe(dev, &kworld_dvb_t_210_config);
		break;
	case SAA7134_BOARD_PHILIPS_TIGER:
		configure_tda827x_fe(dev, &philips_tiger_config);
		break;
	case SAA7134_BOARD_PINNACLE_PCTV_310i:
		configure_tda827x_fe(dev, &pinnacle_pctv_310i_config);
		break;
	case SAA7134_BOARD_HAUPPAUGE_HVR1110:
		configure_tda827x_fe(dev, &hauppauge_hvr_1110_config);
		break;
	case SAA7134_BOARD_ASUSTeK_P7131_DUAL:
		configure_tda827x_fe(dev, &asus_p7131_dual_config);
		break;
	case SAA7134_BOARD_FLYDVBT_LR301:
		configure_tda827x_fe(dev, &tda827x_lifeview_config);
		break;
	case SAA7134_BOARD_FLYDVB_TRIO:
		if(! use_frontend) {	//terrestrial
			configure_tda827x_fe(dev, &lifeview_trio_config);
		} else {  	      //satellite
			dev->dvb.frontend = dvb_attach(tda10086_attach, &flydvbs, &dev->i2c_adap);
			if (dev->dvb.frontend) {
				if (dvb_attach(tda826x_attach, dev->dvb.frontend, 0x63,
									&dev->i2c_adap, 0) == NULL) {
					wprintk("%s: Lifeview Trio, No tda826x found!\n", __FUNCTION__);
				}
				if (dvb_attach(isl6421_attach, dev->dvb.frontend, &dev->i2c_adap,
										0x08, 0, 0) == NULL) {
					wprintk("%s: Lifeview Trio, No ISL6421 found!\n", __FUNCTION__);
				}
			}
		}
		break;
	case SAA7134_BOARD_ADS_DUO_CARDBUS_PTV331:
	case SAA7134_BOARD_FLYDVBT_HYBRID_CARDBUS:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
					       &ads_tech_duo_config,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			if (dvb_attach(tda827x_attach,dev->dvb.frontend,
				   ads_tech_duo_config.tuner_address,
				   &dev->i2c_adap,&ads_duo_cfg) == NULL) {
				wprintk("no tda827x tuner found at addr: %02x\n",
					ads_tech_duo_config.tuner_address);
			}
		}
		break;
	case SAA7134_BOARD_TEVION_DVBT_220RF:
		configure_tda827x_fe(dev, &tevion_dvbt220rf_config);
		break;
	case SAA7134_BOARD_MEDION_MD8800_QUADRO:
		configure_tda827x_fe(dev, &md8800_dvbt_config);
		break;
	case SAA7134_BOARD_AVERMEDIA_AVERTVHD_A180:
		dev->dvb.frontend = dvb_attach(nxt200x_attach, &avertvhda180,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   NULL, DVB_PLL_TDHU2);
		}
		break;
	case SAA7134_BOARD_KWORLD_ATSC110:
		dev->dvb.frontend = dvb_attach(nxt200x_attach, &kworldatsc110,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   NULL, DVB_PLL_TUV1236D);
		}
		break;
	case SAA7134_BOARD_FLYDVBS_LR300:
		dev->dvb.frontend = dvb_attach(tda10086_attach, &flydvbs,
					       &dev->i2c_adap);
		if (dev->dvb.frontend) {
			if (dvb_attach(tda826x_attach, dev->dvb.frontend, 0x60,
				       &dev->i2c_adap, 0) == NULL) {
				wprintk("%s: No tda826x found!\n", __FUNCTION__);
			}
			if (dvb_attach(isl6421_attach, dev->dvb.frontend,
				       &dev->i2c_adap, 0x08, 0, 0) == NULL) {
				wprintk("%s: No ISL6421 found!\n", __FUNCTION__);
			}
		}
		break;
	case SAA7134_BOARD_ASUS_EUROPA2_HYBRID:
		dev->dvb.frontend = tda10046_attach(&medion_cardbus,
						    &dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->original_demod_sleep = dev->dvb.frontend->ops.sleep;
			dev->dvb.frontend->ops.sleep = philips_europa_demod_sleep;

			dvb_attach(dvb_pll_attach, dev->dvb.frontend, medion_cardbus.tuner_address,
				   &dev->i2c_adap, DVB_PLL_FMD1216ME);
		}
		break;
	case SAA7134_BOARD_VIDEOMATE_DVBT_200A:
		dev->dvb.frontend = dvb_attach(tda10046_attach,
				&philips_europa_config,
				&dev->i2c_adap);
		if (dev->dvb.frontend) {
			dev->dvb.frontend->ops.tuner_ops.init = philips_td1316_tuner_init;
			dev->dvb.frontend->ops.tuner_ops.set_params = philips_td1316_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_CINERGY_HT_PCMCIA:
		configure_tda827x_fe(dev, &cinergy_ht_config);
		break;
	case SAA7134_BOARD_CINERGY_HT_PCI:
		configure_tda827x_fe(dev, &cinergy_ht_pci_config);
		break;
	case SAA7134_BOARD_PHILIPS_TIGER_S:
		configure_tda827x_fe(dev, &philips_tiger_s_config);
		break;
	case SAA7134_BOARD_ASUS_P7131_4871:
		configure_tda827x_fe(dev, &asus_p7131_4871_config);
		break;
	case SAA7134_BOARD_ASUSTeK_P7131_HYBRID_LNA:
		configure_tda827x_fe(dev, &asus_p7131_hybrid_lna_config);
		break;
	case SAA7134_BOARD_AVERMEDIA_SUPER_007:
		configure_tda827x_fe(dev, &avermedia_super_007_config);
		break;
	default:
		wprintk("Huh? unknown DVB card?\n");
		break;
	}

	if (NULL == dev->dvb.frontend) {
		printk(KERN_ERR "%s/dvb: frontend initialization failed\n", dev->name);
		return -1;
	}

	/* register everything else */
	ret = videobuf_dvb_register(&dev->dvb, THIS_MODULE, dev, &dev->pci->dev);

	/* this sequence is necessary to make the tda1004x load its firmware
	 * and to enter analog mode of hybrid boards
	 */
	if (!ret) {
		if (dev->dvb.frontend->ops.init)
			dev->dvb.frontend->ops.init(dev->dvb.frontend);
		if (dev->dvb.frontend->ops.sleep)
			dev->dvb.frontend->ops.sleep(dev->dvb.frontend);
		if (dev->dvb.frontend->ops.tuner_ops.sleep)
			dev->dvb.frontend->ops.tuner_ops.sleep(dev->dvb.frontend);
	}
	return ret;
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
