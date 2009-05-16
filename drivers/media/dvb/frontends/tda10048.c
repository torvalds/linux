/*
    NXP TDA10048HN DVB OFDM demodulator driver

    Copyright (C) 2009 Steven Toth <stoth@kernellabs.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/div64.h>
#include "dvb_frontend.h"
#include "dvb_math.h"
#include "tda10048.h"

#define TDA10048_DEFAULT_FIRMWARE "dvb-fe-tda10048-1.0.fw"
#define TDA10048_DEFAULT_FIRMWARE_SIZE 24878

/* Register name definitions */
#define TDA10048_IDENTITY          0x00
#define TDA10048_VERSION           0x01
#define TDA10048_DSP_CODE_CPT      0x0C
#define TDA10048_DSP_CODE_IN       0x0E
#define TDA10048_IN_CONF1          0x10
#define TDA10048_IN_CONF2          0x11
#define TDA10048_IN_CONF3          0x12
#define TDA10048_OUT_CONF1         0x14
#define TDA10048_OUT_CONF2         0x15
#define TDA10048_OUT_CONF3         0x16
#define TDA10048_AUTO              0x18
#define TDA10048_SYNC_STATUS       0x1A
#define TDA10048_CONF_C4_1         0x1E
#define TDA10048_CONF_C4_2         0x1F
#define TDA10048_CODE_IN_RAM       0x20
#define TDA10048_CHANNEL_INFO_1_R  0x22
#define TDA10048_CHANNEL_INFO_2_R  0x23
#define TDA10048_CHANNEL_INFO1     0x24
#define TDA10048_CHANNEL_INFO2     0x25
#define TDA10048_TIME_ERROR_R      0x26
#define TDA10048_TIME_ERROR        0x27
#define TDA10048_FREQ_ERROR_LSB_R  0x28
#define TDA10048_FREQ_ERROR_MSB_R  0x29
#define TDA10048_FREQ_ERROR_LSB    0x2A
#define TDA10048_FREQ_ERROR_MSB    0x2B
#define TDA10048_IT_SEL            0x30
#define TDA10048_IT_STAT           0x32
#define TDA10048_DSP_AD_LSB        0x3C
#define TDA10048_DSP_AD_MSB        0x3D
#define TDA10048_DSP_REF_LSB       0x3E
#define TDA10048_DSP_REF_MSB       0x3F
#define TDA10048_CONF_TRISTATE1    0x44
#define TDA10048_CONF_TRISTATE2    0x45
#define TDA10048_CONF_POLARITY     0x46
#define TDA10048_GPIO_SP_DS0       0x48
#define TDA10048_GPIO_SP_DS1       0x49
#define TDA10048_GPIO_SP_DS2       0x4A
#define TDA10048_GPIO_SP_DS3       0x4B
#define TDA10048_GPIO_OUT_SEL      0x4C
#define TDA10048_GPIO_SELECT       0x4D
#define TDA10048_IC_MODE           0x4E
#define TDA10048_CONF_XO           0x50
#define TDA10048_CONF_PLL1         0x51
#define TDA10048_CONF_PLL2         0x52
#define TDA10048_CONF_PLL3         0x53
#define TDA10048_CONF_ADC          0x54
#define TDA10048_CONF_ADC_2        0x55
#define TDA10048_CONF_C1_1         0x60
#define TDA10048_CONF_C1_3         0x62
#define TDA10048_AGC_CONF          0x70
#define TDA10048_AGC_THRESHOLD_LSB 0x72
#define TDA10048_AGC_THRESHOLD_MSB 0x73
#define TDA10048_AGC_RENORM        0x74
#define TDA10048_AGC_GAINS         0x76
#define TDA10048_AGC_TUN_MIN       0x78
#define TDA10048_AGC_TUN_MAX       0x79
#define TDA10048_AGC_IF_MIN        0x7A
#define TDA10048_AGC_IF_MAX        0x7B
#define TDA10048_AGC_TUN_LEVEL     0x7E
#define TDA10048_AGC_IF_LEVEL      0x7F
#define TDA10048_DIG_AGC_LEVEL     0x81
#define TDA10048_FREQ_PHY2_LSB     0x86
#define TDA10048_FREQ_PHY2_MSB     0x87
#define TDA10048_TIME_INVWREF_LSB  0x88
#define TDA10048_TIME_INVWREF_MSB  0x89
#define TDA10048_TIME_WREF_LSB     0x8A
#define TDA10048_TIME_WREF_MID1    0x8B
#define TDA10048_TIME_WREF_MID2    0x8C
#define TDA10048_TIME_WREF_MSB     0x8D
#define TDA10048_NP_OUT            0xA2
#define TDA10048_CELL_ID_LSB       0xA4
#define TDA10048_CELL_ID_MSB       0xA5
#define TDA10048_EXTTPS_ODD        0xAA
#define TDA10048_EXTTPS_EVEN       0xAB
#define TDA10048_TPS_LENGTH        0xAC
#define TDA10048_FREE_REG_1        0xB2
#define TDA10048_FREE_REG_2        0xB3
#define TDA10048_CONF_C3_1         0xC0
#define TDA10048_CYBER_CTRL        0xC2
#define TDA10048_CBER_NMAX_LSB     0xC4
#define TDA10048_CBER_NMAX_MSB     0xC5
#define TDA10048_CBER_LSB          0xC6
#define TDA10048_CBER_MSB          0xC7
#define TDA10048_VBER_LSB          0xC8
#define TDA10048_VBER_MID          0xC9
#define TDA10048_VBER_MSB          0xCA
#define TDA10048_CYBER_LUT         0xCC
#define TDA10048_UNCOR_CTRL        0xCD
#define TDA10048_UNCOR_CPT_LSB     0xCE
#define TDA10048_UNCOR_CPT_MSB     0xCF
#define TDA10048_SOFT_IT_C3        0xD6
#define TDA10048_CONF_TS2          0xE0
#define TDA10048_CONF_TS1          0xE1

static unsigned int debug;

#define dprintk(level, fmt, arg...)\
	do { if (debug >= level)\
		printk(KERN_DEBUG "tda10048: " fmt, ## arg);\
	} while (0)

struct tda10048_state {

	struct i2c_adapter *i2c;

	/* We'll cache and update the attach config settings */
	struct tda10048_config config;
	struct dvb_frontend frontend;

	int fwloaded;

	u32 freq_if_hz;
	u32 xtal_hz;
	u32 pll_mfactor;
	u32 pll_nfactor;
	u32 pll_pfactor;
	u32 sample_freq;

	enum fe_bandwidth bandwidth;
};

static struct init_tab {
	u8	reg;
	u16	data;
} init_tab[] = {
	{ TDA10048_CONF_PLL1, 0x08 },
	{ TDA10048_CONF_ADC_2, 0x00 },
	{ TDA10048_CONF_C4_1, 0x00 },
	{ TDA10048_CONF_PLL1, 0x0f },
	{ TDA10048_CONF_PLL2, 0x0a },
	{ TDA10048_CONF_PLL3, 0x43 },
	{ TDA10048_FREQ_PHY2_LSB, 0x02 },
	{ TDA10048_FREQ_PHY2_MSB, 0x0a },
	{ TDA10048_TIME_WREF_LSB, 0xbd },
	{ TDA10048_TIME_WREF_MID1, 0xe4 },
	{ TDA10048_TIME_WREF_MID2, 0xa8 },
	{ TDA10048_TIME_WREF_MSB, 0x02 },
	{ TDA10048_TIME_INVWREF_LSB, 0x04 },
	{ TDA10048_TIME_INVWREF_MSB, 0x06 },
	{ TDA10048_CONF_C4_1, 0x00 },
	{ TDA10048_CONF_C1_1, 0xa8 },
	{ TDA10048_AGC_CONF, 0x16 },
	{ TDA10048_CONF_C1_3, 0x0b },
	{ TDA10048_AGC_TUN_MIN, 0x00 },
	{ TDA10048_AGC_TUN_MAX, 0xff },
	{ TDA10048_AGC_IF_MIN, 0x00 },
	{ TDA10048_AGC_IF_MAX, 0xff },
	{ TDA10048_AGC_THRESHOLD_MSB, 0x00 },
	{ TDA10048_AGC_THRESHOLD_LSB, 0x70 },
	{ TDA10048_CYBER_CTRL, 0x38 },
	{ TDA10048_AGC_GAINS, 0x12 },
	{ TDA10048_CONF_XO, 0x00 },
	{ TDA10048_CONF_TS1, 0x07 },
	{ TDA10048_IC_MODE, 0x00 },
	{ TDA10048_CONF_TS2, 0xc0 },
	{ TDA10048_CONF_TRISTATE1, 0x21 },
	{ TDA10048_CONF_TRISTATE2, 0x00 },
	{ TDA10048_CONF_POLARITY, 0x00 },
	{ TDA10048_CONF_C4_2, 0x04 },
	{ TDA10048_CONF_ADC, 0x60 },
	{ TDA10048_CONF_ADC_2, 0x10 },
	{ TDA10048_CONF_ADC, 0x60 },
	{ TDA10048_CONF_ADC_2, 0x00 },
	{ TDA10048_CONF_C1_1, 0xa8 },
	{ TDA10048_UNCOR_CTRL, 0x00 },
	{ TDA10048_CONF_C4_2, 0x04 },
};

static struct pll_tab {
	u32	clk_freq_khz;
	u32	if_freq_khz;
	u8	m, n, p;
} pll_tab[] = {
	{ TDA10048_CLK_4000,  TDA10048_IF_36130, 10, 0, 0 },
	{ TDA10048_CLK_16000, TDA10048_IF_3300,  10, 3, 0 },
	{ TDA10048_CLK_16000, TDA10048_IF_3500,  10, 3, 0 },
	{ TDA10048_CLK_16000, TDA10048_IF_4000,  10, 3, 0 },
	{ TDA10048_CLK_16000, TDA10048_IF_4300,  10, 3, 0 },
	{ TDA10048_CLK_16000, TDA10048_IF_36130, 10, 3, 0 },
};

static int tda10048_writereg(struct tda10048_state *state, u8 reg, u8 data)
{
	struct tda10048_config *config = &state->config;
	int ret;
	u8 buf[] = { reg, data };
	struct i2c_msg msg = {
		.addr = config->demod_address,
		.flags = 0, .buf = buf, .len = 2 };

	dprintk(2, "%s(reg = 0x%02x, data = 0x%02x)\n", __func__, reg, data);

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1)
		printk("%s: writereg error (ret == %i)\n", __func__, ret);

	return (ret != 1) ? -1 : 0;
}

static u8 tda10048_readreg(struct tda10048_state *state, u8 reg)
{
	struct tda10048_config *config = &state->config;
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{ .addr = config->demod_address,
			.flags = 0, .buf = b0, .len = 1 },
		{ .addr = config->demod_address,
			.flags = I2C_M_RD, .buf = b1, .len = 1 } };

	dprintk(2, "%s(reg = 0x%02x)\n", __func__, reg);

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2)
		printk(KERN_ERR "%s: readreg error (ret == %i)\n",
			__func__, ret);

	return b1[0];
}

static int tda10048_writeregbulk(struct tda10048_state *state, u8 reg,
				 const u8 *data, u16 len)
{
	struct tda10048_config *config = &state->config;
	int ret = -EREMOTEIO;
	struct i2c_msg msg;
	u8 *buf;

	dprintk(2, "%s(%d, ?, len = %d)\n", __func__, reg, len);

	buf = kmalloc(len + 1, GFP_KERNEL);
	if (buf == NULL) {
		ret = -ENOMEM;
		goto error;
	}

	*buf = reg;
	memcpy(buf + 1, data, len);

	msg.addr = config->demod_address;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = len + 1;

	dprintk(2, "%s():  write len = %d\n",
		__func__, msg.len);

	ret = i2c_transfer(state->i2c, &msg, 1);
	if (ret != 1) {
		printk(KERN_ERR "%s(): writereg error err %i\n",
			 __func__, ret);
		ret = -EREMOTEIO;
	}

error:
	kfree(buf);

	return ret;
}

static int tda10048_set_phy2(struct dvb_frontend *fe, u32 sample_freq_hz,
			     u32 if_hz)
{
	struct tda10048_state *state = fe->demodulator_priv;
	u64 t;

	dprintk(1, "%s()\n", __func__);

	if (sample_freq_hz == 0)
		return -EINVAL;

	if (if_hz < (sample_freq_hz / 2)) {
		/* PHY2 = (if2/fs) * 2^15 */
		t = if_hz;
		t *= 10;
		t *= 32768;
		do_div(t, sample_freq_hz);
		t += 5;
		do_div(t, 10);
	} else {
		/* PHY2 = ((IF1-fs)/fs) * 2^15 */
		t = sample_freq_hz - if_hz;
		t *= 10;
		t *= 32768;
		do_div(t, sample_freq_hz);
		t += 5;
		do_div(t, 10);
		t = ~t + 1;
	}

	tda10048_writereg(state, TDA10048_FREQ_PHY2_LSB, (u8)t);
	tda10048_writereg(state, TDA10048_FREQ_PHY2_MSB, (u8)(t >> 8));

	return 0;
}

static int tda10048_set_wref(struct dvb_frontend *fe, u32 sample_freq_hz,
			     u32 bw)
{
	struct tda10048_state *state = fe->demodulator_priv;
	u64 t, z;
	u32 b = 8000000;

	dprintk(1, "%s()\n", __func__);

	if (sample_freq_hz == 0)
		return -EINVAL;

	if (bw == BANDWIDTH_6_MHZ)
		b = 6000000;
	else
	if (bw == BANDWIDTH_7_MHZ)
		b = 7000000;

	/* WREF = (B / (7 * fs)) * 2^31 */
	t = b * 10;
	/* avoid warning: this decimal constant is unsigned only in ISO C90 */
	/* t *= 2147483648 on 32bit platforms */
	t *= (2048 * 1024);
	t *= 1024;
	z = 7 * sample_freq_hz;
	do_div(t, z);
	t += 5;
	do_div(t, 10);

	tda10048_writereg(state, TDA10048_TIME_WREF_LSB, (u8)t);
	tda10048_writereg(state, TDA10048_TIME_WREF_MID1, (u8)(t >> 8));
	tda10048_writereg(state, TDA10048_TIME_WREF_MID2, (u8)(t >> 16));
	tda10048_writereg(state, TDA10048_TIME_WREF_MSB, (u8)(t >> 24));

	return 0;
}

static int tda10048_set_invwref(struct dvb_frontend *fe, u32 sample_freq_hz,
				u32 bw)
{
	struct tda10048_state *state = fe->demodulator_priv;
	u64 t;
	u32 b = 8000000;

	dprintk(1, "%s()\n", __func__);

	if (sample_freq_hz == 0)
		return -EINVAL;

	if (bw == BANDWIDTH_6_MHZ)
		b = 6000000;
	else
	if (bw == BANDWIDTH_7_MHZ)
		b = 7000000;

	/* INVWREF = ((7 * fs) / B) * 2^5 */
	t = sample_freq_hz;
	t *= 7;
	t *= 32;
	t *= 10;
	do_div(t, b);
	t += 5;
	do_div(t, 10);

	tda10048_writereg(state, TDA10048_TIME_INVWREF_LSB, (u8)t);
	tda10048_writereg(state, TDA10048_TIME_INVWREF_MSB, (u8)(t >> 8));

	return 0;
}

static int tda10048_set_bandwidth(struct dvb_frontend *fe,
	enum fe_bandwidth bw)
{
	struct tda10048_state *state = fe->demodulator_priv;
	dprintk(1, "%s(bw=%d)\n", __func__, bw);

	/* Bandwidth setting may need to be adjusted */
	switch (bw) {
	case BANDWIDTH_6_MHZ:
	case BANDWIDTH_7_MHZ:
	case BANDWIDTH_8_MHZ:
		tda10048_set_wref(fe, state->sample_freq, bw);
		tda10048_set_invwref(fe, state->sample_freq, bw);
		break;
	default:
		printk(KERN_ERR "%s() invalid bandwidth\n", __func__);
		return -EINVAL;
	}

	state->bandwidth = bw;

	return 0;
}

static int tda10048_set_if(struct dvb_frontend *fe, enum fe_bandwidth bw)
{
	struct tda10048_state *state = fe->demodulator_priv;
	struct tda10048_config *config = &state->config;
	int i;
	u32 if_freq_khz;

	dprintk(1, "%s(bw = %d)\n", __func__, bw);

	/* based on target bandwidth and clk we calculate pll factors */
	switch (bw) {
	case BANDWIDTH_6_MHZ:
		if_freq_khz = config->dtv6_if_freq_khz;
		break;
	case BANDWIDTH_7_MHZ:
		if_freq_khz = config->dtv7_if_freq_khz;
		break;
	case BANDWIDTH_8_MHZ:
		if_freq_khz = config->dtv8_if_freq_khz;
		break;
	default:
		printk(KERN_ERR "%s() no default\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(pll_tab); i++) {
		if ((pll_tab[i].clk_freq_khz == config->clk_freq_khz) &&
			(pll_tab[i].if_freq_khz == if_freq_khz)) {

			state->freq_if_hz = pll_tab[i].if_freq_khz * 1000;
			state->xtal_hz = pll_tab[i].clk_freq_khz * 1000;
			state->pll_mfactor = pll_tab[i].m;
			state->pll_nfactor = pll_tab[i].n;
			state->pll_pfactor = pll_tab[i].p;
			break;
		}
	}
	if (i == ARRAY_SIZE(pll_tab)) {
		printk(KERN_ERR "%s() Incorrect attach settings\n",
			__func__);
		return -EINVAL;
	}

	dprintk(1, "- freq_if_hz = %d\n", state->freq_if_hz);
	dprintk(1, "- xtal_hz = %d\n", state->xtal_hz);
	dprintk(1, "- pll_mfactor = %d\n", state->pll_mfactor);
	dprintk(1, "- pll_nfactor = %d\n", state->pll_nfactor);
	dprintk(1, "- pll_pfactor = %d\n", state->pll_pfactor);

	/* Calculate the sample frequency */
	state->sample_freq = state->xtal_hz * (state->pll_mfactor + 45);
	state->sample_freq /= (state->pll_nfactor + 1);
	state->sample_freq /= (state->pll_pfactor + 4);
	dprintk(1, "- sample_freq = %d\n", state->sample_freq);

	/* Update the I/F */
	tda10048_set_phy2(fe, state->sample_freq, state->freq_if_hz);

	return 0;
}

static int tda10048_firmware_upload(struct dvb_frontend *fe)
{
	struct tda10048_state *state = fe->demodulator_priv;
	struct tda10048_config *config = &state->config;
	const struct firmware *fw;
	int ret;
	int pos = 0;
	int cnt;
	u8 wlen = config->fwbulkwritelen;

	if ((wlen != TDA10048_BULKWRITE_200) && (wlen != TDA10048_BULKWRITE_50))
		wlen = TDA10048_BULKWRITE_200;

	/* request the firmware, this will block and timeout */
	printk(KERN_INFO "%s: waiting for firmware upload (%s)...\n",
		__func__,
		TDA10048_DEFAULT_FIRMWARE);

	ret = request_firmware(&fw, TDA10048_DEFAULT_FIRMWARE,
		state->i2c->dev.parent);
	if (ret) {
		printk(KERN_ERR "%s: Upload failed. (file not found?)\n",
			__func__);
		return -EIO;
	} else {
		printk(KERN_INFO "%s: firmware read %Zu bytes.\n",
			__func__,
			fw->size);
		ret = 0;
	}

	if (fw->size != TDA10048_DEFAULT_FIRMWARE_SIZE) {
		printk(KERN_ERR "%s: firmware incorrect size\n", __func__);
		ret = -EIO;
	} else {
		printk(KERN_INFO "%s: firmware uploading\n", __func__);

		/* Soft reset */
		tda10048_writereg(state, TDA10048_CONF_TRISTATE1,
			tda10048_readreg(state, TDA10048_CONF_TRISTATE1)
				& 0xfe);
		tda10048_writereg(state, TDA10048_CONF_TRISTATE1,
			tda10048_readreg(state, TDA10048_CONF_TRISTATE1)
				| 0x01);

		/* Put the demod into host download mode */
		tda10048_writereg(state, TDA10048_CONF_C4_1,
			tda10048_readreg(state, TDA10048_CONF_C4_1) & 0xf9);

		/* Boot the DSP */
		tda10048_writereg(state, TDA10048_CONF_C4_1,
			tda10048_readreg(state, TDA10048_CONF_C4_1) | 0x08);

		/* Prepare for download */
		tda10048_writereg(state, TDA10048_DSP_CODE_CPT, 0);

		/* Download the firmware payload */
		while (pos < fw->size) {

			if ((fw->size - pos) > wlen)
				cnt = wlen;
			else
				cnt = fw->size - pos;

			tda10048_writeregbulk(state, TDA10048_DSP_CODE_IN,
				&fw->data[pos], cnt);

			pos += cnt;
		}

		ret = -EIO;
		/* Wait up to 250ms for the DSP to boot */
		for (cnt = 0; cnt < 250 ; cnt += 10) {

			msleep(10);

			if (tda10048_readreg(state, TDA10048_SYNC_STATUS)
				& 0x40) {
				ret = 0;
				break;
			}
		}
	}

	release_firmware(fw);

	if (ret == 0) {
		printk(KERN_INFO "%s: firmware uploaded\n", __func__);
		state->fwloaded = 1;
	} else
		printk(KERN_ERR "%s: firmware upload failed\n", __func__);

	return ret;
}

static int tda10048_set_inversion(struct dvb_frontend *fe, int inversion)
{
	struct tda10048_state *state = fe->demodulator_priv;

	dprintk(1, "%s(%d)\n", __func__, inversion);

	if (inversion == TDA10048_INVERSION_ON)
		tda10048_writereg(state, TDA10048_CONF_C1_1,
			tda10048_readreg(state, TDA10048_CONF_C1_1) | 0x20);
	else
		tda10048_writereg(state, TDA10048_CONF_C1_1,
			tda10048_readreg(state, TDA10048_CONF_C1_1) & 0xdf);

	return 0;
}

/* Retrieve the demod settings */
static int tda10048_get_tps(struct tda10048_state *state,
	struct dvb_ofdm_parameters *p)
{
	u8 val;

	/* Make sure the TPS regs are valid */
	if (!(tda10048_readreg(state, TDA10048_AUTO) & 0x01))
		return -EAGAIN;

	val = tda10048_readreg(state, TDA10048_OUT_CONF2);
	switch ((val & 0x60) >> 5) {
	case 0:
		p->constellation = QPSK;
		break;
	case 1:
		p->constellation = QAM_16;
		break;
	case 2:
		p->constellation = QAM_64;
		break;
	}
	switch ((val & 0x18) >> 3) {
	case 0:
		p->hierarchy_information = HIERARCHY_NONE;
		break;
	case 1:
		p->hierarchy_information = HIERARCHY_1;
		break;
	case 2:
		p->hierarchy_information = HIERARCHY_2;
		break;
	case 3:
		p->hierarchy_information = HIERARCHY_4;
		break;
	}
	switch (val & 0x07) {
	case 0:
		p->code_rate_HP = FEC_1_2;
		break;
	case 1:
		p->code_rate_HP = FEC_2_3;
		break;
	case 2:
		p->code_rate_HP = FEC_3_4;
		break;
	case 3:
		p->code_rate_HP = FEC_5_6;
		break;
	case 4:
		p->code_rate_HP = FEC_7_8;
		break;
	}

	val = tda10048_readreg(state, TDA10048_OUT_CONF3);
	switch (val & 0x07) {
	case 0:
		p->code_rate_LP = FEC_1_2;
		break;
	case 1:
		p->code_rate_LP = FEC_2_3;
		break;
	case 2:
		p->code_rate_LP = FEC_3_4;
		break;
	case 3:
		p->code_rate_LP = FEC_5_6;
		break;
	case 4:
		p->code_rate_LP = FEC_7_8;
		break;
	}

	val = tda10048_readreg(state, TDA10048_OUT_CONF1);
	switch ((val & 0x0c) >> 2) {
	case 0:
		p->guard_interval = GUARD_INTERVAL_1_32;
		break;
	case 1:
		p->guard_interval = GUARD_INTERVAL_1_16;
		break;
	case 2:
		p->guard_interval =  GUARD_INTERVAL_1_8;
		break;
	case 3:
		p->guard_interval =  GUARD_INTERVAL_1_4;
		break;
	}
	switch (val & 0x02) {
	case 0:
		p->transmission_mode = TRANSMISSION_MODE_2K;
		break;
	case 1:
		p->transmission_mode = TRANSMISSION_MODE_8K;
		break;
	}

	return 0;
}

static int tda10048_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct tda10048_state *state = fe->demodulator_priv;
	struct tda10048_config *config = &state->config;
	dprintk(1, "%s(%d)\n", __func__, enable);

	if (config->disable_gate_access)
		return 0;

	if (enable)
		return tda10048_writereg(state, TDA10048_CONF_C4_1,
			tda10048_readreg(state, TDA10048_CONF_C4_1) | 0x02);
	else
		return tda10048_writereg(state, TDA10048_CONF_C4_1,
			tda10048_readreg(state, TDA10048_CONF_C4_1) & 0xfd);
}

static int tda10048_output_mode(struct dvb_frontend *fe, int serial)
{
	struct tda10048_state *state = fe->demodulator_priv;
	dprintk(1, "%s(%d)\n", __func__, serial);

	/* Ensure pins are out of tri-state */
	tda10048_writereg(state, TDA10048_CONF_TRISTATE1, 0x21);
	tda10048_writereg(state, TDA10048_CONF_TRISTATE2, 0x00);

	if (serial) {
		tda10048_writereg(state, TDA10048_IC_MODE, 0x80 | 0x20);
		tda10048_writereg(state, TDA10048_CONF_TS2, 0xc0);
	} else {
		tda10048_writereg(state, TDA10048_IC_MODE, 0x00);
		tda10048_writereg(state, TDA10048_CONF_TS2, 0x01);
	}

	return 0;
}

/* Talk to the demod, set the FEC, GUARD, QAM settings etc */
/* TODO: Support manual tuning with specific params */
static int tda10048_set_frontend(struct dvb_frontend *fe,
	struct dvb_frontend_parameters *p)
{
	struct tda10048_state *state = fe->demodulator_priv;

	dprintk(1, "%s(frequency=%d)\n", __func__, p->frequency);

	/* Update the I/F pll's if the bandwidth changes */
	if (p->u.ofdm.bandwidth != state->bandwidth) {
		tda10048_set_if(fe, p->u.ofdm.bandwidth);
		tda10048_set_bandwidth(fe, p->u.ofdm.bandwidth);
	}

	if (fe->ops.tuner_ops.set_params) {

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);

		fe->ops.tuner_ops.set_params(fe, p);

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	/* Enable demod TPS auto detection and begin acquisition */
	tda10048_writereg(state, TDA10048_AUTO, 0x57);

	return 0;
}

/* Establish sane defaults and load firmware. */
static int tda10048_init(struct dvb_frontend *fe)
{
	struct tda10048_state *state = fe->demodulator_priv;
	struct tda10048_config *config = &state->config;
	int ret = 0, i;

	dprintk(1, "%s()\n", __func__);

	/* Apply register defaults */
	for (i = 0; i < ARRAY_SIZE(init_tab); i++)
		tda10048_writereg(state, init_tab[i].reg, init_tab[i].data);

	if (state->fwloaded == 0)
		ret = tda10048_firmware_upload(fe);

	/* Set either serial or parallel */
	tda10048_output_mode(fe, config->output_mode);

	/* Set inversion */
	tda10048_set_inversion(fe, config->inversion);

	/* Establish default RF values */
	tda10048_set_if(fe, BANDWIDTH_8_MHZ);
	tda10048_set_bandwidth(fe, BANDWIDTH_8_MHZ);

	/* Ensure we leave the gate closed */
	tda10048_i2c_gate_ctrl(fe, 0);

	return ret;
}

static int tda10048_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct tda10048_state *state = fe->demodulator_priv;
	u8 reg;

	*status = 0;

	reg = tda10048_readreg(state, TDA10048_SYNC_STATUS);

	dprintk(1, "%s() status =0x%02x\n", __func__, reg);

	if (reg & 0x02)
		*status |= FE_HAS_CARRIER;

	if (reg & 0x04)
		*status |= FE_HAS_SIGNAL;

	if (reg & 0x08) {
		*status |= FE_HAS_LOCK;
		*status |= FE_HAS_VITERBI;
		*status |= FE_HAS_SYNC;
	}

	return 0;
}

static int tda10048_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct tda10048_state *state = fe->demodulator_priv;

	dprintk(1, "%s()\n", __func__);

	/* TODO: A reset may be required here */
	*ber = tda10048_readreg(state, TDA10048_CBER_MSB) << 8 |
		tda10048_readreg(state, TDA10048_CBER_LSB);

	return 0;
}

static int tda10048_read_signal_strength(struct dvb_frontend *fe,
	u16 *signal_strength)
{
	struct tda10048_state *state = fe->demodulator_priv;
	u8 v;

	dprintk(1, "%s()\n", __func__);

	*signal_strength = 65535;

	v = tda10048_readreg(state, TDA10048_NP_OUT);
	if (v > 0)
		*signal_strength -= (v << 8) | v;

	return 0;
}

/* SNR lookup table */
static struct snr_tab {
	u8 val;
	u8 data;
} snr_tab[] = {
	{   0,   0 },
	{   1, 246 },
	{   2, 215 },
	{   3, 198 },
	{   4, 185 },
	{   5, 176 },
	{   6, 168 },
	{   7, 161 },
	{   8, 155 },
	{   9, 150 },
	{  10, 146 },
	{  11, 141 },
	{  12, 138 },
	{  13, 134 },
	{  14, 131 },
	{  15, 128 },
	{  16, 125 },
	{  17, 122 },
	{  18, 120 },
	{  19, 118 },
	{  20, 115 },
	{  21, 113 },
	{  22, 111 },
	{  23, 109 },
	{  24, 107 },
	{  25, 106 },
	{  26, 104 },
	{  27, 102 },
	{  28, 101 },
	{  29,  99 },
	{  30,  98 },
	{  31,  96 },
	{  32,  95 },
	{  33,  94 },
	{  34,  92 },
	{  35,  91 },
	{  36,  90 },
	{  37,  89 },
	{  38,  88 },
	{  39,  86 },
	{  40,  85 },
	{  41,  84 },
	{  42,  83 },
	{  43,  82 },
	{  44,  81 },
	{  45,  80 },
	{  46,  79 },
	{  47,  78 },
	{  48,  77 },
	{  49,  76 },
	{  50,  76 },
	{  51,  75 },
	{  52,  74 },
	{  53,  73 },
	{  54,  72 },
	{  56,  71 },
	{  57,  70 },
	{  58,  69 },
	{  60,  68 },
	{  61,  67 },
	{  63,  66 },
	{  64,  65 },
	{  66,  64 },
	{  67,  63 },
	{  68,  62 },
	{  69,  62 },
	{  70,  61 },
	{  72,  60 },
	{  74,  59 },
	{  75,  58 },
	{  77,  57 },
	{  79,  56 },
	{  81,  55 },
	{  83,  54 },
	{  85,  53 },
	{  87,  52 },
	{  89,  51 },
	{  91,  50 },
	{  93,  49 },
	{  95,  48 },
	{  97,  47 },
	{ 100,  46 },
	{ 102,  45 },
	{ 104,  44 },
	{ 107,  43 },
	{ 109,  42 },
	{ 112,  41 },
	{ 114,  40 },
	{ 117,  39 },
	{ 120,  38 },
	{ 123,  37 },
	{ 125,  36 },
	{ 128,  35 },
	{ 131,  34 },
	{ 134,  33 },
	{ 138,  32 },
	{ 141,  31 },
	{ 144,  30 },
	{ 147,  29 },
	{ 151,  28 },
	{ 154,  27 },
	{ 158,  26 },
	{ 162,  25 },
	{ 165,  24 },
	{ 169,  23 },
	{ 173,  22 },
	{ 177,  21 },
	{ 181,  20 },
	{ 186,  19 },
	{ 190,  18 },
	{ 194,  17 },
	{ 199,  16 },
	{ 204,  15 },
	{ 208,  14 },
	{ 213,  13 },
	{ 218,  12 },
	{ 223,  11 },
	{ 229,  10 },
	{ 234,   9 },
	{ 239,   8 },
	{ 245,   7 },
	{ 251,   6 },
	{ 255,   5 },
};

static int tda10048_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct tda10048_state *state = fe->demodulator_priv;
	u8 v;
	int i, ret = -EINVAL;

	dprintk(1, "%s()\n", __func__);

	v = tda10048_readreg(state, TDA10048_NP_OUT);
	for (i = 0; i < ARRAY_SIZE(snr_tab); i++) {
		if (v <= snr_tab[i].val) {
			*snr = snr_tab[i].data;
			ret = 0;
			break;
		}
	}

	return ret;
}

static int tda10048_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct tda10048_state *state = fe->demodulator_priv;

	dprintk(1, "%s()\n", __func__);

	*ucblocks = tda10048_readreg(state, TDA10048_UNCOR_CPT_MSB) << 8 |
		tda10048_readreg(state, TDA10048_UNCOR_CPT_LSB);

	return 0;
}

static int tda10048_get_frontend(struct dvb_frontend *fe,
	struct dvb_frontend_parameters *p)
{
	struct tda10048_state *state = fe->demodulator_priv;

	dprintk(1, "%s()\n", __func__);

	p->inversion = tda10048_readreg(state, TDA10048_CONF_C1_1)
		& 0x20 ? INVERSION_ON : INVERSION_OFF;

	return tda10048_get_tps(state, &p->u.ofdm);
}

static int tda10048_get_tune_settings(struct dvb_frontend *fe,
	struct dvb_frontend_tune_settings *tune)
{
	tune->min_delay_ms = 1000;
	return 0;
}

static void tda10048_release(struct dvb_frontend *fe)
{
	struct tda10048_state *state = fe->demodulator_priv;
	dprintk(1, "%s()\n", __func__);
	kfree(state);
}

static void tda10048_establish_defaults(struct dvb_frontend *fe)
{
	struct tda10048_state *state = fe->demodulator_priv;
	struct tda10048_config *config = &state->config;

	/* Validate/default the config */
	if (config->dtv6_if_freq_khz == 0) {
		config->dtv6_if_freq_khz = TDA10048_IF_4300;
		printk(KERN_WARNING "%s() tda10048_config.dtv6_if_freq_khz "
			"is not set (defaulting to %d)\n",
			__func__,
			config->dtv6_if_freq_khz);
	}

	if (config->dtv7_if_freq_khz == 0) {
		config->dtv7_if_freq_khz = TDA10048_IF_4300;
		printk(KERN_WARNING "%s() tda10048_config.dtv7_if_freq_khz "
			"is not set (defaulting to %d)\n",
			__func__,
			config->dtv7_if_freq_khz);
	}

	if (config->dtv8_if_freq_khz == 0) {
		config->dtv8_if_freq_khz = TDA10048_IF_4300;
		printk(KERN_WARNING "%s() tda10048_config.dtv8_if_freq_khz "
			"is not set (defaulting to %d)\n",
			__func__,
			config->dtv8_if_freq_khz);
	}

	if (config->clk_freq_khz == 0) {
		config->clk_freq_khz = TDA10048_CLK_16000;
		printk(KERN_WARNING "%s() tda10048_config.clk_freq_khz "
			"is not set (defaulting to %d)\n",
			__func__,
			config->clk_freq_khz);
	}
}

static struct dvb_frontend_ops tda10048_ops;

struct dvb_frontend *tda10048_attach(const struct tda10048_config *config,
	struct i2c_adapter *i2c)
{
	struct tda10048_state *state = NULL;

	dprintk(1, "%s()\n", __func__);

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct tda10048_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state and clone the config */
	memcpy(&state->config, config, sizeof(*config));
	state->i2c = i2c;
	state->fwloaded = 0;
	state->bandwidth = BANDWIDTH_8_MHZ;

	/* check if the demod is present */
	if (tda10048_readreg(state, TDA10048_IDENTITY) != 0x048)
		goto error;

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &tda10048_ops,
		sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	/* Establish any defaults the the user didn't pass */
	tda10048_establish_defaults(&state->frontend);

	/* Set the xtal and freq defaults */
	if (tda10048_set_if(&state->frontend, BANDWIDTH_8_MHZ) != 0)
		goto error;

	/* Default bandwidth */
	if (tda10048_set_bandwidth(&state->frontend, BANDWIDTH_8_MHZ) != 0)
		goto error;

	/* Leave the gate closed */
	tda10048_i2c_gate_ctrl(&state->frontend, 0);

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(tda10048_attach);

static struct dvb_frontend_ops tda10048_ops = {

	.info = {
		.name			= "NXP TDA10048HN DVB-T",
		.type			= FE_OFDM,
		.frequency_min		= 177000000,
		.frequency_max		= 858000000,
		.frequency_stepsize	= 166666,
		.caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
		FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
		FE_CAN_QPSK | FE_CAN_QAM_16 | FE_CAN_QAM_64 | FE_CAN_QAM_AUTO |
		FE_CAN_HIERARCHY_AUTO | FE_CAN_GUARD_INTERVAL_AUTO |
		FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_RECOVER
	},

	.release = tda10048_release,
	.init = tda10048_init,
	.i2c_gate_ctrl = tda10048_i2c_gate_ctrl,
	.set_frontend = tda10048_set_frontend,
	.get_frontend = tda10048_get_frontend,
	.get_tune_settings = tda10048_get_tune_settings,
	.read_status = tda10048_read_status,
	.read_ber = tda10048_read_ber,
	.read_signal_strength = tda10048_read_signal_strength,
	.read_snr = tda10048_read_snr,
	.read_ucblocks = tda10048_read_ucblocks,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Enable verbose debug messages");

MODULE_DESCRIPTION("NXP TDA10048HN DVB-T Demodulator driver");
MODULE_AUTHOR("Steven Toth");
MODULE_LICENSE("GPL");
