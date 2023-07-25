// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for Silicon Labs Si2161 DVB-T and Si2165 DVB-C/-T Demodulator
 *
 *  Copyright (C) 2013-2017 Matthias Schwarzott <zzam@gentoo.org>
 *
 *  References:
 *  https://www.silabs.com/Support%20Documents/TechnicalDocs/Si2165-short.pdf
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/regmap.h>

#include <media/dvb_frontend.h>
#include <linux/int_log.h>
#include "si2165_priv.h"
#include "si2165.h"

/*
 * Hauppauge WinTV-HVR-930C-HD B130 / PCTV QuatroStick 521e 1113xx
 * uses 16 MHz xtal
 *
 * Hauppauge WinTV-HVR-930C-HD B131 / PCTV QuatroStick 522e 1114xx
 * uses 24 MHz clock provided by tuner
 */

struct si2165_state {
	struct i2c_client *client;

	struct regmap *regmap;

	struct dvb_frontend fe;

	struct si2165_config config;

	u8 chip_revcode;
	u8 chip_type;

	/* calculated by xtal and div settings */
	u32 fvco_hz;
	u32 sys_clk;
	u32 adc_clk;

	/* DVBv3 stats */
	u64 ber_prev;

	bool has_dvbc;
	bool has_dvbt;
	bool firmware_loaded;
};

static int si2165_write(struct si2165_state *state, const u16 reg,
			const u8 *src, const int count)
{
	int ret;

	dev_dbg(&state->client->dev, "i2c write: reg: 0x%04x, data: %*ph\n",
		reg, count, src);

	ret = regmap_bulk_write(state->regmap, reg, src, count);

	if (ret)
		dev_err(&state->client->dev, "%s: ret == %d\n", __func__, ret);

	return ret;
}

static int si2165_read(struct si2165_state *state,
		       const u16 reg, u8 *val, const int count)
{
	int ret = regmap_bulk_read(state->regmap, reg, val, count);

	if (ret) {
		dev_err(&state->client->dev, "%s: error (addr %02x reg %04x error (ret == %i)\n",
			__func__, state->config.i2c_addr, reg, ret);
		return ret;
	}

	dev_dbg(&state->client->dev, "i2c read: reg: 0x%04x, data: %*ph\n",
		reg, count, val);

	return 0;
}

static int si2165_readreg8(struct si2165_state *state,
			   const u16 reg, u8 *val)
{
	unsigned int val_tmp;
	int ret = regmap_read(state->regmap, reg, &val_tmp);
	*val = (u8)val_tmp;
	dev_dbg(&state->client->dev, "reg read: R(0x%04x)=0x%02x\n", reg, *val);
	return ret;
}

static int si2165_readreg16(struct si2165_state *state,
			    const u16 reg, u16 *val)
{
	u8 buf[2];

	int ret = si2165_read(state, reg, buf, 2);
	*val = buf[0] | buf[1] << 8;
	dev_dbg(&state->client->dev, "reg read: R(0x%04x)=0x%04x\n", reg, *val);
	return ret;
}

static int si2165_readreg24(struct si2165_state *state,
			    const u16 reg, u32 *val)
{
	u8 buf[3];

	int ret = si2165_read(state, reg, buf, 3);
	*val = buf[0] | buf[1] << 8 | buf[2] << 16;
	dev_dbg(&state->client->dev, "reg read: R(0x%04x)=0x%06x\n", reg, *val);
	return ret;
}

static int si2165_writereg8(struct si2165_state *state, const u16 reg, u8 val)
{
	return regmap_write(state->regmap, reg, val);
}

static int si2165_writereg16(struct si2165_state *state, const u16 reg, u16 val)
{
	u8 buf[2] = { val & 0xff, (val >> 8) & 0xff };

	return si2165_write(state, reg, buf, 2);
}

static int si2165_writereg24(struct si2165_state *state, const u16 reg, u32 val)
{
	u8 buf[3] = { val & 0xff, (val >> 8) & 0xff, (val >> 16) & 0xff };

	return si2165_write(state, reg, buf, 3);
}

static int si2165_writereg32(struct si2165_state *state, const u16 reg, u32 val)
{
	u8 buf[4] = {
		val & 0xff,
		(val >> 8) & 0xff,
		(val >> 16) & 0xff,
		(val >> 24) & 0xff
	};
	return si2165_write(state, reg, buf, 4);
}

static int si2165_writereg_mask8(struct si2165_state *state, const u16 reg,
				 u8 val, u8 mask)
{
	if (mask != 0xff) {
		u8 tmp;
		int ret = si2165_readreg8(state, reg, &tmp);

		if (ret < 0)
			return ret;

		val &= mask;
		tmp &= ~mask;
		val |= tmp;
	}
	return si2165_writereg8(state, reg, val);
}

#define REG16(reg, val) \
	{ (reg), (val) & 0xff }, \
	{ (reg) + 1, (val) >> 8 & 0xff }
struct si2165_reg_value_pair {
	u16 reg;
	u8 val;
};

static int si2165_write_reg_list(struct si2165_state *state,
				 const struct si2165_reg_value_pair *regs,
				 int count)
{
	int i;
	int ret;

	for (i = 0; i < count; i++) {
		ret = si2165_writereg8(state, regs[i].reg, regs[i].val);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int si2165_get_tune_settings(struct dvb_frontend *fe,
				    struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 1000;
	return 0;
}

static int si2165_init_pll(struct si2165_state *state)
{
	u32 ref_freq_hz = state->config.ref_freq_hz;
	u8 divr = 1; /* 1..7 */
	u8 divp = 1; /* only 1 or 4 */
	u8 divn = 56; /* 1..63 */
	u8 divm = 8;
	u8 divl = 12;
	u8 buf[4];

	/*
	 * hardcoded values can be deleted if calculation is verified
	 * or it yields the same values as the windows driver
	 */
	switch (ref_freq_hz) {
	case 16000000u:
		divn = 56;
		break;
	case 24000000u:
		divr = 2;
		divp = 4;
		divn = 19;
		break;
	default:
		/* ref_freq / divr must be between 4 and 16 MHz */
		if (ref_freq_hz > 16000000u)
			divr = 2;

		/*
		 * now select divn and divp such that
		 * fvco is in 1624..1824 MHz
		 */
		if (1624000000u * divr > ref_freq_hz * 2u * 63u)
			divp = 4;

		/* is this already correct regarding rounding? */
		divn = 1624000000u * divr / (ref_freq_hz * 2u * divp);
		break;
	}

	/* adc_clk and sys_clk depend on xtal and pll settings */
	state->fvco_hz = ref_freq_hz / divr
			* 2u * divn * divp;
	state->adc_clk = state->fvco_hz / (divm * 4u);
	state->sys_clk = state->fvco_hz / (divl * 2u);

	/* write all 4 pll registers 0x00a0..0x00a3 at once */
	buf[0] = divl;
	buf[1] = divm;
	buf[2] = (divn & 0x3f) | ((divp == 1) ? 0x40 : 0x00) | 0x80;
	buf[3] = divr;
	return si2165_write(state, REG_PLL_DIVL, buf, 4);
}

static int si2165_adjust_pll_divl(struct si2165_state *state, u8 divl)
{
	state->sys_clk = state->fvco_hz / (divl * 2u);
	return si2165_writereg8(state, REG_PLL_DIVL, divl);
}

static u32 si2165_get_fe_clk(struct si2165_state *state)
{
	/* assume Oversampling mode Ovr4 is used */
	return state->adc_clk;
}

static int si2165_wait_init_done(struct si2165_state *state)
{
	int ret;
	u8 val = 0;
	int i;

	for (i = 0; i < 3; ++i) {
		ret = si2165_readreg8(state, REG_INIT_DONE, &val);
		if (ret < 0)
			return ret;
		if (val == 0x01)
			return 0;
		usleep_range(1000, 50000);
	}
	dev_err(&state->client->dev, "init_done was not set\n");
	return -EINVAL;
}

static int si2165_upload_firmware_block(struct si2165_state *state,
					const u8 *data, u32 len, u32 *poffset,
					u32 block_count)
{
	int ret;
	u8 buf_ctrl[4] = { 0x00, 0x00, 0x00, 0xc0 };
	u8 wordcount;
	u32 cur_block = 0;
	u32 offset = poffset ? *poffset : 0;

	if (len < 4)
		return -EINVAL;
	if (len % 4 != 0)
		return -EINVAL;

	dev_dbg(&state->client->dev,
		"fw load: %s: called with len=0x%x offset=0x%x blockcount=0x%x\n",
		__func__, len, offset, block_count);
	while (offset + 12 <= len && cur_block < block_count) {
		dev_dbg(&state->client->dev,
			"fw load: %s: in while len=0x%x offset=0x%x cur_block=0x%x blockcount=0x%x\n",
			__func__, len, offset, cur_block, block_count);
		wordcount = data[offset];
		if (wordcount < 1 || data[offset + 1] ||
		    data[offset + 2] || data[offset + 3]) {
			dev_warn(&state->client->dev,
				 "bad fw data[0..3] = %*ph\n",
				 4, data);
			return -EINVAL;
		}

		if (offset + 8 + wordcount * 4 > len) {
			dev_warn(&state->client->dev,
				 "len is too small for block len=%d, wordcount=%d\n",
				len, wordcount);
			return -EINVAL;
		}

		buf_ctrl[0] = wordcount - 1;

		ret = si2165_write(state, REG_DCOM_CONTROL_BYTE, buf_ctrl, 4);
		if (ret < 0)
			goto error;
		ret = si2165_write(state, REG_DCOM_ADDR, data + offset + 4, 4);
		if (ret < 0)
			goto error;

		offset += 8;

		while (wordcount > 0) {
			ret = si2165_write(state, REG_DCOM_DATA,
					   data + offset, 4);
			if (ret < 0)
				goto error;
			wordcount--;
			offset += 4;
		}
		cur_block++;
	}

	dev_dbg(&state->client->dev,
		"fw load: %s: after while len=0x%x offset=0x%x cur_block=0x%x blockcount=0x%x\n",
		__func__, len, offset, cur_block, block_count);

	if (poffset)
		*poffset = offset;

	dev_dbg(&state->client->dev,
		"fw load: %s: returned offset=0x%x\n",
		__func__, offset);

	return 0;
error:
	return ret;
}

static int si2165_upload_firmware(struct si2165_state *state)
{
	/* int ret; */
	u8 val[3];
	u16 val16;
	int ret;

	const struct firmware *fw = NULL;
	u8 *fw_file;
	const u8 *data;
	u32 len;
	u32 offset;
	u8 patch_version;
	u8 block_count;
	u16 crc_expected;

	switch (state->chip_revcode) {
	case 0x03: /* revision D */
		fw_file = SI2165_FIRMWARE_REV_D;
		break;
	default:
		dev_info(&state->client->dev, "no firmware file for revision=%d\n",
			 state->chip_revcode);
		return 0;
	}

	/* request the firmware, this will block and timeout */
	ret = request_firmware(&fw, fw_file, &state->client->dev);
	if (ret) {
		dev_warn(&state->client->dev, "firmware file '%s' not found\n",
			 fw_file);
		goto error;
	}

	data = fw->data;
	len = fw->size;

	dev_info(&state->client->dev, "downloading firmware from file '%s' size=%d\n",
		 fw_file, len);

	if (len % 4 != 0) {
		dev_warn(&state->client->dev, "firmware size is not multiple of 4\n");
		ret = -EINVAL;
		goto error;
	}

	/* check header (8 bytes) */
	if (len < 8) {
		dev_warn(&state->client->dev, "firmware header is missing\n");
		ret = -EINVAL;
		goto error;
	}

	if (data[0] != 1 || data[1] != 0) {
		dev_warn(&state->client->dev, "firmware file version is wrong\n");
		ret = -EINVAL;
		goto error;
	}

	patch_version = data[2];
	block_count = data[4];
	crc_expected = data[7] << 8 | data[6];

	/* start uploading fw */
	/* boot/wdog status */
	ret = si2165_writereg8(state, REG_WDOG_AND_BOOT, 0x00);
	if (ret < 0)
		goto error;
	/* reset */
	ret = si2165_writereg8(state, REG_RST_ALL, 0x00);
	if (ret < 0)
		goto error;
	/* boot/wdog status */
	ret = si2165_readreg8(state, REG_WDOG_AND_BOOT, val);
	if (ret < 0)
		goto error;

	/* enable reset on error */
	ret = si2165_readreg8(state, REG_EN_RST_ERROR, val);
	if (ret < 0)
		goto error;
	ret = si2165_readreg8(state, REG_EN_RST_ERROR, val);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, REG_EN_RST_ERROR, 0x02);
	if (ret < 0)
		goto error;

	/* start right after the header */
	offset = 8;

	dev_info(&state->client->dev, "%s: extracted patch_version=0x%02x, block_count=0x%02x, crc_expected=0x%04x\n",
		 __func__, patch_version, block_count, crc_expected);

	ret = si2165_upload_firmware_block(state, data, len, &offset, 1);
	if (ret < 0)
		goto error;

	ret = si2165_writereg8(state, REG_PATCH_VERSION, patch_version);
	if (ret < 0)
		goto error;

	/* reset crc */
	ret = si2165_writereg8(state, REG_RST_CRC, 0x01);
	if (ret)
		goto error;

	ret = si2165_upload_firmware_block(state, data, len,
					   &offset, block_count);
	if (ret < 0) {
		dev_err(&state->client->dev,
			"firmware could not be uploaded\n");
		goto error;
	}

	/* read crc */
	ret = si2165_readreg16(state, REG_CRC, &val16);
	if (ret)
		goto error;

	if (val16 != crc_expected) {
		dev_err(&state->client->dev,
			"firmware crc mismatch %04x != %04x\n",
			val16, crc_expected);
		ret = -EINVAL;
		goto error;
	}

	ret = si2165_upload_firmware_block(state, data, len, &offset, 5);
	if (ret)
		goto error;

	if (len != offset) {
		dev_err(&state->client->dev,
			"firmware len mismatch %04x != %04x\n",
			len, offset);
		ret = -EINVAL;
		goto error;
	}

	/* reset watchdog error register */
	ret = si2165_writereg_mask8(state, REG_WDOG_AND_BOOT, 0x02, 0x02);
	if (ret < 0)
		goto error;

	/* enable reset on error */
	ret = si2165_writereg_mask8(state, REG_EN_RST_ERROR, 0x01, 0x01);
	if (ret < 0)
		goto error;

	dev_info(&state->client->dev, "fw load finished\n");

	ret = 0;
	state->firmware_loaded = true;
error:
	if (fw) {
		release_firmware(fw);
		fw = NULL;
	}

	return ret;
}

static int si2165_init(struct dvb_frontend *fe)
{
	int ret = 0;
	struct si2165_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u8 val;
	u8 patch_version = 0x00;

	dev_dbg(&state->client->dev, "%s: called\n", __func__);

	/* powerup */
	ret = si2165_writereg8(state, REG_CHIP_MODE, state->config.chip_mode);
	if (ret < 0)
		goto error;
	/* dsp_clock_enable */
	ret = si2165_writereg8(state, REG_DSP_CLOCK, 0x01);
	if (ret < 0)
		goto error;
	/* verify chip_mode */
	ret = si2165_readreg8(state, REG_CHIP_MODE, &val);
	if (ret < 0)
		goto error;
	if (val != state->config.chip_mode) {
		dev_err(&state->client->dev, "could not set chip_mode\n");
		return -EINVAL;
	}

	/* agc */
	ret = si2165_writereg8(state, REG_AGC_IF_TRI, 0x00);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, REG_AGC_IF_SLR, 0x01);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, REG_AGC2_OUTPUT, 0x00);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, REG_AGC2_CLKDIV, 0x07);
	if (ret < 0)
		goto error;
	/* rssi pad */
	ret = si2165_writereg8(state, REG_RSSI_PAD_CTRL, 0x00);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, REG_RSSI_ENABLE, 0x00);
	if (ret < 0)
		goto error;

	ret = si2165_init_pll(state);
	if (ret < 0)
		goto error;

	/* enable chip_init */
	ret = si2165_writereg8(state, REG_CHIP_INIT, 0x01);
	if (ret < 0)
		goto error;
	/* set start_init */
	ret = si2165_writereg8(state, REG_START_INIT, 0x01);
	if (ret < 0)
		goto error;
	ret = si2165_wait_init_done(state);
	if (ret < 0)
		goto error;

	/* disable chip_init */
	ret = si2165_writereg8(state, REG_CHIP_INIT, 0x00);
	if (ret < 0)
		goto error;

	/* ber_pkt - default 65535 */
	ret = si2165_writereg16(state, REG_BER_PKT,
				STATISTICS_PERIOD_PKT_COUNT);
	if (ret < 0)
		goto error;

	ret = si2165_readreg8(state, REG_PATCH_VERSION, &patch_version);
	if (ret < 0)
		goto error;

	ret = si2165_writereg8(state, REG_AUTO_RESET, 0x00);
	if (ret < 0)
		goto error;

	/* dsp_addr_jump */
	ret = si2165_writereg32(state, REG_ADDR_JUMP, 0xf4000000);
	if (ret < 0)
		goto error;
	/* boot/wdog status */
	ret = si2165_readreg8(state, REG_WDOG_AND_BOOT, &val);
	if (ret < 0)
		goto error;

	if (patch_version == 0x00) {
		ret = si2165_upload_firmware(state);
		if (ret < 0)
			goto error;
	}

	/* ts output config */
	ret = si2165_writereg8(state, REG_TS_DATA_MODE, 0x20);
	if (ret < 0)
		return ret;
	ret = si2165_writereg16(state, REG_TS_TRI, 0x00fe);
	if (ret < 0)
		return ret;
	ret = si2165_writereg24(state, REG_TS_SLR, 0x555555);
	if (ret < 0)
		return ret;
	ret = si2165_writereg8(state, REG_TS_CLK_MODE, 0x01);
	if (ret < 0)
		return ret;
	ret = si2165_writereg8(state, REG_TS_PARALLEL_MODE, 0x00);
	if (ret < 0)
		return ret;

	c = &state->fe.dtv_property_cache;
	c->cnr.len = 1;
	c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_error.len = 1;
	c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	c->post_bit_count.len = 1;
	c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	return 0;
error:
	return ret;
}

static int si2165_sleep(struct dvb_frontend *fe)
{
	int ret;
	struct si2165_state *state = fe->demodulator_priv;

	/* dsp clock disable */
	ret = si2165_writereg8(state, REG_DSP_CLOCK, 0x00);
	if (ret < 0)
		return ret;
	/* chip mode */
	ret = si2165_writereg8(state, REG_CHIP_MODE, SI2165_MODE_OFF);
	if (ret < 0)
		return ret;
	return 0;
}

static int si2165_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	int ret;
	u8 u8tmp;
	u32 u32tmp;
	struct si2165_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 delsys = c->delivery_system;

	*status = 0;

	switch (delsys) {
	case SYS_DVBT:
		/* check fast signal type */
		ret = si2165_readreg8(state, REG_CHECK_SIGNAL, &u8tmp);
		if (ret < 0)
			return ret;
		switch (u8tmp & 0x3) {
		case 0: /* searching */
		case 1: /* nothing */
			break;
		case 2: /* digital signal */
			*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER;
			break;
		}
		break;
	case SYS_DVBC_ANNEX_A:
		/* check packet sync lock */
		ret = si2165_readreg8(state, REG_PS_LOCK, &u8tmp);
		if (ret < 0)
			return ret;
		if (u8tmp & 0x01) {
			*status |= FE_HAS_SIGNAL;
			*status |= FE_HAS_CARRIER;
			*status |= FE_HAS_VITERBI;
			*status |= FE_HAS_SYNC;
		}
		break;
	}

	/* check fec_lock */
	ret = si2165_readreg8(state, REG_FEC_LOCK, &u8tmp);
	if (ret < 0)
		return ret;
	if (u8tmp & 0x01) {
		*status |= FE_HAS_SIGNAL;
		*status |= FE_HAS_CARRIER;
		*status |= FE_HAS_VITERBI;
		*status |= FE_HAS_SYNC;
		*status |= FE_HAS_LOCK;
	}

	/* CNR */
	if (delsys == SYS_DVBC_ANNEX_A && *status & FE_HAS_VITERBI) {
		ret = si2165_readreg24(state, REG_C_N, &u32tmp);
		if (ret < 0)
			return ret;
		/*
		 * svalue =
		 * 1000 * c_n/dB =
		 * 1000 * 10 * log10(2^24 / regval) =
		 * 1000 * 10 * (log10(2^24) - log10(regval)) =
		 * 1000 * 10 * (intlog10(2^24) - intlog10(regval)) / 2^24
		 *
		 * intlog10(x) = log10(x) * 2^24
		 * intlog10(2^24) = log10(2^24) * 2^24 = 121210686
		 */
		u32tmp = (1000 * 10 * (121210686 - (u64)intlog10(u32tmp)))
				>> 24;
		c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
		c->cnr.stat[0].svalue = u32tmp;
	} else
		c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	/* BER */
	if (*status & FE_HAS_VITERBI) {
		if (c->post_bit_error.stat[0].scale == FE_SCALE_NOT_AVAILABLE) {
			/* start new sampling period to get rid of old data*/
			ret = si2165_writereg8(state, REG_BER_RST, 0x01);
			if (ret < 0)
				return ret;

			/* set scale to enter read code on next call */
			c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
			c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
			c->post_bit_error.stat[0].uvalue = 0;
			c->post_bit_count.stat[0].uvalue = 0;

			/*
			 * reset DVBv3 value to deliver a good result
			 * for the first call
			 */
			state->ber_prev = 0;

		} else {
			ret = si2165_readreg8(state, REG_BER_AVAIL, &u8tmp);
			if (ret < 0)
				return ret;

			if (u8tmp & 1) {
				u32 biterrcnt;

				ret = si2165_readreg24(state, REG_BER_BIT,
							&biterrcnt);
				if (ret < 0)
					return ret;

				c->post_bit_error.stat[0].uvalue +=
					biterrcnt;
				c->post_bit_count.stat[0].uvalue +=
					STATISTICS_PERIOD_BIT_COUNT;

				/* start new sampling period */
				ret = si2165_writereg8(state,
							REG_BER_RST, 0x01);
				if (ret < 0)
					return ret;

				dev_dbg(&state->client->dev,
					"post_bit_error=%u post_bit_count=%u\n",
					biterrcnt, STATISTICS_PERIOD_BIT_COUNT);
			}
		}
	} else {
		c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	return 0;
}

static int si2165_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	if (c->cnr.stat[0].scale == FE_SCALE_DECIBEL)
		*snr = div_s64(c->cnr.stat[0].svalue, 100);
	else
		*snr = 0;
	return 0;
}

static int si2165_read_ber(struct dvb_frontend *fe, u32 *ber)
{
	struct si2165_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	if (c->post_bit_error.stat[0].scale != FE_SCALE_COUNTER) {
		*ber = 0;
		return 0;
	}

	*ber = c->post_bit_error.stat[0].uvalue - state->ber_prev;
	state->ber_prev = c->post_bit_error.stat[0].uvalue;

	return 0;
}

static int si2165_set_oversamp(struct si2165_state *state, u32 dvb_rate)
{
	u64 oversamp;
	u32 reg_value;

	if (!dvb_rate)
		return -EINVAL;

	oversamp = si2165_get_fe_clk(state);
	oversamp <<= 23;
	do_div(oversamp, dvb_rate);
	reg_value = oversamp & 0x3fffffff;

	dev_dbg(&state->client->dev, "Write oversamp=%#x\n", reg_value);
	return si2165_writereg32(state, REG_OVERSAMP, reg_value);
}

static int si2165_set_if_freq_shift(struct si2165_state *state)
{
	struct dvb_frontend *fe = &state->fe;
	u64 if_freq_shift;
	s32 reg_value = 0;
	u32 fe_clk = si2165_get_fe_clk(state);
	u32 IF = 0;

	if (!fe->ops.tuner_ops.get_if_frequency) {
		dev_err(&state->client->dev,
			"Error: get_if_frequency() not defined at tuner. Can't work without it!\n");
		return -EINVAL;
	}

	if (!fe_clk)
		return -EINVAL;

	fe->ops.tuner_ops.get_if_frequency(fe, &IF);
	if_freq_shift = IF;
	if_freq_shift <<= 29;

	do_div(if_freq_shift, fe_clk);
	reg_value = (s32)if_freq_shift;

	if (state->config.inversion)
		reg_value = -reg_value;

	reg_value = reg_value & 0x1fffffff;

	/* if_freq_shift, usbdump contained 0x023ee08f; */
	return si2165_writereg32(state, REG_IF_FREQ_SHIFT, reg_value);
}

static const struct si2165_reg_value_pair dvbt_regs[] = {
	/* standard = DVB-T */
	{ REG_DVB_STANDARD, 0x01 },
	/* impulsive_noise_remover */
	{ REG_IMPULSIVE_NOISE_REM, 0x01 },
	{ REG_AUTO_RESET, 0x00 },
	/* agc2 */
	{ REG_AGC2_MIN, 0x41 },
	{ REG_AGC2_KACQ, 0x0e },
	{ REG_AGC2_KLOC, 0x10 },
	/* agc */
	{ REG_AGC_UNFREEZE_THR, 0x03 },
	{ REG_AGC_CRESTF_DBX8, 0x78 },
	/* agc */
	{ REG_AAF_CRESTF_DBX8, 0x78 },
	{ REG_ACI_CRESTF_DBX8, 0x68 },
	/* freq_sync_range */
	REG16(REG_FREQ_SYNC_RANGE, 0x0064),
	/* gp_reg0 */
	{ REG_GP_REG0_MSB, 0x00 }
};

static int si2165_set_frontend_dvbt(struct dvb_frontend *fe)
{
	int ret;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct si2165_state *state = fe->demodulator_priv;
	u32 dvb_rate = 0;
	u16 bw10k;
	u32 bw_hz = p->bandwidth_hz;

	dev_dbg(&state->client->dev, "%s: called\n", __func__);

	if (!state->has_dvbt)
		return -EINVAL;

	/* no bandwidth auto-detection */
	if (bw_hz == 0)
		return -EINVAL;

	dvb_rate = bw_hz * 8 / 7;
	bw10k = bw_hz / 10000;

	ret = si2165_adjust_pll_divl(state, 12);
	if (ret < 0)
		return ret;

	/* bandwidth in 10KHz steps */
	ret = si2165_writereg16(state, REG_T_BANDWIDTH, bw10k);
	if (ret < 0)
		return ret;
	ret = si2165_set_oversamp(state, dvb_rate);
	if (ret < 0)
		return ret;

	ret = si2165_write_reg_list(state, dvbt_regs, ARRAY_SIZE(dvbt_regs));
	if (ret < 0)
		return ret;

	return 0;
}

static const struct si2165_reg_value_pair dvbc_regs[] = {
	/* standard = DVB-C */
	{ REG_DVB_STANDARD, 0x05 },

	/* agc2 */
	{ REG_AGC2_MIN, 0x50 },
	{ REG_AGC2_KACQ, 0x0e },
	{ REG_AGC2_KLOC, 0x10 },
	/* agc */
	{ REG_AGC_UNFREEZE_THR, 0x03 },
	{ REG_AGC_CRESTF_DBX8, 0x68 },
	/* agc */
	{ REG_AAF_CRESTF_DBX8, 0x68 },
	{ REG_ACI_CRESTF_DBX8, 0x50 },

	{ REG_EQ_AUTO_CONTROL, 0x0d },

	{ REG_KP_LOCK, 0x05 },
	{ REG_CENTRAL_TAP, 0x09 },
	REG16(REG_UNKNOWN_350, 0x3e80),

	{ REG_AUTO_RESET, 0x01 },
	REG16(REG_UNKNOWN_24C, 0x0000),
	REG16(REG_UNKNOWN_27C, 0x0000),
	{ REG_SWEEP_STEP, 0x03 },
	{ REG_AGC_IF_TRI, 0x00 },
};

static int si2165_set_frontend_dvbc(struct dvb_frontend *fe)
{
	struct si2165_state *state = fe->demodulator_priv;
	int ret;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	const u32 dvb_rate = p->symbol_rate;
	u8 u8tmp;

	if (!state->has_dvbc)
		return -EINVAL;

	if (dvb_rate == 0)
		return -EINVAL;

	ret = si2165_adjust_pll_divl(state, 14);
	if (ret < 0)
		return ret;

	/* Oversampling */
	ret = si2165_set_oversamp(state, dvb_rate);
	if (ret < 0)
		return ret;

	switch (p->modulation) {
	case QPSK:
		u8tmp = 0x3;
		break;
	case QAM_16:
		u8tmp = 0x7;
		break;
	case QAM_32:
		u8tmp = 0x8;
		break;
	case QAM_64:
		u8tmp = 0x9;
		break;
	case QAM_128:
		u8tmp = 0xa;
		break;
	case QAM_256:
	default:
		u8tmp = 0xb;
		break;
	}
	ret = si2165_writereg8(state, REG_REQ_CONSTELLATION, u8tmp);
	if (ret < 0)
		return ret;

	ret = si2165_writereg32(state, REG_LOCK_TIMEOUT, 0x007a1200);
	if (ret < 0)
		return ret;

	ret = si2165_write_reg_list(state, dvbc_regs, ARRAY_SIZE(dvbc_regs));
	if (ret < 0)
		return ret;

	return 0;
}

static const struct si2165_reg_value_pair adc_rewrite[] = {
	{ REG_ADC_RI1, 0x46 },
	{ REG_ADC_RI3, 0x00 },
	{ REG_ADC_RI5, 0x0a },
	{ REG_ADC_RI6, 0xff },
	{ REG_ADC_RI8, 0x70 }
};

static int si2165_set_frontend(struct dvb_frontend *fe)
{
	struct si2165_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 delsys = p->delivery_system;
	int ret;
	u8 val[3];

	/* initial setting of if freq shift */
	ret = si2165_set_if_freq_shift(state);
	if (ret < 0)
		return ret;

	switch (delsys) {
	case SYS_DVBT:
		ret = si2165_set_frontend_dvbt(fe);
		if (ret < 0)
			return ret;
		break;
	case SYS_DVBC_ANNEX_A:
		ret = si2165_set_frontend_dvbc(fe);
		if (ret < 0)
			return ret;
		break;
	default:
		return -EINVAL;
	}

	/* dsp_addr_jump */
	ret = si2165_writereg32(state, REG_ADDR_JUMP, 0xf4000000);
	if (ret < 0)
		return ret;

	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);

	/* recalc if_freq_shift if IF might has changed */
	ret = si2165_set_if_freq_shift(state);
	if (ret < 0)
		return ret;

	/* boot/wdog status */
	ret = si2165_readreg8(state, REG_WDOG_AND_BOOT, val);
	if (ret < 0)
		return ret;
	ret = si2165_writereg8(state, REG_WDOG_AND_BOOT, 0x00);
	if (ret < 0)
		return ret;

	/* reset all */
	ret = si2165_writereg8(state, REG_RST_ALL, 0x00);
	if (ret < 0)
		return ret;
	/* gp_reg0 */
	ret = si2165_writereg32(state, REG_GP_REG0_LSB, 0x00000000);
	if (ret < 0)
		return ret;

	/* write adc values after each reset*/
	ret = si2165_write_reg_list(state, adc_rewrite,
				    ARRAY_SIZE(adc_rewrite));
	if (ret < 0)
		return ret;

	/* start_synchro */
	ret = si2165_writereg8(state, REG_START_SYNCHRO, 0x01);
	if (ret < 0)
		return ret;
	/* boot/wdog status */
	ret = si2165_readreg8(state, REG_WDOG_AND_BOOT, val);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct dvb_frontend_ops si2165_ops = {
	.info = {
		.name = "Silicon Labs ",
		 /* For DVB-C */
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 7200000,
		/* For DVB-T */
		.frequency_stepsize_hz = 166667,
		.caps = FE_CAN_FEC_1_2 |
			FE_CAN_FEC_2_3 |
			FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 |
			FE_CAN_FEC_7_8 |
			FE_CAN_FEC_AUTO |
			FE_CAN_QPSK |
			FE_CAN_QAM_16 |
			FE_CAN_QAM_32 |
			FE_CAN_QAM_64 |
			FE_CAN_QAM_128 |
			FE_CAN_QAM_256 |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_MUTE_TS |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_RECOVER
	},

	.get_tune_settings = si2165_get_tune_settings,

	.init = si2165_init,
	.sleep = si2165_sleep,

	.set_frontend      = si2165_set_frontend,
	.read_status       = si2165_read_status,
	.read_snr          = si2165_read_snr,
	.read_ber          = si2165_read_ber,
};

static int si2165_probe(struct i2c_client *client)
{
	struct si2165_state *state = NULL;
	struct si2165_platform_data *pdata = client->dev.platform_data;
	int n;
	int ret = 0;
	u8 val;
	char rev_char;
	const char *chip_name;
	static const struct regmap_config regmap_config = {
		.reg_bits = 16,
		.val_bits = 8,
		.max_register = 0x08ff,
	};

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		ret = -ENOMEM;
		goto error;
	}

	/* create regmap */
	state->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(state->regmap)) {
		ret = PTR_ERR(state->regmap);
		goto error;
	}

	/* setup the state */
	state->client = client;
	state->config.i2c_addr = client->addr;
	state->config.chip_mode = pdata->chip_mode;
	state->config.ref_freq_hz = pdata->ref_freq_hz;
	state->config.inversion = pdata->inversion;

	if (state->config.ref_freq_hz < 4000000 ||
	    state->config.ref_freq_hz > 27000000) {
		dev_err(&state->client->dev, "ref_freq of %d Hz not supported by this driver\n",
			state->config.ref_freq_hz);
		ret = -EINVAL;
		goto error;
	}

	/* create dvb_frontend */
	memcpy(&state->fe.ops, &si2165_ops,
	       sizeof(struct dvb_frontend_ops));
	state->fe.ops.release = NULL;
	state->fe.demodulator_priv = state;
	i2c_set_clientdata(client, state);

	/* powerup */
	ret = si2165_writereg8(state, REG_CHIP_MODE, state->config.chip_mode);
	if (ret < 0)
		goto nodev_error;

	ret = si2165_readreg8(state, REG_CHIP_MODE, &val);
	if (ret < 0)
		goto nodev_error;
	if (val != state->config.chip_mode)
		goto nodev_error;

	ret = si2165_readreg8(state, REG_CHIP_REVCODE, &state->chip_revcode);
	if (ret < 0)
		goto nodev_error;

	ret = si2165_readreg8(state, REV_CHIP_TYPE, &state->chip_type);
	if (ret < 0)
		goto nodev_error;

	/* powerdown */
	ret = si2165_writereg8(state, REG_CHIP_MODE, SI2165_MODE_OFF);
	if (ret < 0)
		goto nodev_error;

	if (state->chip_revcode < 26)
		rev_char = 'A' + state->chip_revcode;
	else
		rev_char = '?';

	switch (state->chip_type) {
	case 0x06:
		chip_name = "Si2161";
		state->has_dvbt = true;
		break;
	case 0x07:
		chip_name = "Si2165";
		state->has_dvbt = true;
		state->has_dvbc = true;
		break;
	default:
		dev_err(&state->client->dev, "Unsupported Silicon Labs chip (type %d, rev %d)\n",
			state->chip_type, state->chip_revcode);
		goto nodev_error;
	}

	dev_info(&state->client->dev,
		 "Detected Silicon Labs %s-%c (type %d, rev %d)\n",
		chip_name, rev_char, state->chip_type,
		state->chip_revcode);

	strlcat(state->fe.ops.info.name, chip_name,
		sizeof(state->fe.ops.info.name));

	n = 0;
	if (state->has_dvbt) {
		state->fe.ops.delsys[n++] = SYS_DVBT;
		strlcat(state->fe.ops.info.name, " DVB-T",
			sizeof(state->fe.ops.info.name));
	}
	if (state->has_dvbc) {
		state->fe.ops.delsys[n++] = SYS_DVBC_ANNEX_A;
		strlcat(state->fe.ops.info.name, " DVB-C",
			sizeof(state->fe.ops.info.name));
	}

	/* return fe pointer */
	*pdata->fe = &state->fe;

	return 0;

nodev_error:
	ret = -ENODEV;
error:
	kfree(state);
	dev_dbg(&client->dev, "failed=%d\n", ret);
	return ret;
}

static void si2165_remove(struct i2c_client *client)
{
	struct si2165_state *state = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "\n");

	kfree(state);
}

static const struct i2c_device_id si2165_id_table[] = {
	{"si2165", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, si2165_id_table);

static struct i2c_driver si2165_driver = {
	.driver = {
		.name	= "si2165",
	},
	.probe		= si2165_probe,
	.remove		= si2165_remove,
	.id_table	= si2165_id_table,
};

module_i2c_driver(si2165_driver);

MODULE_DESCRIPTION("Silicon Labs Si2165 DVB-C/-T Demodulator driver");
MODULE_AUTHOR("Matthias Schwarzott <zzam@gentoo.org>");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(SI2165_FIRMWARE_REV_D);
