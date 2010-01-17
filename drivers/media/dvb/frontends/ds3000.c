/*
    Montage Technology DS3000/TS2020 - DVBS/S2 Demodulator/Tuner driver
    Copyright (C) 2009 Konstantin Dimitrov <kosio.dimitrov@gmail.com>

    Copyright (C) 2009 TurboSight.com

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

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/firmware.h>

#include "dvb_frontend.h"
#include "ds3000.h"

static int debug;

#define dprintk(args...) \
	do { \
		if (debug) \
			printk(args); \
	} while (0)

/* as of March 2009 current DS3000 firmware version is 1.78 */
/* DS3000 FW v1.78 MD5: a32d17910c4f370073f9346e71d34b80 */
#define DS3000_DEFAULT_FIRMWARE "dvb-fe-ds3000.fw"

#define DS3000_SAMPLE_RATE 96000 /* in kHz */
#define DS3000_XTAL_FREQ   27000 /* in kHz */

/* Register values to initialise the demod in DVB-S mode */
static u8 ds3000_dvbs_init_tab[] = {
	0x23, 0x05,
	0x08, 0x03,
	0x0c, 0x00,
	0x21, 0x54,
	0x25, 0x82,
	0x27, 0x31,
	0x30, 0x08,
	0x31, 0x40,
	0x32, 0x32,
	0x33, 0x35,
	0x35, 0xff,
	0x3a, 0x00,
	0x37, 0x10,
	0x38, 0x10,
	0x39, 0x02,
	0x42, 0x60,
	0x4a, 0x40,
	0x4b, 0x04,
	0x4d, 0x91,
	0x5d, 0xc8,
	0x50, 0x77,
	0x51, 0x77,
	0x52, 0x36,
	0x53, 0x36,
	0x56, 0x01,
	0x63, 0x43,
	0x64, 0x30,
	0x65, 0x40,
	0x68, 0x26,
	0x69, 0x4c,
	0x70, 0x20,
	0x71, 0x70,
	0x72, 0x04,
	0x73, 0x00,
	0x70, 0x40,
	0x71, 0x70,
	0x72, 0x04,
	0x73, 0x00,
	0x70, 0x60,
	0x71, 0x70,
	0x72, 0x04,
	0x73, 0x00,
	0x70, 0x80,
	0x71, 0x70,
	0x72, 0x04,
	0x73, 0x00,
	0x70, 0xa0,
	0x71, 0x70,
	0x72, 0x04,
	0x73, 0x00,
	0x70, 0x1f,
	0x76, 0x00,
	0x77, 0xd1,
	0x78, 0x0c,
	0x79, 0x80,
	0x7f, 0x04,
	0x7c, 0x00,
	0x80, 0x86,
	0x81, 0xa6,
	0x85, 0x04,
	0xcd, 0xf4,
	0x90, 0x33,
	0xa0, 0x44,
	0xc0, 0x18,
	0xc3, 0x10,
	0xc4, 0x08,
	0xc5, 0x80,
	0xc6, 0x80,
	0xc7, 0x0a,
	0xc8, 0x1a,
	0xc9, 0x80,
	0xfe, 0x92,
	0xe0, 0xf8,
	0xe6, 0x8b,
	0xd0, 0x40,
	0xf8, 0x20,
	0xfa, 0x0f,
	0xfd, 0x20,
	0xad, 0x20,
	0xae, 0x07,
	0xb8, 0x00,
};

/* Register values to initialise the demod in DVB-S2 mode */
static u8 ds3000_dvbs2_init_tab[] = {
	0x23, 0x0f,
	0x08, 0x07,
	0x0c, 0x00,
	0x21, 0x54,
	0x25, 0x82,
	0x27, 0x31,
	0x30, 0x08,
	0x31, 0x32,
	0x32, 0x32,
	0x33, 0x35,
	0x35, 0xff,
	0x3a, 0x00,
	0x37, 0x10,
	0x38, 0x10,
	0x39, 0x02,
	0x42, 0x60,
	0x4a, 0x80,
	0x4b, 0x04,
	0x4d, 0x81,
	0x5d, 0x88,
	0x50, 0x36,
	0x51, 0x36,
	0x52, 0x36,
	0x53, 0x36,
	0x63, 0x60,
	0x64, 0x10,
	0x65, 0x10,
	0x68, 0x04,
	0x69, 0x29,
	0x70, 0x20,
	0x71, 0x70,
	0x72, 0x04,
	0x73, 0x00,
	0x70, 0x40,
	0x71, 0x70,
	0x72, 0x04,
	0x73, 0x00,
	0x70, 0x60,
	0x71, 0x70,
	0x72, 0x04,
	0x73, 0x00,
	0x70, 0x80,
	0x71, 0x70,
	0x72, 0x04,
	0x73, 0x00,
	0x70, 0xa0,
	0x71, 0x70,
	0x72, 0x04,
	0x73, 0x00,
	0x70, 0x1f,
	0xa0, 0x44,
	0xc0, 0x08,
	0xc1, 0x10,
	0xc2, 0x08,
	0xc3, 0x10,
	0xc4, 0x08,
	0xc5, 0xf0,
	0xc6, 0xf0,
	0xc7, 0x0a,
	0xc8, 0x1a,
	0xc9, 0x80,
	0xca, 0x23,
	0xcb, 0x24,
	0xce, 0x74,
	0x90, 0x03,
	0x76, 0x80,
	0x77, 0x42,
	0x78, 0x0a,
	0x79, 0x80,
	0xad, 0x40,
	0xae, 0x07,
	0x7f, 0xd4,
	0x7c, 0x00,
	0x80, 0xa8,
	0x81, 0xda,
	0x7c, 0x01,
	0x80, 0xda,
	0x81, 0xec,
	0x7c, 0x02,
	0x80, 0xca,
	0x81, 0xeb,
	0x7c, 0x03,
	0x80, 0xba,
	0x81, 0xdb,
	0x85, 0x08,
	0x86, 0x00,
	0x87, 0x02,
	0x89, 0x80,
	0x8b, 0x44,
	0x8c, 0xaa,
	0x8a, 0x10,
	0xba, 0x00,
	0xf5, 0x04,
	0xfe, 0x44,
	0xd2, 0x32,
	0xb8, 0x00,
};

/* DS3000 doesn't need some parameters as input and auto-detects them */
/* save input from the application of those parameters */
struct ds3000_tuning {
	u32 frequency;
	u32 symbol_rate;
	fe_spectral_inversion_t inversion;
	enum fe_code_rate fec;

	/* input values */
	u8 inversion_val;
	fe_modulation_t delivery;
	u8 rolloff;
};

struct ds3000_state {
	struct i2c_adapter *i2c;
	const struct ds3000_config *config;

	struct dvb_frontend frontend;

	struct ds3000_tuning dcur;
	struct ds3000_tuning dnxt;

	u8 skip_fw_load;

	/* previous uncorrected block counter for DVB-S2 */
	u16 prevUCBS2;
};

static int ds3000_writereg(struct ds3000_state *state, int reg, int data)
{
	u8 buf[] = { reg, data };
	struct i2c_msg msg = { .addr = state->config->demod_address,
		.flags = 0, .buf = buf, .len = 2 };
	int err;

	dprintk("%s: write reg 0x%02x, value 0x%02x\n", __func__, reg, data);

	err = i2c_transfer(state->i2c, &msg, 1);
	if (err != 1) {
		printk(KERN_ERR "%s: writereg error(err == %i, reg == 0x%02x,"
			 " value == 0x%02x)\n", __func__, err, reg, data);
		return -EREMOTEIO;
	}

	return 0;
}

static int ds3000_tuner_writereg(struct ds3000_state *state, int reg, int data)
{
	u8 buf[] = { reg, data };
	struct i2c_msg msg = { .addr = 0x60,
		.flags = 0, .buf = buf, .len = 2 };
	int err;

	dprintk("%s: write reg 0x%02x, value 0x%02x\n", __func__, reg, data);

	ds3000_writereg(state, 0x03, 0x11);
	err = i2c_transfer(state->i2c, &msg, 1);
	if (err != 1) {
		printk("%s: writereg error(err == %i, reg == 0x%02x,"
			 " value == 0x%02x)\n", __func__, err, reg, data);
		return -EREMOTEIO;
	}

	return 0;
}

/* I2C write for 8k firmware load */
static int ds3000_writeFW(struct ds3000_state *state, int reg,
				const u8 *data, u16 len)
{
	int i, ret = -EREMOTEIO;
	struct i2c_msg msg;
	u8 *buf;

	buf = kmalloc(3, GFP_KERNEL);
	if (buf == NULL) {
		printk(KERN_ERR "Unable to kmalloc\n");
		ret = -ENOMEM;
		goto error;
	}

	*(buf) = reg;

	msg.addr = state->config->demod_address;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = 3;

	for (i = 0; i < len; i += 2) {
		memcpy(buf + 1, data + i, 2);

		dprintk("%s: write reg 0x%02x, len = %d\n", __func__, reg, len);

		ret = i2c_transfer(state->i2c, &msg, 1);
		if (ret != 1) {
			printk(KERN_ERR "%s: write error(err == %i, "
				"reg == 0x%02x\n", __func__, ret, reg);
			ret = -EREMOTEIO;
		}
	}

error:
	kfree(buf);

	return ret;
}

static int ds3000_readreg(struct ds3000_state *state, u8 reg)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{
			.addr = state->config->demod_address,
			.flags = 0,
			.buf = b0,
			.len = 1
		}, {
			.addr = state->config->demod_address,
			.flags = I2C_M_RD,
			.buf = b1,
			.len = 1
		}
	};

	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) {
		printk(KERN_ERR "%s: reg=0x%x(error=%d)\n", __func__, reg, ret);
		return ret;
	}

	dprintk("%s: read reg 0x%02x, value 0x%02x\n", __func__, reg, b1[0]);

	return b1[0];
}

static int ds3000_tuner_readreg(struct ds3000_state *state, u8 reg)
{
	int ret;
	u8 b0[] = { reg };
	u8 b1[] = { 0 };
	struct i2c_msg msg[] = {
		{
			.addr = 0x60,
			.flags = 0,
			.buf = b0,
			.len = 1
		}, {
			.addr = 0x60,
			.flags = I2C_M_RD,
			.buf = b1,
			.len = 1
		}
	};

	ds3000_writereg(state, 0x03, 0x12);
	ret = i2c_transfer(state->i2c, msg, 2);

	if (ret != 2) {
		printk(KERN_ERR "%s: reg=0x%x(error=%d)\n", __func__, reg, ret);
		return ret;
	}

	dprintk("%s: read reg 0x%02x, value 0x%02x\n", __func__, reg, b1[0]);

	return b1[0];
}

static int ds3000_set_inversion(struct ds3000_state *state,
					fe_spectral_inversion_t inversion)
{
	dprintk("%s(%d)\n", __func__, inversion);

	switch (inversion) {
	case INVERSION_OFF:
	case INVERSION_ON:
	case INVERSION_AUTO:
		break;
	default:
		return -EINVAL;
	}

	state->dnxt.inversion = inversion;

	return 0;
}

static int ds3000_set_symbolrate(struct ds3000_state *state, u32 rate)
{
	int ret = 0;

	dprintk("%s()\n", __func__);

	dprintk("%s() symbol_rate = %d\n", __func__, state->dnxt.symbol_rate);

	/*  check if symbol rate is within limits */
	if ((state->dnxt.symbol_rate >
				state->frontend.ops.info.symbol_rate_max) ||
	    (state->dnxt.symbol_rate <
				state->frontend.ops.info.symbol_rate_min))
		ret = -EOPNOTSUPP;

	state->dnxt.symbol_rate = rate;

	return ret;
}

static int ds3000_load_firmware(struct dvb_frontend *fe,
					const struct firmware *fw);

static int ds3000_firmware_ondemand(struct dvb_frontend *fe)
{
	struct ds3000_state *state = fe->demodulator_priv;
	const struct firmware *fw;
	int ret = 0;

	dprintk("%s()\n", __func__);

	if (ds3000_readreg(state, 0xb2) <= 0)
		return ret;

	if (state->skip_fw_load)
		return 0;
	/* Load firmware */
	/* request the firmware, this will block until someone uploads it */
	printk(KERN_INFO "%s: Waiting for firmware upload (%s)...\n", __func__,
				DS3000_DEFAULT_FIRMWARE);
	ret = request_firmware(&fw, DS3000_DEFAULT_FIRMWARE,
				state->i2c->dev.parent);
	printk(KERN_INFO "%s: Waiting for firmware upload(2)...\n", __func__);
	if (ret) {
		printk(KERN_ERR "%s: No firmware uploaded (timeout or file not "
				"found?)\n", __func__);
		return ret;
	}

	/* Make sure we don't recurse back through here during loading */
	state->skip_fw_load = 1;

	ret = ds3000_load_firmware(fe, fw);
	if (ret)
		printk("%s: Writing firmware to device failed\n", __func__);

	release_firmware(fw);

	dprintk("%s: Firmware upload %s\n", __func__,
			ret == 0 ? "complete" : "failed");

	/* Ensure firmware is always loaded if required */
	state->skip_fw_load = 0;

	return ret;
}

static int ds3000_load_firmware(struct dvb_frontend *fe,
					const struct firmware *fw)
{
	struct ds3000_state *state = fe->demodulator_priv;

	dprintk("%s\n", __func__);
	dprintk("Firmware is %zu bytes (%02x %02x .. %02x %02x)\n",
			fw->size,
			fw->data[0],
			fw->data[1],
			fw->data[fw->size - 2],
			fw->data[fw->size - 1]);

	/* Begin the firmware load process */
	ds3000_writereg(state, 0xb2, 0x01);
	/* write the entire firmware */
	ds3000_writeFW(state, 0xb0, fw->data, fw->size);
	ds3000_writereg(state, 0xb2, 0x00);

	return 0;
}

static void ds3000_dump_registers(struct dvb_frontend *fe)
{
	struct ds3000_state *state = fe->demodulator_priv;
	int x, y, reg = 0, val;

	for (y = 0; y < 16; y++) {
		dprintk("%s: %02x: ", __func__, y);
		for (x = 0; x < 16; x++) {
			reg = (y << 4) + x;
			val = ds3000_readreg(state, reg);
			if (x != 15)
				dprintk("%02x ",  val);
			else
				dprintk("%02x\n", val);
		}
	}
	dprintk("%s: -- DS3000 DUMP DONE --\n", __func__);
}

static int ds3000_read_status(struct dvb_frontend *fe, fe_status_t* status)
{
	struct ds3000_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int lock;

	*status = 0;

	switch (c->delivery_system) {
	case SYS_DVBS:
		lock = ds3000_readreg(state, 0xd1);
		if ((lock & 0x07) == 0x07)
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI | FE_HAS_SYNC |
				FE_HAS_LOCK;

		break;
	case SYS_DVBS2:
		lock = ds3000_readreg(state, 0x0d);
		if ((lock & 0x8f) == 0x8f)
			*status = FE_HAS_SIGNAL | FE_HAS_CARRIER |
				FE_HAS_VITERBI | FE_HAS_SYNC |
				FE_HAS_LOCK;

		break;
	default:
		return 1;
	}

	dprintk("%s: status = 0x%02x\n", __func__, lock);

	return 0;
}

#define FE_IS_TUNED (FE_HAS_SIGNAL + FE_HAS_LOCK)
static int ds3000_is_tuned(struct dvb_frontend *fe)
{
	fe_status_t tunerstat;

	ds3000_read_status(fe, &tunerstat);

	return ((tunerstat & FE_IS_TUNED) == FE_IS_TUNED);
}

/* read DS3000 BER value */
static int ds3000_read_ber(struct dvb_frontend *fe, u32* ber)
{
	struct ds3000_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u8 data;
	u32 ber_reading, lpdc_frames;

	dprintk("%s()\n", __func__);

	switch (c->delivery_system) {
	case SYS_DVBS:
		/* set the number of bytes checked during
		BER estimation */
		ds3000_writereg(state, 0xf9, 0x04);
		/* read BER estimation status */
		data = ds3000_readreg(state, 0xf8);
		/* check if BER estimation is ready */
		if ((data & 0x10) == 0) {
			/* this is the number of error bits,
			to calculate the bit error rate
			divide to 8388608 */
			*ber = (ds3000_readreg(state, 0xf7) << 8) |
				ds3000_readreg(state, 0xf6);
			/* start counting error bits */
			/* need to be set twice
			otherwise it fails sometimes */
			data |= 0x10;
			ds3000_writereg(state, 0xf8, data);
			ds3000_writereg(state, 0xf8, data);
		} else
			/* used to indicate that BER estimation
			is not ready, i.e. BER is unknown */
			*ber = 0xffffffff;
		break;
	case SYS_DVBS2:
		/* read the number of LPDC decoded frames */
		lpdc_frames = (ds3000_readreg(state, 0xd7) << 16) |
				(ds3000_readreg(state, 0xd6) << 8) |
				ds3000_readreg(state, 0xd5);
		/* read the number of packets with bad CRC */
		ber_reading = (ds3000_readreg(state, 0xf8) << 8) |
				ds3000_readreg(state, 0xf7);
		if (lpdc_frames > 750) {
			/* clear LPDC frame counters */
			ds3000_writereg(state, 0xd1, 0x01);
			/* clear bad packets counter */
			ds3000_writereg(state, 0xf9, 0x01);
			/* enable bad packets counter */
			ds3000_writereg(state, 0xf9, 0x00);
			/* enable LPDC frame counters */
			ds3000_writereg(state, 0xd1, 0x00);
			*ber = ber_reading;
		} else
			/* used to indicate that BER estimation is not ready,
			i.e. BER is unknown */
			*ber = 0xffffffff;
		break;
	default:
		return 1;
	}

	return 0;
}

/* read TS2020 signal strength */
static int ds3000_read_signal_strength(struct dvb_frontend *fe,
						u16 *signal_strength)
{
	struct ds3000_state *state = fe->demodulator_priv;
	u16 sig_reading, sig_strength;
	u8 rfgain, bbgain;

	dprintk("%s()\n", __func__);

	rfgain = ds3000_tuner_readreg(state, 0x3d) & 0x1f;
	bbgain = ds3000_tuner_readreg(state, 0x21) & 0x1f;

	if (rfgain > 15)
		rfgain = 15;
	if (bbgain > 13)
		bbgain = 13;

	sig_reading = rfgain * 2 + bbgain * 3;

	sig_strength = 40 + (64 - sig_reading) * 50 / 64 ;

	/* cook the value to be suitable for szap-s2 human readable output */
	*signal_strength = sig_strength * 1000;

	dprintk("%s: raw / cooked = 0x%04x / 0x%04x\n", __func__,
			sig_reading, *signal_strength);

	return 0;
}

/* calculate DS3000 snr value in dB */
static int ds3000_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct ds3000_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u8 snr_reading, snr_value;
	u32 dvbs2_signal_reading, dvbs2_noise_reading, tmp;
	static const u16 dvbs_snr_tab[] = { /* 20 x Table (rounded up) */
		0x0000, 0x1b13, 0x2aea, 0x3627, 0x3ede, 0x45fe, 0x4c03,
		0x513a, 0x55d4, 0x59f2, 0x5dab, 0x6111, 0x6431, 0x6717,
		0x69c9, 0x6c4e, 0x6eac, 0x70e8, 0x7304, 0x7505
	};
	static const u16 dvbs2_snr_tab[] = { /* 80 x Table (rounded up) */
		0x0000, 0x0bc2, 0x12a3, 0x1785, 0x1b4e, 0x1e65, 0x2103,
		0x2347, 0x2546, 0x2710, 0x28ae, 0x2a28, 0x2b83, 0x2cc5,
		0x2df1, 0x2f09, 0x3010, 0x3109, 0x31f4, 0x32d2, 0x33a6,
		0x3470, 0x3531, 0x35ea, 0x369b, 0x3746, 0x37ea, 0x3888,
		0x3920, 0x39b3, 0x3a42, 0x3acc, 0x3b51, 0x3bd3, 0x3c51,
		0x3ccb, 0x3d42, 0x3db6, 0x3e27, 0x3e95, 0x3f00, 0x3f68,
		0x3fcf, 0x4033, 0x4094, 0x40f4, 0x4151, 0x41ac, 0x4206,
		0x425e, 0x42b4, 0x4308, 0x435b, 0x43ac, 0x43fc, 0x444a,
		0x4497, 0x44e2, 0x452d, 0x4576, 0x45bd, 0x4604, 0x4649,
		0x468e, 0x46d1, 0x4713, 0x4755, 0x4795, 0x47d4, 0x4813,
		0x4851, 0x488d, 0x48c9, 0x4904, 0x493f, 0x4978, 0x49b1,
		0x49e9, 0x4a20, 0x4a57
	};

	dprintk("%s()\n", __func__);

	switch (c->delivery_system) {
	case SYS_DVBS:
		snr_reading = ds3000_readreg(state, 0xff);
		snr_reading /= 8;
		if (snr_reading == 0)
			*snr = 0x0000;
		else {
			if (snr_reading > 20)
				snr_reading = 20;
			snr_value = dvbs_snr_tab[snr_reading - 1] * 10 / 23026;
			/* cook the value to be suitable for szap-s2
			human readable output */
			*snr = snr_value * 8 * 655;
		}
		dprintk("%s: raw / cooked = 0x%02x / 0x%04x\n", __func__,
				snr_reading, *snr);
		break;
	case SYS_DVBS2:
		dvbs2_noise_reading = (ds3000_readreg(state, 0x8c) & 0x3f) +
				(ds3000_readreg(state, 0x8d) << 4);
		dvbs2_signal_reading = ds3000_readreg(state, 0x8e);
		tmp = dvbs2_signal_reading * dvbs2_signal_reading >> 1;
		if (dvbs2_signal_reading == 0) {
			*snr = 0x0000;
			return 0;
		}
		if (dvbs2_noise_reading == 0) {
			snr_value = 0x0013;
			/* cook the value to be suitable for szap-s2
			human readable output */
			*snr = 0xffff;
			return 0;
		}
		if (tmp > dvbs2_noise_reading) {
			snr_reading = tmp / dvbs2_noise_reading;
			if (snr_reading > 80)
				snr_reading = 80;
			snr_value = dvbs2_snr_tab[snr_reading - 1] / 1000;
			/* cook the value to be suitable for szap-s2
			human readable output */
			*snr = snr_value * 5 * 655;
		} else {
			snr_reading = dvbs2_noise_reading / tmp;
			if (snr_reading > 80)
				snr_reading = 80;
			*snr = -(dvbs2_snr_tab[snr_reading] / 1000);
		}
		dprintk("%s: raw / cooked = 0x%02x / 0x%04x\n", __func__,
				snr_reading, *snr);
		break;
	default:
		return 1;
	}

	return 0;
}

/* read DS3000 uncorrected blocks */
static int ds3000_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct ds3000_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u8 data;
	u16 _ucblocks;

	dprintk("%s()\n", __func__);

	switch (c->delivery_system) {
	case SYS_DVBS:
		*ucblocks = (ds3000_readreg(state, 0xf5) << 8) |
				ds3000_readreg(state, 0xf4);
		data = ds3000_readreg(state, 0xf8);
		/* clear packet counters */
		data &= ~0x20;
		ds3000_writereg(state, 0xf8, data);
		/* enable packet counters */
		data |= 0x20;
		ds3000_writereg(state, 0xf8, data);
		break;
	case SYS_DVBS2:
		_ucblocks = (ds3000_readreg(state, 0xe2) << 8) |
				ds3000_readreg(state, 0xe1);
		if (_ucblocks > state->prevUCBS2)
			*ucblocks = _ucblocks - state->prevUCBS2;
		else
			*ucblocks = state->prevUCBS2 - _ucblocks;
		state->prevUCBS2 = _ucblocks;
		break;
	default:
		return 1;
	}

	return 0;
}

/* Overwrite the current tuning params, we are about to tune */
static void ds3000_clone_params(struct dvb_frontend *fe)
{
	struct ds3000_state *state = fe->demodulator_priv;
	memcpy(&state->dcur, &state->dnxt, sizeof(state->dcur));
}

static int ds3000_set_tone(struct dvb_frontend *fe, fe_sec_tone_mode_t tone)
{
	struct ds3000_state *state = fe->demodulator_priv;
	u8 data;

	dprintk("%s(%d)\n", __func__, tone);
	if ((tone != SEC_TONE_ON) && (tone != SEC_TONE_OFF)) {
		printk(KERN_ERR "%s: Invalid, tone=%d\n", __func__, tone);
		return -EINVAL;
	}

	data = ds3000_readreg(state, 0xa2);
	data &= ~0xc0;
	ds3000_writereg(state, 0xa2, data);

	switch (tone) {
	case SEC_TONE_ON:
		dprintk("%s: setting tone on\n", __func__);
		data = ds3000_readreg(state, 0xa1);
		data &= ~0x43;
		data |= 0x04;
		ds3000_writereg(state, 0xa1, data);
		break;
	case SEC_TONE_OFF:
		dprintk("%s: setting tone off\n", __func__);
		data = ds3000_readreg(state, 0xa2);
		data |= 0x80;
		ds3000_writereg(state, 0xa2, data);
		break;
	}

	return 0;
}

static int ds3000_send_diseqc_msg(struct dvb_frontend *fe,
				struct dvb_diseqc_master_cmd *d)
{
	struct ds3000_state *state = fe->demodulator_priv;
	int i;
	u8 data;

	/* Dump DiSEqC message */
	dprintk("%s(", __func__);
	for (i = 0 ; i < d->msg_len;) {
		dprintk("0x%02x", d->msg[i]);
		if (++i < d->msg_len)
			dprintk(", ");
	}

	/* enable DiSEqC message send pin */
	data = ds3000_readreg(state, 0xa2);
	data &= ~0xc0;
	ds3000_writereg(state, 0xa2, data);

	/* DiSEqC message */
	for (i = 0; i < d->msg_len; i++)
		ds3000_writereg(state, 0xa3 + i, d->msg[i]);

	data = ds3000_readreg(state, 0xa1);
	/* clear DiSEqC message length and status,
	enable DiSEqC message send */
	data &= ~0xf8;
	/* set DiSEqC mode, modulation active during 33 pulses,
	set DiSEqC message length */
	data |= ((d->msg_len - 1) << 3) | 0x07;
	ds3000_writereg(state, 0xa1, data);

	/* wait up to 150ms for DiSEqC transmission to complete */
	for (i = 0; i < 15; i++) {
		data = ds3000_readreg(state, 0xa1);
		if ((data & 0x40) == 0)
			break;
		msleep(10);
	}

	/* DiSEqC timeout after 150ms */
	if (i == 15) {
		data = ds3000_readreg(state, 0xa1);
		data &= ~0x80;
		data |= 0x40;
		ds3000_writereg(state, 0xa1, data);

		data = ds3000_readreg(state, 0xa2);
		data &= ~0xc0;
		data |= 0x80;
		ds3000_writereg(state, 0xa2, data);

		return 1;
	}

	data = ds3000_readreg(state, 0xa2);
	data &= ~0xc0;
	data |= 0x80;
	ds3000_writereg(state, 0xa2, data);

	return 0;
}

/* Send DiSEqC burst */
static int ds3000_diseqc_send_burst(struct dvb_frontend *fe,
					fe_sec_mini_cmd_t burst)
{
	struct ds3000_state *state = fe->demodulator_priv;
	int i;
	u8 data;

	dprintk("%s()\n", __func__);

	data = ds3000_readreg(state, 0xa2);
	data &= ~0xc0;
	ds3000_writereg(state, 0xa2, data);

	/* DiSEqC burst */
	if (burst == SEC_MINI_A)
		/* Unmodulated tone burst */
		ds3000_writereg(state, 0xa1, 0x02);
	else if (burst == SEC_MINI_B)
		/* Modulated tone burst */
		ds3000_writereg(state, 0xa1, 0x01);
	else
		return -EINVAL;

	msleep(13);
	for (i = 0; i < 5; i++) {
		data = ds3000_readreg(state, 0xa1);
		if ((data & 0x40) == 0)
			break;
		msleep(1);
	}

	if (i == 5) {
		data = ds3000_readreg(state, 0xa1);
		data &= ~0x80;
		data |= 0x40;
		ds3000_writereg(state, 0xa1, data);

		data = ds3000_readreg(state, 0xa2);
		data &= ~0xc0;
		data |= 0x80;
		ds3000_writereg(state, 0xa2, data);

		return 1;
	}

	data = ds3000_readreg(state, 0xa2);
	data &= ~0xc0;
	data |= 0x80;
	ds3000_writereg(state, 0xa2, data);

	return 0;
}

static void ds3000_release(struct dvb_frontend *fe)
{
	struct ds3000_state *state = fe->demodulator_priv;
	dprintk("%s\n", __func__);
	kfree(state);
}

static struct dvb_frontend_ops ds3000_ops;

struct dvb_frontend *ds3000_attach(const struct ds3000_config *config,
				    struct i2c_adapter *i2c)
{
	struct ds3000_state *state = NULL;
	int ret;

	dprintk("%s\n", __func__);

	/* allocate memory for the internal state */
	state = kmalloc(sizeof(struct ds3000_state), GFP_KERNEL);
	if (state == NULL) {
		printk(KERN_ERR "Unable to kmalloc\n");
		goto error2;
	}

	/* setup the state */
	memset(state, 0, sizeof(struct ds3000_state));

	state->config = config;
	state->i2c = i2c;
	state->prevUCBS2 = 0;

	/* check if the demod is present */
	ret = ds3000_readreg(state, 0x00) & 0xfe;
	if (ret != 0xe0) {
		printk(KERN_ERR "Invalid probe, probably not a DS3000\n");
		goto error3;
	}

	printk(KERN_INFO "DS3000 chip version: %d.%d attached.\n",
			ds3000_readreg(state, 0x02),
			ds3000_readreg(state, 0x01));

	memcpy(&state->frontend.ops, &ds3000_ops,
			sizeof(struct dvb_frontend_ops));
	state->frontend.demodulator_priv = state;
	return &state->frontend;

error3:
	kfree(state);
error2:
	return NULL;
}
EXPORT_SYMBOL(ds3000_attach);

static int ds3000_set_property(struct dvb_frontend *fe,
	struct dtv_property *tvp)
{
	dprintk("%s(..)\n", __func__);
	return 0;
}

static int ds3000_get_property(struct dvb_frontend *fe,
	struct dtv_property *tvp)
{
	dprintk("%s(..)\n", __func__);
	return 0;
}

static int ds3000_tune(struct dvb_frontend *fe,
				struct dvb_frontend_parameters *p)
{
	struct ds3000_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	int ret = 0, retune, i;
	u8 status, mlpf, mlpf_new, mlpf_max, mlpf_min, nlpf;
	u16 value, ndiv;
	u32 f3db;

	dprintk("%s() ", __func__);

	/* Load the firmware if required */
	ret = ds3000_firmware_ondemand(fe);
	if (ret != 0) {
		printk(KERN_ERR "%s: Unable initialise the firmware\n",
								__func__);
		return ret;
	}

	state->dnxt.delivery = c->modulation;
	state->dnxt.frequency = c->frequency;
	state->dnxt.rolloff = 2; /* fixme */
	state->dnxt.fec = c->fec_inner;

	ret = ds3000_set_inversion(state, p->inversion);
	if (ret !=  0)
		return ret;

	ret = ds3000_set_symbolrate(state, c->symbol_rate);
	if (ret !=  0)
		return ret;

	/* discard the 'current' tuning parameters and prepare to tune */
	ds3000_clone_params(fe);

	retune = 1;	/* try 1 times */
	dprintk("%s:   retune = %d\n", __func__, retune);
	dprintk("%s:   frequency   = %d\n", __func__, state->dcur.frequency);
	dprintk("%s:   symbol_rate = %d\n", __func__, state->dcur.symbol_rate);
	dprintk("%s:   FEC	 = %d \n", __func__,
		state->dcur.fec);
	dprintk("%s:   Inversion   = %d\n", __func__, state->dcur.inversion);

	do {
		/* Reset status register */
		status = 0;
		/* Tune */
		/* TS2020 init */
		ds3000_tuner_writereg(state, 0x42, 0x73);
		ds3000_tuner_writereg(state, 0x05, 0x01);
		ds3000_tuner_writereg(state, 0x62, 0xf5);
		/* unknown */
		ds3000_tuner_writereg(state, 0x07, 0x02);
		ds3000_tuner_writereg(state, 0x10, 0x00);
		ds3000_tuner_writereg(state, 0x60, 0x79);
		ds3000_tuner_writereg(state, 0x08, 0x01);
		ds3000_tuner_writereg(state, 0x00, 0x01);
		/* calculate and set freq divider */
		if (state->dcur.frequency < 1146000) {
			ds3000_tuner_writereg(state, 0x10, 0x11);
			ndiv = ((state->dcur.frequency * (6 + 8) * 4) +
					(DS3000_XTAL_FREQ / 2)) /
					DS3000_XTAL_FREQ - 1024;
		} else {
			ds3000_tuner_writereg(state, 0x10, 0x01);
			ndiv = ((state->dcur.frequency * (6 + 8) * 2) +
					(DS3000_XTAL_FREQ / 2)) /
					DS3000_XTAL_FREQ - 1024;
		}

		ds3000_tuner_writereg(state, 0x01, (ndiv & 0x0f00) >> 8);
		ds3000_tuner_writereg(state, 0x02, ndiv & 0x00ff);

		/* set pll */
		ds3000_tuner_writereg(state, 0x03, 0x06);
		ds3000_tuner_writereg(state, 0x51, 0x0f);
		ds3000_tuner_writereg(state, 0x51, 0x1f);
		ds3000_tuner_writereg(state, 0x50, 0x10);
		ds3000_tuner_writereg(state, 0x50, 0x00);
		msleep(5);

		/* unknown */
		ds3000_tuner_writereg(state, 0x51, 0x17);
		ds3000_tuner_writereg(state, 0x51, 0x1f);
		ds3000_tuner_writereg(state, 0x50, 0x08);
		ds3000_tuner_writereg(state, 0x50, 0x00);
		msleep(5);

		value = ds3000_tuner_readreg(state, 0x3d);
		value &= 0x0f;
		if ((value > 4) && (value < 15)) {
			value -= 3;
			if (value < 4)
				value = 4;
			value = ((value << 3) | 0x01) & 0x79;
		}

		ds3000_tuner_writereg(state, 0x60, value);
		ds3000_tuner_writereg(state, 0x51, 0x17);
		ds3000_tuner_writereg(state, 0x51, 0x1f);
		ds3000_tuner_writereg(state, 0x50, 0x08);
		ds3000_tuner_writereg(state, 0x50, 0x00);

		/* set low-pass filter period */
		ds3000_tuner_writereg(state, 0x04, 0x2e);
		ds3000_tuner_writereg(state, 0x51, 0x1b);
		ds3000_tuner_writereg(state, 0x51, 0x1f);
		ds3000_tuner_writereg(state, 0x50, 0x04);
		ds3000_tuner_writereg(state, 0x50, 0x00);
		msleep(5);

		f3db = ((state->dcur.symbol_rate / 1000) << 2) / 5 + 2000;
		if ((state->dcur.symbol_rate / 1000) < 5000)
			f3db += 3000;
		if (f3db < 7000)
			f3db = 7000;
		if (f3db > 40000)
			f3db = 40000;

		/* set low-pass filter baseband */
		value = ds3000_tuner_readreg(state, 0x26);
		mlpf = 0x2e * 207 / ((value << 1) + 151);
		mlpf_max = mlpf * 135 / 100;
		mlpf_min = mlpf * 78 / 100;
		if (mlpf_max > 63)
			mlpf_max = 63;

		/* rounded to the closest integer */
		nlpf = ((mlpf * f3db * 1000) + (2766 * DS3000_XTAL_FREQ / 2))
				/ (2766 * DS3000_XTAL_FREQ);
		if (nlpf > 23)
			nlpf = 23;
		if (nlpf < 1)
			nlpf = 1;

		/* rounded to the closest integer */
		mlpf_new = ((DS3000_XTAL_FREQ * nlpf * 2766) +
				(1000 * f3db / 2)) / (1000 * f3db);

		if (mlpf_new < mlpf_min) {
			nlpf++;
			mlpf_new = ((DS3000_XTAL_FREQ * nlpf * 2766) +
					(1000 * f3db / 2)) / (1000 * f3db);
		}

		if (mlpf_new > mlpf_max)
			mlpf_new = mlpf_max;

		ds3000_tuner_writereg(state, 0x04, mlpf_new);
		ds3000_tuner_writereg(state, 0x06, nlpf);
		ds3000_tuner_writereg(state, 0x51, 0x1b);
		ds3000_tuner_writereg(state, 0x51, 0x1f);
		ds3000_tuner_writereg(state, 0x50, 0x04);
		ds3000_tuner_writereg(state, 0x50, 0x00);
		msleep(5);

		/* unknown */
		ds3000_tuner_writereg(state, 0x51, 0x1e);
		ds3000_tuner_writereg(state, 0x51, 0x1f);
		ds3000_tuner_writereg(state, 0x50, 0x01);
		ds3000_tuner_writereg(state, 0x50, 0x00);
		msleep(60);

		/* ds3000 global reset */
		ds3000_writereg(state, 0x07, 0x80);
		ds3000_writereg(state, 0x07, 0x00);
		/* ds3000 build-in uC reset */
		ds3000_writereg(state, 0xb2, 0x01);
		/* ds3000 software reset */
		ds3000_writereg(state, 0x00, 0x01);

		switch (c->delivery_system) {
		case SYS_DVBS:
			/* initialise the demod in DVB-S mode */
			for (i = 0; i < sizeof(ds3000_dvbs_init_tab); i += 2)
				ds3000_writereg(state,
					ds3000_dvbs_init_tab[i],
					ds3000_dvbs_init_tab[i + 1]);
			value = ds3000_readreg(state, 0xfe);
			value &= 0xc0;
			value |= 0x1b;
			ds3000_writereg(state, 0xfe, value);
			break;
		case SYS_DVBS2:
			/* initialise the demod in DVB-S2 mode */
			for (i = 0; i < sizeof(ds3000_dvbs2_init_tab); i += 2)
				ds3000_writereg(state,
					ds3000_dvbs2_init_tab[i],
					ds3000_dvbs2_init_tab[i + 1]);
			ds3000_writereg(state, 0xfe, 0x54);
			break;
		default:
			return 1;
		}

		/* enable 27MHz clock output */
		ds3000_writereg(state, 0x29, 0x80);
		/* enable ac coupling */
		ds3000_writereg(state, 0x25, 0x8a);

		/* enhance symbol rate performance */
		if ((state->dcur.symbol_rate / 1000) <= 5000) {
			value = 29777 / (state->dcur.symbol_rate / 1000) + 1;
			if (value % 2 != 0)
				value++;
			ds3000_writereg(state, 0xc3, 0x0d);
			ds3000_writereg(state, 0xc8, value);
			ds3000_writereg(state, 0xc4, 0x10);
			ds3000_writereg(state, 0xc7, 0x0e);
		} else if ((state->dcur.symbol_rate / 1000) <= 10000) {
			value = 92166 / (state->dcur.symbol_rate / 1000) + 1;
			if (value % 2 != 0)
				value++;
			ds3000_writereg(state, 0xc3, 0x07);
			ds3000_writereg(state, 0xc8, value);
			ds3000_writereg(state, 0xc4, 0x09);
			ds3000_writereg(state, 0xc7, 0x12);
		} else if ((state->dcur.symbol_rate / 1000) <= 20000) {
			value = 64516 / (state->dcur.symbol_rate / 1000) + 1;
			ds3000_writereg(state, 0xc3, value);
			ds3000_writereg(state, 0xc8, 0x0e);
			ds3000_writereg(state, 0xc4, 0x07);
			ds3000_writereg(state, 0xc7, 0x18);
		} else {
			value = 129032 / (state->dcur.symbol_rate / 1000) + 1;
			ds3000_writereg(state, 0xc3, value);
			ds3000_writereg(state, 0xc8, 0x0a);
			ds3000_writereg(state, 0xc4, 0x05);
			ds3000_writereg(state, 0xc7, 0x24);
		}

		/* normalized symbol rate rounded to the closest integer */
		value = (((state->dcur.symbol_rate / 1000) << 16) +
				(DS3000_SAMPLE_RATE / 2)) / DS3000_SAMPLE_RATE;
		ds3000_writereg(state, 0x61, value & 0x00ff);
		ds3000_writereg(state, 0x62, (value & 0xff00) >> 8);

		/* co-channel interference cancellation disabled */
		ds3000_writereg(state, 0x56, 0x00);

		/* equalizer disabled */
		ds3000_writereg(state, 0x76, 0x00);

		/*ds3000_writereg(state, 0x08, 0x03);
		ds3000_writereg(state, 0xfd, 0x22);
		ds3000_writereg(state, 0x08, 0x07);
		ds3000_writereg(state, 0xfd, 0x42);
		ds3000_writereg(state, 0x08, 0x07);*/

		/* ds3000 out of software reset */
		ds3000_writereg(state, 0x00, 0x00);
		/* start ds3000 build-in uC */
		ds3000_writereg(state, 0xb2, 0x00);

		/* TODO: calculate and set carrier offset */

		/* wait before retrying */
		for (i = 0; i < 30 ; i++) {
			if (ds3000_is_tuned(fe)) {
				dprintk("%s: Tuned\n", __func__);
				ds3000_dump_registers(fe);
				goto tuned;
			}
			msleep(1);
		}

		dprintk("%s: Not tuned\n", __func__);
		ds3000_dump_registers(fe);

	} while (--retune);

tuned:
	return ret;
}

static enum dvbfe_algo ds3000_get_algo(struct dvb_frontend *fe)
{
	dprintk("%s()\n", __func__);
	return DVBFE_ALGO_SW;
}

/*
 * Initialise or wake up device
 *
 * Power config will reset and load initial firmware if required
 */
static int ds3000_initfe(struct dvb_frontend *fe)
{
	dprintk("%s()\n", __func__);
	return 0;
}

/* Put device to sleep */
static int ds3000_sleep(struct dvb_frontend *fe)
{
	dprintk("%s()\n", __func__);
	return 0;
}

static struct dvb_frontend_ops ds3000_ops = {

	.info = {
		.name = "Montage Technology DS3000/TS2020",
		.type = FE_QPSK,
		.frequency_min = 950000,
		.frequency_max = 2150000,
		.frequency_stepsize = 1011, /* kHz for QPSK frontends */
		.frequency_tolerance = 5000,
		.symbol_rate_min = 1000000,
		.symbol_rate_max = 45000000,
		.caps = FE_CAN_INVERSION_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_4_5 | FE_CAN_FEC_5_6 | FE_CAN_FEC_6_7 |
			FE_CAN_FEC_7_8 | FE_CAN_FEC_AUTO |
			FE_CAN_2G_MODULATION |
			FE_CAN_QPSK | FE_CAN_RECOVER
	},

	.release = ds3000_release,

	.init = ds3000_initfe,
	.sleep = ds3000_sleep,
	.read_status = ds3000_read_status,
	.read_ber = ds3000_read_ber,
	.read_signal_strength = ds3000_read_signal_strength,
	.read_snr = ds3000_read_snr,
	.read_ucblocks = ds3000_read_ucblocks,
	.set_tone = ds3000_set_tone,
	.diseqc_send_master_cmd = ds3000_send_diseqc_msg,
	.diseqc_send_burst = ds3000_diseqc_send_burst,
	.get_frontend_algo = ds3000_get_algo,

	.set_property = ds3000_set_property,
	.get_property = ds3000_get_property,
	.set_frontend = ds3000_tune,
};

module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Activates frontend debugging (default:0)");

MODULE_DESCRIPTION("DVB Frontend module for Montage Technology "
			"DS3000/TS2020 hardware");
MODULE_AUTHOR("Konstantin Dimitrov");
MODULE_LICENSE("GPL");
