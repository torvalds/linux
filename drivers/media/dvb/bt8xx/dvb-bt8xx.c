/*
 * Bt8xx based DVB adapter driver
 *
 * Copyright (C) 2002,2003 Florian Schirmer <jolt@tuxbox.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb-bt8xx.h"
#include "bt878.h"

static int debug;

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off debugging (default:off).");

#define dprintk( args... ) \
	do \
		if (debug) printk(KERN_DEBUG args); \
	while (0)

#define IF_FREQUENCYx6 217    /* 6 * 36.16666666667MHz */

static void dvb_bt8xx_task(unsigned long data)
{
	struct dvb_bt8xx_card *card = (struct dvb_bt8xx_card *)data;

	//printk("%d ", card->bt->finished_block);

	while (card->bt->last_block != card->bt->finished_block) {
		(card->bt->TS_Size ? dvb_dmx_swfilter_204 : dvb_dmx_swfilter)
			(&card->demux,
			 &card->bt->buf_cpu[card->bt->last_block *
					    card->bt->block_bytes],
			 card->bt->block_bytes);
		card->bt->last_block = (card->bt->last_block + 1) %
					card->bt->block_count;
	}
}

static int dvb_bt8xx_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux*dvbdmx = dvbdmxfeed->demux;
	struct dvb_bt8xx_card *card = dvbdmx->priv;
	int rc;

	dprintk("dvb_bt8xx: start_feed\n");

	if (!dvbdmx->dmx.frontend)
		return -EINVAL;

	mutex_lock(&card->lock);
	card->nfeeds++;
	rc = card->nfeeds;
	if (card->nfeeds == 1)
		bt878_start(card->bt, card->gpio_mode,
			    card->op_sync_orin, card->irq_err_ignore);
	mutex_unlock(&card->lock);
	return rc;
}

static int dvb_bt8xx_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct dvb_bt8xx_card *card = dvbdmx->priv;

	dprintk("dvb_bt8xx: stop_feed\n");

	if (!dvbdmx->dmx.frontend)
		return -EINVAL;

	mutex_lock(&card->lock);
	card->nfeeds--;
	if (card->nfeeds == 0)
		bt878_stop(card->bt);
	mutex_unlock(&card->lock);

	return 0;
}

static int is_pci_slot_eq(struct pci_dev* adev, struct pci_dev* bdev)
{
	if ((adev->subsystem_vendor == bdev->subsystem_vendor) &&
		(adev->subsystem_device == bdev->subsystem_device) &&
		(adev->bus->number == bdev->bus->number) &&
		(PCI_SLOT(adev->devfn) == PCI_SLOT(bdev->devfn)))
		return 1;
	return 0;
}

static struct bt878 __devinit *dvb_bt8xx_878_match(unsigned int bttv_nr, struct pci_dev* bttv_pci_dev)
{
	unsigned int card_nr;

	/* Hmm, n squared. Hope n is small */
	for (card_nr = 0; card_nr < bt878_num; card_nr++)
		if (is_pci_slot_eq(bt878[card_nr].dev, bttv_pci_dev))
			return &bt878[card_nr];
	return NULL;
}

static int thomson_dtt7579_demod_init(struct dvb_frontend* fe)
{
	static u8 mt352_clock_config [] = { 0x89, 0x38, 0x38 };
	static u8 mt352_reset [] = { 0x50, 0x80 };
	static u8 mt352_adc_ctl_1_cfg [] = { 0x8E, 0x40 };
	static u8 mt352_agc_cfg [] = { 0x67, 0x28, 0x20 };
	static u8 mt352_gpp_ctl_cfg [] = { 0x8C, 0x33 };
	static u8 mt352_capt_range_cfg[] = { 0x75, 0x32 };

	mt352_write(fe, mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(fe, mt352_reset, sizeof(mt352_reset));
	mt352_write(fe, mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));

	mt352_write(fe, mt352_agc_cfg, sizeof(mt352_agc_cfg));
	mt352_write(fe, mt352_gpp_ctl_cfg, sizeof(mt352_gpp_ctl_cfg));
	mt352_write(fe, mt352_capt_range_cfg, sizeof(mt352_capt_range_cfg));

	return 0;
}

static int thomson_dtt7579_tuner_calc_regs(struct dvb_frontend* fe, struct dvb_frontend_parameters* params, u8* pllbuf, int buf_len)
{
	u32 div;
	unsigned char bs = 0;
	unsigned char cp = 0;

	if (buf_len < 5)
		return -EINVAL;

	div = (((params->frequency + 83333) * 3) / 500000) + IF_FREQUENCYx6;

	if (params->frequency < 542000000)
		cp = 0xb4;
	else if (params->frequency < 771000000)
		cp = 0xbc;
	else
		cp = 0xf4;

	if (params->frequency == 0)
		bs = 0x03;
	else if (params->frequency < 443250000)
		bs = 0x02;
	else
		bs = 0x08;

	pllbuf[0] = 0x60;
	pllbuf[1] = div >> 8;
	pllbuf[2] = div & 0xff;
	pllbuf[3] = cp;
	pllbuf[4] = bs;

	return 5;
}

static struct mt352_config thomson_dtt7579_config = {
	.demod_address = 0x0f,
	.demod_init = thomson_dtt7579_demod_init,
};

static struct zl10353_config thomson_dtt7579_zl10353_config = {
	.demod_address = 0x0f,
};

static int cx24108_tuner_set_params(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	u32 freq = params->frequency;

	int i, a, n, pump;
	u32 band, pll;

	u32 osci[]={950000,1019000,1075000,1178000,1296000,1432000,
		1576000,1718000,1856000,2036000,2150000};
	u32 bandsel[]={0,0x00020000,0x00040000,0x00100800,0x00101000,
		0x00102000,0x00104000,0x00108000,0x00110000,
		0x00120000,0x00140000};

	#define XTAL 1011100 /* Hz, really 1.0111 MHz and a /10 prescaler */
	printk("cx24108 debug: entering SetTunerFreq, freq=%d\n",freq);

	/* This is really the bit driving the tuner chip cx24108 */

	if (freq<950000)
		freq = 950000; /* kHz */
	else if (freq>2150000)
		freq = 2150000; /* satellite IF is 950..2150MHz */

	/* decide which VCO to use for the input frequency */
	for(i=1;(i<sizeof(osci)/sizeof(osci[0]))&&(osci[i]<freq);i++);
	printk("cx24108 debug: select vco #%d (f=%d)\n",i,freq);
	band=bandsel[i];
	/* the gain values must be set by SetSymbolrate */
	/* compute the pll divider needed, from Conexant data sheet,
	   resolved for (n*32+a), remember f(vco) is f(receive) *2 or *4,
	   depending on the divider bit. It is set to /4 on the 2 lowest
	   bands  */
	n=((i<=2?2:1)*freq*10L)/(XTAL/100);
	a=n%32; n/=32; if(a==0) n--;
	pump=(freq<(osci[i-1]+osci[i])/2);
	pll=0xf8000000|
	    ((pump?1:2)<<(14+11))|
	    ((n&0x1ff)<<(5+11))|
	    ((a&0x1f)<<11);
	/* everything is shifted left 11 bits to left-align the bits in the
	   32bit word. Output to the tuner goes MSB-aligned, after all */
	printk("cx24108 debug: pump=%d, n=%d, a=%d\n",pump,n,a);
	cx24110_pll_write(fe,band);
	/* set vga and vca to their widest-band settings, as a precaution.
	   SetSymbolrate might not be called to set this up */
	cx24110_pll_write(fe,0x500c0000);
	cx24110_pll_write(fe,0x83f1f800);
	cx24110_pll_write(fe,pll);
	//writereg(client,0x56,0x7f);

	return 0;
}

static int pinnsat_tuner_init(struct dvb_frontend* fe)
{
	struct dvb_bt8xx_card *card = fe->dvb->priv;

	bttv_gpio_enable(card->bttv_nr, 1, 1);  /* output */
	bttv_write_gpio(card->bttv_nr, 1, 1);   /* relay on */

	return 0;
}

static int pinnsat_tuner_sleep(struct dvb_frontend* fe)
{
	struct dvb_bt8xx_card *card = fe->dvb->priv;

	bttv_write_gpio(card->bttv_nr, 1, 0);   /* relay off */

	return 0;
}

static struct cx24110_config pctvsat_config = {
	.demod_address = 0x55,
};

static int microtune_mt7202dtf_tuner_set_params(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct dvb_bt8xx_card *card = (struct dvb_bt8xx_card *) fe->dvb->priv;
	u8 cfg, cpump, band_select;
	u8 data[4];
	u32 div;
	struct i2c_msg msg = { .addr = 0x60, .flags = 0, .buf = data, .len = sizeof(data) };

	div = (36000000 + params->frequency + 83333) / 166666;
	cfg = 0x88;

	if (params->frequency < 175000000)
		cpump = 2;
	else if (params->frequency < 390000000)
		cpump = 1;
	else if (params->frequency < 470000000)
		cpump = 2;
	else if (params->frequency < 750000000)
		cpump = 2;
	else
		cpump = 3;

	if (params->frequency < 175000000)
		band_select = 0x0e;
	else if (params->frequency < 470000000)
		band_select = 0x05;
	else
		band_select = 0x03;

	data[0] = (div >> 8) & 0x7f;
	data[1] = div & 0xff;
	data[2] = ((div >> 10) & 0x60) | cfg;
	data[3] = (cpump << 6) | band_select;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(card->i2c_adapter, &msg, 1);
	return (div * 166666 - 36000000);
}

static int microtune_mt7202dtf_request_firmware(struct dvb_frontend* fe, const struct firmware **fw, char* name)
{
	struct dvb_bt8xx_card* bt = (struct dvb_bt8xx_card*) fe->dvb->priv;

	return request_firmware(fw, name, &bt->bt->dev->dev);
}

static struct sp887x_config microtune_mt7202dtf_config = {
	.demod_address = 0x70,
	.request_firmware = microtune_mt7202dtf_request_firmware,
};

static int advbt771_samsung_tdtc9251dh0_demod_init(struct dvb_frontend* fe)
{
	static u8 mt352_clock_config [] = { 0x89, 0x38, 0x2d };
	static u8 mt352_reset [] = { 0x50, 0x80 };
	static u8 mt352_adc_ctl_1_cfg [] = { 0x8E, 0x40 };
	static u8 mt352_agc_cfg [] = { 0x67, 0x10, 0x23, 0x00, 0xFF, 0xFF,
				       0x00, 0xFF, 0x00, 0x40, 0x40 };
	static u8 mt352_av771_extra[] = { 0xB5, 0x7A };
	static u8 mt352_capt_range_cfg[] = { 0x75, 0x32 };

	mt352_write(fe, mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(fe, mt352_reset, sizeof(mt352_reset));
	mt352_write(fe, mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));

	mt352_write(fe, mt352_agc_cfg,sizeof(mt352_agc_cfg));
	udelay(2000);
	mt352_write(fe, mt352_av771_extra,sizeof(mt352_av771_extra));
	mt352_write(fe, mt352_capt_range_cfg, sizeof(mt352_capt_range_cfg));

	return 0;
}

static int advbt771_samsung_tdtc9251dh0_tuner_calc_regs(struct dvb_frontend* fe, struct dvb_frontend_parameters* params, u8* pllbuf, int buf_len)
{
	u32 div;
	unsigned char bs = 0;
	unsigned char cp = 0;

	if (buf_len < 5) return -EINVAL;

	div = (((params->frequency + 83333) * 3) / 500000) + IF_FREQUENCYx6;

	if (params->frequency < 150000000)
		cp = 0xB4;
	else if (params->frequency < 173000000)
		cp = 0xBC;
	else if (params->frequency < 250000000)
		cp = 0xB4;
	else if (params->frequency < 400000000)
		cp = 0xBC;
	else if (params->frequency < 420000000)
		cp = 0xF4;
	else if (params->frequency < 470000000)
		cp = 0xFC;
	else if (params->frequency < 600000000)
		cp = 0xBC;
	else if (params->frequency < 730000000)
		cp = 0xF4;
	else
		cp = 0xFC;

	if (params->frequency < 150000000)
		bs = 0x01;
	else if (params->frequency < 173000000)
		bs = 0x01;
	else if (params->frequency < 250000000)
		bs = 0x02;
	else if (params->frequency < 400000000)
		bs = 0x02;
	else if (params->frequency < 420000000)
		bs = 0x02;
	else if (params->frequency < 470000000)
		bs = 0x02;
	else if (params->frequency < 600000000)
		bs = 0x08;
	else if (params->frequency < 730000000)
		bs = 0x08;
	else
		bs = 0x08;

	pllbuf[0] = 0x61;
	pllbuf[1] = div >> 8;
	pllbuf[2] = div & 0xff;
	pllbuf[3] = cp;
	pllbuf[4] = bs;

	return 5;
}

static struct mt352_config advbt771_samsung_tdtc9251dh0_config = {
	.demod_address = 0x0f,
	.demod_init = advbt771_samsung_tdtc9251dh0_demod_init,
};

static struct dst_config dst_config = {
	.demod_address = 0x55,
};

static int or51211_request_firmware(struct dvb_frontend* fe, const struct firmware **fw, char* name)
{
	struct dvb_bt8xx_card* bt = (struct dvb_bt8xx_card*) fe->dvb->priv;

	return request_firmware(fw, name, &bt->bt->dev->dev);
}

static void or51211_setmode(struct dvb_frontend * fe, int mode)
{
	struct dvb_bt8xx_card *bt = fe->dvb->priv;
	bttv_write_gpio(bt->bttv_nr, 0x0002, mode);   /* Reset */
	msleep(20);
}

static void or51211_reset(struct dvb_frontend * fe)
{
	struct dvb_bt8xx_card *bt = fe->dvb->priv;

	/* RESET DEVICE
	 * reset is controled by GPIO-0
	 * when set to 0 causes reset and when to 1 for normal op
	 * must remain reset for 128 clock cycles on a 50Mhz clock
	 * also PRM1 PRM2 & PRM4 are controled by GPIO-1,GPIO-2 & GPIO-4
	 * We assume that the reset has be held low long enough or we
	 * have been reset by a power on.  When the driver is unloaded
	 * reset set to 0 so if reloaded we have been reset.
	 */
	/* reset & PRM1,2&4 are outputs */
	int ret = bttv_gpio_enable(bt->bttv_nr, 0x001F, 0x001F);
	if (ret != 0)
		printk(KERN_WARNING "or51211: Init Error - Can't Reset DVR (%i)\n", ret);
	bttv_write_gpio(bt->bttv_nr, 0x001F, 0x0000);   /* Reset */
	msleep(20);
	/* Now set for normal operation */
	bttv_write_gpio(bt->bttv_nr, 0x0001F, 0x0001);
	/* wait for operation to begin */
	msleep(500);
}

static void or51211_sleep(struct dvb_frontend * fe)
{
	struct dvb_bt8xx_card *bt = fe->dvb->priv;
	bttv_write_gpio(bt->bttv_nr, 0x0001, 0x0000);
}

static struct or51211_config or51211_config = {
	.demod_address = 0x15,
	.request_firmware = or51211_request_firmware,
	.setmode = or51211_setmode,
	.reset = or51211_reset,
	.sleep = or51211_sleep,
};

static int vp3021_alps_tded4_tuner_set_params(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	struct dvb_bt8xx_card *card = (struct dvb_bt8xx_card *) fe->dvb->priv;
	u8 buf[4];
	u32 div;
	struct i2c_msg msg = { .addr = 0x60, .flags = 0, .buf = buf, .len = sizeof(buf) };

	div = (params->frequency + 36166667) / 166667;

	buf[0] = (div >> 8) & 0x7F;
	buf[1] = div & 0xFF;
	buf[2] = 0x85;
	if ((params->frequency >= 47000000) && (params->frequency < 153000000))
		buf[3] = 0x01;
	else if ((params->frequency >= 153000000) && (params->frequency < 430000000))
		buf[3] = 0x02;
	else if ((params->frequency >= 430000000) && (params->frequency < 824000000))
		buf[3] = 0x0C;
	else if ((params->frequency >= 824000000) && (params->frequency < 863000000))
		buf[3] = 0x8C;
	else
		return -EINVAL;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	i2c_transfer(card->i2c_adapter, &msg, 1);
	return 0;
}

static struct nxt6000_config vp3021_alps_tded4_config = {
	.demod_address = 0x0a,
	.clock_inversion = 1,
};

static int digitv_alps_tded4_demod_init(struct dvb_frontend* fe)
{
	static u8 mt352_clock_config [] = { 0x89, 0x38, 0x2d };
	static u8 mt352_reset [] = { 0x50, 0x80 };
	static u8 mt352_adc_ctl_1_cfg [] = { 0x8E, 0x40 };
	static u8 mt352_agc_cfg [] = { 0x67, 0x20, 0xa0 };
	static u8 mt352_capt_range_cfg[] = { 0x75, 0x32 };

	mt352_write(fe, mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(fe, mt352_reset, sizeof(mt352_reset));
	mt352_write(fe, mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));
	mt352_write(fe, mt352_agc_cfg,sizeof(mt352_agc_cfg));
	mt352_write(fe, mt352_capt_range_cfg, sizeof(mt352_capt_range_cfg));

	return 0;
}

static int digitv_alps_tded4_tuner_calc_regs(struct dvb_frontend* fe, struct dvb_frontend_parameters* params, u8* pllbuf, int buf_len)
{
	u32 div;
	struct dvb_ofdm_parameters *op = &params->u.ofdm;

	if (buf_len < 5)
		return -EINVAL;

	div = (((params->frequency + 83333) * 3) / 500000) + IF_FREQUENCYx6;

	pllbuf[0] = 0x61;
	pllbuf[1] = (div >> 8) & 0x7F;
	pllbuf[2] = div & 0xFF;
	pllbuf[3] = 0x85;

	dprintk("frequency %u, div %u\n", params->frequency, div);

	if (params->frequency < 470000000)
		pllbuf[4] = 0x02;
	else if (params->frequency > 823000000)
		pllbuf[4] = 0x88;
	else
		pllbuf[4] = 0x08;

	if (op->bandwidth == 8)
		pllbuf[4] |= 0x04;

	return 5;
}

static void digitv_alps_tded4_reset(struct dvb_bt8xx_card *bt)
{
	/*
	 * Reset the frontend, must be called before trying
	 * to initialise the MT352 or mt352_attach
	 * will fail. Same goes for the nxt6000 frontend.
	 *
	 */

	int ret = bttv_gpio_enable(bt->bttv_nr, 0x08, 0x08);
	if (ret != 0)
		printk(KERN_WARNING "digitv_alps_tded4: Init Error - Can't Reset DVR (%i)\n", ret);

	/* Pulse the reset line */
	bttv_write_gpio(bt->bttv_nr, 0x08, 0x08); /* High */
	bttv_write_gpio(bt->bttv_nr, 0x08, 0x00); /* Low  */
	msleep(100);

	bttv_write_gpio(bt->bttv_nr, 0x08, 0x08); /* High */
}

static struct mt352_config digitv_alps_tded4_config = {
	.demod_address = 0x0a,
	.demod_init = digitv_alps_tded4_demod_init,
};

static struct lgdt330x_config tdvs_tua6034_config = {
	.demod_address    = 0x0e,
	.demod_chip       = LGDT3303,
	.serial_mpeg      = 0x40, /* TPSERIAL for 3303 in TOP_CONTROL */
};

static void lgdt330x_reset(struct dvb_bt8xx_card *bt)
{
	/* Set pin 27 of the lgdt3303 chip high to reset the frontend */

	/* Pulse the reset line */
	bttv_write_gpio(bt->bttv_nr, 0x00e00007, 0x00000001); /* High */
	bttv_write_gpio(bt->bttv_nr, 0x00e00007, 0x00000000); /* Low  */
	msleep(100);

	bttv_write_gpio(bt->bttv_nr, 0x00e00007, 0x00000001); /* High */
	msleep(100);
}

static void frontend_init(struct dvb_bt8xx_card *card, u32 type)
{
	struct dst_state* state = NULL;

	switch(type) {
	case BTTV_BOARD_DVICO_DVBT_LITE:
		card->fe = dvb_attach(mt352_attach, &thomson_dtt7579_config, card->i2c_adapter);

		if (card->fe == NULL)
			card->fe = dvb_attach(zl10353_attach, &thomson_dtt7579_zl10353_config,
						  card->i2c_adapter);

		if (card->fe != NULL) {
			card->fe->ops.tuner_ops.calc_regs = thomson_dtt7579_tuner_calc_regs;
			card->fe->ops.info.frequency_min = 174000000;
			card->fe->ops.info.frequency_max = 862000000;
		}
		break;

	case BTTV_BOARD_DVICO_FUSIONHDTV_5_LITE:
		lgdt330x_reset(card);
		card->fe = dvb_attach(lgdt330x_attach, &tdvs_tua6034_config, card->i2c_adapter);
		if (card->fe != NULL) {
			dvb_attach(lgh06xf_attach, card->fe, card->i2c_adapter);
			dprintk ("dvb_bt8xx: lgdt330x detected\n");
		}
		break;

	case BTTV_BOARD_NEBULA_DIGITV:
		/*
		 * It is possible to determine the correct frontend using the I2C bus (see the Nebula SDK);
		 * this would be a cleaner solution than trying each frontend in turn.
		 */

		/* Old Nebula (marked (c)2003 on high profile pci card) has nxt6000 demod */
		digitv_alps_tded4_reset(card);
		card->fe = dvb_attach(nxt6000_attach, &vp3021_alps_tded4_config, card->i2c_adapter);
		if (card->fe != NULL) {
			card->fe->ops.tuner_ops.set_params = vp3021_alps_tded4_tuner_set_params;
			dprintk ("dvb_bt8xx: an nxt6000 was detected on your digitv card\n");
			break;
		}

		/* New Nebula (marked (c)2005 on low profile pci card) has mt352 demod */
		digitv_alps_tded4_reset(card);
		card->fe = dvb_attach(mt352_attach, &digitv_alps_tded4_config, card->i2c_adapter);

		if (card->fe != NULL) {
			card->fe->ops.tuner_ops.calc_regs = digitv_alps_tded4_tuner_calc_regs;
			dprintk ("dvb_bt8xx: an mt352 was detected on your digitv card\n");
		}
		break;

	case BTTV_BOARD_AVDVBT_761:
		card->fe = dvb_attach(sp887x_attach, &microtune_mt7202dtf_config, card->i2c_adapter);
		if (card->fe) {
			card->fe->ops.tuner_ops.set_params = microtune_mt7202dtf_tuner_set_params;
		}
		break;

	case BTTV_BOARD_AVDVBT_771:
		card->fe = dvb_attach(mt352_attach, &advbt771_samsung_tdtc9251dh0_config, card->i2c_adapter);
		if (card->fe != NULL) {
			card->fe->ops.tuner_ops.calc_regs = advbt771_samsung_tdtc9251dh0_tuner_calc_regs;
			card->fe->ops.info.frequency_min = 174000000;
			card->fe->ops.info.frequency_max = 862000000;
		}
		break;

	case BTTV_BOARD_TWINHAN_DST:
		/*	DST is not a frontend driver !!!		*/
		state = (struct dst_state *) kmalloc(sizeof (struct dst_state), GFP_KERNEL);
		if (!state) {
			printk("dvb_bt8xx: No memory\n");
			break;
		}
		/*	Setup the Card					*/
		state->config = &dst_config;
		state->i2c = card->i2c_adapter;
		state->bt = card->bt;
		state->dst_ca = NULL;
		/*	DST is not a frontend, attaching the ASIC	*/
		if (dvb_attach(dst_attach, state, &card->dvb_adapter) == NULL) {
			printk("%s: Could not find a Twinhan DST.\n", __FUNCTION__);
			break;
		}
		/*	Attach other DST peripherals if any		*/
		/*	Conditional Access device			*/
		card->fe = &state->frontend;
		if (state->dst_hw_cap & DST_TYPE_HAS_CA)
			dvb_attach(dst_ca_attach, state, &card->dvb_adapter);
		break;

	case BTTV_BOARD_PINNACLESAT:
		card->fe = dvb_attach(cx24110_attach, &pctvsat_config, card->i2c_adapter);
		if (card->fe) {
			card->fe->ops.tuner_ops.init = pinnsat_tuner_init;
			card->fe->ops.tuner_ops.sleep = pinnsat_tuner_sleep;
			card->fe->ops.tuner_ops.set_params = cx24108_tuner_set_params;
		}
		break;

	case BTTV_BOARD_PC_HDTV:
		card->fe = dvb_attach(or51211_attach, &or51211_config, card->i2c_adapter);
		break;
	}

	if (card->fe == NULL)
		printk("dvb-bt8xx: A frontend driver was not found for device %04x/%04x subsystem %04x/%04x\n",
		       card->bt->dev->vendor,
		       card->bt->dev->device,
		       card->bt->dev->subsystem_vendor,
		       card->bt->dev->subsystem_device);
	else
		if (dvb_register_frontend(&card->dvb_adapter, card->fe)) {
			printk("dvb-bt8xx: Frontend registration failed!\n");
			dvb_frontend_detach(card->fe);
			card->fe = NULL;
		}
}

static int __devinit dvb_bt8xx_load_card(struct dvb_bt8xx_card *card, u32 type)
{
	int result;

	if ((result = dvb_register_adapter(&card->dvb_adapter, card->card_name, THIS_MODULE, &card->bt->dev->dev)) < 0) {
		printk("dvb_bt8xx: dvb_register_adapter failed (errno = %d)\n", result);
		return result;
	}
	card->dvb_adapter.priv = card;

	card->bt->adapter = card->i2c_adapter;

	memset(&card->demux, 0, sizeof(struct dvb_demux));

	card->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING | DMX_MEMORY_BASED_FILTERING;

	card->demux.priv = card;
	card->demux.filternum = 256;
	card->demux.feednum = 256;
	card->demux.start_feed = dvb_bt8xx_start_feed;
	card->demux.stop_feed = dvb_bt8xx_stop_feed;
	card->demux.write_to_decoder = NULL;

	if ((result = dvb_dmx_init(&card->demux)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		dvb_unregister_adapter(&card->dvb_adapter);
		return result;
	}

	card->dmxdev.filternum = 256;
	card->dmxdev.demux = &card->demux.dmx;
	card->dmxdev.capabilities = 0;

	if ((result = dvb_dmxdev_init(&card->dmxdev, &card->dvb_adapter)) < 0) {
		printk("dvb_bt8xx: dvb_dmxdev_init failed (errno = %d)\n", result);

		dvb_dmx_release(&card->demux);
		dvb_unregister_adapter(&card->dvb_adapter);
		return result;
	}

	card->fe_hw.source = DMX_FRONTEND_0;

	if ((result = card->demux.dmx.add_frontend(&card->demux.dmx, &card->fe_hw)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
		dvb_unregister_adapter(&card->dvb_adapter);
		return result;
	}

	card->fe_mem.source = DMX_MEMORY_FE;

	if ((result = card->demux.dmx.add_frontend(&card->demux.dmx, &card->fe_mem)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
		dvb_unregister_adapter(&card->dvb_adapter);
		return result;
	}

	if ((result = card->demux.dmx.connect_frontend(&card->demux.dmx, &card->fe_hw)) < 0) {
		printk("dvb_bt8xx: dvb_dmx_init failed (errno = %d)\n", result);

		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_mem);
		card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
		dvb_dmxdev_release(&card->dmxdev);
		dvb_dmx_release(&card->demux);
		dvb_unregister_adapter(&card->dvb_adapter);
		return result;
	}

	dvb_net_init(&card->dvb_adapter, &card->dvbnet, &card->demux.dmx);

	tasklet_init(&card->bt->tasklet, dvb_bt8xx_task, (unsigned long) card);

	frontend_init(card, type);

	return 0;
}

static int __devinit dvb_bt8xx_probe(struct bttv_sub_device *sub)
{
	struct dvb_bt8xx_card *card;
	struct pci_dev* bttv_pci_dev;
	int ret;

	if (!(card = kzalloc(sizeof(struct dvb_bt8xx_card), GFP_KERNEL)))
		return -ENOMEM;

	mutex_init(&card->lock);
	card->bttv_nr = sub->core->nr;
	strncpy(card->card_name, sub->core->name, sizeof(sub->core->name));
	card->i2c_adapter = &sub->core->i2c_adap;

	switch(sub->core->type) {
	case BTTV_BOARD_PINNACLESAT:
		card->gpio_mode = 0x0400c060;
		/* should be: BT878_A_GAIN=0,BT878_A_PWRDN,BT878_DA_DPM,BT878_DA_SBR,
			      BT878_DA_IOM=1,BT878_DA_APP to enable serial highspeed mode. */
		card->op_sync_orin = BT878_RISC_SYNC_MASK;
		card->irq_err_ignore = BT878_AFBUS | BT878_AFDSR;
		break;

	case BTTV_BOARD_DVICO_DVBT_LITE:
		card->gpio_mode = 0x0400C060;
		card->op_sync_orin = BT878_RISC_SYNC_MASK;
		card->irq_err_ignore = BT878_AFBUS | BT878_AFDSR;
		/* 26, 15, 14, 6, 5
		 * A_PWRDN  DA_DPM DA_SBR DA_IOM_DA
		 * DA_APP(parallel) */
		break;

	case BTTV_BOARD_DVICO_FUSIONHDTV_5_LITE:
		card->gpio_mode = 0x0400c060;
		card->op_sync_orin = BT878_RISC_SYNC_MASK;
		card->irq_err_ignore = BT878_AFBUS | BT878_AFDSR;
		break;

	case BTTV_BOARD_NEBULA_DIGITV:
	case BTTV_BOARD_AVDVBT_761:
		card->gpio_mode = (1 << 26) | (1 << 14) | (1 << 5);
		card->op_sync_orin = BT878_RISC_SYNC_MASK;
		card->irq_err_ignore = BT878_AFBUS | BT878_AFDSR;
		/* A_PWRDN DA_SBR DA_APP (high speed serial) */
		break;

	case BTTV_BOARD_AVDVBT_771: //case 0x07711461:
		card->gpio_mode = 0x0400402B;
		card->op_sync_orin = BT878_RISC_SYNC_MASK;
		card->irq_err_ignore = BT878_AFBUS | BT878_AFDSR;
		/* A_PWRDN DA_SBR  DA_APP[0] PKTP=10 RISC_ENABLE FIFO_ENABLE*/
		break;

	case BTTV_BOARD_TWINHAN_DST:
		card->gpio_mode = 0x2204f2c;
		card->op_sync_orin = BT878_RISC_SYNC_MASK;
		card->irq_err_ignore = BT878_APABORT | BT878_ARIPERR |
				       BT878_APPERR | BT878_AFBUS;
		/* 25,21,14,11,10,9,8,3,2 then
		 * 0x33 = 5,4,1,0
		 * A_SEL=SML, DA_MLB, DA_SBR,
		 * DA_SDR=f, fifo trigger = 32 DWORDS
		 * IOM = 0 == audio A/D
		 * DPM = 0 == digital audio mode
		 * == async data parallel port
		 * then 0x33 (13 is set by start_capture)
		 * DA_APP = async data parallel port,
		 * ACAP_EN = 1,
		 * RISC+FIFO ENABLE */
		break;

	case BTTV_BOARD_PC_HDTV:
		card->gpio_mode = 0x0100EC7B;
		card->op_sync_orin = BT878_RISC_SYNC_MASK;
		card->irq_err_ignore = BT878_AFBUS | BT878_AFDSR;
		break;

	default:
		printk(KERN_WARNING "dvb_bt8xx: Unknown bttv card type: %d.\n",
				sub->core->type);
		kfree(card);
		return -ENODEV;
	}

	dprintk("dvb_bt8xx: identified card%d as %s\n", card->bttv_nr, card->card_name);

	if (!(bttv_pci_dev = bttv_get_pcidev(card->bttv_nr))) {
		printk("dvb_bt8xx: no pci device for card %d\n", card->bttv_nr);
		kfree(card);
		return -EFAULT;
	}

	if (!(card->bt = dvb_bt8xx_878_match(card->bttv_nr, bttv_pci_dev))) {
		printk("dvb_bt8xx: unable to determine DMA core of card %d,\n",
		       card->bttv_nr);
		printk("dvb_bt8xx: if you have the ALSA bt87x audio driver "
		       "installed, try removing it.\n");

		kfree(card);
		return -EFAULT;
	}

	mutex_init(&card->bt->gpio_lock);
	card->bt->bttv_nr = sub->core->nr;

	if ( (ret = dvb_bt8xx_load_card(card, sub->core->type)) ) {
		kfree(card);
		return ret;
	}

	dev_set_drvdata(&sub->dev, card);
	return 0;
}

static void dvb_bt8xx_remove(struct bttv_sub_device *sub)
{
	struct dvb_bt8xx_card *card = dev_get_drvdata(&sub->dev);

	dprintk("dvb_bt8xx: unloading card%d\n", card->bttv_nr);

	bt878_stop(card->bt);
	tasklet_kill(&card->bt->tasklet);
	dvb_net_release(&card->dvbnet);
	card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_mem);
	card->demux.dmx.remove_frontend(&card->demux.dmx, &card->fe_hw);
	dvb_dmxdev_release(&card->dmxdev);
	dvb_dmx_release(&card->demux);
	if (card->fe) {
		dvb_unregister_frontend(card->fe);
		dvb_frontend_detach(card->fe);
	}
	dvb_unregister_adapter(&card->dvb_adapter);

	kfree(card);
}

static struct bttv_sub_driver driver = {
	.drv = {
		.name		= "dvb-bt8xx",
	},
	.probe		= dvb_bt8xx_probe,
	.remove		= dvb_bt8xx_remove,
	/* FIXME:
	 * .shutdown	= dvb_bt8xx_shutdown,
	 * .suspend	= dvb_bt8xx_suspend,
	 * .resume	= dvb_bt8xx_resume,
	 */
};

static int __init dvb_bt8xx_init(void)
{
	return bttv_sub_register(&driver, "dvb");
}

static void __exit dvb_bt8xx_exit(void)
{
	bttv_sub_unregister(&driver);
}

module_init(dvb_bt8xx_init);
module_exit(dvb_bt8xx_exit);

MODULE_DESCRIPTION("Bt8xx based DVB adapter driver");
MODULE_AUTHOR("Florian Schirmer <jolt@tuxbox.org>");
MODULE_LICENSE("GPL");
