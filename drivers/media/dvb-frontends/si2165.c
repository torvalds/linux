/*
    Driver for Silicon Labs SI2165 DVB-C/-T Demodulator

    Copyright (C) 2013-2014 Matthias Schwarzott <zzam@gentoo.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    References:
    http://www.silabs.com/Support%20Documents/TechnicalDocs/Si2165-short.pdf
*/

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/firmware.h>

#include "dvb_frontend.h"
#include "dvb_math.h"
#include "si2165_priv.h"
#include "si2165.h"

/* Hauppauge WinTV-HVR-930C-HD B130 / PCTV QuatroStick 521e 1113xx
 * uses 16 MHz xtal */

/* Hauppauge WinTV-HVR-930C-HD B131 / PCTV QuatroStick 522e 1114xx
 * uses 24 MHz clock provided by tuner */

struct si2165_state {
	struct i2c_adapter *i2c;

	struct dvb_frontend frontend;

	struct si2165_config config;

	/* chip revision */
	u8 revcode;
	/* chip type */
	u8 chip_type;

	/* calculated by xtal and div settings */
	u32 fvco_hz;
	u32 sys_clk;
	u32 adc_clk;

	bool has_dvbc;
	bool has_dvbt;
	bool firmware_loaded;
};

#define DEBUG_OTHER	0x01
#define DEBUG_I2C_WRITE	0x02
#define DEBUG_I2C_READ	0x04
#define DEBUG_REG_READ	0x08
#define DEBUG_REG_WRITE	0x10
#define DEBUG_FW_LOAD	0x20

static int debug = 0x00;

#define dprintk(args...) \
	do { \
		if (debug & DEBUG_OTHER) \
			printk(KERN_DEBUG "si2165: " args); \
	} while (0)

#define deb_i2c_write(args...) \
	do { \
		if (debug & DEBUG_I2C_WRITE) \
			printk(KERN_DEBUG "si2165: i2c write: " args); \
	} while (0)

#define deb_i2c_read(args...) \
	do { \
		if (debug & DEBUG_I2C_READ) \
			printk(KERN_DEBUG "si2165: i2c read: " args); \
	} while (0)

#define deb_readreg(args...) \
	do { \
		if (debug & DEBUG_REG_READ) \
			printk(KERN_DEBUG "si2165: reg read: " args); \
	} while (0)

#define deb_writereg(args...) \
	do { \
		if (debug & DEBUG_REG_WRITE) \
			printk(KERN_DEBUG "si2165: reg write: " args); \
	} while (0)

#define deb_fw_load(args...) \
	do { \
		if (debug & DEBUG_FW_LOAD) \
			printk(KERN_DEBUG "si2165: fw load: " args); \
	} while (0)

static int si2165_write(struct si2165_state *state, const u16 reg,
		       const u8 *src, const int count)
{
	int ret;
	struct i2c_msg msg;
	u8 buf[2 + 4]; /* write a maximum of 4 bytes of data */

	if (count + 2 > sizeof(buf)) {
		dev_warn(&state->i2c->dev,
			  "%s: i2c wr reg=%04x: count=%d is too big!\n",
			  KBUILD_MODNAME, reg, count);
		return -EINVAL;
	}
	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	memcpy(buf + 2, src, count);

	msg.addr = state->config.i2c_addr;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = count + 2;

	if (debug & DEBUG_I2C_WRITE)
		deb_i2c_write("reg: 0x%04x, data: %*ph\n", reg, count, src);

	ret = i2c_transfer(state->i2c, &msg, 1);

	if (ret != 1) {
		dev_err(&state->i2c->dev, "%s: ret == %d\n", __func__, ret);
		if (ret < 0)
			return ret;
		else
			return -EREMOTEIO;
	}

	return 0;
}

static int si2165_read(struct si2165_state *state,
		       const u16 reg, u8 *val, const int count)
{
	int ret;
	u8 reg_buf[] = { reg >> 8, reg & 0xff };
	struct i2c_msg msg[] = {
		{ .addr = state->config.i2c_addr,
		  .flags = 0, .buf = reg_buf, .len = 2 },
		{ .addr = state->config.i2c_addr,
		  .flags = I2C_M_RD, .buf = val, .len = count },
	};

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) {
		dev_err(&state->i2c->dev, "%s: error (addr %02x reg %04x error (ret == %i)\n",
			__func__, state->config.i2c_addr, reg, ret);
		if (ret < 0)
			return ret;
		else
			return -EREMOTEIO;
	}

	if (debug & DEBUG_I2C_READ)
		deb_i2c_read("reg: 0x%04x, data: %*ph\n", reg, count, val);

	return 0;
}

static int si2165_readreg8(struct si2165_state *state,
		       const u16 reg, u8 *val)
{
	int ret;

	ret = si2165_read(state, reg, val, 1);
	deb_readreg("R(0x%04x)=0x%02x\n", reg, *val);
	return ret;
}

static int si2165_readreg16(struct si2165_state *state,
		       const u16 reg, u16 *val)
{
	u8 buf[2];

	int ret = si2165_read(state, reg, buf, 2);
	*val = buf[0] | buf[1] << 8;
	deb_readreg("R(0x%04x)=0x%04x\n", reg, *val);
	return ret;
}

static int si2165_writereg8(struct si2165_state *state, const u16 reg, u8 val)
{
	return si2165_write(state, reg, &val, 1);
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
	int ret;
	u8 tmp;

	if (mask != 0xff) {
		ret = si2165_readreg8(state, reg, &tmp);
		if (ret < 0)
			goto err;

		val &= mask;
		tmp &= ~mask;
		val |= tmp;
	}

	ret = si2165_writereg8(state, reg, val);
err:
	return ret;
}

static int si2165_get_tune_settings(struct dvb_frontend *fe,
				    struct dvb_frontend_tune_settings *s)
{
	s->min_delay_ms = 1000;
	return 0;
}

static int si2165_init_pll(struct si2165_state *state)
{
	u32 ref_freq_Hz = state->config.ref_freq_Hz;
	u8 divr = 1; /* 1..7 */
	u8 divp = 1; /* only 1 or 4 */
	u8 divn = 56; /* 1..63 */
	u8 divm = 8;
	u8 divl = 12;
	u8 buf[4];

	/* hardcoded values can be deleted if calculation is verified
	 * or it yields the same values as the windows driver */
	switch (ref_freq_Hz) {
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
		if (ref_freq_Hz > 16000000u)
			divr = 2;

		/* now select divn and divp such that
		 * fvco is in 1624..1824 MHz */
		if (1624000000u * divr > ref_freq_Hz * 2u * 63u)
			divp = 4;

		/* is this already correct regarding rounding? */
		divn = 1624000000u * divr / (ref_freq_Hz * 2u * divp);
		break;
	}

	/* adc_clk and sys_clk depend on xtal and pll settings */
	state->fvco_hz = ref_freq_Hz / divr
			* 2u * divn * divp;
	state->adc_clk = state->fvco_hz / (divm * 4u);
	state->sys_clk = state->fvco_hz / (divl * 2u);

	/* write pll registers 0x00a0..0x00a3 at once */
	buf[0] = divl;
	buf[1] = divm;
	buf[2] = (divn & 0x3f) | ((divp == 1) ? 0x40 : 0x00) | 0x80;
	buf[3] = divr;
	return si2165_write(state, 0x00a0, buf, 4);
}

static int si2165_adjust_pll_divl(struct si2165_state *state, u8 divl)
{
	state->sys_clk = state->fvco_hz / (divl * 2u);
	return si2165_writereg8(state, 0x00a0, divl); /* pll_divl */
}

static u32 si2165_get_fe_clk(struct si2165_state *state)
{
	/* assume Oversampling mode Ovr4 is used */
	return state->adc_clk;
}

static bool si2165_wait_init_done(struct si2165_state *state)
{
	int ret = -EINVAL;
	u8 val = 0;
	int i;

	for (i = 0; i < 3; ++i) {
		si2165_readreg8(state, 0x0054, &val);
		if (val == 0x01)
			return 0;
		usleep_range(1000, 50000);
	}
	dev_err(&state->i2c->dev, "%s: init_done was not set\n",
		KBUILD_MODNAME);
	return ret;
}

static int si2165_upload_firmware_block(struct si2165_state *state,
	const u8 *data, u32 len, u32 *poffset, u32 block_count)
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

	deb_fw_load("si2165_upload_firmware_block called with len=0x%x offset=0x%x blockcount=0x%x\n",
				len, offset, block_count);
	while (offset+12 <= len && cur_block < block_count) {
		deb_fw_load("si2165_upload_firmware_block in while len=0x%x offset=0x%x cur_block=0x%x blockcount=0x%x\n",
					len, offset, cur_block, block_count);
		wordcount = data[offset];
		if (wordcount < 1 || data[offset+1] ||
		    data[offset+2] || data[offset+3]) {
			dev_warn(&state->i2c->dev,
				 "%s: bad fw data[0..3] = %*ph\n",
				KBUILD_MODNAME, 4, data);
			return -EINVAL;
		}

		if (offset + 8 + wordcount * 4 > len) {
			dev_warn(&state->i2c->dev,
				 "%s: len is too small for block len=%d, wordcount=%d\n",
				KBUILD_MODNAME, len, wordcount);
			return -EINVAL;
		}

		buf_ctrl[0] = wordcount - 1;

		ret = si2165_write(state, 0x0364, buf_ctrl, 4);
		if (ret < 0)
			goto error;
		ret = si2165_write(state, 0x0368, data+offset+4, 4);
		if (ret < 0)
			goto error;

		offset += 8;

		while (wordcount > 0) {
			ret = si2165_write(state, 0x36c, data+offset, 4);
			if (ret < 0)
				goto error;
			wordcount--;
			offset += 4;
		}
		cur_block++;
	}

	deb_fw_load("si2165_upload_firmware_block after while len=0x%x offset=0x%x cur_block=0x%x blockcount=0x%x\n",
				len, offset, cur_block, block_count);

	if (poffset)
		*poffset = offset;

	deb_fw_load("si2165_upload_firmware_block returned offset=0x%x\n",
				offset);

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
	u8 *fw_file = SI2165_FIRMWARE;
	const u8 *data;
	u32 len;
	u32 offset;
	u8 patch_version;
	u8 block_count;
	u16 crc_expected;

	/* request the firmware, this will block and timeout */
	ret = request_firmware(&fw, fw_file, state->i2c->dev.parent);
	if (ret) {
		dev_warn(&state->i2c->dev, "%s: firmare file '%s' not found\n",
				KBUILD_MODNAME, fw_file);
		goto error;
	}

	data = fw->data;
	len = fw->size;

	dev_info(&state->i2c->dev, "%s: downloading firmware from file '%s' size=%d\n",
			KBUILD_MODNAME, fw_file, len);

	if (len % 4 != 0) {
		dev_warn(&state->i2c->dev, "%s: firmware size is not multiple of 4\n",
				KBUILD_MODNAME);
		ret = -EINVAL;
		goto error;
	}

	/* check header (8 bytes) */
	if (len < 8) {
		dev_warn(&state->i2c->dev, "%s: firmware header is missing\n",
				KBUILD_MODNAME);
		ret = -EINVAL;
		goto error;
	}

	if (data[0] != 1 || data[1] != 0) {
		dev_warn(&state->i2c->dev, "%s: firmware file version is wrong\n",
				KBUILD_MODNAME);
		ret = -EINVAL;
		goto error;
	}

	patch_version = data[2];
	block_count = data[4];
	crc_expected = data[7] << 8 | data[6];

	/* start uploading fw */
	/* boot/wdog status */
	ret = si2165_writereg8(state, 0x0341, 0x00);
	if (ret < 0)
		goto error;
	/* reset */
	ret = si2165_writereg8(state, 0x00c0, 0x00);
	if (ret < 0)
		goto error;
	/* boot/wdog status */
	ret = si2165_readreg8(state, 0x0341, val);
	if (ret < 0)
		goto error;

	/* enable reset on error */
	ret = si2165_readreg8(state, 0x035c, val);
	if (ret < 0)
		goto error;
	ret = si2165_readreg8(state, 0x035c, val);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, 0x035c, 0x02);
	if (ret < 0)
		goto error;

	/* start right after the header */
	offset = 8;

	dev_info(&state->i2c->dev, "%s: si2165_upload_firmware extracted patch_version=0x%02x, block_count=0x%02x, crc_expected=0x%04x\n",
		KBUILD_MODNAME, patch_version, block_count, crc_expected);

	ret = si2165_upload_firmware_block(state, data, len, &offset, 1);
	if (ret < 0)
		goto error;

	ret = si2165_writereg8(state, 0x0344, patch_version);
	if (ret < 0)
		goto error;

	/* reset crc */
	ret = si2165_writereg8(state, 0x0379, 0x01);
	if (ret)
		return ret;

	ret = si2165_upload_firmware_block(state, data, len,
					   &offset, block_count);
	if (ret < 0) {
		dev_err(&state->i2c->dev,
			"%s: firmare could not be uploaded\n",
			KBUILD_MODNAME);
		goto error;
	}

	/* read crc */
	ret = si2165_readreg16(state, 0x037a, &val16);
	if (ret)
		goto error;

	if (val16 != crc_expected) {
		dev_err(&state->i2c->dev,
			"%s: firmware crc mismatch %04x != %04x\n",
			KBUILD_MODNAME, val16, crc_expected);
		ret = -EINVAL;
		goto error;
	}

	ret = si2165_upload_firmware_block(state, data, len, &offset, 5);
	if (ret)
		goto error;

	if (len != offset) {
		dev_err(&state->i2c->dev,
			"%s: firmare len mismatch %04x != %04x\n",
			KBUILD_MODNAME, len, offset);
		ret = -EINVAL;
		goto error;
	}

	/* reset watchdog error register */
	ret = si2165_writereg_mask8(state, 0x0341, 0x02, 0x02);
	if (ret < 0)
		goto error;

	/* enable reset on error */
	ret = si2165_writereg_mask8(state, 0x035c, 0x01, 0x01);
	if (ret < 0)
		goto error;

	dev_info(&state->i2c->dev, "%s: fw load finished\n", KBUILD_MODNAME);

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
	u8 val;
	u8 patch_version = 0x00;

	dprintk("%s: called\n", __func__);

	/* powerup */
	ret = si2165_writereg8(state, 0x0000, state->config.chip_mode);
	if (ret < 0)
		goto error;
	/* dsp_clock_enable */
	ret = si2165_writereg8(state, 0x0104, 0x01);
	if (ret < 0)
		goto error;
	ret = si2165_readreg8(state, 0x0000, &val); /* verify chip_mode */
	if (ret < 0)
		goto error;
	if (val != state->config.chip_mode) {
		dev_err(&state->i2c->dev, "%s: could not set chip_mode\n",
			KBUILD_MODNAME);
		return -EINVAL;
	}

	/* agc */
	ret = si2165_writereg8(state, 0x018b, 0x00);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, 0x0190, 0x01);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, 0x0170, 0x00);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, 0x0171, 0x07);
	if (ret < 0)
		goto error;
	/* rssi pad */
	ret = si2165_writereg8(state, 0x0646, 0x00);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, 0x0641, 0x00);
	if (ret < 0)
		goto error;

	ret = si2165_init_pll(state);
	if (ret < 0)
		goto error;

	/* enable chip_init */
	ret = si2165_writereg8(state, 0x0050, 0x01);
	if (ret < 0)
		goto error;
	/* set start_init */
	ret = si2165_writereg8(state, 0x0096, 0x01);
	if (ret < 0)
		goto error;
	ret = si2165_wait_init_done(state);
	if (ret < 0)
		goto error;

	/* disable chip_init */
	ret = si2165_writereg8(state, 0x0050, 0x00);
	if (ret < 0)
		goto error;

	/* ber_pkt */
	ret = si2165_writereg16(state, 0x0470 , 0x7530);
	if (ret < 0)
		goto error;

	ret = si2165_readreg8(state, 0x0344, &patch_version);
	if (ret < 0)
		goto error;

	ret = si2165_writereg8(state, 0x00cb, 0x00);
	if (ret < 0)
		goto error;

	/* dsp_addr_jump */
	ret = si2165_writereg32(state, 0x0348, 0xf4000000);
	if (ret < 0)
		goto error;
	/* boot/wdog status */
	ret = si2165_readreg8(state, 0x0341, &val);
	if (ret < 0)
		goto error;

	if (patch_version == 0x00) {
		ret = si2165_upload_firmware(state);
		if (ret < 0)
			goto error;
	}

	/* write adc values after each reset*/
	ret = si2165_writereg8(state, 0x012a, 0x46);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, 0x012c, 0x00);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, 0x012e, 0x0a);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, 0x012f, 0xff);
	if (ret < 0)
		goto error;
	ret = si2165_writereg8(state, 0x0123, 0x70);
	if (ret < 0)
		goto error;

	return 0;
error:
	return ret;
}

static int si2165_sleep(struct dvb_frontend *fe)
{
	int ret;
	struct si2165_state *state = fe->demodulator_priv;

	/* dsp clock disable */
	ret = si2165_writereg8(state, 0x0104, 0x00);
	if (ret < 0)
		return ret;
	/* chip mode */
	ret = si2165_writereg8(state, 0x0000, SI2165_MODE_OFF);
	if (ret < 0)
		return ret;
	return 0;
}

static int si2165_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	int ret;
	u8 fec_lock = 0;
	struct si2165_state *state = fe->demodulator_priv;

	if (!state->has_dvbt)
		return -EINVAL;

	/* check fec_lock */
	ret = si2165_readreg8(state, 0x4e0, &fec_lock);
	if (ret < 0)
		return ret;
	*status = 0;
	if (fec_lock & 0x01) {
		*status |= FE_HAS_SIGNAL;
		*status |= FE_HAS_CARRIER;
		*status |= FE_HAS_VITERBI;
		*status |= FE_HAS_SYNC;
		*status |= FE_HAS_LOCK;
	}

	return 0;
}

static int si2165_set_oversamp(struct si2165_state *state, u32 dvb_rate)
{
	u64 oversamp;
	u32 reg_value;

	oversamp = si2165_get_fe_clk(state);
	oversamp <<= 23;
	do_div(oversamp, dvb_rate);
	reg_value = oversamp & 0x3fffffff;

	/* oversamp, usbdump contained 0x03100000; */
	return si2165_writereg32(state, 0x00e4, reg_value);
}

static int si2165_set_if_freq_shift(struct si2165_state *state, u32 IF)
{
	u64 if_freq_shift;
	s32 reg_value = 0;
	u32 fe_clk = si2165_get_fe_clk(state);

	if_freq_shift = IF;
	if_freq_shift <<= 29;

	do_div(if_freq_shift, fe_clk);
	reg_value = (s32)if_freq_shift;

	if (state->config.inversion)
		reg_value = -reg_value;

	reg_value = reg_value & 0x1fffffff;

	/* if_freq_shift, usbdump contained 0x023ee08f; */
	return si2165_writereg32(state, 0x00e8, reg_value);
}

static int si2165_set_parameters(struct dvb_frontend *fe)
{
	int ret;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct si2165_state *state = fe->demodulator_priv;
	u8 val[3];
	u32 IF;
	u32 dvb_rate = 0;
	u16 bw10k;

	dprintk("%s: called\n", __func__);

	if (!fe->ops.tuner_ops.get_if_frequency) {
		dev_err(&state->i2c->dev,
			"%s: Error: get_if_frequency() not defined at tuner. Can't work without it!\n",
			KBUILD_MODNAME);
		return -EINVAL;
	}

	if (!state->has_dvbt)
		return -EINVAL;

	if (p->bandwidth_hz > 0) {
		dvb_rate = p->bandwidth_hz * 8 / 7;
		bw10k = p->bandwidth_hz / 10000;
	} else {
		dvb_rate = 8 * 8 / 7;
		bw10k = 800;
	}

	/* standard = DVB-T */
	ret = si2165_writereg8(state, 0x00ec, 0x01);
	if (ret < 0)
		return ret;
	ret = si2165_adjust_pll_divl(state, 12);
	if (ret < 0)
		return ret;

	fe->ops.tuner_ops.get_if_frequency(fe, &IF);
	ret = si2165_set_if_freq_shift(state, IF);
	if (ret < 0)
		return ret;
	ret = si2165_writereg8(state, 0x08f8, 0x00);
	if (ret < 0)
		return ret;
	/* ts output config */
	ret = si2165_writereg8(state, 0x04e4, 0x20);
	if (ret < 0)
		return ret;
	ret = si2165_writereg16(state, 0x04ef, 0x00fe);
	if (ret < 0)
		return ret;
	ret = si2165_writereg24(state, 0x04f4, 0x555555);
	if (ret < 0)
		return ret;
	ret = si2165_writereg8(state, 0x04e5, 0x01);
	if (ret < 0)
		return ret;
	/* bandwidth in 10KHz steps */
	ret = si2165_writereg16(state, 0x0308, bw10k);
	if (ret < 0)
		return ret;
	ret = si2165_set_oversamp(state, dvb_rate);
	if (ret < 0)
		return ret;
	/* impulsive_noise_remover */
	ret = si2165_writereg8(state, 0x031c, 0x01);
	if (ret < 0)
		return ret;
	ret = si2165_writereg8(state, 0x00cb, 0x00);
	if (ret < 0)
		return ret;
	/* agc2 */
	ret = si2165_writereg8(state, 0x016e, 0x41);
	if (ret < 0)
		return ret;
	ret = si2165_writereg8(state, 0x016c, 0x0e);
	if (ret < 0)
		return ret;
	ret = si2165_writereg8(state, 0x016d, 0x10);
	if (ret < 0)
		return ret;
	/* agc */
	ret = si2165_writereg8(state, 0x015b, 0x03);
	if (ret < 0)
		return ret;
	ret = si2165_writereg8(state, 0x0150, 0x78);
	if (ret < 0)
		return ret;
	/* agc */
	ret = si2165_writereg8(state, 0x01a0, 0x78);
	if (ret < 0)
		return ret;
	ret = si2165_writereg8(state, 0x01c8, 0x68);
	if (ret < 0)
		return ret;
	/* freq_sync_range */
	ret = si2165_writereg16(state, 0x030c, 0x0064);
	if (ret < 0)
		return ret;
	/* gp_reg0 */
	ret = si2165_readreg8(state, 0x0387, val);
	if (ret < 0)
		return ret;
	ret = si2165_writereg8(state, 0x0387, 0x00);
	if (ret < 0)
		return ret;
	/* dsp_addr_jump */
	ret = si2165_writereg32(state, 0x0348, 0xf4000000);
	if (ret < 0)
		return ret;

	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);

	/* recalc if_freq_shift if IF might has changed */
	fe->ops.tuner_ops.get_if_frequency(fe, &IF);
	ret = si2165_set_if_freq_shift(state, IF);
	if (ret < 0)
		return ret;

	/* boot/wdog status */
	ret = si2165_readreg8(state, 0x0341, val);
	if (ret < 0)
		return ret;
	ret = si2165_writereg8(state, 0x0341, 0x00);
	if (ret < 0)
		return ret;
	/* reset all */
	ret = si2165_writereg8(state, 0x00c0, 0x00);
	if (ret < 0)
		return ret;
	/* gp_reg0 */
	ret = si2165_writereg32(state, 0x0384, 0x00000000);
	if (ret < 0)
		return ret;
	/* start_synchro */
	ret = si2165_writereg8(state, 0x02e0, 0x01);
	if (ret < 0)
		return ret;
	/* boot/wdog status */
	ret = si2165_readreg8(state, 0x0341, val);
	if (ret < 0)
		return ret;

	return 0;
}

static void si2165_release(struct dvb_frontend *fe)
{
	struct si2165_state *state = fe->demodulator_priv;

	dprintk("%s: called\n", __func__);
	kfree(state);
}

static struct dvb_frontend_ops si2165_ops = {
	.info = {
		.name = "Silicon Labs Si2165",
		.caps =	FE_CAN_FEC_1_2 |
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
			FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_GUARD_INTERVAL_AUTO |
			FE_CAN_HIERARCHY_AUTO |
			FE_CAN_MUTE_TS |
			FE_CAN_TRANSMISSION_MODE_AUTO |
			FE_CAN_RECOVER
	},

	.get_tune_settings = si2165_get_tune_settings,

	.init = si2165_init,
	.sleep = si2165_sleep,

	.set_frontend      = si2165_set_parameters,
	.read_status       = si2165_read_status,

	.release = si2165_release,
};

struct dvb_frontend *si2165_attach(const struct si2165_config *config,
				   struct i2c_adapter *i2c)
{
	struct si2165_state *state = NULL;
	int n;
	int io_ret;
	u8 val;

	if (config == NULL || i2c == NULL)
		goto error;

	/* allocate memory for the internal state */
	state = kzalloc(sizeof(struct si2165_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	/* setup the state */
	state->i2c = i2c;
	state->config = *config;

	if (state->config.ref_freq_Hz < 4000000
	    || state->config.ref_freq_Hz > 27000000) {
		dev_err(&state->i2c->dev, "%s: ref_freq of %d Hz not supported by this driver\n",
			 KBUILD_MODNAME, state->config.ref_freq_Hz);
		goto error;
	}

	/* create dvb_frontend */
	memcpy(&state->frontend.ops, &si2165_ops,
		sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;

	/* powerup */
	io_ret = si2165_writereg8(state, 0x0000, state->config.chip_mode);
	if (io_ret < 0)
		goto error;

	io_ret = si2165_readreg8(state, 0x0000, &val);
	if (io_ret < 0)
		goto error;
	if (val != state->config.chip_mode)
		goto error;

	io_ret = si2165_readreg8(state, 0x0023 , &state->revcode);
	if (io_ret < 0)
		goto error;

	io_ret = si2165_readreg8(state, 0x0118, &state->chip_type);
	if (io_ret < 0)
		goto error;

	/* powerdown */
	io_ret = si2165_writereg8(state, 0x0000, SI2165_MODE_OFF);
	if (io_ret < 0)
		goto error;

	dev_info(&state->i2c->dev, "%s: hardware revision 0x%02x, chip type 0x%02x\n",
		 KBUILD_MODNAME, state->revcode, state->chip_type);

	/* It is a guess that register 0x0118 (chip type?) can be used to
	 * differ between si2161, si2163 and si2165
	 * Only si2165 has been tested.
	 */
	if (state->revcode == 0x03 && state->chip_type == 0x07) {
		state->has_dvbt = true;
		state->has_dvbc = true;
	} else {
		dev_err(&state->i2c->dev, "%s: Unsupported chip.\n",
			KBUILD_MODNAME);
		goto error;
	}

	n = 0;
	if (state->has_dvbt) {
		state->frontend.ops.delsys[n++] = SYS_DVBT;
		strlcat(state->frontend.ops.info.name, " DVB-T",
			sizeof(state->frontend.ops.info.name));
	}
	if (state->has_dvbc)
		dev_warn(&state->i2c->dev, "%s: DVB-C is not yet supported.\n",
		       KBUILD_MODNAME);

	return &state->frontend;

error:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(si2165_attach);

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Turn on/off frontend debugging (default:off).");

MODULE_DESCRIPTION("Silicon Labs Si2165 DVB-C/-T Demodulator driver");
MODULE_AUTHOR("Matthias Schwarzott <zzam@gentoo.org>");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(SI2165_FIRMWARE);
