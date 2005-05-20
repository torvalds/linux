/*
 * $Id: saa7134-dvb.c,v 1.12 2005/02/18 12:28:29 kraxel Exp $
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

#include "dvb-pll.h"
#include "mt352.h"
#include "mt352_priv.h" /* FIXME */
#include "tda1004x.h"

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

static int mt352_pinnacle_pll_set(struct dvb_frontend* fe,
				  struct dvb_frontend_parameters* params,
				  u8* pllbuf)
{
	static int on  = TDA9887_PRESENT | TDA9887_PORT2_INACTIVE;
	static int off = TDA9887_PRESENT | TDA9887_PORT2_ACTIVE;
	struct saa7134_dev *dev = fe->dvb->priv;
	struct v4l2_frequency f;

	/* set frequency (mt2050) */
	f.tuner     = 0;
	f.type      = V4L2_TUNER_DIGITAL_TV;
	f.frequency = params->frequency / 1000 * 16 / 1000;
	saa7134_i2c_call_clients(dev,TDA9887_SET_CONFIG,&on);
	saa7134_i2c_call_clients(dev,VIDIOC_S_FREQUENCY,&f);
	saa7134_i2c_call_clients(dev,TDA9887_SET_CONFIG,&off);

	pinnacle_antenna_pwr(dev, antenna_pwr);

	/* mt352 setup */
	mt352_pinnacle_init(fe);
	pllbuf[0] = 0xc2;
	pllbuf[1] = 0x00;
	pllbuf[2] = 0x00;
	pllbuf[3] = 0x80;
	pllbuf[4] = 0x00;
	return 0;
}

static struct mt352_config pinnacle_300i = {
	.demod_address = 0x3c >> 1,
	.adc_clock     = 20333,
	.if2           = 36150,
	.no_tuner      = 1,
	.demod_init    = mt352_pinnacle_init,
	.pll_set       = mt352_pinnacle_pll_set,
};

/* ------------------------------------------------------------------ */

static int medion_cardbus_init(struct dvb_frontend* fe)
{
	/* anything to do here ??? */
	return 0;
}

static int medion_cardbus_pll_set(struct dvb_frontend* fe,
				  struct dvb_frontend_parameters* params)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	struct v4l2_frequency f;

	/*
	 * this instructs tuner.o to set the frequency, the call will
	 * end up in tuner_command(), VIDIOC_S_FREQUENCY switch.
	 * tda9887.o will see that as well.
	 */
	f.tuner     = 0;
	f.type      = V4L2_TUNER_DIGITAL_TV;
	f.frequency = params->frequency / 1000 * 16 / 1000;
	saa7134_i2c_call_clients(dev,VIDIOC_S_FREQUENCY,&f);
	return 0;
}

static int fe_request_firmware(struct dvb_frontend* fe,
			       const struct firmware **fw, char* name)
{
	struct saa7134_dev *dev = fe->dvb->priv;
	return request_firmware(fw, name, &dev->pci->dev);
}

static struct tda1004x_config medion_cardbus = {
	.demod_address = 0x08,  /* not sure this is correct */
	.invert        = 0,
        .invert_oclk   = 0,
        .pll_init      = medion_cardbus_init,
        .pll_set       = medion_cardbus_pll_set,
        .request_firmware = fe_request_firmware,
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
		dev->dvb.frontend = mt352_attach(&pinnacle_300i,
						 &dev->i2c_adap);
		break;
	case SAA7134_BOARD_MD7134:
		dev->dvb.frontend = tda10046_attach(&medion_cardbus,
						    &dev->i2c_adap);
		if (NULL == dev->dvb.frontend)
			printk("%s: Hmm, looks like this is the old MD7134 "
			       "version without DVB-T support\n",dev->name);
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
	return videobuf_dvb_register(&dev->dvb, THIS_MODULE, dev);
}

static int dvb_fini(struct saa7134_dev *dev)
{
	static int on  = TDA9887_PRESENT | TDA9887_PORT2_INACTIVE;

	printk("%s: %s\n",dev->name,__FUNCTION__);

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
