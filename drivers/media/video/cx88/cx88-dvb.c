/*
 *
 * device driver for Conexant 2388x based TV cards
 * MPEG Transport Stream (DVB) routines
 *
 * (c) 2004, 2005 Chris Pascoe <c.pascoe@itee.uq.edu.au>
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
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/file.h>
#include <linux/suspend.h>

#include "cx88.h"
#include "dvb-pll.h"
#include <media/v4l2-common.h>

#include "mt352.h"
#include "mt352_priv.h"
#ifdef HAVE_VP3054_I2C
# include "cx88-vp3054-i2c.h"
#endif
#include "zl10353.h"
#include "cx22702.h"
#include "or51132.h"
#include "lgdt330x.h"
#include "lgh06xf.h"
#include "nxt200x.h"
#include "cx24123.h"
#include "isl6421.h"

MODULE_DESCRIPTION("driver for cx2388x based DVB cards");
MODULE_AUTHOR("Chris Pascoe <c.pascoe@itee.uq.edu.au>");
MODULE_AUTHOR("Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]");
MODULE_LICENSE("GPL");

static unsigned int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug,"enable debug messages [dvb]");

#define dprintk(level,fmt, arg...)	if (debug >= level) \
	printk(KERN_DEBUG "%s/2-dvb: " fmt, core->name, ## arg)

/* ------------------------------------------------------------------ */

static int dvb_buf_setup(struct videobuf_queue *q,
			 unsigned int *count, unsigned int *size)
{
	struct cx8802_dev *dev = q->priv_data;

	dev->ts_packet_size  = 188 * 4;
	dev->ts_packet_count = 32;

	*size  = dev->ts_packet_size * dev->ts_packet_count;
	*count = 32;
	return 0;
}

static int dvb_buf_prepare(struct videobuf_queue *q, struct videobuf_buffer *vb,
			   enum v4l2_field field)
{
	struct cx8802_dev *dev = q->priv_data;
	return cx8802_buf_prepare(q, dev, (struct cx88_buffer*)vb,field);
}

static void dvb_buf_queue(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	struct cx8802_dev *dev = q->priv_data;
	cx8802_buf_queue(dev, (struct cx88_buffer*)vb);
}

static void dvb_buf_release(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	cx88_free_buffer(q, (struct cx88_buffer*)vb);
}

static struct videobuf_queue_ops dvb_qops = {
	.buf_setup    = dvb_buf_setup,
	.buf_prepare  = dvb_buf_prepare,
	.buf_queue    = dvb_buf_queue,
	.buf_release  = dvb_buf_release,
};

/* ------------------------------------------------------------------ */

static int cx88_dvb_bus_ctrl(struct dvb_frontend* fe, int acquire)
{
	struct cx8802_dev *dev= fe->dvb->priv;
	struct cx8802_driver *drv = NULL;
	int ret = 0;

	drv = cx8802_get_driver(dev, CX88_MPEG_DVB);
	if (drv) {
		if(acquire)
			ret = drv->request_acquire(drv);
		else
			ret = drv->request_release(drv);
	}

	return ret;
}

/* ------------------------------------------------------------------ */

static int dvico_fusionhdtv_demod_init(struct dvb_frontend* fe)
{
	static u8 clock_config []  = { CLOCK_CTL,  0x38, 0x39 };
	static u8 reset []         = { RESET,      0x80 };
	static u8 adc_ctl_1_cfg [] = { ADC_CTL_1,  0x40 };
	static u8 agc_cfg []       = { AGC_TARGET, 0x24, 0x20 };
	static u8 gpp_ctl_cfg []   = { GPP_CTL,    0x33 };
	static u8 capt_range_cfg[] = { CAPT_RANGE, 0x32 };

	mt352_write(fe, clock_config,   sizeof(clock_config));
	udelay(200);
	mt352_write(fe, reset,          sizeof(reset));
	mt352_write(fe, adc_ctl_1_cfg,  sizeof(adc_ctl_1_cfg));

	mt352_write(fe, agc_cfg,        sizeof(agc_cfg));
	mt352_write(fe, gpp_ctl_cfg,    sizeof(gpp_ctl_cfg));
	mt352_write(fe, capt_range_cfg, sizeof(capt_range_cfg));
	return 0;
}

static int dvico_dual_demod_init(struct dvb_frontend *fe)
{
	static u8 clock_config []  = { CLOCK_CTL,  0x38, 0x38 };
	static u8 reset []         = { RESET,      0x80 };
	static u8 adc_ctl_1_cfg [] = { ADC_CTL_1,  0x40 };
	static u8 agc_cfg []       = { AGC_TARGET, 0x28, 0x20 };
	static u8 gpp_ctl_cfg []   = { GPP_CTL,    0x33 };
	static u8 capt_range_cfg[] = { CAPT_RANGE, 0x32 };

	mt352_write(fe, clock_config,   sizeof(clock_config));
	udelay(200);
	mt352_write(fe, reset,          sizeof(reset));
	mt352_write(fe, adc_ctl_1_cfg,  sizeof(adc_ctl_1_cfg));

	mt352_write(fe, agc_cfg,        sizeof(agc_cfg));
	mt352_write(fe, gpp_ctl_cfg,    sizeof(gpp_ctl_cfg));
	mt352_write(fe, capt_range_cfg, sizeof(capt_range_cfg));

	return 0;
}

static int dntv_live_dvbt_demod_init(struct dvb_frontend* fe)
{
	static u8 clock_config []  = { 0x89, 0x38, 0x39 };
	static u8 reset []         = { 0x50, 0x80 };
	static u8 adc_ctl_1_cfg [] = { 0x8E, 0x40 };
	static u8 agc_cfg []       = { 0x67, 0x10, 0x23, 0x00, 0xFF, 0xFF,
				       0x00, 0xFF, 0x00, 0x40, 0x40 };
	static u8 dntv_extra[]     = { 0xB5, 0x7A };
	static u8 capt_range_cfg[] = { 0x75, 0x32 };

	mt352_write(fe, clock_config,   sizeof(clock_config));
	udelay(2000);
	mt352_write(fe, reset,          sizeof(reset));
	mt352_write(fe, adc_ctl_1_cfg,  sizeof(adc_ctl_1_cfg));

	mt352_write(fe, agc_cfg,        sizeof(agc_cfg));
	udelay(2000);
	mt352_write(fe, dntv_extra,     sizeof(dntv_extra));
	mt352_write(fe, capt_range_cfg, sizeof(capt_range_cfg));

	return 0;
}

static struct mt352_config dvico_fusionhdtv = {
	.demod_address = 0x0f,
	.demod_init    = dvico_fusionhdtv_demod_init,
};

static struct mt352_config dntv_live_dvbt_config = {
	.demod_address = 0x0f,
	.demod_init    = dntv_live_dvbt_demod_init,
};

static struct mt352_config dvico_fusionhdtv_dual = {
	.demod_address = 0x0f,
	.demod_init    = dvico_dual_demod_init,
};

#ifdef HAVE_VP3054_I2C
static int dntv_live_dvbt_pro_demod_init(struct dvb_frontend* fe)
{
	static u8 clock_config []  = { 0x89, 0x38, 0x38 };
	static u8 reset []         = { 0x50, 0x80 };
	static u8 adc_ctl_1_cfg [] = { 0x8E, 0x40 };
	static u8 agc_cfg []       = { 0x67, 0x10, 0x20, 0x00, 0xFF, 0xFF,
				       0x00, 0xFF, 0x00, 0x40, 0x40 };
	static u8 dntv_extra[]     = { 0xB5, 0x7A };
	static u8 capt_range_cfg[] = { 0x75, 0x32 };

	mt352_write(fe, clock_config,   sizeof(clock_config));
	udelay(2000);
	mt352_write(fe, reset,          sizeof(reset));
	mt352_write(fe, adc_ctl_1_cfg,  sizeof(adc_ctl_1_cfg));

	mt352_write(fe, agc_cfg,        sizeof(agc_cfg));
	udelay(2000);
	mt352_write(fe, dntv_extra,     sizeof(dntv_extra));
	mt352_write(fe, capt_range_cfg, sizeof(capt_range_cfg));

	return 0;
}

static int philips_fmd1216_pll_init(struct dvb_frontend *fe)
{
	struct cx8802_dev *dev= fe->dvb->priv;

	/* this message is to set up ATC and ALC */
	static u8 fmd1216_init[] = { 0x0b, 0xdc, 0x9c, 0xa0 };
	struct i2c_msg msg =
		{ .addr = dev->core->pll_addr, .flags = 0,
		  .buf = fmd1216_init, .len = sizeof(fmd1216_init) };
	int err;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if ((err = i2c_transfer(&dev->core->i2c_adap, &msg, 1)) != 1) {
		if (err < 0)
			return err;
		else
			return -EREMOTEIO;
	}

	return 0;
}

static int dntv_live_dvbt_pro_tuner_set_params(struct dvb_frontend* fe,
					       struct dvb_frontend_parameters* params)
{
	struct cx8802_dev *dev= fe->dvb->priv;
	u8 buf[4];
	struct i2c_msg msg =
		{ .addr = dev->core->pll_addr, .flags = 0,
		  .buf = buf, .len = 4 };
	int err;

	/* Switch PLL to DVB mode */
	err = philips_fmd1216_pll_init(fe);
	if (err)
		return err;

	/* Tune PLL */
	dvb_pll_configure(dev->core->pll_desc, buf,
			  params->frequency,
			  params->u.ofdm.bandwidth);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if ((err = i2c_transfer(&dev->core->i2c_adap, &msg, 1)) != 1) {

		printk(KERN_WARNING "cx88-dvb: %s error "
		       "(addr %02x <- %02x, err = %i)\n",
		       __FUNCTION__, dev->core->pll_addr, buf[0], err);
		if (err < 0)
			return err;
		else
			return -EREMOTEIO;
	}

	return 0;
}

static struct mt352_config dntv_live_dvbt_pro_config = {
	.demod_address = 0x0f,
	.no_tuner      = 1,
	.demod_init    = dntv_live_dvbt_pro_demod_init,
};
#endif

static struct zl10353_config dvico_fusionhdtv_hybrid = {
	.demod_address = 0x0f,
	.no_tuner      = 1,
};

static struct zl10353_config dvico_fusionhdtv_plus_v1_1 = {
	.demod_address = 0x0f,
};

static struct cx22702_config connexant_refboard_config = {
	.demod_address = 0x43,
	.output_mode   = CX22702_SERIAL_OUTPUT,
};

static struct cx22702_config hauppauge_hvr_config = {
	.demod_address = 0x63,
	.output_mode   = CX22702_SERIAL_OUTPUT,
};

static int or51132_set_ts_param(struct dvb_frontend* fe,
				int is_punctured)
{
	struct cx8802_dev *dev= fe->dvb->priv;
	dev->ts_gen_cntrl = is_punctured ? 0x04 : 0x00;
	return 0;
}

static struct or51132_config pchdtv_hd3000 = {
	.demod_address = 0x15,
	.set_ts_params = or51132_set_ts_param,
};

static int lgdt330x_pll_rf_set(struct dvb_frontend* fe, int index)
{
	struct cx8802_dev *dev= fe->dvb->priv;
	struct cx88_core *core = dev->core;

	dprintk(1, "%s: index = %d\n", __FUNCTION__, index);
	if (index == 0)
		cx_clear(MO_GP0_IO, 8);
	else
		cx_set(MO_GP0_IO, 8);
	return 0;
}

static int lgdt330x_set_ts_param(struct dvb_frontend* fe, int is_punctured)
{
	struct cx8802_dev *dev= fe->dvb->priv;
	if (is_punctured)
		dev->ts_gen_cntrl |= 0x04;
	else
		dev->ts_gen_cntrl &= ~0x04;
	return 0;
}

static struct lgdt330x_config fusionhdtv_3_gold = {
	.demod_address = 0x0e,
	.demod_chip    = LGDT3302,
	.serial_mpeg   = 0x04, /* TPSERIAL for 3302 in TOP_CONTROL */
	.set_ts_params = lgdt330x_set_ts_param,
};

static struct lgdt330x_config fusionhdtv_5_gold = {
	.demod_address = 0x0e,
	.demod_chip    = LGDT3303,
	.serial_mpeg   = 0x40, /* TPSERIAL for 3303 in TOP_CONTROL */
	.set_ts_params = lgdt330x_set_ts_param,
};

static struct lgdt330x_config pchdtv_hd5500 = {
	.demod_address = 0x59,
	.demod_chip    = LGDT3303,
	.serial_mpeg   = 0x40, /* TPSERIAL for 3303 in TOP_CONTROL */
	.set_ts_params = lgdt330x_set_ts_param,
};

static int nxt200x_set_ts_param(struct dvb_frontend* fe,
				int is_punctured)
{
	struct cx8802_dev *dev= fe->dvb->priv;
	dev->ts_gen_cntrl = is_punctured ? 0x04 : 0x00;
	return 0;
}

static int nxt200x_set_pll_input(u8* buf, int input)
{
	if (input)
		buf[3] |= 0x08;
	else
		buf[3] &= ~0x08;
	return 0;
}

static struct nxt200x_config ati_hdtvwonder = {
	.demod_address = 0x0a,
	.set_pll_input = nxt200x_set_pll_input,
	.set_ts_params = nxt200x_set_ts_param,
};

static int cx24123_set_ts_param(struct dvb_frontend* fe,
	int is_punctured)
{
	struct cx8802_dev *dev= fe->dvb->priv;
	dev->ts_gen_cntrl = 0x02;
	return 0;
}

static int kworld_dvbs_100_set_voltage(struct dvb_frontend* fe,
				       fe_sec_voltage_t voltage)
{
	struct cx8802_dev *dev= fe->dvb->priv;
	struct cx88_core *core = dev->core;

	if (voltage == SEC_VOLTAGE_OFF) {
		cx_write(MO_GP0_IO, 0x000006fb);
	} else {
		cx_write(MO_GP0_IO, 0x000006f9);
	}

	if (core->prev_set_voltage)
		return core->prev_set_voltage(fe, voltage);
	return 0;
}

static int geniatech_dvbs_set_voltage(struct dvb_frontend *fe,
				      fe_sec_voltage_t voltage)
{
	struct cx8802_dev *dev= fe->dvb->priv;
	struct cx88_core *core = dev->core;

	if (voltage == SEC_VOLTAGE_OFF) {
		dprintk(1,"LNB Voltage OFF\n");
		cx_write(MO_GP0_IO, 0x0000efff);
	}

	if (core->prev_set_voltage)
		return core->prev_set_voltage(fe, voltage);
	return 0;
}

static struct cx24123_config geniatech_dvbs_config = {
	.demod_address = 0x55,
	.set_ts_params = cx24123_set_ts_param,
};

static struct cx24123_config hauppauge_novas_config = {
	.demod_address = 0x55,
	.set_ts_params = cx24123_set_ts_param,
};

static struct cx24123_config kworld_dvbs_100_config = {
	.demod_address = 0x15,
	.set_ts_params = cx24123_set_ts_param,
	.lnb_polarity  = 1,
};

static int dvb_register(struct cx8802_dev *dev)
{
	/* init struct videobuf_dvb */
	dev->dvb.name = dev->core->name;
	dev->ts_gen_cntrl = 0x0c;

	/* init frontend */
	switch (dev->core->board) {
	case CX88_BOARD_HAUPPAUGE_DVB_T1:
		dev->dvb.frontend = dvb_attach(cx22702_attach,
					       &connexant_refboard_config,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   &dev->core->i2c_adap,
				   &dvb_pll_thomson_dtt759x);
		}
		break;
	case CX88_BOARD_TERRATEC_CINERGY_1400_DVB_T1:
	case CX88_BOARD_CONEXANT_DVB_T1:
	case CX88_BOARD_KWORLD_DVB_T_CX22702:
	case CX88_BOARD_WINFAST_DTV1000:
		dev->dvb.frontend = dvb_attach(cx22702_attach,
					       &connexant_refboard_config,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x60,
				   &dev->core->i2c_adap,
				   &dvb_pll_thomson_dtt7579);
		}
		break;
	case CX88_BOARD_WINFAST_DTV2000H:
	case CX88_BOARD_HAUPPAUGE_HVR1100:
	case CX88_BOARD_HAUPPAUGE_HVR1100LP:
		dev->dvb.frontend = dvb_attach(cx22702_attach,
					       &hauppauge_hvr_config,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   &dev->core->i2c_adap,
				   &dvb_pll_fmd1216me);
		}
		break;
	case CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_PLUS:
		dev->dvb.frontend = dvb_attach(mt352_attach,
					       &dvico_fusionhdtv,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x60,
				   NULL, &dvb_pll_thomson_dtt7579);
			break;
		}
		/* ZL10353 replaces MT352 on later cards */
		dev->dvb.frontend = dvb_attach(zl10353_attach,
					       &dvico_fusionhdtv_plus_v1_1,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x60,
				   NULL, &dvb_pll_thomson_dtt7579);
		}
		break;
	case CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_DUAL:
		/* The tin box says DEE1601, but it seems to be DTT7579
		 * compatible, with a slightly different MT352 AGC gain. */
		dev->dvb.frontend = dvb_attach(mt352_attach,
					       &dvico_fusionhdtv_dual,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   NULL, &dvb_pll_thomson_dtt7579);
			break;
		}
		/* ZL10353 replaces MT352 on later cards */
		dev->dvb.frontend = dvb_attach(zl10353_attach,
					       &dvico_fusionhdtv_plus_v1_1,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   NULL, &dvb_pll_thomson_dtt7579);
		}
		break;
	case CX88_BOARD_DVICO_FUSIONHDTV_DVB_T1:
		dev->dvb.frontend = dvb_attach(mt352_attach,
					       &dvico_fusionhdtv,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   NULL, &dvb_pll_lg_z201);
		}
		break;
	case CX88_BOARD_KWORLD_DVB_T:
	case CX88_BOARD_DNTV_LIVE_DVB_T:
	case CX88_BOARD_ADSTECH_DVB_T_PCI:
		dev->dvb.frontend = dvb_attach(mt352_attach,
					       &dntv_live_dvbt_config,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   NULL, &dvb_pll_unknown_1);
		}
		break;
	case CX88_BOARD_DNTV_LIVE_DVB_T_PRO:
#ifdef HAVE_VP3054_I2C
		dev->core->pll_addr = 0x61;
		dev->core->pll_desc = &dvb_pll_fmd1216me;
		dev->dvb.frontend = dvb_attach(mt352_attach, &dntv_live_dvbt_pro_config,
			&((struct vp3054_i2c_state *)dev->card_priv)->adap);
		if (dev->dvb.frontend != NULL) {
			dev->dvb.frontend->ops.tuner_ops.set_params = dntv_live_dvbt_pro_tuner_set_params;
		}
#else
		printk("%s: built without vp3054 support\n", dev->core->name);
#endif
		break;
	case CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_HYBRID:
		dev->dvb.frontend = dvb_attach(zl10353_attach,
					       &dvico_fusionhdtv_hybrid,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   &dev->core->i2c_adap,
				   &dvb_pll_thomson_fe6600);
		}
		break;
	case CX88_BOARD_PCHDTV_HD3000:
		dev->dvb.frontend = dvb_attach(or51132_attach,
					       &pchdtv_hd3000,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   &dev->core->i2c_adap,
				   &dvb_pll_thomson_dtt761x);
		}
		break;
	case CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD_Q:
		dev->ts_gen_cntrl = 0x08;
		{
		/* Do a hardware reset of chip before using it. */
		struct cx88_core *core = dev->core;

		cx_clear(MO_GP0_IO, 1);
		mdelay(100);
		cx_set(MO_GP0_IO, 1);
		mdelay(200);

		/* Select RF connector callback */
		fusionhdtv_3_gold.pll_rf_set = lgdt330x_pll_rf_set;
		dev->dvb.frontend = dvb_attach(lgdt330x_attach,
					       &fusionhdtv_3_gold,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   &dev->core->i2c_adap,
				   &dvb_pll_microtune_4042);
		}
		}
		break;
	case CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD_T:
		dev->ts_gen_cntrl = 0x08;
		{
		/* Do a hardware reset of chip before using it. */
		struct cx88_core *core = dev->core;

		cx_clear(MO_GP0_IO, 1);
		mdelay(100);
		cx_set(MO_GP0_IO, 9);
		mdelay(200);
		dev->dvb.frontend = dvb_attach(lgdt330x_attach,
					       &fusionhdtv_3_gold,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   &dev->core->i2c_adap,
				   &dvb_pll_thomson_dtt761x);
		}
		}
		break;
	case CX88_BOARD_DVICO_FUSIONHDTV_5_GOLD:
		dev->ts_gen_cntrl = 0x08;
		{
		/* Do a hardware reset of chip before using it. */
		struct cx88_core *core = dev->core;

		cx_clear(MO_GP0_IO, 1);
		mdelay(100);
		cx_set(MO_GP0_IO, 1);
		mdelay(200);
		dev->dvb.frontend = dvb_attach(lgdt330x_attach,
					       &fusionhdtv_5_gold,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(lgh06xf_attach, dev->dvb.frontend,
				   &dev->core->i2c_adap);
		}
		}
		break;
	case CX88_BOARD_PCHDTV_HD5500:
		dev->ts_gen_cntrl = 0x08;
		{
		/* Do a hardware reset of chip before using it. */
		struct cx88_core *core = dev->core;

		cx_clear(MO_GP0_IO, 1);
		mdelay(100);
		cx_set(MO_GP0_IO, 1);
		mdelay(200);
		dev->dvb.frontend = dvb_attach(lgdt330x_attach,
					       &pchdtv_hd5500,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(lgh06xf_attach, dev->dvb.frontend,
				   &dev->core->i2c_adap);
		}
		}
		break;
	case CX88_BOARD_ATI_HDTVWONDER:
		dev->dvb.frontend = dvb_attach(nxt200x_attach,
					       &ati_hdtvwonder,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   NULL, &dvb_pll_tuv1236d);
		}
		break;
	case CX88_BOARD_HAUPPAUGE_NOVASPLUS_S1:
	case CX88_BOARD_HAUPPAUGE_NOVASE2_S1:
		dev->dvb.frontend = dvb_attach(cx24123_attach,
					       &hauppauge_novas_config,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend) {
			dvb_attach(isl6421_attach, dev->dvb.frontend,
				   &dev->core->i2c_adap, 0x08, 0x00, 0x00);
		}
		break;
	case CX88_BOARD_KWORLD_DVBS_100:
		dev->dvb.frontend = dvb_attach(cx24123_attach,
					       &kworld_dvbs_100_config,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend) {
			dev->core->prev_set_voltage = dev->dvb.frontend->ops.set_voltage;
			dev->dvb.frontend->ops.set_voltage = kworld_dvbs_100_set_voltage;
		}
		break;
	case CX88_BOARD_GENIATECH_DVBS:
		dev->dvb.frontend = dvb_attach(cx24123_attach,
					       &geniatech_dvbs_config,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend) {
			dev->core->prev_set_voltage = dev->dvb.frontend->ops.set_voltage;
			dev->dvb.frontend->ops.set_voltage = geniatech_dvbs_set_voltage;
		}
		break;
	case CX88_BOARD_HAUPPAUGE_HVR1300:
		dev->dvb.frontend = dvb_attach(cx22702_attach,
					       &hauppauge_hvr_config,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   &dev->core->i2c_adap,
				   &dvb_pll_fmd1216me);
		}
		break;
	case CX88_BOARD_HAUPPAUGE_HVR3000:
		dev->dvb.frontend = dvb_attach(cx22702_attach,
					       &hauppauge_hvr_config,
					       &dev->core->i2c_adap);
		if (dev->dvb.frontend != NULL) {
			dvb_attach(dvb_pll_attach, dev->dvb.frontend, 0x61,
				   &dev->core->i2c_adap,
				   &dvb_pll_fmd1216me);
		}
		break;
	default:
		printk("%s: The frontend of your DVB/ATSC card isn't supported yet\n",
		       dev->core->name);
		break;
	}
	if (NULL == dev->dvb.frontend) {
		printk("%s: frontend initialization failed\n",dev->core->name);
		return -1;
	}

	if (dev->core->pll_desc) {
		dev->dvb.frontend->ops.info.frequency_min = dev->core->pll_desc->min;
		dev->dvb.frontend->ops.info.frequency_max = dev->core->pll_desc->max;
	}
	/* Ensure all frontends negotiate bus access */
	dev->dvb.frontend->ops.ts_bus_ctrl = cx88_dvb_bus_ctrl;

	/* Put the analog decoder in standby to keep it quiet */
	cx88_call_i2c_clients (dev->core, TUNER_SET_STANDBY, NULL);

	/* register everything */
	return videobuf_dvb_register(&dev->dvb, THIS_MODULE, dev, &dev->pci->dev);
}

/* ----------------------------------------------------------- */

/* CX8802 MPEG -> mini driver - We have been given the hardware */
static int cx8802_dvb_advise_acquire(struct cx8802_driver *drv)
{
	struct cx88_core *core = drv->core;
	int err = 0;
	dprintk( 1, "%s\n", __FUNCTION__);

	switch (core->board) {
	case CX88_BOARD_HAUPPAUGE_HVR1300:
		/* We arrive here with either the cx23416 or the cx22702
		 * on the bus. Take the bus from the cx23416 and enable the
		 * cx22702 demod
		 */
		cx_set(MO_GP0_IO,   0x00000080); /* cx22702 out of reset and enable */
		cx_clear(MO_GP0_IO, 0x00000004);
		udelay(1000);
		break;
	default:
		err = -ENODEV;
	}
	return err;
}

/* CX8802 MPEG -> mini driver - We no longer have the hardware */
static int cx8802_dvb_advise_release(struct cx8802_driver *drv)
{
	struct cx88_core *core = drv->core;
	int err = 0;
	dprintk( 1, "%s\n", __FUNCTION__);

	switch (core->board) {
	case CX88_BOARD_HAUPPAUGE_HVR1300:
		/* Do Nothing, leave the cx22702 on the bus. */
		break;
	default:
		err = -ENODEV;
	}
	return err;
}

static int cx8802_dvb_probe(struct cx8802_driver *drv)
{
	struct cx88_core *core = drv->core;
	struct cx8802_dev *dev = drv->core->dvbdev;
	int err;

	dprintk( 1, "%s\n", __FUNCTION__);
	dprintk( 1, " ->being probed by Card=%d Name=%s, PCI %02x:%02x\n",
		core->board,
		core->name,
		core->pci_bus,
		core->pci_slot);

	err = -ENODEV;
	if (!(cx88_boards[core->board].mpeg & CX88_MPEG_DVB))
		goto fail_core;

#ifdef HAVE_VP3054_I2C
	err = vp3054_i2c_probe(dev);
	if (0 != err)
		goto fail_core;
#endif

	/* dvb stuff */
	printk("%s/2: cx2388x based dvb card\n", core->name);
	videobuf_queue_init(&dev->dvb.dvbq, &dvb_qops,
			    dev->pci, &dev->slock,
			    V4L2_BUF_TYPE_VIDEO_CAPTURE,
			    V4L2_FIELD_TOP,
			    sizeof(struct cx88_buffer),
			    dev);
	err = dvb_register(dev);
	if (err != 0)
		printk("%s dvb_register failed err = %d\n", __FUNCTION__, err);

 fail_core:
	return err;
}

static int cx8802_dvb_remove(struct cx8802_driver *drv)
{
	struct cx8802_dev *dev = drv->core->dvbdev;

	/* dvb */
	videobuf_dvb_unregister(&dev->dvb);

#ifdef HAVE_VP3054_I2C
	vp3054_i2c_remove(dev);
#endif

	return 0;
}

static struct cx8802_driver cx8802_dvb_driver = {
	.type_id        = CX88_MPEG_DVB,
	.hw_access      = CX8802_DRVCTL_SHARED,
	.probe          = cx8802_dvb_probe,
	.remove         = cx8802_dvb_remove,
	.advise_acquire = cx8802_dvb_advise_acquire,
	.advise_release = cx8802_dvb_advise_release,
};

static int dvb_init(void)
{
	printk(KERN_INFO "cx2388x dvb driver version %d.%d.%d loaded\n",
	       (CX88_VERSION_CODE >> 16) & 0xff,
	       (CX88_VERSION_CODE >>  8) & 0xff,
	       CX88_VERSION_CODE & 0xff);
#ifdef SNAPSHOT
	printk(KERN_INFO "cx2388x: snapshot date %04d-%02d-%02d\n",
	       SNAPSHOT/10000, (SNAPSHOT/100)%100, SNAPSHOT%100);
#endif
	return cx8802_register_driver(&cx8802_dvb_driver);
}

static void dvb_fini(void)
{
	cx8802_unregister_driver(&cx8802_dvb_driver);
}

module_init(dvb_init);
module_exit(dvb_fini);

/*
 * Local variables:
 * c-basic-offset: 8
 * compile-command: "make DVB=1"
 * End:
 */
