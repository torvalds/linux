/*
 * dvb-dibusb-fe-i2c.c is part of the driver for mobile USB Budget DVB-T devices
 * based on reference design made by DiBcom (http://www.dibcom.fr/)
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 * see dvb-dibusb-core.c for more copyright details.
 *
 * This file contains functions for attaching, initializing of an appropriate
 * demodulator/frontend. I2C-stuff is also located here.
 *
 */
#include "dvb-dibusb.h"

#include <linux/usb.h>

static int dibusb_i2c_msg(struct usb_dibusb *dib, u8 addr,
			  u8 *wbuf, u16 wlen, u8 *rbuf, u16 rlen)
{
	u8 sndbuf[wlen+4]; /* lead(1) devaddr,direction(1) addr(2) data(wlen) (len(2) (when reading)) */
	/* write only ? */
	int wo = (rbuf == NULL || rlen == 0),
		len = 2 + wlen + (wo ? 0 : 2);

	sndbuf[0] = wo ? DIBUSB_REQ_I2C_WRITE : DIBUSB_REQ_I2C_READ;
	sndbuf[1] = (addr << 1) | (wo ? 0 : 1);

	memcpy(&sndbuf[2],wbuf,wlen);

	if (!wo) {
		sndbuf[wlen+2] = (rlen >> 8) & 0xff;
		sndbuf[wlen+3] = rlen & 0xff;
	}

	return dibusb_readwrite_usb(dib,sndbuf,len,rbuf,rlen);
}

/*
 * I2C master xfer function
 */
static int dibusb_i2c_xfer(struct i2c_adapter *adap,struct i2c_msg *msg,int num)
{
	struct usb_dibusb *dib = i2c_get_adapdata(adap);
	int i;

	if (down_interruptible(&dib->i2c_sem) < 0)
		return -EAGAIN;

	if (num > 2)
		warn("more than 2 i2c messages at a time is not handled yet. TODO.");

	for (i = 0; i < num; i++) {
		/* write/read request */
		if (i+1 < num && (msg[i+1].flags & I2C_M_RD)) {
			if (dibusb_i2c_msg(dib, msg[i].addr, msg[i].buf,msg[i].len,
						msg[i+1].buf,msg[i+1].len) < 0)
				break;
			i++;
		} else
			if (dibusb_i2c_msg(dib, msg[i].addr, msg[i].buf,msg[i].len,NULL,0) < 0)
				break;
	}

	up(&dib->i2c_sem);
	return i;
}

static u32 dibusb_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C;
}

static struct i2c_algorithm dibusb_algo = {
	.name			= "DiBcom USB i2c algorithm",
	.id				= I2C_ALGO_BIT,
	.master_xfer	= dibusb_i2c_xfer,
	.functionality	= dibusb_i2c_func,
};

static int dibusb_general_demod_init(struct dvb_frontend *fe);
static u8 dibusb_general_pll_addr(struct dvb_frontend *fe);
static int dibusb_general_pll_init(struct dvb_frontend *fe, u8 pll_buf[5]);
static int dibusb_general_pll_set(struct dvb_frontend *fe,
		struct dvb_frontend_parameters* params, u8 pll_buf[5]);

static struct mt352_config mt352_hanftek_umt_010_config = {
	.demod_address = 0x1e,
	.demod_init = dibusb_general_demod_init,
	.pll_set = dibusb_general_pll_set,
};

static int dibusb_tuner_quirk(struct usb_dibusb *dib)
{
	switch (dib->dibdev->dev_cl->id) {
		case DIBUSB1_1: /* some these device have the ENV77H11D5 and some the THOMSON CABLE */
		case DIBUSB1_1_AN2235: { /* actually its this device, but in warm state they are indistinguishable */
			struct dibusb_tuner *t;
			u8 b[2] = { 0,0 } ,b2[1];
			struct i2c_msg msg[2] = {
				{ .flags = 0, .buf = b, .len = 2 },
				{ .flags = I2C_M_RD, .buf = b2, .len = 1},
			};

			t = &dibusb_tuner[DIBUSB_TUNER_COFDM_PANASONIC_ENV77H11D5];

			msg[0].addr = msg[1].addr = t->pll_addr;

			if (dib->xfer_ops.tuner_pass_ctrl != NULL)
				dib->xfer_ops.tuner_pass_ctrl(dib->fe,1,t->pll_addr);
			dibusb_i2c_xfer(&dib->i2c_adap,msg,2);
			if (dib->xfer_ops.tuner_pass_ctrl != NULL)
				dib->xfer_ops.tuner_pass_ctrl(dib->fe,0,t->pll_addr);

			if (b2[0] == 0xfe)
				info("this device has the Thomson Cable onboard. Which is default.");
			else {
				dib->tuner = t;
				info("this device has the Panasonic ENV77H11D5 onboard.");
			}
			break;
		}
		default:
			break;
	}
	return 0;
}

int dibusb_fe_init(struct usb_dibusb* dib)
{
	struct dib3000_config demod_cfg;
	int i;

	if (dib->init_state & DIBUSB_STATE_I2C) {
		for (i = 0; i < sizeof(dib->dibdev->dev_cl->demod->i2c_addrs) / sizeof(unsigned char) &&
				dib->dibdev->dev_cl->demod->i2c_addrs[i] != 0; i++) {

			demod_cfg.demod_address = dib->dibdev->dev_cl->demod->i2c_addrs[i];
			demod_cfg.pll_addr = dibusb_general_pll_addr;
			demod_cfg.pll_set = dibusb_general_pll_set;
			demod_cfg.pll_init = dibusb_general_pll_init;

			deb_info("demod id: %d %d\n",dib->dibdev->dev_cl->demod->id,DTT200U_FE);

			switch (dib->dibdev->dev_cl->demod->id) {
				case DIBUSB_DIB3000MB:
					dib->fe = dib3000mb_attach(&demod_cfg,&dib->i2c_adap,&dib->xfer_ops);
				break;
				case DIBUSB_DIB3000MC:
					dib->fe = dib3000mc_attach(&demod_cfg,&dib->i2c_adap,&dib->xfer_ops);
				break;
				case DIBUSB_MT352:
					mt352_hanftek_umt_010_config.demod_address = dib->dibdev->dev_cl->demod->i2c_addrs[i];
					dib->fe = mt352_attach(&mt352_hanftek_umt_010_config, &dib->i2c_adap);
				break;
				case DTT200U_FE:
					dib->fe = dtt200u_fe_attach(dib,&dib->xfer_ops);
				break;
			}
			if (dib->fe != NULL) {
				info("found demodulator at i2c address 0x%x",dib->dibdev->dev_cl->demod->i2c_addrs[i]);
				break;
			}
		}
		/* if a frontend was found */
		if (dib->fe != NULL) {
			if (dib->fe->ops->sleep != NULL)
				dib->fe_sleep = dib->fe->ops->sleep;
			dib->fe->ops->sleep = dibusb_hw_sleep;

			if (dib->fe->ops->init != NULL )
				dib->fe_init = dib->fe->ops->init;
			dib->fe->ops->init = dibusb_hw_wakeup;

			/* setting the default tuner */
			dib->tuner = dib->dibdev->dev_cl->tuner;

			/* check which tuner is mounted on this device, in case this is unsure */
			dibusb_tuner_quirk(dib);
		}
	}
	if (dib->fe == NULL) {
		err("A frontend driver was not found for device '%s'.",
		       dib->dibdev->name);
		return -ENODEV;
	} else {
		if (dvb_register_frontend(&dib->adapter, dib->fe)) {
			err("Frontend registration failed.");
			if (dib->fe->ops->release)
				dib->fe->ops->release(dib->fe);
			dib->fe = NULL;
			return -ENODEV;
		}
	}

	return 0;
}

int dibusb_fe_exit(struct usb_dibusb *dib)
{
	if (dib->fe != NULL)
		dvb_unregister_frontend(dib->fe);
	return 0;
}

int dibusb_i2c_init(struct usb_dibusb *dib)
{
	int ret = 0;

	dib->adapter.priv = dib;

	strncpy(dib->i2c_adap.name,dib->dibdev->name,I2C_NAME_SIZE);
#ifdef I2C_ADAP_CLASS_TV_DIGITAL
	dib->i2c_adap.class = I2C_ADAP_CLASS_TV_DIGITAL,
#else
	dib->i2c_adap.class = I2C_CLASS_TV_DIGITAL,
#endif
	dib->i2c_adap.algo		= &dibusb_algo;
	dib->i2c_adap.algo_data = NULL;
	dib->i2c_adap.id		= I2C_ALGO_BIT;

	i2c_set_adapdata(&dib->i2c_adap, dib);

	if ((ret = i2c_add_adapter(&dib->i2c_adap)) < 0)
		err("could not add i2c adapter");

	dib->init_state |= DIBUSB_STATE_I2C;

	return ret;
}

int dibusb_i2c_exit(struct usb_dibusb *dib)
{
	if (dib->init_state & DIBUSB_STATE_I2C)
		i2c_del_adapter(&dib->i2c_adap);
	dib->init_state &= ~DIBUSB_STATE_I2C;
	return 0;
}


/* pll stuff, maybe removed soon (thx to Gerd/Andrew in advance) */
static int thomson_cable_eu_pll_set(struct dvb_frontend_parameters *fep, u8 pllbuf[4])
{
	u32 tfreq = (fep->frequency + 36125000) / 62500;
	int vu,p0,p1,p2;

	if (fep->frequency > 403250000)
		vu = 1, p2 = 1, p1 = 0, p0 = 1;
	else if (fep->frequency > 115750000)
		vu = 0, p2 = 1, p1 = 1, p0 = 0;
	else if (fep->frequency > 44250000)
		vu = 0, p2 = 0, p1 = 1, p0 = 1;
	else
		return -EINVAL;

	pllbuf[0] = (tfreq >> 8) & 0x7f;
	pllbuf[1] = tfreq & 0xff;
	pllbuf[2] = 0x8e;
	pllbuf[3] = (vu << 7) | (p2 << 2) | (p1 << 1) | p0;
	return 0;
}

static int panasonic_cofdm_env57h1xd5_pll_set(struct dvb_frontend_parameters *fep, u8 pllbuf[4])
{
	u32 freq_khz = fep->frequency / 1000;
	u32 tfreq = ((freq_khz + 36125)*6 + 500) / 1000;
	u8 TA, T210, R210, ctrl1, cp210, p4321;
	if (freq_khz > 858000) {
		err("frequency cannot be larger than 858 MHz.");
		return -EINVAL;
	}

	// contol data 1 : 1 | T/A=1 | T2,T1,T0 = 0,0,0 | R2,R1,R0 = 0,1,0
	TA = 1;
	T210 = 0;
	R210 = 0x2;
	ctrl1 = (1 << 7) | (TA << 6) | (T210 << 3) | R210;

// ********    CHARGE PUMP CONFIG vs RF FREQUENCIES     *****************
	if (freq_khz < 470000)
		cp210 = 2;  // VHF Low and High band ch E12 to E4 to E12
	else if (freq_khz < 526000)
		cp210 = 4;  // UHF band Ch E21 to E27
	else // if (freq < 862000000)
		cp210 = 5;  // UHF band ch E28 to E69

//*********************    BW select  *******************************
	if (freq_khz < 153000)
		p4321  = 1; // BW selected for VHF low
	else if (freq_khz < 470000)
		p4321  = 2; // BW selected for VHF high E5 to E12
	else // if (freq < 862000000)
		p4321  = 4; // BW selection for UHF E21 to E69

	pllbuf[0] = (tfreq >> 8) & 0xff;
	pllbuf[1] = (tfreq >> 0) & 0xff;
	pllbuf[2] = 0xff & ctrl1;
	pllbuf[3] =  (cp210 << 5) | (p4321);

	return 0;
}

/*
 *			    7	6		5	4	3	2	1	0
 * Address Byte             1	1		0	0	0	MA1	MA0	R/~W=0
 *
 * Program divider byte 1   0	n14		n13	n12	n11	n10	n9	n8
 * Program divider byte 2	n7	n6		n5	n4	n3	n2	n1	n0
 *
 * Control byte 1           1	T/A=1	T2	T1	T0	R2	R1	R0
 *                          1	T/A=0	0	0	ATC	AL2	AL1	AL0
 *
 * Control byte 2           CP2	CP1		CP0	BS5	BS4	BS3	BS2	BS1
 *
 * MA0/1 = programmable address bits
 * R/~W  = read/write bit (0 for writing)
 * N14-0 = programmable LO frequency
 *
 * T/A   = test AGC bit (0 = next 6 bits AGC setting,
 *                       1 = next 6 bits test and reference divider ratio settings)
 * T2-0  = test bits
 * R2-0  = reference divider ratio and programmable frequency step
 * ATC   = AGC current setting and time constant
 *         ATC = 0: AGC current = 220nA, AGC time constant = 2s
 *         ATC = 1: AGC current = 9uA, AGC time constant = 50ms
 * AL2-0 = AGC take-over point bits
 * CP2-0 = charge pump current
 * BS5-1 = PMOS ports control bits;
 *             BSn = 0 corresponding port is off, high-impedance state (at power-on)
 *             BSn = 1 corresponding port is on
 */
static int panasonic_cofdm_env77h11d5_tda6650_init(struct dvb_frontend *fe, u8 pllbuf[4])
{
	pllbuf[0] = 0x0b;
	pllbuf[1] = 0xf5;
	pllbuf[2] = 0x85;
	pllbuf[3] = 0xab;
	return 0;
}

static int panasonic_cofdm_env77h11d5_tda6650_set (struct dvb_frontend_parameters *fep,u8 pllbuf[4])
{
	int tuner_frequency = 0;
	u8 band, cp, filter;

	// determine charge pump
	tuner_frequency = fep->frequency + 36166000;
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

	// determine band
	if (fep->frequency < 49000000)
		return -EINVAL;
	else if (fep->frequency < 161000000)
		band = 1;
	else if (fep->frequency < 444000000)
		band = 2;
	else if (fep->frequency < 861000000)
		band = 4;
	else
		return -EINVAL;

	// setup PLL filter
	switch (fep->u.ofdm.bandwidth) {
		case BANDWIDTH_6_MHZ:
		case BANDWIDTH_7_MHZ:
			filter = 0;
			break;
		case BANDWIDTH_8_MHZ:
			filter = 1;
			break;
		default:
			return -EINVAL;
	}

	// calculate divisor
	// ((36166000+((1000000/6)/2)) + Finput)/(1000000/6)
	tuner_frequency = (((fep->frequency / 1000) * 6) + 217496) / 1000;

	// setup tuner buffer
	pllbuf[0] = (tuner_frequency >> 8) & 0x7f;
	pllbuf[1] = tuner_frequency & 0xff;
	pllbuf[2] = 0xca;
	pllbuf[3] = (cp << 5) | (filter << 3) | band;
	return 0;
}

/*
 *			    7	6	5	4	3	2	1	0
 * Address Byte             1	1	0	0	0	MA1	MA0	R/~W=0
 *
 * Program divider byte 1   0	n14	n13	n12	n11	n10	n9	n8
 * Program divider byte 2	n7	n6	n5	n4	n3	n2	n1	n0
 *
 * Control byte             1	CP	T2	T1	T0	RSA	RSB	OS
 *
 * Band Switch byte         X	X	X	P4	P3	P2	P1	P0
 *
 * Auxiliary byte           ATC	AL2	AL1	AL0	0	0	0	0
 *
 * Address: MA1	MA0	Address
 *          0	0	c0
 *          0	1	c2 (always valid)
 *          1	0	c4
 *          1	1	c6
 */
static int lg_tdtp_e102p_tua6034(struct dvb_frontend_parameters* fep, u8 pllbuf[4])
{
	u32 div;
	u8 p210, p3;

#define TUNER_MUL 62500

	div = (fep->frequency + 36125000 + TUNER_MUL / 2) / TUNER_MUL;
//	div = ((fep->frequency/1000 + 36166) * 6) / 1000;

	if (fep->frequency < 174500000)
		p210 = 1; // not supported by the tdtp_e102p
	else if (fep->frequency < 230000000) // VHF
		p210 = 2;
	else
		p210 = 4;

	if (fep->u.ofdm.bandwidth == BANDWIDTH_7_MHZ)
		p3 = 0;
	else
		p3 = 1;

	pllbuf[0] = (div >> 8) & 0x7f;
	pllbuf[1] = div & 0xff;
	pllbuf[2] = 0xce;
//	pllbuf[2] = 0xcc;
	pllbuf[3] = (p3 << 3) | p210;

	return 0;
}

static int lg_tdtp_e102p_mt352_demod_init(struct dvb_frontend *fe)
{
	static u8 mt352_clock_config[] = { 0x89, 0xb8, 0x2d };
	static u8 mt352_reset[] = { 0x50, 0x80 };
	static u8 mt352_mclk_ratio[] = { 0x8b, 0x00 };
	static u8 mt352_adc_ctl_1_cfg[] = { 0x8E, 0x40 };
	static u8 mt352_agc_cfg[] = { 0x67, 0x10, 0xa0 };

	static u8 mt352_sec_agc_cfg1[] = { 0x6a, 0xff };
	static u8 mt352_sec_agc_cfg2[] = { 0x6d, 0xff };
	static u8 mt352_sec_agc_cfg3[] = { 0x70, 0x40 };
	static u8 mt352_sec_agc_cfg4[] = { 0x7b, 0x03 };
	static u8 mt352_sec_agc_cfg5[] = { 0x7d, 0x0f };

	static u8 mt352_acq_ctl[] = { 0x53, 0x50 };
	static u8 mt352_input_freq_1[] = { 0x56, 0x31, 0x06 };

	mt352_write(fe, mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(fe, mt352_reset, sizeof(mt352_reset));
	mt352_write(fe, mt352_mclk_ratio, sizeof(mt352_mclk_ratio));

	mt352_write(fe, mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));
	mt352_write(fe, mt352_agc_cfg, sizeof(mt352_agc_cfg));

	mt352_write(fe, mt352_sec_agc_cfg1, sizeof(mt352_sec_agc_cfg1));
	mt352_write(fe, mt352_sec_agc_cfg2, sizeof(mt352_sec_agc_cfg2));
	mt352_write(fe, mt352_sec_agc_cfg3, sizeof(mt352_sec_agc_cfg3));
	mt352_write(fe, mt352_sec_agc_cfg4, sizeof(mt352_sec_agc_cfg4));
	mt352_write(fe, mt352_sec_agc_cfg5, sizeof(mt352_sec_agc_cfg5));

	mt352_write(fe, mt352_acq_ctl, sizeof(mt352_acq_ctl));
	mt352_write(fe, mt352_input_freq_1, sizeof(mt352_input_freq_1));

	return 0;
}

static int dibusb_general_demod_init(struct dvb_frontend *fe)
{
	struct usb_dibusb* dib = (struct usb_dibusb*) fe->dvb->priv;
	switch (dib->dibdev->dev_cl->id) {
		case UMT2_0:
			return lg_tdtp_e102p_mt352_demod_init(fe);
		default: /* other device classes do not have device specific demod inits */
			break;
	}
	return 0;
}

static u8 dibusb_general_pll_addr(struct dvb_frontend *fe)
{
	struct usb_dibusb* dib = (struct usb_dibusb*) fe->dvb->priv;
	return dib->tuner->pll_addr;
}

static int dibusb_pll_i2c_helper(struct usb_dibusb *dib, u8 pll_buf[5], u8 buf[4])
{
	if (pll_buf == NULL) {
		struct i2c_msg msg = {
			.addr = dib->tuner->pll_addr,
			.flags = 0,
			.buf = buf,
			.len = sizeof(buf)
		};
		if (i2c_transfer (&dib->i2c_adap, &msg, 1) != 1)
			return -EIO;
		msleep(1);
	} else {
		pll_buf[0] = dib->tuner->pll_addr << 1;
		memcpy(&pll_buf[1],buf,4);
	}

	return 0;
}

static int dibusb_general_pll_init(struct dvb_frontend *fe,
		u8 pll_buf[5])
{
	struct usb_dibusb* dib = (struct usb_dibusb*) fe->dvb->priv;
	u8 buf[4];
	int ret=0;
	switch (dib->tuner->id) {
		case DIBUSB_TUNER_COFDM_PANASONIC_ENV77H11D5:
			ret = panasonic_cofdm_env77h11d5_tda6650_init(fe,buf);
			break;
		default:
			break;
	}

	if (ret)
		return ret;

	return dibusb_pll_i2c_helper(dib,pll_buf,buf);
}

static int dibusb_general_pll_set(struct dvb_frontend *fe,
		struct dvb_frontend_parameters *fep, u8 pll_buf[5])
{
	struct usb_dibusb* dib = (struct usb_dibusb*) fe->dvb->priv;
	u8 buf[4];
	int ret=0;

	switch (dib->tuner->id) {
		case DIBUSB_TUNER_CABLE_THOMSON:
			ret = thomson_cable_eu_pll_set(fep, buf);
			break;
		case DIBUSB_TUNER_COFDM_PANASONIC_ENV57H1XD5:
			ret = panasonic_cofdm_env57h1xd5_pll_set(fep, buf);
			break;
		case DIBUSB_TUNER_CABLE_LG_TDTP_E102P:
			ret = lg_tdtp_e102p_tua6034(fep, buf);
			break;
		case DIBUSB_TUNER_COFDM_PANASONIC_ENV77H11D5:
			ret = panasonic_cofdm_env77h11d5_tda6650_set(fep,buf);
			break;
		default:
			warn("no pll programming routine found for tuner %d.\n",dib->tuner->id);
			ret = -ENODEV;
			break;
	}

	if (ret)
		return ret;

	return dibusb_pll_i2c_helper(dib,pll_buf,buf);
}
