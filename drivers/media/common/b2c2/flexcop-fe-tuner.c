/*
 * Linux driver for digital TV devices equipped with B2C2 FlexcopII(b)/III
 * flexcop-fe-tuner.c - methods for frontend attachment and DiSEqC controlling
 * see flexcop.c for copyright information
 */
#include <media/tuner.h>
#include "flexcop.h"
#include "mt312.h"
#include "stv0299.h"
#include "s5h1420.h"
#include "itd1000.h"
#include "cx24113.h"
#include "cx24123.h"
#include "isl6421.h"
#include "cx24120.h"
#include "mt352.h"
#include "bcm3510.h"
#include "nxt200x.h"
#include "dvb-pll.h"
#include "lgdt330x.h"
#include "tuner-simple.h"
#include "stv0297.h"


/* Can we use the specified front-end?  Remember that if we are compiled
 * into the kernel we can't call code that's in modules.  */
#define FE_SUPPORTED(fe) (defined(CONFIG_DVB_##fe) || \
	(defined(CONFIG_DVB_##fe##_MODULE) && defined(MODULE)))

#if FE_SUPPORTED(BCM3510) || (FE_SUPPORTED(CX24120) && FE_SUPPORTED(ISL6421))
static int flexcop_fe_request_firmware(struct dvb_frontend *fe,
	const struct firmware **fw, char *name)
{
	struct flexcop_device *fc = fe->dvb->priv;

	return request_firmware(fw, name, fc->dev);
}
#endif

/* lnb control */
#if (FE_SUPPORTED(MT312) || FE_SUPPORTED(STV0299)) && FE_SUPPORTED(PLL)
static int flexcop_set_voltage(struct dvb_frontend *fe,
			       enum fe_sec_voltage voltage)
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
#endif

#if FE_SUPPORTED(S5H1420) || FE_SUPPORTED(STV0299) || FE_SUPPORTED(MT312)
static int __maybe_unused flexcop_sleep(struct dvb_frontend* fe)
{
	struct flexcop_device *fc = fe->dvb->priv;
	if (fc->fe_sleep)
		return fc->fe_sleep(fe);
	return 0;
}
#endif

/* SkyStar2 DVB-S rev 2.3 */
#if FE_SUPPORTED(MT312) && FE_SUPPORTED(PLL)
static int flexcop_set_tone(struct dvb_frontend *fe, enum fe_sec_tone_mode tone)
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

static int flexcop_send_diseqc_msg(struct dvb_frontend *fe,
	int len, u8 *msg, unsigned long burst)
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
			mdelay(12);
			udelay(500);
			flexcop_set_tone(fe, SEC_TONE_OFF);
		}
		msleep(20);
	}
	return 0;
}

static int flexcop_diseqc_send_master_cmd(struct dvb_frontend *fe,
	struct dvb_diseqc_master_cmd *cmd)
{
	return flexcop_send_diseqc_msg(fe, cmd->msg_len, cmd->msg, 0);
}

static int flexcop_diseqc_send_burst(struct dvb_frontend *fe,
				     enum fe_sec_mini_cmd minicmd)
{
	return flexcop_send_diseqc_msg(fe, 0, NULL, minicmd);
}

static struct mt312_config skystar23_samsung_tbdu18132_config = {
	.demod_address = 0x0e,
};

static int skystar2_rev23_attach(struct flexcop_device *fc,
	struct i2c_adapter *i2c)
{
	struct dvb_frontend_ops *ops;

	fc->fe = dvb_attach(mt312_attach, &skystar23_samsung_tbdu18132_config, i2c);
	if (!fc->fe)
		return 0;

	if (!dvb_attach(dvb_pll_attach, fc->fe, 0x61, i2c,
			DVB_PLL_SAMSUNG_TBDU18132))
		return 0;

	ops = &fc->fe->ops;
	ops->diseqc_send_master_cmd = flexcop_diseqc_send_master_cmd;
	ops->diseqc_send_burst      = flexcop_diseqc_send_burst;
	ops->set_tone               = flexcop_set_tone;
	ops->set_voltage            = flexcop_set_voltage;
	fc->fe_sleep                = ops->sleep;
	ops->sleep                  = flexcop_sleep;
	return 1;
}
#else
#define skystar2_rev23_attach NULL
#endif

/* SkyStar2 DVB-S rev 2.6 */
#if FE_SUPPORTED(STV0299) && FE_SUPPORTED(PLL)
static int samsung_tbmu24112_set_symbol_rate(struct dvb_frontend *fe,
	u32 srate, u32 ratio)
{
	u8 aclk = 0;
	u8 bclk = 0;

	if (srate < 1500000) {
		aclk = 0xb7; bclk = 0x47;
	} else if (srate < 3000000) {
		aclk = 0xb7; bclk = 0x4b;
	} else if (srate < 7000000) {
		aclk = 0xb7; bclk = 0x4f;
	} else if (srate < 14000000) {
		aclk = 0xb7; bclk = 0x53;
	} else if (srate < 30000000) {
		aclk = 0xb6; bclk = 0x53;
	} else if (srate < 45000000) {
		aclk = 0xb4; bclk = 0x51;
	}

	stv0299_writereg(fe, 0x13, aclk);
	stv0299_writereg(fe, 0x14, bclk);
	stv0299_writereg(fe, 0x1f, (ratio >> 16) & 0xff);
	stv0299_writereg(fe, 0x20, (ratio >>  8) & 0xff);
	stv0299_writereg(fe, 0x21,  ratio        & 0xf0);
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
	.lock_output = STV0299_LOCKOUTPUT_LK,
	.volt13_op0_op1 = STV0299_VOLT13_OP1,
	.min_delay_ms = 100,
	.set_symbol_rate = samsung_tbmu24112_set_symbol_rate,
};

static int skystar2_rev26_attach(struct flexcop_device *fc,
	struct i2c_adapter *i2c)
{
	fc->fe = dvb_attach(stv0299_attach, &samsung_tbmu24112_config, i2c);
	if (!fc->fe)
		return 0;

	if (!dvb_attach(dvb_pll_attach, fc->fe, 0x61, i2c,
			DVB_PLL_SAMSUNG_TBMU24112))
		return 0;

	fc->fe->ops.set_voltage = flexcop_set_voltage;
	fc->fe_sleep = fc->fe->ops.sleep;
	fc->fe->ops.sleep = flexcop_sleep;
	return 1;

}
#else
#define skystar2_rev26_attach NULL
#endif

/* SkyStar2 DVB-S rev 2.7 */
#if FE_SUPPORTED(S5H1420) && FE_SUPPORTED(ISL6421) && FE_SUPPORTED(TUNER_ITD1000)
static struct s5h1420_config skystar2_rev2_7_s5h1420_config = {
	.demod_address = 0x53,
	.invert = 1,
	.repeated_start_workaround = 1,
	.serial_mpeg = 1,
};

static struct itd1000_config skystar2_rev2_7_itd1000_config = {
	.i2c_address = 0x61,
};

static int skystar2_rev27_attach(struct flexcop_device *fc,
	struct i2c_adapter *i2c)
{
	flexcop_ibi_value r108;
	struct i2c_adapter *i2c_tuner;

	/* enable no_base_addr - no repeated start when reading */
	fc->fc_i2c_adap[0].no_base_addr = 1;
	fc->fe = dvb_attach(s5h1420_attach, &skystar2_rev2_7_s5h1420_config,
			    i2c);
	if (!fc->fe)
		goto fail;

	i2c_tuner = s5h1420_get_tuner_i2c_adapter(fc->fe);
	if (!i2c_tuner)
		goto fail;

	fc->fe_sleep = fc->fe->ops.sleep;
	fc->fe->ops.sleep = flexcop_sleep;

	/* enable no_base_addr - no repeated start when reading */
	fc->fc_i2c_adap[2].no_base_addr = 1;
	if (!dvb_attach(isl6421_attach, fc->fe, &fc->fc_i2c_adap[2].i2c_adap,
			0x08, 1, 1, false)) {
		err("ISL6421 could NOT be attached");
		goto fail_isl;
	}
	info("ISL6421 successfully attached");

	/* the ITD1000 requires a lower i2c clock - is it a problem ? */
	r108.raw = 0x00000506;
	fc->write_ibi_reg(fc, tw_sm_c_108, r108);
	if (!dvb_attach(itd1000_attach, fc->fe, i2c_tuner,
			&skystar2_rev2_7_itd1000_config)) {
		err("ITD1000 could NOT be attached");
		/* Should i2c clock be restored? */
		goto fail_isl;
	}
	info("ITD1000 successfully attached");

	return 1;

fail_isl:
	fc->fc_i2c_adap[2].no_base_addr = 0;
fail:
	/* for the next devices we need it again */
	fc->fc_i2c_adap[0].no_base_addr = 0;
	return 0;
}
#else
#define skystar2_rev27_attach NULL
#endif

/* SkyStar2 rev 2.8 */
#if FE_SUPPORTED(CX24123) && FE_SUPPORTED(ISL6421) && FE_SUPPORTED(TUNER_CX24113)
static struct cx24123_config skystar2_rev2_8_cx24123_config = {
	.demod_address = 0x55,
	.dont_use_pll = 1,
	.agc_callback = cx24113_agc_callback,
};

static const struct cx24113_config skystar2_rev2_8_cx24113_config = {
	.i2c_addr = 0x54,
	.xtal_khz = 10111,
};

static int skystar2_rev28_attach(struct flexcop_device *fc,
	struct i2c_adapter *i2c)
{
	struct i2c_adapter *i2c_tuner;

	fc->fe = dvb_attach(cx24123_attach, &skystar2_rev2_8_cx24123_config,
			    i2c);
	if (!fc->fe)
		return 0;

	i2c_tuner = cx24123_get_tuner_i2c_adapter(fc->fe);
	if (!i2c_tuner)
		return 0;

	if (!dvb_attach(cx24113_attach, fc->fe, &skystar2_rev2_8_cx24113_config,
			i2c_tuner)) {
		err("CX24113 could NOT be attached");
		return 0;
	}
	info("CX24113 successfully attached");

	fc->fc_i2c_adap[2].no_base_addr = 1;
	if (!dvb_attach(isl6421_attach, fc->fe, &fc->fc_i2c_adap[2].i2c_adap,
			0x08, 0, 0, false)) {
		err("ISL6421 could NOT be attached");
		fc->fc_i2c_adap[2].no_base_addr = 0;
		return 0;
	}
	info("ISL6421 successfully attached");
	/* TODO on i2c_adap[1] addr 0x11 (EEPROM) there seems to be an
	 * IR-receiver (PIC16F818) - but the card has no input for that ??? */
	return 1;
}
#else
#define skystar2_rev28_attach NULL
#endif

/* AirStar DVB-T */
#if FE_SUPPORTED(MT352) && FE_SUPPORTED(PLL)
static int samsung_tdtc9251dh0_demod_init(struct dvb_frontend *fe)
{
	static u8 mt352_clock_config[] = { 0x89, 0x18, 0x2d };
	static u8 mt352_reset[] = { 0x50, 0x80 };
	static u8 mt352_adc_ctl_1_cfg[] = { 0x8E, 0x40 };
	static u8 mt352_agc_cfg[] = { 0x67, 0x28, 0xa1 };
	static u8 mt352_capt_range_cfg[] = { 0x75, 0x32 };

	mt352_write(fe, mt352_clock_config, sizeof(mt352_clock_config));
	udelay(2000);
	mt352_write(fe, mt352_reset, sizeof(mt352_reset));
	mt352_write(fe, mt352_adc_ctl_1_cfg, sizeof(mt352_adc_ctl_1_cfg));
	mt352_write(fe, mt352_agc_cfg, sizeof(mt352_agc_cfg));
	mt352_write(fe, mt352_capt_range_cfg, sizeof(mt352_capt_range_cfg));
	return 0;
}

static struct mt352_config samsung_tdtc9251dh0_config = {
	.demod_address = 0x0f,
	.demod_init    = samsung_tdtc9251dh0_demod_init,
};

static int airstar_dvbt_attach(struct flexcop_device *fc,
	struct i2c_adapter *i2c)
{
	fc->fe = dvb_attach(mt352_attach, &samsung_tdtc9251dh0_config, i2c);
	if (!fc->fe)
		return 0;

	return !!dvb_attach(dvb_pll_attach, fc->fe, 0x61, NULL,
			    DVB_PLL_SAMSUNG_TDTC9251DH0);
}
#else
#define airstar_dvbt_attach NULL
#endif

/* AirStar ATSC 1st generation */
#if FE_SUPPORTED(BCM3510)
static struct bcm3510_config air2pc_atsc_first_gen_config = {
	.demod_address    = 0x0f,
	.request_firmware = flexcop_fe_request_firmware,
};

static int airstar_atsc1_attach(struct flexcop_device *fc,
	struct i2c_adapter *i2c)
{
	fc->fe = dvb_attach(bcm3510_attach, &air2pc_atsc_first_gen_config, i2c);
	return fc->fe != NULL;
}
#else
#define airstar_atsc1_attach NULL
#endif

/* AirStar ATSC 2nd generation */
#if FE_SUPPORTED(NXT200X) && FE_SUPPORTED(PLL)
static struct nxt200x_config samsung_tbmv_config = {
	.demod_address = 0x0a,
};

static int airstar_atsc2_attach(struct flexcop_device *fc,
	struct i2c_adapter *i2c)
{
	fc->fe = dvb_attach(nxt200x_attach, &samsung_tbmv_config, i2c);
	if (!fc->fe)
		return 0;

	return !!dvb_attach(dvb_pll_attach, fc->fe, 0x61, NULL,
			    DVB_PLL_SAMSUNG_TBMV);
}
#else
#define airstar_atsc2_attach NULL
#endif

/* AirStar ATSC 3rd generation */
#if FE_SUPPORTED(LGDT330X)
static struct lgdt330x_config air2pc_atsc_hd5000_config = {
	.demod_address       = 0x59,
	.demod_chip          = LGDT3303,
	.serial_mpeg         = 0x04,
	.clock_polarity_flip = 1,
};

static int airstar_atsc3_attach(struct flexcop_device *fc,
	struct i2c_adapter *i2c)
{
	fc->fe = dvb_attach(lgdt330x_attach, &air2pc_atsc_hd5000_config, i2c);
	if (!fc->fe)
		return 0;

	return !!dvb_attach(simple_tuner_attach, fc->fe, i2c, 0x61,
			    TUNER_LG_TDVS_H06XF);
}
#else
#define airstar_atsc3_attach NULL
#endif

/* CableStar2 DVB-C */
#if FE_SUPPORTED(STV0297) && FE_SUPPORTED(PLL)
static u8 alps_tdee4_stv0297_inittab[] = {
	0x80, 0x01,
	0x80, 0x00,
	0x81, 0x01,
	0x81, 0x00,
	0x00, 0x48,
	0x01, 0x58,
	0x03, 0x00,
	0x04, 0x00,
	0x07, 0x00,
	0x08, 0x00,
	0x30, 0xff,
	0x31, 0x9d,
	0x32, 0xff,
	0x33, 0x00,
	0x34, 0x29,
	0x35, 0x55,
	0x36, 0x80,
	0x37, 0x6e,
	0x38, 0x9c,
	0x40, 0x1a,
	0x41, 0xfe,
	0x42, 0x33,
	0x43, 0x00,
	0x44, 0xff,
	0x45, 0x00,
	0x46, 0x00,
	0x49, 0x04,
	0x4a, 0x51,
	0x4b, 0xf8,
	0x52, 0x30,
	0x53, 0x06,
	0x59, 0x06,
	0x5a, 0x5e,
	0x5b, 0x04,
	0x61, 0x49,
	0x62, 0x0a,
	0x70, 0xff,
	0x71, 0x04,
	0x72, 0x00,
	0x73, 0x00,
	0x74, 0x0c,
	0x80, 0x20,
	0x81, 0x00,
	0x82, 0x30,
	0x83, 0x00,
	0x84, 0x04,
	0x85, 0x22,
	0x86, 0x08,
	0x87, 0x1b,
	0x88, 0x00,
	0x89, 0x00,
	0x90, 0x00,
	0x91, 0x04,
	0xa0, 0x86,
	0xa1, 0x00,
	0xa2, 0x00,
	0xb0, 0x91,
	0xb1, 0x0b,
	0xc0, 0x5b,
	0xc1, 0x10,
	0xc2, 0x12,
	0xd0, 0x02,
	0xd1, 0x00,
	0xd2, 0x00,
	0xd3, 0x00,
	0xd4, 0x02,
	0xd5, 0x00,
	0xde, 0x00,
	0xdf, 0x01,
	0xff, 0xff,
};

static struct stv0297_config alps_tdee4_stv0297_config = {
	.demod_address = 0x1c,
	.inittab = alps_tdee4_stv0297_inittab,
};

static int cablestar2_attach(struct flexcop_device *fc,
	struct i2c_adapter *i2c)
{
	fc->fc_i2c_adap[0].no_base_addr = 1;
	fc->fe = dvb_attach(stv0297_attach, &alps_tdee4_stv0297_config, i2c);
	if (!fc->fe)
		goto fail;

	/* This tuner doesn't use the stv0297's I2C gate, but instead the
	 * tuner is connected to a different flexcop I2C adapter.  */
	if (fc->fe->ops.i2c_gate_ctrl)
		fc->fe->ops.i2c_gate_ctrl(fc->fe, 0);
	fc->fe->ops.i2c_gate_ctrl = NULL;

	if (!dvb_attach(dvb_pll_attach, fc->fe, 0x61,
			&fc->fc_i2c_adap[2].i2c_adap, DVB_PLL_TDEE4))
		goto fail;

	return 1;

fail:
	/* Reset for next frontend to try */
	fc->fc_i2c_adap[0].no_base_addr = 0;
	return 0;
}
#else
#define cablestar2_attach NULL
#endif

/* SkyStar S2 PCI DVB-S/S2 card based on Conexant cx24120/cx24118 */
#if FE_SUPPORTED(CX24120) && FE_SUPPORTED(ISL6421)
static const struct cx24120_config skystar2_rev3_3_cx24120_config = {
	.i2c_addr = 0x55,
	.xtal_khz = 10111,
	.initial_mpeg_config = { 0xa1, 0x76, 0x07 },
	.request_firmware = flexcop_fe_request_firmware,
	.i2c_wr_max = 4,
};

static int skystarS2_rev33_attach(struct flexcop_device *fc,
	struct i2c_adapter *i2c)
{
	fc->fe = dvb_attach(cx24120_attach,
			    &skystar2_rev3_3_cx24120_config, i2c);
	if (!fc->fe)
		return 0;

	fc->dev_type = FC_SKYS2_REV33;
	fc->fc_i2c_adap[2].no_base_addr = 1;
	if (!dvb_attach(isl6421_attach, fc->fe, &fc->fc_i2c_adap[2].i2c_adap,
			0x08, 0, 0, false)) {
		err("ISL6421 could NOT be attached!");
		fc->fc_i2c_adap[2].no_base_addr = 0;
		return 0;
	}
	info("ISL6421 successfully attached.");

	if (fc->has_32_hw_pid_filter)
		fc->skip_6_hw_pid_filter = 1;

	return 1;
}
#else
#define skystarS2_rev33_attach NULL
#endif

static struct {
	flexcop_device_type_t type;
	int (*attach)(struct flexcop_device *, struct i2c_adapter *);
} flexcop_frontends[] = {
	{ FC_SKY_REV27, skystar2_rev27_attach },
	{ FC_SKY_REV28, skystar2_rev28_attach },
	{ FC_SKY_REV26, skystar2_rev26_attach },
	{ FC_AIR_DVBT, airstar_dvbt_attach },
	{ FC_AIR_ATSC2, airstar_atsc2_attach },
	{ FC_AIR_ATSC3, airstar_atsc3_attach },
	{ FC_AIR_ATSC1, airstar_atsc1_attach },
	{ FC_CABLE, cablestar2_attach },
	{ FC_SKY_REV23, skystar2_rev23_attach },
	{ FC_SKYS2_REV33, skystarS2_rev33_attach },
};

/* try to figure out the frontend */
int flexcop_frontend_init(struct flexcop_device *fc)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(flexcop_frontends); i++) {
		if (!flexcop_frontends[i].attach)
			continue;
		/* type needs to be set before, because of some workarounds
		 * done based on the probed card type */
		fc->dev_type = flexcop_frontends[i].type;
		if (flexcop_frontends[i].attach(fc, &fc->fc_i2c_adap[0].i2c_adap))
			goto fe_found;
		/* Clean up partially attached frontend */
		if (fc->fe) {
			dvb_frontend_detach(fc->fe);
			fc->fe = NULL;
		}
	}
	fc->dev_type = FC_UNK;
	err("no frontend driver found for this B2C2/FlexCop adapter");
	return -ENODEV;

fe_found:
	info("found '%s' .", fc->fe->ops.info.name);
	if (dvb_register_frontend(&fc->dvb_adapter, fc->fe)) {
		err("frontend registration failed!");
		dvb_frontend_detach(fc->fe);
		fc->fe = NULL;
		return -EINVAL;
	}
	fc->init_state |= FC_STATE_FE_INIT;
	return 0;
}

void flexcop_frontend_exit(struct flexcop_device *fc)
{
	if (fc->init_state & FC_STATE_FE_INIT) {
		dvb_unregister_frontend(fc->fe);
		dvb_frontend_detach(fc->fe);
	}
	fc->init_state &= ~FC_STATE_FE_INIT;
}
