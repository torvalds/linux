/*
 * Realtek RTL2832 DVB-T demodulator driver
 *
 * Copyright (C) 2012 Thomas Mair <thomas.mair86@gmail.com>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License along
 *	with this program; if not, write to the Free Software Foundation, Inc.,
 *	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "rtl2832_priv.h"
#include <linux/bitops.h>

int rtl2832_debug;
module_param_named(debug, rtl2832_debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

#define REG_MASK(b) (BIT(b + 1) - 1)

static const struct rtl2832_reg_entry registers[] = {
	[DVBT_SOFT_RST]		= {0x1, 0x1,   2, 2},
	[DVBT_IIC_REPEAT]	= {0x1, 0x1,   3, 3},
	[DVBT_TR_WAIT_MIN_8K]	= {0x1, 0x88, 11, 2},
	[DVBT_RSD_BER_FAIL_VAL]	= {0x1, 0x8f, 15, 0},
	[DVBT_EN_BK_TRK]	= {0x1, 0xa6,  7, 7},
	[DVBT_AD_EN_REG]	= {0x0, 0x8,   7, 7},
	[DVBT_AD_EN_REG1]	= {0x0, 0x8,   6, 6},
	[DVBT_EN_BBIN]		= {0x1, 0xb1,  0, 0},
	[DVBT_MGD_THD0]		= {0x1, 0x95,  7, 0},
	[DVBT_MGD_THD1]		= {0x1, 0x96,  7, 0},
	[DVBT_MGD_THD2]		= {0x1, 0x97,  7, 0},
	[DVBT_MGD_THD3]		= {0x1, 0x98,  7, 0},
	[DVBT_MGD_THD4]		= {0x1, 0x99,  7, 0},
	[DVBT_MGD_THD5]		= {0x1, 0x9a,  7, 0},
	[DVBT_MGD_THD6]		= {0x1, 0x9b,  7, 0},
	[DVBT_MGD_THD7]		= {0x1, 0x9c,  7, 0},
	[DVBT_EN_CACQ_NOTCH]	= {0x1, 0x61,  4, 4},
	[DVBT_AD_AV_REF]	= {0x0, 0x9,   6, 0},
	[DVBT_REG_PI]		= {0x0, 0xa,   2, 0},
	[DVBT_PIP_ON]		= {0x0, 0x21,  3, 3},
	[DVBT_SCALE1_B92]	= {0x2, 0x92,  7, 0},
	[DVBT_SCALE1_B93]	= {0x2, 0x93,  7, 0},
	[DVBT_SCALE1_BA7]	= {0x2, 0xa7,  7, 0},
	[DVBT_SCALE1_BA9]	= {0x2, 0xa9,  7, 0},
	[DVBT_SCALE1_BAA]	= {0x2, 0xaa,  7, 0},
	[DVBT_SCALE1_BAB]	= {0x2, 0xab,  7, 0},
	[DVBT_SCALE1_BAC]	= {0x2, 0xac,  7, 0},
	[DVBT_SCALE1_BB0]	= {0x2, 0xb0,  7, 0},
	[DVBT_SCALE1_BB1]	= {0x2, 0xb1,  7, 0},
	[DVBT_KB_P1]		= {0x1, 0x64,  3, 1},
	[DVBT_KB_P2]		= {0x1, 0x64,  6, 4},
	[DVBT_KB_P3]		= {0x1, 0x65,  2, 0},
	[DVBT_OPT_ADC_IQ]	= {0x0, 0x6,   5, 4},
	[DVBT_AD_AVI]		= {0x0, 0x9,   1, 0},
	[DVBT_AD_AVQ]		= {0x0, 0x9,   3, 2},
	[DVBT_K1_CR_STEP12]	= {0x2, 0xad,  9, 4},
	[DVBT_TRK_KS_P2]	= {0x1, 0x6f,  2, 0},
	[DVBT_TRK_KS_I2]	= {0x1, 0x70,  5, 3},
	[DVBT_TR_THD_SET2]	= {0x1, 0x72,  3, 0},
	[DVBT_TRK_KC_P2]	= {0x1, 0x73,  5, 3},
	[DVBT_TRK_KC_I2]	= {0x1, 0x75,  2, 0},
	[DVBT_CR_THD_SET2]	= {0x1, 0x76,  7, 6},
	[DVBT_PSET_IFFREQ]	= {0x1, 0x19, 21, 0},
	[DVBT_SPEC_INV]		= {0x1, 0x15,  0, 0},
	[DVBT_RSAMP_RATIO]	= {0x1, 0x9f, 27, 2},
	[DVBT_CFREQ_OFF_RATIO]	= {0x1, 0x9d, 23, 4},
	[DVBT_FSM_STAGE]	= {0x3, 0x51,  6, 3},
	[DVBT_RX_CONSTEL]	= {0x3, 0x3c,  3, 2},
	[DVBT_RX_HIER]		= {0x3, 0x3c,  6, 4},
	[DVBT_RX_C_RATE_LP]	= {0x3, 0x3d,  2, 0},
	[DVBT_RX_C_RATE_HP]	= {0x3, 0x3d,  5, 3},
	[DVBT_GI_IDX]		= {0x3, 0x51,  1, 0},
	[DVBT_FFT_MODE_IDX]	= {0x3, 0x51,  2, 2},
	[DVBT_RSD_BER_EST]	= {0x3, 0x4e, 15, 0},
	[DVBT_CE_EST_EVM]	= {0x4, 0xc,  15, 0},
	[DVBT_RF_AGC_VAL]	= {0x3, 0x5b, 13, 0},
	[DVBT_IF_AGC_VAL]	= {0x3, 0x59, 13, 0},
	[DVBT_DAGC_VAL]		= {0x3, 0x5,   7, 0},
	[DVBT_SFREQ_OFF]	= {0x3, 0x18, 13, 0},
	[DVBT_CFREQ_OFF]	= {0x3, 0x5f, 17, 0},
	[DVBT_POLAR_RF_AGC]	= {0x0, 0xe,   1, 1},
	[DVBT_POLAR_IF_AGC]	= {0x0, 0xe,   0, 0},
	[DVBT_AAGC_HOLD]	= {0x1, 0x4,   5, 5},
	[DVBT_EN_RF_AGC]	= {0x1, 0x4,   6, 6},
	[DVBT_EN_IF_AGC]	= {0x1, 0x4,   7, 7},
	[DVBT_IF_AGC_MIN]	= {0x1, 0x8,   7, 0},
	[DVBT_IF_AGC_MAX]	= {0x1, 0x9,   7, 0},
	[DVBT_RF_AGC_MIN]	= {0x1, 0xa,   7, 0},
	[DVBT_RF_AGC_MAX]	= {0x1, 0xb,   7, 0},
	[DVBT_IF_AGC_MAN]	= {0x1, 0xc,   6, 6},
	[DVBT_IF_AGC_MAN_VAL]	= {0x1, 0xc,  13, 0},
	[DVBT_RF_AGC_MAN]	= {0x1, 0xe,   6, 6},
	[DVBT_RF_AGC_MAN_VAL]	= {0x1, 0xe,  13, 0},
	[DVBT_DAGC_TRG_VAL]	= {0x1, 0x12,  7, 0},
	[DVBT_AGC_TARG_VAL_0]	= {0x1, 0x2,   0, 0},
	[DVBT_AGC_TARG_VAL_8_1]	= {0x1, 0x3,   7, 0},
	[DVBT_AAGC_LOOP_GAIN]	= {0x1, 0xc7,  5, 1},
	[DVBT_LOOP_GAIN2_3_0]	= {0x1, 0x4,   4, 1},
	[DVBT_LOOP_GAIN2_4]	= {0x1, 0x5,   7, 7},
	[DVBT_LOOP_GAIN3]	= {0x1, 0xc8,  4, 0},
	[DVBT_VTOP1]		= {0x1, 0x6,   5, 0},
	[DVBT_VTOP2]		= {0x1, 0xc9,  5, 0},
	[DVBT_VTOP3]		= {0x1, 0xca,  5, 0},
	[DVBT_KRF1]		= {0x1, 0xcb,  7, 0},
	[DVBT_KRF2]		= {0x1, 0x7,   7, 0},
	[DVBT_KRF3]		= {0x1, 0xcd,  7, 0},
	[DVBT_KRF4]		= {0x1, 0xce,  7, 0},
	[DVBT_EN_GI_PGA]	= {0x1, 0xe5,  0, 0},
	[DVBT_THD_LOCK_UP]	= {0x1, 0xd9,  8, 0},
	[DVBT_THD_LOCK_DW]	= {0x1, 0xdb,  8, 0},
	[DVBT_THD_UP1]		= {0x1, 0xdd,  7, 0},
	[DVBT_THD_DW1]		= {0x1, 0xde,  7, 0},
	[DVBT_INTER_CNT_LEN]	= {0x1, 0xd8,  3, 0},
	[DVBT_GI_PGA_STATE]	= {0x1, 0xe6,  3, 3},
	[DVBT_EN_AGC_PGA]	= {0x1, 0xd7,  0, 0},
	[DVBT_CKOUTPAR]		= {0x1, 0x7b,  5, 5},
	[DVBT_CKOUT_PWR]	= {0x1, 0x7b,  6, 6},
	[DVBT_SYNC_DUR]		= {0x1, 0x7b,  7, 7},
	[DVBT_ERR_DUR]		= {0x1, 0x7c,  0, 0},
	[DVBT_SYNC_LVL]		= {0x1, 0x7c,  1, 1},
	[DVBT_ERR_LVL]		= {0x1, 0x7c,  2, 2},
	[DVBT_VAL_LVL]		= {0x1, 0x7c,  3, 3},
	[DVBT_SERIAL]		= {0x1, 0x7c,  4, 4},
	[DVBT_SER_LSB]		= {0x1, 0x7c,  5, 5},
	[DVBT_CDIV_PH0]		= {0x1, 0x7d,  3, 0},
	[DVBT_CDIV_PH1]		= {0x1, 0x7d,  7, 4},
	[DVBT_MPEG_IO_OPT_2_2]	= {0x0, 0x6,   7, 7},
	[DVBT_MPEG_IO_OPT_1_0]	= {0x0, 0x7,   7, 6},
	[DVBT_CKOUTPAR_PIP]	= {0x0, 0xb7,  4, 4},
	[DVBT_CKOUT_PWR_PIP]	= {0x0, 0xb7,  3, 3},
	[DVBT_SYNC_LVL_PIP]	= {0x0, 0xb7,  2, 2},
	[DVBT_ERR_LVL_PIP]	= {0x0, 0xb7,  1, 1},
	[DVBT_VAL_LVL_PIP]	= {0x0, 0xb7,  0, 0},
	[DVBT_CKOUTPAR_PID]	= {0x0, 0xb9,  4, 4},
	[DVBT_CKOUT_PWR_PID]	= {0x0, 0xb9,  3, 3},
	[DVBT_SYNC_LVL_PID]	= {0x0, 0xb9,  2, 2},
	[DVBT_ERR_LVL_PID]	= {0x0, 0xb9,  1, 1},
	[DVBT_VAL_LVL_PID]	= {0x0, 0xb9,  0, 0},
	[DVBT_SM_PASS]		= {0x1, 0x93, 11, 0},
	[DVBT_AD7_SETTING]	= {0x0, 0x11, 15, 0},
	[DVBT_RSSI_R]		= {0x3, 0x1,   6, 0},
	[DVBT_ACI_DET_IND]	= {0x3, 0x12,  0, 0},
	[DVBT_REG_MON]		= {0x0, 0xd,   1, 0},
	[DVBT_REG_MONSEL]	= {0x0, 0xd,   2, 2},
	[DVBT_REG_GPE]		= {0x0, 0xd,   7, 7},
	[DVBT_REG_GPO]		= {0x0, 0x10,  0, 0},
	[DVBT_REG_4MSEL]	= {0x0, 0x13,  0, 0},
};

/* write multiple hardware registers */
static int rtl2832_wr(struct rtl2832_priv *priv, u8 reg, u8 *val, int len)
{
	int ret;
	u8 buf[1+len];
	struct i2c_msg msg[1] = {
		{
			.addr = priv->cfg.i2c_addr,
			.flags = 0,
			.len = 1+len,
			.buf = buf,
		}
	};

	buf[0] = reg;
	memcpy(&buf[1], val, len);

	ret = i2c_transfer(priv->i2c, msg, 1);
	if (ret == 1) {
		ret = 0;
	} else {
		warn("i2c wr failed=%d reg=%02x len=%d", ret, reg, len);
		ret = -EREMOTEIO;
	}
	return ret;
}

/* read multiple hardware registers */
static int rtl2832_rd(struct rtl2832_priv *priv, u8 reg, u8 *val, int len)
{
	int ret;
	struct i2c_msg msg[2] = {
		{
			.addr = priv->cfg.i2c_addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		}, {
			.addr = priv->cfg.i2c_addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = val,
		}
	};

	ret = i2c_transfer(priv->i2c, msg, 2);
	if (ret == 2) {
		ret = 0;
	} else {
		warn("i2c rd failed=%d reg=%02x len=%d", ret, reg, len);
		ret = -EREMOTEIO;
}
return ret;
}

/* write multiple registers */
static int rtl2832_wr_regs(struct rtl2832_priv *priv, u8 reg, u8 page, u8 *val,
	int len)
{
	int ret;


	/* switch bank if needed */
	if (page != priv->page) {
		ret = rtl2832_wr(priv, 0x00, &page, 1);
		if (ret)
			return ret;

		priv->page = page;
}

return rtl2832_wr(priv, reg, val, len);
}

/* read multiple registers */
static int rtl2832_rd_regs(struct rtl2832_priv *priv, u8 reg, u8 page, u8 *val,
	int len)
{
	int ret;

	/* switch bank if needed */
	if (page != priv->page) {
		ret = rtl2832_wr(priv, 0x00, &page, 1);
		if (ret)
			return ret;

		priv->page = page;
	}

	return rtl2832_rd(priv, reg, val, len);
}

#if 0 /* currently not used */
/* write single register */
static int rtl2832_wr_reg(struct rtl2832_priv *priv, u8 reg, u8 page, u8 val)
{
	return rtl2832_wr_regs(priv, reg, page, &val, 1);
}
#endif

/* read single register */
static int rtl2832_rd_reg(struct rtl2832_priv *priv, u8 reg, u8 page, u8 *val)
{
	return rtl2832_rd_regs(priv, reg, page, val, 1);
}

int rtl2832_rd_demod_reg(struct rtl2832_priv *priv, int reg, u32 *val)
{
	int ret;

	u8 reg_start_addr;
	u8 msb, lsb;
	u8 page;
	u8 reading[4];
	u32 reading_tmp;
	int i;

	u8 len;
	u32 mask;

	reg_start_addr = registers[reg].start_address;
	msb = registers[reg].msb;
	lsb = registers[reg].lsb;
	page = registers[reg].page;

	len = (msb >> 3) + 1;
	mask = REG_MASK(msb - lsb);

	ret = rtl2832_rd_regs(priv, reg_start_addr, page, &reading[0], len);
	if (ret)
		goto err;

	reading_tmp = 0;
	for (i = 0; i < len; i++)
		reading_tmp |= reading[i] << ((len - 1 - i) * 8);

	*val = (reading_tmp >> lsb) & mask;

	return ret;

err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;

}

int rtl2832_wr_demod_reg(struct rtl2832_priv *priv, int reg, u32 val)
{
	int ret, i;
	u8 len;
	u8 reg_start_addr;
	u8 msb, lsb;
	u8 page;
	u32 mask;


	u8 reading[4];
	u8 writing[4];
	u32 reading_tmp;
	u32 writing_tmp;


	reg_start_addr = registers[reg].start_address;
	msb = registers[reg].msb;
	lsb = registers[reg].lsb;
	page = registers[reg].page;

	len = (msb >> 3) + 1;
	mask = REG_MASK(msb - lsb);


	ret = rtl2832_rd_regs(priv, reg_start_addr, page, &reading[0], len);
	if (ret)
		goto err;

	reading_tmp = 0;
	for (i = 0; i < len; i++)
		reading_tmp |= reading[i] << ((len - 1 - i) * 8);

	writing_tmp = reading_tmp & ~(mask << lsb);
	writing_tmp |= ((val & mask) << lsb);


	for (i = 0; i < len; i++)
		writing[i] = (writing_tmp >> ((len - 1 - i) * 8)) & 0xff;

	ret = rtl2832_wr_regs(priv, reg_start_addr, page, &writing[0], len);
	if (ret)
		goto err;

	return ret;

err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;

}


static int rtl2832_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	int ret;
	struct rtl2832_priv *priv = fe->demodulator_priv;

	dbg("%s: enable=%d", __func__, enable);

	/* gate already open or close */
	if (priv->i2c_gate_state == enable)
		return 0;

	ret = rtl2832_wr_demod_reg(priv, DVBT_IIC_REPEAT, (enable ? 0x1 : 0x0));
	if (ret)
		goto err;

	priv->i2c_gate_state = enable;

	return ret;
err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}



static int rtl2832_init(struct dvb_frontend *fe)
{
	struct rtl2832_priv *priv = fe->demodulator_priv;
	int i, ret;

	u8 en_bbin;
	u64 pset_iffreq;

	/* initialization values for the demodulator registers */
	struct rtl2832_reg_value rtl2832_initial_regs[] = {
		{DVBT_AD_EN_REG,		0x1},
		{DVBT_AD_EN_REG1,		0x1},
		{DVBT_RSD_BER_FAIL_VAL,		0x2800},
		{DVBT_MGD_THD0,			0x10},
		{DVBT_MGD_THD1,			0x20},
		{DVBT_MGD_THD2,			0x20},
		{DVBT_MGD_THD3,			0x40},
		{DVBT_MGD_THD4,			0x22},
		{DVBT_MGD_THD5,			0x32},
		{DVBT_MGD_THD6,			0x37},
		{DVBT_MGD_THD7,			0x39},
		{DVBT_EN_BK_TRK,		0x0},
		{DVBT_EN_CACQ_NOTCH,		0x0},
		{DVBT_AD_AV_REF,		0x2a},
		{DVBT_REG_PI,			0x6},
		{DVBT_PIP_ON,			0x0},
		{DVBT_CDIV_PH0,			0x8},
		{DVBT_CDIV_PH1,			0x8},
		{DVBT_SCALE1_B92,		0x4},
		{DVBT_SCALE1_B93,		0xb0},
		{DVBT_SCALE1_BA7,		0x78},
		{DVBT_SCALE1_BA9,		0x28},
		{DVBT_SCALE1_BAA,		0x59},
		{DVBT_SCALE1_BAB,		0x83},
		{DVBT_SCALE1_BAC,		0xd4},
		{DVBT_SCALE1_BB0,		0x65},
		{DVBT_SCALE1_BB1,		0x43},
		{DVBT_KB_P1,			0x1},
		{DVBT_KB_P2,			0x4},
		{DVBT_KB_P3,			0x7},
		{DVBT_K1_CR_STEP12,		0xa},
		{DVBT_REG_GPE,			0x1},
		{DVBT_SERIAL,			0x0},
		{DVBT_CDIV_PH0,			0x9},
		{DVBT_CDIV_PH1,			0x9},
		{DVBT_MPEG_IO_OPT_2_2,		0x0},
		{DVBT_MPEG_IO_OPT_1_0,		0x0},
		{DVBT_TRK_KS_P2,		0x4},
		{DVBT_TRK_KS_I2,		0x7},
		{DVBT_TR_THD_SET2,		0x6},
		{DVBT_TRK_KC_I2,		0x5},
		{DVBT_CR_THD_SET2,		0x1},
		{DVBT_SPEC_INV,			0x0},
		{DVBT_DAGC_TRG_VAL,		0x5a},
		{DVBT_AGC_TARG_VAL_0,		0x0},
		{DVBT_AGC_TARG_VAL_8_1,		0x5a},
		{DVBT_AAGC_LOOP_GAIN,		0x16},
		{DVBT_LOOP_GAIN2_3_0,		0x6},
		{DVBT_LOOP_GAIN2_4,		0x1},
		{DVBT_LOOP_GAIN3,		0x16},
		{DVBT_VTOP1,			0x35},
		{DVBT_VTOP2,			0x21},
		{DVBT_VTOP3,			0x21},
		{DVBT_KRF1,			0x0},
		{DVBT_KRF2,			0x40},
		{DVBT_KRF3,			0x10},
		{DVBT_KRF4,			0x10},
		{DVBT_IF_AGC_MIN,		0x80},
		{DVBT_IF_AGC_MAX,		0x7f},
		{DVBT_RF_AGC_MIN,		0x80},
		{DVBT_RF_AGC_MAX,		0x7f},
		{DVBT_POLAR_RF_AGC,		0x0},
		{DVBT_POLAR_IF_AGC,		0x0},
		{DVBT_AD7_SETTING,		0xe9bf},
		{DVBT_EN_GI_PGA,		0x0},
		{DVBT_THD_LOCK_UP,		0x0},
		{DVBT_THD_LOCK_DW,		0x0},
		{DVBT_THD_UP1,			0x11},
		{DVBT_THD_DW1,			0xef},
		{DVBT_INTER_CNT_LEN,		0xc},
		{DVBT_GI_PGA_STATE,		0x0},
		{DVBT_EN_AGC_PGA,		0x1},
		{DVBT_IF_AGC_MAN,		0x0},
	};


	dbg("%s", __func__);

	en_bbin = (priv->cfg.if_dvbt == 0 ? 0x1 : 0x0);

	/*
	* PSET_IFFREQ = - floor((IfFreqHz % CrystalFreqHz) * pow(2, 22)
	*		/ CrystalFreqHz)
	*/
	pset_iffreq = priv->cfg.if_dvbt % priv->cfg.xtal;
	pset_iffreq *= 0x400000;
	pset_iffreq = div_u64(pset_iffreq, priv->cfg.xtal);
	pset_iffreq = pset_iffreq & 0x3fffff;



	for (i = 0; i < ARRAY_SIZE(rtl2832_initial_regs); i++) {
		ret = rtl2832_wr_demod_reg(priv, rtl2832_initial_regs[i].reg,
			rtl2832_initial_regs[i].value);
		if (ret)
			goto err;
	}

	/* if frequency settings */
	ret = rtl2832_wr_demod_reg(priv, DVBT_EN_BBIN, en_bbin);
		if (ret)
			goto err;

	ret = rtl2832_wr_demod_reg(priv, DVBT_PSET_IFFREQ, pset_iffreq);
		if (ret)
			goto err;

	priv->sleeping = false;

	return ret;

err:
	dbg("%s: failed=%d", __func__, ret);
	return ret;
}

static int rtl2832_sleep(struct dvb_frontend *fe)
{
	struct rtl2832_priv *priv = fe->demodulator_priv;

	dbg("%s", __func__);
	priv->sleeping = true;
	return 0;
}

int rtl2832_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *s)
{
	dbg("%s", __func__);
	s->min_delay_ms = 1000;
	s->step_size = fe->ops.info.frequency_stepsize * 2;
	s->max_drift = (fe->ops.info.frequency_stepsize * 2) + 1;
	return 0;
}

static int rtl2832_set_frontend(struct dvb_frontend *fe)
{
	struct rtl2832_priv *priv = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int ret, i, j;
	u64 bw_mode, num, num2;
	u32 resamp_ratio, cfreq_off_ratio;


	static u8 bw_params[3][32] = {
	/* 6 MHz bandwidth */
		{
		0xf5, 0xff, 0x15, 0x38, 0x5d, 0x6d, 0x52, 0x07, 0xfa, 0x2f,
		0x53, 0xf5, 0x3f, 0xca, 0x0b, 0x91, 0xea, 0x30, 0x63, 0xb2,
		0x13, 0xda, 0x0b, 0xc4, 0x18, 0x7e, 0x16, 0x66, 0x08, 0x67,
		0x19, 0xe0,
		},

	/*  7 MHz bandwidth */
		{
		0xe7, 0xcc, 0xb5, 0xba, 0xe8, 0x2f, 0x67, 0x61, 0x00, 0xaf,
		0x86, 0xf2, 0xbf, 0x59, 0x04, 0x11, 0xb6, 0x33, 0xa4, 0x30,
		0x15, 0x10, 0x0a, 0x42, 0x18, 0xf8, 0x17, 0xd9, 0x07, 0x22,
		0x19, 0x10,
		},

	/*  8 MHz bandwidth */
		{
		0x09, 0xf6, 0xd2, 0xa7, 0x9a, 0xc9, 0x27, 0x77, 0x06, 0xbf,
		0xec, 0xf4, 0x4f, 0x0b, 0xfc, 0x01, 0x63, 0x35, 0x54, 0xa7,
		0x16, 0x66, 0x08, 0xb4, 0x19, 0x6e, 0x19, 0x65, 0x05, 0xc8,
		0x19, 0xe0,
		},
	};


	dbg("%s: frequency=%d bandwidth_hz=%d inversion=%d", __func__,
		c->frequency, c->bandwidth_hz, c->inversion);


	/* program tuner */
	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);


	switch (c->bandwidth_hz) {
	case 6000000:
		i = 0;
		bw_mode = 48000000;
		break;
	case 7000000:
		i = 1;
		bw_mode = 56000000;
		break;
	case 8000000:
		i = 2;
		bw_mode = 64000000;
		break;
	default:
		dbg("invalid bandwidth");
		return -EINVAL;
	}

	for (j = 0; j < sizeof(bw_params[0]); j++) {
		ret = rtl2832_wr_regs(priv, 0x1c+j, 1, &bw_params[i][j], 1);
		if (ret)
			goto err;
	}

	/* calculate and set resample ratio
	* RSAMP_RATIO = floor(CrystalFreqHz * 7 * pow(2, 22)
	*	/ ConstWithBandwidthMode)
	*/
	num = priv->cfg.xtal * 7;
	num *= 0x400000;
	num = div_u64(num, bw_mode);
	resamp_ratio =  num & 0x3ffffff;
	ret = rtl2832_wr_demod_reg(priv, DVBT_RSAMP_RATIO, resamp_ratio);
	if (ret)
		goto err;

	/* calculate and set cfreq off ratio
	* CFREQ_OFF_RATIO = - floor(ConstWithBandwidthMode * pow(2, 20)
	*	/ (CrystalFreqHz * 7))
	*/
	num = bw_mode << 20;
	num2 = priv->cfg.xtal * 7;
	num = div_u64(num, num2);
	num = -num;
	cfreq_off_ratio = num & 0xfffff;
	ret = rtl2832_wr_demod_reg(priv, DVBT_CFREQ_OFF_RATIO, cfreq_off_ratio);
	if (ret)
		goto err;


	/* soft reset */
	ret = rtl2832_wr_demod_reg(priv, DVBT_SOFT_RST, 0x1);
	if (ret)
		goto err;

	ret = rtl2832_wr_demod_reg(priv, DVBT_SOFT_RST, 0x0);
	if (ret)
		goto err;

	return ret;
err:
	info("%s: failed=%d", __func__, ret);
	return ret;
}

static int rtl2832_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct rtl2832_priv *priv = fe->demodulator_priv;
	int ret;
	u32 tmp;
	*status = 0;


	dbg("%s", __func__);
	if (priv->sleeping)
		return 0;

	ret = rtl2832_rd_demod_reg(priv, DVBT_FSM_STAGE, &tmp);
	if (ret)
		goto err;

	if (tmp == 11) {
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI | FE_HAS_SYNC | FE_HAS_LOCK;
	}
	/* TODO find out if this is also true for rtl2832? */
	/*else if (tmp == 10) {
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI;
	}*/

	return ret;
err:
	info("%s: failed=%d", __func__, ret);
	return ret;
}

static int rtl2832_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	*snr = 0;
	return 0;
}

static int rtl2832_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	*ber = 0;
	return 0;
}

static int rtl2832_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	*ucblocks = 0;
	return 0;
}


static int rtl2832_read_signal_strength(struct dvb_frontend *fe, u16 *strength)
{
	*strength = 0;
	return 0;
}

static struct dvb_frontend_ops rtl2832_ops;

static void rtl2832_release(struct dvb_frontend *fe)
{
	struct rtl2832_priv *priv = fe->demodulator_priv;

	dbg("%s", __func__);
	kfree(priv);
}

struct dvb_frontend *rtl2832_attach(const struct rtl2832_config *cfg,
	struct i2c_adapter *i2c)
{
	struct rtl2832_priv *priv = NULL;
	int ret = 0;
	u8 tmp;

	dbg("%s", __func__);

	/* allocate memory for the internal state */
	priv = kzalloc(sizeof(struct rtl2832_priv), GFP_KERNEL);
	if (priv == NULL)
		goto err;

	/* setup the priv */
	priv->i2c = i2c;
	priv->tuner = cfg->tuner;
	memcpy(&priv->cfg, cfg, sizeof(struct rtl2832_config));

	/* check if the demod is there */
	ret = rtl2832_rd_reg(priv, 0x00, 0x0, &tmp);
	if (ret)
		goto err;

	/* create dvb_frontend */
	memcpy(&priv->fe.ops, &rtl2832_ops, sizeof(struct dvb_frontend_ops));
	priv->fe.demodulator_priv = priv;

	/* TODO implement sleep mode */
	priv->sleeping = true;

	return &priv->fe;
err:
	dbg("%s: failed=%d", __func__, ret);
	kfree(priv);
	return NULL;
}
EXPORT_SYMBOL(rtl2832_attach);

static struct dvb_frontend_ops rtl2832_ops = {
	.delsys = { SYS_DVBT },
	.info = {
		.name = "Realtek RTL2832 (DVB-T)",
		.frequency_min	  = 174000000,
		.frequency_max	  = 862000000,
		.frequency_stepsize = 166667,
		.caps = FE_CAN_FEC_1_2 |
			FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 |
			FE_CAN_FEC_7_8 |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK |
			FE_CAN_QAM_16 |
			FE_CAN_QAM_64 |
			FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_RECOVER |
			FE_CAN_MUTE_TS
	 },

	.release = rtl2832_release,

	.init = rtl2832_init,
	.sleep = rtl2832_sleep,

	.get_tune_settings = rtl2832_get_tune_settings,

	.set_frontend = rtl2832_set_frontend,

	.read_status = rtl2832_read_status,
	.read_snr = rtl2832_read_snr,
	.read_ber = rtl2832_read_ber,
	.read_ucblocks = rtl2832_read_ucblocks,
	.read_signal_strength = rtl2832_read_signal_strength,
	.i2c_gate_ctrl = rtl2832_i2c_gate_ctrl,
};

MODULE_AUTHOR("Thomas Mair <mair.thomas86@gmail.com>");
MODULE_DESCRIPTION("Realtek RTL2832 DVB-T demodulator driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.5");
