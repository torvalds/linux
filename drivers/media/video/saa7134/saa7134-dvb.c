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
#include <dvb_frontend.h>

#include "mt352.h"
#include "mt352_priv.h" /* FIXME */
#include "tda1004x.h"
#include "nxt200x.h"
#include "tuner-xc2028.h"

#include "tda10086.h"
#include "tda826x.h"
#include "tda827x.h"
#include "isl6421.h"
#include "isl6405.h"
#include "lnbp21.h"
#include "tuner-simple.h"
#include "tda10048.h"
#include "tda18271.h"
#include "lgdt3305.h"
#include "tda8290.h"

#include "zl10353.h"

#include "zl10036.h"
#include "mt312.h"

MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

static unsigned int antenna_pwr;

module_param(antenna_pwr, int, 0444);
MODULE_PARM_DESC(antenna_pwr,"enable antenna power (Pinnacle 300i)");

static int use_frontend;
module_param(use_frontend, int, 0644);
MODULE_PARM_DESC(use_frontend,"for cards with multiple frontends (0: terrestrial, 1: satellite)");

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off module debugging (default:off).");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

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
	dprintk("%s %s\n", __func__, ok ? "on" : "off");

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

	dprintk("%s called\n", __func__);

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

static int mt352_avermedia_xc3028_init(struct dvb_frontend *fe)
{
	static u8 clock_config []  = { CLOCK_CTL, 0x38, 0x2d };
	static u8 reset []         = { RESET, 0x80 };
	static u8 adc_ctl_1_cfg [] = { ADC_CTL_1, 0x40 };
	static u8 agc_cfg []       = { AGC_TARGET, 0xe };
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
	saa_call_all(dev, tuner, s_frequency, &f);
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

static struct mt352_config avermedia_xc3028_mt352_dev = {
	.demod_address   = (0x1e >> 1),
	.no_tuner        = 1,
	.demod_init      = mt352_avermedia_xc3028_init,
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

static int configure_tda827x_fe(struct saa7134_dev *dev,
				struct tda1004x_config *cdec_conf,
				struct tda827x_config *tuner_conf)
{
	struct videobuf_dvb_frontend *fe0;

	/* Get the first frontend */
	fe0 = videobuf_dvb_get_frontend(&dev->frontends, 1);

	fe0->dvb.frontend = dvb_attach(tda10046_attach, cdec_conf, &dev->i2c_adap);
	if (fe0->dvb.frontend) {
		if (cdec_conf->i2c_gate)
			fe0->dvb.frontend->ops.i2c_gate_ctrl = tda8290_i2c_gate_ctrl;
		if (dvb_attach(tda827x_attach, fe0->dvb.frontend,
			       cdec_conf->tuner_address,
			       &dev->i2c_adap, tuner_conf))
			return 0;

		wprintk("no tda827x tuner found at addr: %02x\n",
				cdec_conf->tuner_address);
	}
	return -EINVAL;
}

/* ------------------------------------------------------------------ */

static struct tda827x_config tda827x_cfg_0 = {
	.init = philips_tda827x_tuner_init,
	.sleep = philips_tda827x_tuner_sleep,
	.config = 0,
	.switch_addr = 0
};

static struct tda827x_config tda827x_cfg_1 = {
	.init = philips_tda827x_tuner_init,
	.sleep = philips_tda827x_tuner_sleep,
	.config = 1,
	.switch_addr = 0x4b
};

static struct tda827x_config tda827x_cfg_2 = {
	.init = philips_tda827x_tuner_init,
	.sleep = philips_tda827x_tuner_sleep,
	.config = 2,
	.switch_addr = 0x4b
};

static struct tda827x_config tda827x_cfg_2_sw42 = {
	.init = philips_tda827x_tuner_init,
	.sleep = philips_tda827x_tuner_sleep,
	.config = 2,
	.switch_addr = 0x42
};

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
	.antenna_switch= 1,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config twinhan_dtv_dvb_3056_config = {
	.demod_address = 0x08,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP01_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x42,
	.tuner_address = 0x61,
	.antenna_switch = 1,
	.request_firmware = philips_tda1004x_request_firmware
};

static struct tda1004x_config asus_tiger_3in1_config = {
	.demod_address = 0x0b,
	.invert        = 1,
	.invert_oclk   = 0,
	.xtal_freq     = TDA10046_XTAL_16M,
	.agc_config    = TDA10046_AGC_TDA827X,
	.gpio_config   = TDA10046_GP11_I,
	.if_freq       = TDA10046_FREQ_045,
	.i2c_gate      = 0x4b,
	.tuner_address = 0x61,
	.antenna_switch = 1,
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
	.init = ads_duo_tuner_init,
	.sleep = ads_duo_tuner_sleep,
	.config = 0
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

static struct zl10353_config behold_h6_config = {
	.demod_address = 0x1e>>1,
	.no_tuner      = 1,
	.parallel_ts   = 1,
	.disable_i2c_gate_ctrl = 1,
};

/* ==================================================================
 * tda10086 based DVB-S cards, helper functions
 */

static struct tda10086_config flydvbs = {
	.demod_address = 0x0e,
	.invert = 0,
	.diseqc_tone = 0,
	.xtal_freq = TDA10086_XTAL_16M,
};

static struct tda10086_config sd1878_4m = {
	.demod_address = 0x0e,
	.invert = 0,
	.diseqc_tone = 0,
	.xtal_freq = TDA10086_XTAL_4M,
};

/* ------------------------------------------------------------------
 * special case: lnb supply is connected to the gated i2c
 */

static int md8800_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	int res = -EIO;
	struct saa7134_dev *dev = fe->dvb->priv;
	if (fe->ops.i2c_gate_ctrl) {
		fe->ops.i2c_gate_ctrl(fe, 1);
		if (dev->original_set_voltage)
			res = dev->original_set_voltage(fe, voltage);
		fe->ops.i2c_gate_ctrl(fe, 0);
	}
	return res;
};

static int md8800_set_high_voltage(struct dvb_frontend *fe, long arg)
{
	int res = -EIO;
	struct saa7134_dev *dev = fe->dvb->priv;
	if (fe->ops.i2c_gate_ctrl) {
		fe->ops.i2c_gate_ctrl(fe, 1);
		if (dev->original_set_high_voltage)
			res = dev->original_set_high_voltage(fe, arg);
		fe->ops.i2c_gate_ctrl(fe, 0);
	}
	return res;
};

static int md8800_set_voltage2(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	u8 wbuf[2] = { 0x1f, 00 };
	u8 rbuf;
	struct i2c_msg msg[] = { { .addr = 0x08, .flags = 0, .buf = wbuf, .len = 1 },
				 { .addr = 0x08, .flags = I2C_M_RD, .buf = &rbuf, .len = 1 } };

	if (i2c_transfer(&dev->i2c_adap, msg, 2) != 2)
		return -EIO;
	/* NOTE: this assumes that gpo1 is used, it might be bit 5 (gpo2) */
	if (voltage == SEC_VOLTAGE_18)
		wbuf[1] = rbuf | 0x10;
	else
		wbuf[1] = rbuf & 0xef;
	msg[0].len = 2;
	i2c_transfer(&dev->i2c_adap, msg, 1);
	return 0;
}

static int md8800_set_high_voltage2(struct dvb_frontend *fe, long arg)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	wprintk("%s: sorry can't set high LNB supply voltage from here\n", __func__);
	return -EIO;
}

/* ==================================================================
 * nxt200x based ATSC cards, helper functions
 */

static struct nxt200x_config avertvhda180 = {
	.demod_address    = 0x0a,
};

static struct nxt200x_config kworldatsc110 = {
	.demod_address    = 0x0a,
};

/* ------------------------------------------------------------------ */

static struct mt312_config avertv_a700_mt312 = {
	.demod_address = 0x0e,
	.voltage_inverted = 1,
};

static struct zl10036_config avertv_a700_tuner = {
	.tuner_address = 0x60,
};

static struct lgdt3305_config hcw_lgdt3305_config = {
	.i2c_addr           = 0x0e,
	.mpeg_mode          = LGDT3305_MPEG_SERIAL,
	.tpclk_edge         = LGDT3305_TPCLK_RISING_EDGE,
	.tpvalid_polarity   = LGDT3305_TP_VALID_HIGH,
	.deny_i2c_rptr      = 1,
	.spectral_inversion = 1,
	.qam_if_khz         = 4000,
	.vsb_if_khz         = 3250,
};

static struct tda10048_config hcw_tda10048_config = {
	.demod_address    = 0x10 >> 1,
	.output_mode      = TDA10048_SERIAL_OUTPUT,
	.fwbulkwritelen   = TDA10048_BULKWRITE_200,
	.inversion        = TDA10048_INVERSION_ON,
	.dtv6_if_freq_khz = TDA10048_IF_3300,
	.dtv7_if_freq_khz = TDA10048_IF_3500,
	.dtv8_if_freq_khz = TDA10048_IF_4000,
	.clk_freq_khz     = TDA10048_CLK_16000,
	.disable_gate_access = 1,
};

static struct tda18271_std_map hauppauge_tda18271_std_map = {
	.atsc_6   = { .if_freq = 3250, .agc_mode = 3, .std = 4,
		      .if_lvl = 1, .rfagc_top = 0x58, },
	.qam_6    = { .if_freq = 4000, .agc_mode = 3, .std = 5,
		      .if_lvl = 1, .rfagc_top = 0x58, },
};

static struct tda18271_config hcw_tda18271_config = {
	.std_map = &hauppauge_tda18271_std_map,
	.gate    = TDA18271_GATE_ANALOG,
	.config  = 3,
};

static struct tda829x_config tda829x_no_probe = {
	.probe_tuner = TDA829X_DONT_PROBE,
};

/* ==================================================================
 * Core code
 */

static int dvb_init(struct saa7134_dev *dev)
{
	int ret;
	int attach_xc3028 = 0;
	struct videobuf_dvb_frontend *fe0;

	/* FIXME: add support for multi-frontend */
	mutex_init(&dev->frontends.lock);
	INIT_LIST_HEAD(&dev->frontends.felist);

	printk(KERN_INFO "%s() allocating 1 frontend\n", __func__);
	fe0 = videobuf_dvb_alloc_frontend(&dev->frontends, 1);
	if (!fe0) {
		printk(KERN_ERR "%s() failed to alloc\n", __func__);
		return -ENOMEM;
	}

	/* init struct videobuf_dvb */
	dev->ts.nr_bufs    = 32;
	dev->ts.nr_packets = 32*4;
	fe0->dvb.name = dev->name;
	videobuf_queue_sg_init(&fe0->dvb.dvbq, &saa7134_ts_qops,
			    &dev->pci->dev, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_ALTERNATE,
			    sizeof(struct saa7134_buf),
			    dev);

	switch (dev->board) {
	case SAA7134_BOARD_PINNACLE_300I_DVBT_PAL:
		dprintk("pinnacle 300i dvb setup\n");
		fe0->dvb.frontend = dvb_attach(mt352_attach, &pinnacle_300i,
					       &dev->i2c_adap);
		if (fe0->dvb.frontend) {
			fe0->dvb.frontend->ops.tuner_ops.set_params = mt352_pinnacle_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_AVERMEDIA_777:
	case SAA7134_BOARD_AVERMEDIA_A16AR:
		dprintk("avertv 777 dvb setup\n");
		fe0->dvb.frontend = dvb_attach(mt352_attach, &avermedia_777,
					       &dev->i2c_adap);
		if (fe0->dvb.frontend) {
			dvb_attach(simple_tuner_attach, fe0->dvb.frontend,
				   &dev->i2c_adap, 0x61,
				   TUNER_PHILIPS_TD1316);
		}
		break;
	case SAA7134_BOARD_AVERMEDIA_A16D:
		dprintk("AverMedia A16D dvb setup\n");
		fe0->dvb.frontend = dvb_attach(mt352_attach,
						&avermedia_xc3028_mt352_dev,
						&dev->i2c_adap);
		attach_xc3028 = 1;
		break;
	case SAA7134_BOARD_MD7134:
		fe0->dvb.frontend = dvb_attach(tda10046_attach,
					       &medion_cardbus,
					       &dev->i2c_adap);
		if (fe0->dvb.frontend) {
			dvb_attach(simple_tuner_attach, fe0->dvb.frontend,
				   &dev->i2c_adap, medion_cardbus.tuner_address,
				   TUNER_PHILIPS_FMD1216ME_MK3);
		}
		break;
	case SAA7134_BOARD_PHILIPS_TOUGH:
		fe0->dvb.frontend = dvb_attach(tda10046_attach,
					       &philips_tu1216_60_config,
					       &dev->i2c_adap);
		if (fe0->dvb.frontend) {
			fe0->dvb.frontend->ops.tuner_ops.init = philips_tu1216_init;
			fe0->dvb.frontend->ops.tuner_ops.set_params = philips_tda6651_pll_set;
		}
		break;
	case SAA7134_BOARD_FLYDVBTDUO:
	case SAA7134_BOARD_FLYDVBT_DUO_CARDBUS:
		if (configure_tda827x_fe(dev, &tda827x_lifeview_config,
					 &tda827x_cfg_0) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_PHILIPS_EUROPA:
	case SAA7134_BOARD_VIDEOMATE_DVBT_300:
		fe0->dvb.frontend = dvb_attach(tda10046_attach,
					       &philips_europa_config,
					       &dev->i2c_adap);
		if (fe0->dvb.frontend) {
			dev->original_demod_sleep = fe0->dvb.frontend->ops.sleep;
			fe0->dvb.frontend->ops.sleep = philips_europa_demod_sleep;
			fe0->dvb.frontend->ops.tuner_ops.init = philips_europa_tuner_init;
			fe0->dvb.frontend->ops.tuner_ops.sleep = philips_europa_tuner_sleep;
			fe0->dvb.frontend->ops.tuner_ops.set_params = philips_td1316_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_VIDEOMATE_DVBT_200:
		fe0->dvb.frontend = dvb_attach(tda10046_attach,
					       &philips_tu1216_61_config,
					       &dev->i2c_adap);
		if (fe0->dvb.frontend) {
			fe0->dvb.frontend->ops.tuner_ops.init = philips_tu1216_init;
			fe0->dvb.frontend->ops.tuner_ops.set_params = philips_tda6651_pll_set;
		}
		break;
	case SAA7134_BOARD_KWORLD_DVBT_210:
		if (configure_tda827x_fe(dev, &kworld_dvb_t_210_config,
					 &tda827x_cfg_2) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_HAUPPAUGE_HVR1120:
		fe0->dvb.frontend = dvb_attach(tda10048_attach,
					       &hcw_tda10048_config,
					       &dev->i2c_adap);
		if (fe0->dvb.frontend != NULL) {
			dvb_attach(tda829x_attach, fe0->dvb.frontend,
				   &dev->i2c_adap, 0x4b,
				   &tda829x_no_probe);
			dvb_attach(tda18271_attach, fe0->dvb.frontend,
				   0x60, &dev->i2c_adap,
				   &hcw_tda18271_config);
		}
		break;
	case SAA7134_BOARD_PHILIPS_TIGER:
		if (configure_tda827x_fe(dev, &philips_tiger_config,
					 &tda827x_cfg_0) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_PINNACLE_PCTV_310i:
		if (configure_tda827x_fe(dev, &pinnacle_pctv_310i_config,
					 &tda827x_cfg_1) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_HAUPPAUGE_HVR1110:
		if (configure_tda827x_fe(dev, &hauppauge_hvr_1110_config,
					 &tda827x_cfg_1) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_HAUPPAUGE_HVR1150:
		fe0->dvb.frontend = dvb_attach(lgdt3305_attach,
					       &hcw_lgdt3305_config,
					       &dev->i2c_adap);
		if (fe0->dvb.frontend) {
			dvb_attach(tda829x_attach, fe0->dvb.frontend,
				   &dev->i2c_adap, 0x4b,
				   &tda829x_no_probe);
			dvb_attach(tda18271_attach, fe0->dvb.frontend,
				   0x60, &dev->i2c_adap,
				   &hcw_tda18271_config);
		}
		break;
	case SAA7134_BOARD_ASUSTeK_P7131_DUAL:
		if (configure_tda827x_fe(dev, &asus_p7131_dual_config,
					 &tda827x_cfg_0) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_FLYDVBT_LR301:
		if (configure_tda827x_fe(dev, &tda827x_lifeview_config,
					 &tda827x_cfg_0) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_FLYDVB_TRIO:
		if (!use_frontend) {	/* terrestrial */
			if (configure_tda827x_fe(dev, &lifeview_trio_config,
						 &tda827x_cfg_0) < 0)
				goto dettach_frontend;
		} else {  		/* satellite */
			fe0->dvb.frontend = dvb_attach(tda10086_attach, &flydvbs, &dev->i2c_adap);
			if (fe0->dvb.frontend) {
				if (dvb_attach(tda826x_attach, fe0->dvb.frontend, 0x63,
									&dev->i2c_adap, 0) == NULL) {
					wprintk("%s: Lifeview Trio, No tda826x found!\n", __func__);
					goto dettach_frontend;
				}
				if (dvb_attach(isl6421_attach, fe0->dvb.frontend, &dev->i2c_adap,
										0x08, 0, 0) == NULL) {
					wprintk("%s: Lifeview Trio, No ISL6421 found!\n", __func__);
					goto dettach_frontend;
				}
			}
		}
		break;
	case SAA7134_BOARD_ADS_DUO_CARDBUS_PTV331:
	case SAA7134_BOARD_FLYDVBT_HYBRID_CARDBUS:
		fe0->dvb.frontend = dvb_attach(tda10046_attach,
					       &ads_tech_duo_config,
					       &dev->i2c_adap);
		if (fe0->dvb.frontend) {
			if (dvb_attach(tda827x_attach,fe0->dvb.frontend,
				   ads_tech_duo_config.tuner_address, &dev->i2c_adap,
								&ads_duo_cfg) == NULL) {
				wprintk("no tda827x tuner found at addr: %02x\n",
					ads_tech_duo_config.tuner_address);
				goto dettach_frontend;
			}
		} else
			wprintk("failed to attach tda10046\n");
		break;
	case SAA7134_BOARD_TEVION_DVBT_220RF:
		if (configure_tda827x_fe(dev, &tevion_dvbt220rf_config,
					 &tda827x_cfg_0) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_MEDION_MD8800_QUADRO:
		if (!use_frontend) {     /* terrestrial */
			if (configure_tda827x_fe(dev, &md8800_dvbt_config,
						 &tda827x_cfg_0) < 0)
				goto dettach_frontend;
		} else {        /* satellite */
			fe0->dvb.frontend = dvb_attach(tda10086_attach,
							&flydvbs, &dev->i2c_adap);
			if (fe0->dvb.frontend) {
				struct dvb_frontend *fe = fe0->dvb.frontend;
				u8 dev_id = dev->eedata[2];
				u8 data = 0xc4;
				struct i2c_msg msg = {.addr = 0x08, .flags = 0, .len = 1};

				if (dvb_attach(tda826x_attach, fe0->dvb.frontend,
						0x60, &dev->i2c_adap, 0) == NULL) {
					wprintk("%s: Medion Quadro, no tda826x "
						"found !\n", __func__);
					goto dettach_frontend;
				}
				if (dev_id != 0x08) {
					/* we need to open the i2c gate (we know it exists) */
					fe->ops.i2c_gate_ctrl(fe, 1);
					if (dvb_attach(isl6405_attach, fe,
							&dev->i2c_adap, 0x08, 0, 0) == NULL) {
						wprintk("%s: Medion Quadro, no ISL6405 "
							"found !\n", __func__);
						goto dettach_frontend;
					}
					if (dev_id == 0x07) {
						/* fire up the 2nd section of the LNB supply since
						   we can't do this from the other section */
						msg.buf = &data;
						i2c_transfer(&dev->i2c_adap, &msg, 1);
					}
					fe->ops.i2c_gate_ctrl(fe, 0);
					dev->original_set_voltage = fe->ops.set_voltage;
					fe->ops.set_voltage = md8800_set_voltage;
					dev->original_set_high_voltage = fe->ops.enable_high_lnb_voltage;
					fe->ops.enable_high_lnb_voltage = md8800_set_high_voltage;
				} else {
					fe->ops.set_voltage = md8800_set_voltage2;
					fe->ops.enable_high_lnb_voltage = md8800_set_high_voltage2;
				}
			}
		}
		break;
	case SAA7134_BOARD_AVERMEDIA_AVERTVHD_A180:
		fe0->dvb.frontend = dvb_attach(nxt200x_attach, &avertvhda180,
					       &dev->i2c_adap);
		if (fe0->dvb.frontend)
			dvb_attach(dvb_pll_attach, fe0->dvb.frontend, 0x61,
				   NULL, DVB_PLL_TDHU2);
		break;
	case SAA7134_BOARD_ADS_INSTANT_HDTV_PCI:
	case SAA7134_BOARD_KWORLD_ATSC110:
		fe0->dvb.frontend = dvb_attach(nxt200x_attach, &kworldatsc110,
					       &dev->i2c_adap);
		if (fe0->dvb.frontend)
			dvb_attach(simple_tuner_attach, fe0->dvb.frontend,
				   &dev->i2c_adap, 0x61,
				   TUNER_PHILIPS_TUV1236D);
		break;
	case SAA7134_BOARD_FLYDVBS_LR300:
		fe0->dvb.frontend = dvb_attach(tda10086_attach, &flydvbs,
					       &dev->i2c_adap);
		if (fe0->dvb.frontend) {
			if (dvb_attach(tda826x_attach, fe0->dvb.frontend, 0x60,
				       &dev->i2c_adap, 0) == NULL) {
				wprintk("%s: No tda826x found!\n", __func__);
				goto dettach_frontend;
			}
			if (dvb_attach(isl6421_attach, fe0->dvb.frontend,
				       &dev->i2c_adap, 0x08, 0, 0) == NULL) {
				wprintk("%s: No ISL6421 found!\n", __func__);
				goto dettach_frontend;
			}
		}
		break;
	case SAA7134_BOARD_ASUS_EUROPA2_HYBRID:
		fe0->dvb.frontend = dvb_attach(tda10046_attach,
					       &medion_cardbus,
					       &dev->i2c_adap);
		if (fe0->dvb.frontend) {
			dev->original_demod_sleep = fe0->dvb.frontend->ops.sleep;
			fe0->dvb.frontend->ops.sleep = philips_europa_demod_sleep;

			dvb_attach(simple_tuner_attach, fe0->dvb.frontend,
				   &dev->i2c_adap, medion_cardbus.tuner_address,
				   TUNER_PHILIPS_FMD1216ME_MK3);
		}
		break;
	case SAA7134_BOARD_VIDEOMATE_DVBT_200A:
		fe0->dvb.frontend = dvb_attach(tda10046_attach,
				&philips_europa_config,
				&dev->i2c_adap);
		if (fe0->dvb.frontend) {
			fe0->dvb.frontend->ops.tuner_ops.init = philips_td1316_tuner_init;
			fe0->dvb.frontend->ops.tuner_ops.set_params = philips_td1316_tuner_set_params;
		}
		break;
	case SAA7134_BOARD_CINERGY_HT_PCMCIA:
		if (configure_tda827x_fe(dev, &cinergy_ht_config,
					 &tda827x_cfg_0) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_CINERGY_HT_PCI:
		if (configure_tda827x_fe(dev, &cinergy_ht_pci_config,
					 &tda827x_cfg_0) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_PHILIPS_TIGER_S:
		if (configure_tda827x_fe(dev, &philips_tiger_s_config,
					 &tda827x_cfg_2) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_ASUS_P7131_4871:
		if (configure_tda827x_fe(dev, &asus_p7131_4871_config,
					 &tda827x_cfg_2) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_ASUSTeK_P7131_HYBRID_LNA:
		if (configure_tda827x_fe(dev, &asus_p7131_hybrid_lna_config,
					 &tda827x_cfg_2) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_AVERMEDIA_SUPER_007:
		if (configure_tda827x_fe(dev, &avermedia_super_007_config,
					 &tda827x_cfg_0) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_TWINHAN_DTV_DVB_3056:
		if (configure_tda827x_fe(dev, &twinhan_dtv_dvb_3056_config,
					 &tda827x_cfg_2_sw42) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_PHILIPS_SNAKE:
		fe0->dvb.frontend = dvb_attach(tda10086_attach, &flydvbs,
						&dev->i2c_adap);
		if (fe0->dvb.frontend) {
			if (dvb_attach(tda826x_attach, fe0->dvb.frontend, 0x60,
					&dev->i2c_adap, 0) == NULL) {
				wprintk("%s: No tda826x found!\n", __func__);
				goto dettach_frontend;
			}
			if (dvb_attach(lnbp21_attach, fe0->dvb.frontend,
					&dev->i2c_adap, 0, 0) == NULL) {
				wprintk("%s: No lnbp21 found!\n", __func__);
				goto dettach_frontend;
			}
		}
		break;
	case SAA7134_BOARD_CREATIX_CTX953:
		if (configure_tda827x_fe(dev, &md8800_dvbt_config,
					 &tda827x_cfg_0) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_MSI_TVANYWHERE_AD11:
		if (configure_tda827x_fe(dev, &philips_tiger_s_config,
					 &tda827x_cfg_2) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_AVERMEDIA_CARDBUS_506:
		dprintk("AverMedia E506R dvb setup\n");
		saa7134_set_gpio(dev, 25, 0);
		msleep(10);
		saa7134_set_gpio(dev, 25, 1);
		fe0->dvb.frontend = dvb_attach(mt352_attach,
						&avermedia_xc3028_mt352_dev,
						&dev->i2c_adap);
		attach_xc3028 = 1;
		break;
	case SAA7134_BOARD_MD7134_BRIDGE_2:
		fe0->dvb.frontend = dvb_attach(tda10086_attach,
						&sd1878_4m, &dev->i2c_adap);
		if (fe0->dvb.frontend) {
			struct dvb_frontend *fe;
			if (dvb_attach(dvb_pll_attach, fe0->dvb.frontend, 0x60,
				  &dev->i2c_adap, DVB_PLL_PHILIPS_SD1878_TDA8261) == NULL) {
				wprintk("%s: MD7134 DVB-S, no SD1878 "
					"found !\n", __func__);
				goto dettach_frontend;
			}
			/* we need to open the i2c gate (we know it exists) */
			fe = fe0->dvb.frontend;
			fe->ops.i2c_gate_ctrl(fe, 1);
			if (dvb_attach(isl6405_attach, fe,
					&dev->i2c_adap, 0x08, 0, 0) == NULL) {
				wprintk("%s: MD7134 DVB-S, no ISL6405 "
					"found !\n", __func__);
				goto dettach_frontend;
			}
			fe->ops.i2c_gate_ctrl(fe, 0);
			dev->original_set_voltage = fe->ops.set_voltage;
			fe->ops.set_voltage = md8800_set_voltage;
			dev->original_set_high_voltage = fe->ops.enable_high_lnb_voltage;
			fe->ops.enable_high_lnb_voltage = md8800_set_high_voltage;
		}
		break;
	case SAA7134_BOARD_AVERMEDIA_M103:
		saa7134_set_gpio(dev, 25, 0);
		msleep(10);
		saa7134_set_gpio(dev, 25, 1);
		fe0->dvb.frontend = dvb_attach(mt352_attach,
						&avermedia_xc3028_mt352_dev,
						&dev->i2c_adap);
		attach_xc3028 = 1;
		break;
	case SAA7134_BOARD_ASUSTeK_TIGER_3IN1:
		if (!use_frontend) {     /* terrestrial */
			if (configure_tda827x_fe(dev, &asus_tiger_3in1_config,
							&tda827x_cfg_2) < 0)
				goto dettach_frontend;
		} else {  		/* satellite */
			fe0->dvb.frontend = dvb_attach(tda10086_attach,
						&flydvbs, &dev->i2c_adap);
			if (fe0->dvb.frontend) {
				if (dvb_attach(tda826x_attach,
						fe0->dvb.frontend, 0x60,
						&dev->i2c_adap, 0) == NULL) {
					wprintk("%s: Asus Tiger 3in1, no "
						"tda826x found!\n", __func__);
					goto dettach_frontend;
				}
				if (dvb_attach(lnbp21_attach, fe0->dvb.frontend,
						&dev->i2c_adap, 0, 0) == NULL) {
					wprintk("%s: Asus Tiger 3in1, no lnbp21"
						" found!\n", __func__);
					goto dettach_frontend;
				}
			}
		}
		break;
	case SAA7134_BOARD_ASUSTeK_TIGER:
		if (configure_tda827x_fe(dev, &philips_tiger_config,
					 &tda827x_cfg_0) < 0)
			goto dettach_frontend;
		break;
	case SAA7134_BOARD_BEHOLD_H6:
		fe0->dvb.frontend = dvb_attach(zl10353_attach,
						&behold_h6_config,
						&dev->i2c_adap);
		if (fe0->dvb.frontend) {
			dvb_attach(simple_tuner_attach, fe0->dvb.frontend,
				   &dev->i2c_adap, 0x61,
				   TUNER_PHILIPS_FMD1216ME_MK3);
		}
		break;
	case SAA7134_BOARD_AVERMEDIA_A700_PRO:
	case SAA7134_BOARD_AVERMEDIA_A700_HYBRID:
		/* Zarlink ZL10313 */
		fe0->dvb.frontend = dvb_attach(mt312_attach,
			&avertv_a700_mt312, &dev->i2c_adap);
		if (fe0->dvb.frontend) {
			if (dvb_attach(zl10036_attach, fe0->dvb.frontend,
					&avertv_a700_tuner, &dev->i2c_adap) == NULL) {
				wprintk("%s: No zl10036 found!\n",
					__func__);
			}
		}
		break;
	default:
		wprintk("Huh? unknown DVB card?\n");
		break;
	}

	if (attach_xc3028) {
		struct dvb_frontend *fe;
		struct xc2028_config cfg = {
			.i2c_adap  = &dev->i2c_adap,
			.i2c_addr  = 0x61,
		};

		if (!fe0->dvb.frontend)
			goto dettach_frontend;

		fe = dvb_attach(xc2028_attach, fe0->dvb.frontend, &cfg);
		if (!fe) {
			printk(KERN_ERR "%s/2: xc3028 attach failed\n",
			       dev->name);
			goto dettach_frontend;
		}
	}

	if (NULL == fe0->dvb.frontend) {
		printk(KERN_ERR "%s/dvb: frontend initialization failed\n", dev->name);
		goto dettach_frontend;
	}
	/* define general-purpose callback pointer */
	fe0->dvb.frontend->callback = saa7134_tuner_callback;

	/* register everything else */
	ret = videobuf_dvb_register_bus(&dev->frontends, THIS_MODULE, dev,
		&dev->pci->dev, adapter_nr, 0);

	/* this sequence is necessary to make the tda1004x load its firmware
	 * and to enter analog mode of hybrid boards
	 */
	if (!ret) {
		if (fe0->dvb.frontend->ops.init)
			fe0->dvb.frontend->ops.init(fe0->dvb.frontend);
		if (fe0->dvb.frontend->ops.sleep)
			fe0->dvb.frontend->ops.sleep(fe0->dvb.frontend);
		if (fe0->dvb.frontend->ops.tuner_ops.sleep)
			fe0->dvb.frontend->ops.tuner_ops.sleep(fe0->dvb.frontend);
	}
	return ret;

dettach_frontend:
	videobuf_dvb_dealloc_frontends(&dev->frontends);
	return -EINVAL;
}

static int dvb_fini(struct saa7134_dev *dev)
{
	struct videobuf_dvb_frontend *fe0;

	/* Get the first frontend */
	fe0 = videobuf_dvb_get_frontend(&dev->frontends, 1);
	if (!fe0)
		return -EINVAL;

	/* FIXME: I suspect that this code is bogus, since the entry for
	   Pinnacle 300I DVB-T PAL already defines the proper init to allow
	   the detection of mt2032 (TDA9887_PORT2_INACTIVE)
	 */
	if (dev->board == SAA7134_BOARD_PINNACLE_300I_DVBT_PAL) {
		struct v4l2_priv_tun_config tda9887_cfg;
		static int on  = TDA9887_PRESENT | TDA9887_PORT2_INACTIVE;

		tda9887_cfg.tuner = TUNER_TDA9887;
		tda9887_cfg.priv  = &on;

		/* otherwise we don't detect the tuner on next insmod */
		saa_call_all(dev, tuner, s_config, &tda9887_cfg);
	} else if (dev->board == SAA7134_BOARD_MEDION_MD8800_QUADRO) {
		if ((dev->eedata[2] == 0x07) && use_frontend) {
			/* turn off the 2nd lnb supply */
			u8 data = 0x80;
			struct i2c_msg msg = {.addr = 0x08, .buf = &data, .flags = 0, .len = 1};
			struct dvb_frontend *fe;
			fe = fe0->dvb.frontend;
			if (fe->ops.i2c_gate_ctrl) {
				fe->ops.i2c_gate_ctrl(fe, 1);
				i2c_transfer(&dev->i2c_adap, &msg, 1);
				fe->ops.i2c_gate_ctrl(fe, 0);
			}
		}
	}
	videobuf_dvb_unregister_bus(&dev->frontends);
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
