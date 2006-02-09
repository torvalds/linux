/*
 * This file is part of linux driver the digital TV devices equipped with B2C2 FlexcopII(b)/III
 *
 * flexcop-fe-tuner.c - methods for attaching a frontend and controlling DiSEqC.
 *
 * see flexcop.c for copyright information.
 */
#include "flexcop.h"

#include "stv0299.h"
#include "mt352.h"
#include "nxt200x.h"
#include "bcm3510.h"
#include "stv0297.h"
#include "mt312.h"
#include "lgdt330x.h"
#include "dvb-pll.h"

/* lnb control */

static int flexcop_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	struct flexcop_device *fc = fe->dvb->priv;
	flexcop_ibi_value v;
	deb_tuner("polarity/voltage = %u\n", voltage);

	v = fc->read_ibi_reg(fc, misc_204);
	switch (voltage) {
		case SEC_VOLTAGE_OFF:
			v.misc_204.ACPI1_sig = 1;
			break;
		case SEC_VOLTAGE_13:
			v.misc_204.ACPI1_sig = 0;
			v.misc_204.LNB_L_H_sig = 0;
			break;
		case SEC_VOLTAGE_18:
			v.misc_204.ACPI1_sig = 0;
			v.misc_204.LNB_L_H_sig = 1;
			break;
		default:
			err("unknown SEC_VOLTAGE value");
			return -EINVAL;
	}
	return fc->write_ibi_reg(fc, misc_204, v);
}

static int flexcop_sleep(struct dvb_frontend* fe)
{
	struct flexcop_device *fc = fe->dvb->priv;
/*	flexcop_ibi_value v = fc->read_ibi_reg(fc,misc_204); */

	if (fc->fe_sleep)
		return fc->fe_sleep(fe);

/*	v.misc_204.ACPI3_sig = 1;
	fc->write_ibi_reg(fc,misc_204,v);*/

	return 0;
}

static int flexcop_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	/* u16 wz_half_period_for_45_mhz[] = { 0x01ff, 0x0154, 0x00ff, 0x00cc }; */
	struct flexcop_device *fc = fe->dvb->priv;
	flexcop_ibi_value v;
	u16 ax;
	v.raw = 0;

	deb_tuner("tone = %u\n",tone);

	switch (tone) {
		case SEC_TONE_ON:
			ax = 0x01ff;
			break;
		case SEC_TONE_OFF:
			ax = 0;
			break;
		default:
			err("unknown SEC_TONE value");
			return -EINVAL;
	}

	v.lnb_switch_freq_200.LNB_CTLPrescaler_sig = 1; /* divide by 2 */

	v.lnb_switch_freq_200.LNB_CTLHighCount_sig = ax;
	v.lnb_switch_freq_200.LNB_CTLLowCount_sig  = ax == 0 ? 0x1ff : ax;

	return fc->write_ibi_reg(fc,lnb_switch_freq_200,v);
}

static void flexcop_diseqc_send_bit(struct dvb_frontend* fe, int data)
{
	flexcop_set_tone(fe, SEC_TONE_ON);
	udelay(data ? 500 : 1000);
	flexcop_set_tone(fe, SEC_TONE_OFF);
	udelay(data ? 1000 : 500);
}

static void flexcop_diseqc_send_byte(struct dvb_frontend* fe, int data)
{
	int i, par = 1, d;

	for (i = 7; i >= 0; i--) {
		d = (data >> i) & 1;
		par ^= d;
		flexcop_diseqc_send_bit(fe, d);
	}

	flexcop_diseqc_send_bit(fe, par);
}

static int flexcop_send_diseqc_msg(struct dvb_frontend* fe, int len, u8 *msg, unsigned long burst)
{
	int i;

	flexcop_set_tone(fe, SEC_TONE_OFF);
	mdelay(16);

	for (i = 0; i < len; i++)
		flexcop_diseqc_send_byte(fe,msg[i]);

	mdelay(16);

	if (burst != -1) {
		if (burst)
			flexcop_diseqc_send_byte(fe, 0xff);
		else {
			flexcop_set_tone(fe, SEC_TONE_ON);
			udelay(12500);
			flexcop_set_tone(fe, SEC_TONE_OFF);
		}
		msleep(20);
	}
	return 0;
}

static int flexcop_diseqc_send_master_cmd(struct dvb_frontend* fe, struct dvb_diseqc_master_cmd* cmd)
{
	return flexcop_send_diseqc_msg(fe, cmd->msg_len, cmd->msg, 0);
}

static int flexcop_diseqc_send_burst(struct dvb_frontend* fe, fe_sec_mini_cmd_t minicmd)
{
	return flexcop_send_diseqc_msg(fe, 0, NULL, minicmd);
}

/* dvb-s stv0299 */
static int samsung_tbmu24112_set_symbol_rate(struct dvb_frontend* fe, u32 srate, u32 ratio)
{
	u8 aclk = 0;
	u8 bclk = 0;

	if (srate < 1500000) { aclk = 0xb7; bclk = 0x47; }
	else if (srate < 3000000) { aclk = 0xb7; bclk = 0x4b; }
	else if (srate < 7000000) { aclk = 0xb7; bclk = 0x4f; }
	else if (srate < 14000000) { aclk = 0xb7; bclk = 0x53; }
	else if (srate < 30000000) { aclk = 0xb6; bclk = 0x53; }
	else if (srate < 45000000) { aclk = 0xb4; bclk = 0x51; }

	stv0299_writereg (fe, 0x13, aclk);
	stv0299_writereg (fe, 0x14, bclk);
	stv0299_writereg (fe, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg (fe, 0x20, (ratio >>  8) & 0xff);
	stv0299_writereg (fe, 0x21, (ratio      ) & 0xf0);

	return 0;
}

static int samsung_tbmu24112_pll_set(struct dvb_frontend* fe, struct i2c_adapter *i2c, struct dvb_frontend_parameters* params)
{
	u8 buf[4];
	u32 div;
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = buf, .len = sizeof(buf) };

	div = params->frequency / 125;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x84;  /* 0xC4 */
	buf[3] = 0x08;

	if (params->frequency < 1500000) buf[3] |= 0x10;

	if (i2c_transfer(i2c, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static u8 samsung_tbmu24112_inittab[] = {
	     0x01, 0x15,
	     0x02, 0x30,
	     0x03, 0x00,
	     0x04, 0x7D,
	     0x05, 0x35,
	     0x06, 0x02,
	     0x07, 0x00,
	     0x08, 0xC3,
	     0x0C, 0x00,
	     0x0D, 0x81,
	     0x0E, 0x23,
	     0x0F, 0x12,
	     0x10, 0x7E,
	     0x11, 0x84,
	     0x12, 0xB9,
	     0x13, 0x88,
	     0x14, 0x89,
	     0x15, 0xC9,
	     0x16, 0x00,
	     0x17, 0x5C,
	     0x18, 0x00,
	     0x19, 0x00,
	     0x1A, 0x00,
	     0x1C, 0x00,
	     0x1D, 0x00,
	     0x1E, 0x00,
	     0x1F, 0x3A,
	     0x20, 0x2E,
	     0x21, 0x80,
	     0x22, 0xFF,
	     0x23, 0xC1,
	     0x28, 0x00,
	     0x29, 0x1E,
	     0x2A, 0x14,
	     0x2B, 0x0F,
	     0x2C, 0x09,
	     0x2D, 0x05,
	     0x31, 0x1F,
	     0x32, 0x19,
	     0x33, 0xFE,
	     0x34, 0x93,
	     0xff, 0xff,
};

static struct stv0299_config samsung_tbmu24112_config = {
	.demod_address = 0x68,
	.inittab = samsung_tbmu24112_inittab,
	.mclk = 88000000UL,
	.invert = 0,
	.skip_reinit = 0,
	.lock_output = STV0229_LOCKOUTPUT_LK,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 100,
	.set_symbol_rate = samsung_tbmu24112_set_symbol_rate,
	.pll_set = samsung_tbmu24112_pll_set,
};

/* dvb-t mt352 */
static int samsung_tdtc9251dh0_demod_init(struct dvb_frontend* fe)
{
	static u8 mt352_clock_config [] = { 0x89, 0x18, 0x2d };
	static u8 mt352_reset [] = { 0x50, 0x80 };
	static u8 mt352_adc_ctl_1_cfg [] = { 0x8E, 0x40 };
	static u8 mt352_agc_cfg [] = { 0x67, 0x28, 0xa1 };
	static u8 mt352_capt_range_cfg[] = { 0x75, 0x32 };

	mt352_write(fe, mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(fe, mt352_reset, sizeof(mt352_reset));
	mt352_write(fe, mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));

	mt352_write(fe, mt352_agc_cfg, sizeof(mt352_agc_cfg));
	mt352_write(fe, mt352_capt_range_cfg, sizeof(mt352_capt_range_cfg));

	return 0;
}

static int samsung_tdtc9251dh0_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params, u8* pllbuf)
{
	u32 div;
	unsigned char bs = 0;

	#define IF_FREQUENCYx6 217    /* 6 * 36.16666666667MHz */
	div = (((params->frequency + 83333) * 3) / 500000) + IF_FREQUENCYx6;

	if (params->frequency >= 48000000 && params->frequency <= 154000000) bs = 0x09;
	if (params->frequency >= 161000000 && params->frequency <= 439000000) bs = 0x0a;
	if (params->frequency >= 447000000 && params->frequency <= 863000000) bs = 0x08;

	pllbuf[0] = 0xc2; /* Note: non-linux standard PLL i2c address */
	pllbuf[1] = div >> 8;
	pllbuf[2] = div & 0xff;
	pllbuf[3] = 0xcc;
	pllbuf[4] = bs;

	return 0;
}

static struct mt352_config samsung_tdtc9251dh0_config = {
	.demod_address = 0x0f,
	.demod_init    = samsung_tdtc9251dh0_demod_init,
	.pll_set       = samsung_tdtc9251dh0_pll_set,
};

static int flexcop_fe_request_firmware(struct dvb_frontend* fe, const struct firmware **fw, char* name)
{
	struct flexcop_device *fc = fe->dvb->priv;
	return request_firmware(fw, name, fc->dev);
}

static int lgdt3303_pll_set(struct dvb_frontend* fe,
			    struct dvb_frontend_parameters* params)
{
	struct flexcop_device *fc = fe->dvb->priv;
	u8 buf[4];
	struct i2c_msg msg =
		{ .addr = 0x61, .flags = 0, .buf = buf, .len = 4 };
	int err;

	dvb_pll_configure(&dvb_pll_tdvs_tua6034,buf, params->frequency, 0);
	dprintk(1, "%s: tuner at 0x%02x bytes: 0x%02x 0x%02x 0x%02x 0x%02x\n",
			__FUNCTION__, msg.addr, buf[0],buf[1],buf[2],buf[3]);
	if ((err = i2c_transfer(&fc->i2c_adap, &msg, 1)) != 1) {
		printk(KERN_WARNING "lgdt3303: %s error "
			   "(addr %02x <- %02x, err = %i)\n",
			   __FUNCTION__, buf[0], buf[1], err);
		if (err < 0)
			return err;
		else
			return -EREMOTEIO;
	}

	buf[0] = 0x86 | 0x18;
	buf[1] = 0x50;
	msg.len = 2;
	if ((err = i2c_transfer(&fc->i2c_adap, &msg, 1)) != 1) {
		printk(KERN_WARNING "lgdt3303: %s error "
			   "(addr %02x <- %02x, err = %i)\n",
			   __FUNCTION__, buf[0], buf[1], err);
		if (err < 0)
			return err;
		else
			return -EREMOTEIO;
	}

	return 0;
}

static struct lgdt330x_config air2pc_atsc_hd5000_config = {
	.demod_address       = 0x59,
	.demod_chip          = LGDT3303,
	.serial_mpeg         = 0x04,
	.pll_set             = lgdt3303_pll_set,
	.clock_polarity_flip = 1,
};

static struct nxt200x_config samsung_tbmv_config = {
	.demod_address    = 0x0a,
	.pll_address      = 0xc2,
	.pll_desc         = &dvb_pll_samsung_tbmv,
};

static struct bcm3510_config air2pc_atsc_first_gen_config = {
	.demod_address    = 0x0f,
	.request_firmware = flexcop_fe_request_firmware,
};

static int skystar23_samsung_tbdu18132_pll_set(struct dvb_frontend* fe, struct dvb_frontend_parameters* params)
{
	u8 buf[4];
	u32 div;
	struct i2c_msg msg = { .addr = 0x61, .flags = 0, .buf = buf, .len = sizeof(buf) };
	struct flexcop_device *fc = fe->dvb->priv;

	div = (params->frequency + (125/2)) / 125;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = (div >> 0) & 0xff;
	buf[2] = 0x84 | ((div >> 10) & 0x60);
	buf[3] = 0x80;

	if (params->frequency < 1550000)
		buf[3] |= 0x02;

	if (i2c_transfer(&fc->i2c_adap, &msg, 1) != 1)
		return -EIO;
	return 0;
}

static struct mt312_config skystar23_samsung_tbdu18132_config = {

	.demod_address = 0x0e,
	.pll_set = skystar23_samsung_tbdu18132_pll_set,
};


static u8 alps_tdee4_stv0297_inittab[] = {
	0x80, 0x01,
	0x80, 0x00,
	0x81, 0x01,
	0x81, 0x00,
	0x00, 0x09,
	0x01, 0x69,
	0x03, 0x00,
	0x04, 0x00,
	0x07, 0x00,
	0x08, 0x00,
	0x20, 0x00,
	0x21, 0x40,
	0x22, 0x00,
	0x23, 0x00,
	0x24, 0x40,
	0x25, 0x88,
	0x30, 0xff,
	0x31, 0x00,
	0x32, 0xff,
	0x33, 0x00,
	0x34, 0x50,
	0x35, 0x7f,
	0x36, 0x00,
	0x37, 0x20,
	0x38, 0x00,
	0x40, 0x1c,
	0x41, 0xff,
	0x42, 0x29,
	0x43, 0x00,
	0x44, 0xff,
	0x45, 0x00,
	0x46, 0x00,
	0x49, 0x04,
	0x4a, 0x00,
	0x4b, 0xf8,
	0x52, 0x30,
	0x55, 0xae,
	0x56, 0x47,
	0x57, 0xe1,
	0x58, 0x3a,
	0x5a, 0x1e,
	0x5b, 0x34,
	0x60, 0x00,
	0x63, 0x00,
	0x64, 0x00,
	0x65, 0x00,
	0x66, 0x00,
	0x67, 0x00,
	0x68, 0x00,
	0x69, 0x00,
	0x6a, 0x02,
	0x6b, 0x00,
	0x70, 0xff,
	0x71, 0x00,
	0x72, 0x00,
	0x73, 0x00,
	0x74, 0x0c,
	0x80, 0x00,
	0x81, 0x00,
	0x82, 0x00,
	0x83, 0x00,
	0x84, 0x04,
	0x85, 0x80,
	0x86, 0x24,
	0x87, 0x78,
	0x88, 0x10,
	0x89, 0x00,
	0x90, 0x01,
	0x91, 0x01,
	0xa0, 0x04,
	0xa1, 0x00,
	0xa2, 0x00,
	0xb0, 0x91,
	0xb1, 0x0b,
	0xc0, 0x53,
	0xc1, 0x70,
	0xc2, 0x12,
	0xd0, 0x00,
	0xd1, 0x00,
	0xd2, 0x00,
	0xd3, 0x00,
	0xd4, 0x00,
	0xd5, 0x00,
	0xde, 0x00,
	0xdf, 0x00,
	0x61, 0x49,
	0x62, 0x0b,
	0x53, 0x08,
	0x59, 0x08,
	0xff, 0xff,
};

static struct stv0297_config alps_tdee4_stv0297_config = {
	.demod_address = 0x1c,
	.inittab = alps_tdee4_stv0297_inittab,
//	.invert = 1,
//	.pll_set = alps_tdee4_stv0297_pll_set,
};

/* try to figure out the frontend, each card/box can have on of the following list */
int flexcop_frontend_init(struct flexcop_device *fc)
{
	struct dvb_frontend_ops *ops;

	/* try the sky v2.6 (stv0299/Samsung tbmu24112(sl1935)) */
	if ((fc->fe = stv0299_attach(&samsung_tbmu24112_config, &fc->i2c_adap)) != NULL) {
		ops = fc->fe->ops;

		ops->set_voltage = flexcop_set_voltage;

		fc->fe_sleep             = ops->sleep;
		ops->sleep               = flexcop_sleep;

		fc->dev_type          = FC_SKY;
		info("found the stv0299 at i2c address: 0x%02x",samsung_tbmu24112_config.demod_address);
	} else
	/* try the air dvb-t (mt352/Samsung tdtc9251dh0(??)) */
	if ((fc->fe = mt352_attach(&samsung_tdtc9251dh0_config, &fc->i2c_adap)) != NULL ) {
		fc->dev_type          = FC_AIR_DVB;
		info("found the mt352 at i2c address: 0x%02x",samsung_tdtc9251dh0_config.demod_address);
	} else
	/* try the air atsc 2nd generation (nxt2002) */
	if ((fc->fe = nxt200x_attach(&samsung_tbmv_config, &fc->i2c_adap)) != NULL) {
		fc->dev_type          = FC_AIR_ATSC2;
		info("found the nxt2002 at i2c address: 0x%02x",samsung_tbmv_config.demod_address);
	} else
	/* try the air atsc 3nd generation (lgdt3303) */
	if ((fc->fe = lgdt330x_attach(&air2pc_atsc_hd5000_config, &fc->i2c_adap)) != NULL) {
		fc->dev_type          = FC_AIR_ATSC3;
		info("found the lgdt3303 at i2c address: 0x%02x",air2pc_atsc_hd5000_config.demod_address);
	} else
	/* try the air atsc 1nd generation (bcm3510)/panasonic ct10s */
	if ((fc->fe = bcm3510_attach(&air2pc_atsc_first_gen_config, &fc->i2c_adap)) != NULL) {
		fc->dev_type          = FC_AIR_ATSC1;
		info("found the bcm3510 at i2c address: 0x%02x",air2pc_atsc_first_gen_config.demod_address);
	} else
	/* try the cable dvb (stv0297) */
	if ((fc->fe = stv0297_attach(&alps_tdee4_stv0297_config, &fc->i2c_adap)) != NULL) {
		fc->dev_type                        = FC_CABLE;
		info("found the stv0297 at i2c address: 0x%02x",alps_tdee4_stv0297_config.demod_address);
	} else
	/* try the sky v2.3 (vp310/Samsung tbdu18132(tsa5059)) */
	if ((fc->fe = vp310_attach(&skystar23_samsung_tbdu18132_config, &fc->i2c_adap)) != NULL) {
		ops = fc->fe->ops;

		ops->diseqc_send_master_cmd = flexcop_diseqc_send_master_cmd;
		ops->diseqc_send_burst      = flexcop_diseqc_send_burst;
		ops->set_tone               = flexcop_set_tone;
		ops->set_voltage            = flexcop_set_voltage;

		fc->fe_sleep                = ops->sleep;
		ops->sleep                  = flexcop_sleep;

		fc->dev_type                = FC_SKY_OLD;
		info("found the vp310 (aka mt312) at i2c address: 0x%02x",skystar23_samsung_tbdu18132_config.demod_address);
	}

	if (fc->fe == NULL) {
		err("no frontend driver found for this B2C2/FlexCop adapter");
		return -ENODEV;
	} else {
		if (dvb_register_frontend(&fc->dvb_adapter, fc->fe)) {
			err("frontend registration failed!");
			ops = fc->fe->ops;
			if (ops->release != NULL)
				ops->release(fc->fe);
			fc->fe = NULL;
			return -EINVAL;
		}
	}
	fc->init_state |= FC_STATE_FE_INIT;
	return 0;
}

void flexcop_frontend_exit(struct flexcop_device *fc)
{
	if (fc->init_state & FC_STATE_FE_INIT)
		dvb_unregister_frontend(fc->fe);

	fc->init_state &= ~FC_STATE_FE_INIT;
}
