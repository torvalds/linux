// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the MaxLinear MxL5xx family of tuners/demods
 *
 * Copyright (C) 2014-2015 Ralph Metzler <rjkm@metzlerbros.de>
 *                         Marcus Metzler <mocm@metzlerbros.de>
 *                         developed for Digital Devices GmbH
 *
 * based on code:
 * Copyright (c) 2011-2013 MaxLinear, Inc. All rights reserved
 * which was released under GPL V2
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <asm/div64.h>
#include <asm/unaligned.h>

#include <media/dvb_frontend.h>
#include "mxl5xx.h"
#include "mxl5xx_regs.h"
#include "mxl5xx_defs.h"

#define BYTE0(v) ((v >>  0) & 0xff)
#define BYTE1(v) ((v >>  8) & 0xff)
#define BYTE2(v) ((v >> 16) & 0xff)
#define BYTE3(v) ((v >> 24) & 0xff)

static LIST_HEAD(mxllist);

struct mxl_base {
	struct list_head     mxllist;
	struct list_head     mxls;

	u8                   adr;
	struct i2c_adapter  *i2c;

	u32                  count;
	u32                  type;
	u32                  sku_type;
	u32                  chipversion;
	u32                  clock;
	u32                  fwversion;

	u8                  *ts_map;
	u8                   can_clkout;
	u8                   chan_bond;
	u8                   demod_num;
	u8                   tuner_num;

	unsigned long        next_tune;

	struct mutex         i2c_lock;
	struct mutex         status_lock;
	struct mutex         tune_lock;

	u8                   buf[MXL_HYDRA_OEM_MAX_CMD_BUFF_LEN];

	u32                  cmd_size;
	u8                   cmd_data[MAX_CMD_DATA];
};

struct mxl {
	struct list_head     mxl;

	struct mxl_base     *base;
	struct dvb_frontend  fe;
	struct device       *i2cdev;
	u32                  demod;
	u32                  tuner;
	u32                  tuner_in_use;
	u8                   xbar[3];

	unsigned long        tune_time;
};

static void convert_endian(u8 flag, u32 size, u8 *d)
{
	u32 i;

	if (!flag)
		return;
	for (i = 0; i < (size & ~3); i += 4) {
		d[i + 0] ^= d[i + 3];
		d[i + 3] ^= d[i + 0];
		d[i + 0] ^= d[i + 3];

		d[i + 1] ^= d[i + 2];
		d[i + 2] ^= d[i + 1];
		d[i + 1] ^= d[i + 2];
	}

	switch (size & 3) {
	case 0:
	case 1:
		/* do nothing */
		break;
	case 2:
		d[i + 0] ^= d[i + 1];
		d[i + 1] ^= d[i + 0];
		d[i + 0] ^= d[i + 1];
		break;

	case 3:
		d[i + 0] ^= d[i + 2];
		d[i + 2] ^= d[i + 0];
		d[i + 0] ^= d[i + 2];
		break;
	}

}

static int i2c_write(struct i2c_adapter *adap, u8 adr,
			    u8 *data, u32 len)
{
	struct i2c_msg msg = {.addr = adr, .flags = 0,
			      .buf = data, .len = len};

	return (i2c_transfer(adap, &msg, 1) == 1) ? 0 : -1;
}

static int i2c_read(struct i2c_adapter *adap, u8 adr,
			   u8 *data, u32 len)
{
	struct i2c_msg msg = {.addr = adr, .flags = I2C_M_RD,
			      .buf = data, .len = len};

	return (i2c_transfer(adap, &msg, 1) == 1) ? 0 : -1;
}

static int i2cread(struct mxl *state, u8 *data, int len)
{
	return i2c_read(state->base->i2c, state->base->adr, data, len);
}

static int i2cwrite(struct mxl *state, u8 *data, int len)
{
	return i2c_write(state->base->i2c, state->base->adr, data, len);
}

static int read_register_unlocked(struct mxl *state, u32 reg, u32 *val)
{
	int stat;
	u8 data[MXL_HYDRA_REG_SIZE_IN_BYTES + MXL_HYDRA_I2C_HDR_SIZE] = {
		MXL_HYDRA_PLID_REG_READ, 0x04,
		GET_BYTE(reg, 0), GET_BYTE(reg, 1),
		GET_BYTE(reg, 2), GET_BYTE(reg, 3),
	};

	stat = i2cwrite(state, data,
			MXL_HYDRA_REG_SIZE_IN_BYTES + MXL_HYDRA_I2C_HDR_SIZE);
	if (stat)
		dev_err(state->i2cdev, "i2c read error 1\n");
	if (!stat)
		stat = i2cread(state, (u8 *) val,
			       MXL_HYDRA_REG_SIZE_IN_BYTES);
	le32_to_cpus(val);
	if (stat)
		dev_err(state->i2cdev, "i2c read error 2\n");
	return stat;
}

#define DMA_I2C_INTERRUPT_ADDR 0x8000011C
#define DMA_INTR_PROT_WR_CMP 0x08

static int send_command(struct mxl *state, u32 size, u8 *buf)
{
	int stat;
	u32 val, count = 10;

	mutex_lock(&state->base->i2c_lock);
	if (state->base->fwversion > 0x02010109)  {
		read_register_unlocked(state, DMA_I2C_INTERRUPT_ADDR, &val);
		if (DMA_INTR_PROT_WR_CMP & val)
			dev_info(state->i2cdev, "%s busy\n", __func__);
		while ((DMA_INTR_PROT_WR_CMP & val) && --count) {
			mutex_unlock(&state->base->i2c_lock);
			usleep_range(1000, 2000);
			mutex_lock(&state->base->i2c_lock);
			read_register_unlocked(state, DMA_I2C_INTERRUPT_ADDR,
					       &val);
		}
		if (!count) {
			dev_info(state->i2cdev, "%s busy\n", __func__);
			mutex_unlock(&state->base->i2c_lock);
			return -EBUSY;
		}
	}
	stat = i2cwrite(state, buf, size);
	mutex_unlock(&state->base->i2c_lock);
	return stat;
}

static int write_register(struct mxl *state, u32 reg, u32 val)
{
	int stat;
	u8 data[MXL_HYDRA_REG_WRITE_LEN] = {
		MXL_HYDRA_PLID_REG_WRITE, 0x08,
		BYTE0(reg), BYTE1(reg), BYTE2(reg), BYTE3(reg),
		BYTE0(val), BYTE1(val), BYTE2(val), BYTE3(val),
	};
	mutex_lock(&state->base->i2c_lock);
	stat = i2cwrite(state, data, sizeof(data));
	mutex_unlock(&state->base->i2c_lock);
	if (stat)
		dev_err(state->i2cdev, "i2c write error\n");
	return stat;
}

static int write_firmware_block(struct mxl *state,
				u32 reg, u32 size, u8 *reg_data_ptr)
{
	int stat;
	u8 *buf = state->base->buf;

	mutex_lock(&state->base->i2c_lock);
	buf[0] = MXL_HYDRA_PLID_REG_WRITE;
	buf[1] = size + 4;
	buf[2] = GET_BYTE(reg, 0);
	buf[3] = GET_BYTE(reg, 1);
	buf[4] = GET_BYTE(reg, 2);
	buf[5] = GET_BYTE(reg, 3);
	memcpy(&buf[6], reg_data_ptr, size);
	stat = i2cwrite(state, buf,
			MXL_HYDRA_I2C_HDR_SIZE +
			MXL_HYDRA_REG_SIZE_IN_BYTES + size);
	mutex_unlock(&state->base->i2c_lock);
	if (stat)
		dev_err(state->i2cdev, "fw block write failed\n");
	return stat;
}

static int read_register(struct mxl *state, u32 reg, u32 *val)
{
	int stat;
	u8 data[MXL_HYDRA_REG_SIZE_IN_BYTES + MXL_HYDRA_I2C_HDR_SIZE] = {
		MXL_HYDRA_PLID_REG_READ, 0x04,
		GET_BYTE(reg, 0), GET_BYTE(reg, 1),
		GET_BYTE(reg, 2), GET_BYTE(reg, 3),
	};

	mutex_lock(&state->base->i2c_lock);
	stat = i2cwrite(state, data,
			MXL_HYDRA_REG_SIZE_IN_BYTES + MXL_HYDRA_I2C_HDR_SIZE);
	if (stat)
		dev_err(state->i2cdev, "i2c read error 1\n");
	if (!stat)
		stat = i2cread(state, (u8 *) val,
			       MXL_HYDRA_REG_SIZE_IN_BYTES);
	mutex_unlock(&state->base->i2c_lock);
	le32_to_cpus(val);
	if (stat)
		dev_err(state->i2cdev, "i2c read error 2\n");
	return stat;
}

static int read_register_block(struct mxl *state, u32 reg, u32 size, u8 *data)
{
	int stat;
	u8 *buf = state->base->buf;

	mutex_lock(&state->base->i2c_lock);

	buf[0] = MXL_HYDRA_PLID_REG_READ;
	buf[1] = size + 4;
	buf[2] = GET_BYTE(reg, 0);
	buf[3] = GET_BYTE(reg, 1);
	buf[4] = GET_BYTE(reg, 2);
	buf[5] = GET_BYTE(reg, 3);
	stat = i2cwrite(state, buf,
			MXL_HYDRA_I2C_HDR_SIZE + MXL_HYDRA_REG_SIZE_IN_BYTES);
	if (!stat) {
		stat = i2cread(state, data, size);
		convert_endian(MXL_ENABLE_BIG_ENDIAN, size, data);
	}
	mutex_unlock(&state->base->i2c_lock);
	return stat;
}

static int read_by_mnemonic(struct mxl *state,
			    u32 reg, u8 lsbloc, u8 numofbits, u32 *val)
{
	u32 data = 0, mask = 0;
	int stat;

	stat = read_register(state, reg, &data);
	if (stat)
		return stat;
	mask = MXL_GET_REG_MASK_32(lsbloc, numofbits);
	data &= mask;
	data >>= lsbloc;
	*val = data;
	return 0;
}


static int update_by_mnemonic(struct mxl *state,
			      u32 reg, u8 lsbloc, u8 numofbits, u32 val)
{
	u32 data, mask;
	int stat;

	stat = read_register(state, reg, &data);
	if (stat)
		return stat;
	mask = MXL_GET_REG_MASK_32(lsbloc, numofbits);
	data = (data & ~mask) | ((val << lsbloc) & mask);
	stat = write_register(state, reg, data);
	return stat;
}

static int firmware_is_alive(struct mxl *state)
{
	u32 hb0, hb1;

	if (read_register(state, HYDRA_HEAR_BEAT, &hb0))
		return 0;
	msleep(20);
	if (read_register(state, HYDRA_HEAR_BEAT, &hb1))
		return 0;
	if (hb1 == hb0)
		return 0;
	return 1;
}

static int init(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	/* init fe stats */
	p->strength.len = 1;
	p->strength.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->cnr.len = 1;
	p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->pre_bit_error.len = 1;
	p->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->pre_bit_count.len = 1;
	p->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_error.len = 1;
	p->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_count.len = 1;
	p->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	return 0;
}

static void release(struct dvb_frontend *fe)
{
	struct mxl *state = fe->demodulator_priv;

	list_del(&state->mxl);
	/* Release one frontend, two more shall take its place! */
	state->base->count--;
	if (state->base->count == 0) {
		list_del(&state->base->mxllist);
		kfree(state->base);
	}
	kfree(state);
}

static enum dvbfe_algo get_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static u32 gold2root(u32 gold)
{
	u32 x, g, tmp = gold;

	if (tmp >= 0x3ffff)
		tmp = 0;
	for (g = 0, x = 1; g < tmp; g++)
		x = (((x ^ (x >> 7)) & 1) << 17) | (x >> 1);
	return x;
}

static int cfg_scrambler(struct mxl *state, u32 gold)
{
	u32 root;
	u8 buf[26] = {
		MXL_HYDRA_PLID_CMD_WRITE, 24,
		0, MXL_HYDRA_DEMOD_SCRAMBLE_CODE_CMD, 0, 0,
		state->demod, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 1, 0, 0, 0,
	};

	root = gold2root(gold);

	buf[25] = (root >> 24) & 0xff;
	buf[24] = (root >> 16) & 0xff;
	buf[23] = (root >> 8) & 0xff;
	buf[22] = root & 0xff;

	return send_command(state, sizeof(buf), buf);
}

static int cfg_demod_abort_tune(struct mxl *state)
{
	struct MXL_HYDRA_DEMOD_ABORT_TUNE_T abort_tune_cmd;
	u8 cmd_size = sizeof(abort_tune_cmd);
	u8 cmd_buff[MXL_HYDRA_OEM_MAX_CMD_BUFF_LEN];

	abort_tune_cmd.demod_id = state->demod;
	BUILD_HYDRA_CMD(MXL_HYDRA_ABORT_TUNE_CMD, MXL_CMD_WRITE,
			cmd_size, &abort_tune_cmd, cmd_buff);
	return send_command(state, cmd_size + MXL_HYDRA_CMD_HEADER_SIZE,
			    &cmd_buff[0]);
}

static int send_master_cmd(struct dvb_frontend *fe,
			   struct dvb_diseqc_master_cmd *cmd)
{
	/*struct mxl *state = fe->demodulator_priv;*/

	return 0; /*CfgDemodAbortTune(state);*/
}

static int set_parameters(struct dvb_frontend *fe)
{
	struct mxl *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct MXL_HYDRA_DEMOD_PARAM_T demod_chan_cfg;
	u8 cmd_size = sizeof(demod_chan_cfg);
	u8 cmd_buff[MXL_HYDRA_OEM_MAX_CMD_BUFF_LEN];
	u32 srange = 10;
	int stat;

	if (p->frequency < 950000 || p->frequency > 2150000)
		return -EINVAL;
	if (p->symbol_rate < 1000000 || p->symbol_rate > 45000000)
		return -EINVAL;

	/* CfgDemodAbortTune(state); */

	switch (p->delivery_system) {
	case SYS_DSS:
		demod_chan_cfg.standard = MXL_HYDRA_DSS;
		demod_chan_cfg.roll_off = MXL_HYDRA_ROLLOFF_AUTO;
		break;
	case SYS_DVBS:
		srange = p->symbol_rate / 1000000;
		if (srange > 10)
			srange = 10;
		demod_chan_cfg.standard = MXL_HYDRA_DVBS;
		demod_chan_cfg.roll_off = MXL_HYDRA_ROLLOFF_0_35;
		demod_chan_cfg.modulation_scheme = MXL_HYDRA_MOD_QPSK;
		demod_chan_cfg.pilots = MXL_HYDRA_PILOTS_OFF;
		break;
	case SYS_DVBS2:
		demod_chan_cfg.standard = MXL_HYDRA_DVBS2;
		demod_chan_cfg.roll_off = MXL_HYDRA_ROLLOFF_AUTO;
		demod_chan_cfg.modulation_scheme = MXL_HYDRA_MOD_AUTO;
		demod_chan_cfg.pilots = MXL_HYDRA_PILOTS_AUTO;
		cfg_scrambler(state, p->scrambling_sequence_index);
		break;
	default:
		return -EINVAL;
	}
	demod_chan_cfg.tuner_index = state->tuner;
	demod_chan_cfg.demod_index = state->demod;
	demod_chan_cfg.frequency_in_hz = p->frequency * 1000;
	demod_chan_cfg.symbol_rate_in_hz = p->symbol_rate;
	demod_chan_cfg.max_carrier_offset_in_mhz = srange;
	demod_chan_cfg.spectrum_inversion = MXL_HYDRA_SPECTRUM_AUTO;
	demod_chan_cfg.fec_code_rate = MXL_HYDRA_FEC_AUTO;

	mutex_lock(&state->base->tune_lock);
	if (time_after(jiffies + msecs_to_jiffies(200),
		       state->base->next_tune))
		while (time_before(jiffies, state->base->next_tune))
			usleep_range(10000, 11000);
	state->base->next_tune = jiffies + msecs_to_jiffies(100);
	state->tuner_in_use = state->tuner;
	BUILD_HYDRA_CMD(MXL_HYDRA_DEMOD_SET_PARAM_CMD, MXL_CMD_WRITE,
			cmd_size, &demod_chan_cfg, cmd_buff);
	stat = send_command(state, cmd_size + MXL_HYDRA_CMD_HEADER_SIZE,
			    &cmd_buff[0]);
	mutex_unlock(&state->base->tune_lock);
	return stat;
}

static int enable_tuner(struct mxl *state, u32 tuner, u32 enable);

static int sleep(struct dvb_frontend *fe)
{
	struct mxl *state = fe->demodulator_priv;
	struct mxl *p;

	cfg_demod_abort_tune(state);
	if (state->tuner_in_use != 0xffffffff) {
		mutex_lock(&state->base->tune_lock);
		state->tuner_in_use = 0xffffffff;
		list_for_each_entry(p, &state->base->mxls, mxl) {
			if (p->tuner_in_use == state->tuner)
				break;
		}
		if (&p->mxl == &state->base->mxls)
			enable_tuner(state, state->tuner, 0);
		mutex_unlock(&state->base->tune_lock);
	}
	return 0;
}

static int read_snr(struct dvb_frontend *fe)
{
	struct mxl *state = fe->demodulator_priv;
	int stat;
	u32 reg_data = 0;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	mutex_lock(&state->base->status_lock);
	HYDRA_DEMOD_STATUS_LOCK(state, state->demod);
	stat = read_register(state, (HYDRA_DMD_SNR_ADDR_OFFSET +
				     HYDRA_DMD_STATUS_OFFSET(state->demod)),
			     &reg_data);
	HYDRA_DEMOD_STATUS_UNLOCK(state, state->demod);
	mutex_unlock(&state->base->status_lock);

	p->cnr.stat[0].scale = FE_SCALE_DECIBEL;
	p->cnr.stat[0].svalue = (s16)reg_data * 10;

	return stat;
}

static int read_ber(struct dvb_frontend *fe)
{
	struct mxl *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 reg[8];

	mutex_lock(&state->base->status_lock);
	HYDRA_DEMOD_STATUS_LOCK(state, state->demod);
	read_register_block(state,
		(HYDRA_DMD_DVBS_1ST_CORR_RS_ERRORS_ADDR_OFFSET +
		 HYDRA_DMD_STATUS_OFFSET(state->demod)),
		(4 * sizeof(u32)),
		(u8 *) &reg[0]);
	HYDRA_DEMOD_STATUS_UNLOCK(state, state->demod);

	switch (p->delivery_system) {
	case SYS_DSS:
	case SYS_DVBS:
		p->pre_bit_error.stat[0].scale = FE_SCALE_COUNTER;
		p->pre_bit_error.stat[0].uvalue = reg[2];
		p->pre_bit_count.stat[0].scale = FE_SCALE_COUNTER;
		p->pre_bit_count.stat[0].uvalue = reg[3];
		break;
	default:
		break;
	}

	read_register_block(state,
		(HYDRA_DMD_DVBS2_CRC_ERRORS_ADDR_OFFSET +
		 HYDRA_DMD_STATUS_OFFSET(state->demod)),
		(7 * sizeof(u32)),
		(u8 *) &reg[0]);

	switch (p->delivery_system) {
	case SYS_DSS:
	case SYS_DVBS:
		p->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
		p->post_bit_error.stat[0].uvalue = reg[5];
		p->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
		p->post_bit_count.stat[0].uvalue = reg[6];
		break;
	case SYS_DVBS2:
		p->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
		p->post_bit_error.stat[0].uvalue = reg[1];
		p->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
		p->post_bit_count.stat[0].uvalue = reg[2];
		break;
	default:
		break;
	}

	mutex_unlock(&state->base->status_lock);

	return 0;
}

static int read_signal_strength(struct dvb_frontend *fe)
{
	struct mxl *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	int stat;
	u32 reg_data = 0;

	mutex_lock(&state->base->status_lock);
	HYDRA_DEMOD_STATUS_LOCK(state, state->demod);
	stat = read_register(state, (HYDRA_DMD_STATUS_INPUT_POWER_ADDR +
				     HYDRA_DMD_STATUS_OFFSET(state->demod)),
			     &reg_data);
	HYDRA_DEMOD_STATUS_UNLOCK(state, state->demod);
	mutex_unlock(&state->base->status_lock);

	p->strength.stat[0].scale = FE_SCALE_DECIBEL;
	p->strength.stat[0].svalue = (s16) reg_data * 10; /* fix scale */

	return stat;
}

static int read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct mxl *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 reg_data = 0;

	mutex_lock(&state->base->status_lock);
	HYDRA_DEMOD_STATUS_LOCK(state, state->demod);
	read_register(state, (HYDRA_DMD_LOCK_STATUS_ADDR_OFFSET +
			     HYDRA_DMD_STATUS_OFFSET(state->demod)),
			     &reg_data);
	HYDRA_DEMOD_STATUS_UNLOCK(state, state->demod);
	mutex_unlock(&state->base->status_lock);

	*status = (reg_data == 1) ? 0x1f : 0;

	/* signal statistics */

	/* signal strength is always available */
	read_signal_strength(fe);

	if (*status & FE_HAS_CARRIER)
		read_snr(fe);
	else
		p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	if (*status & FE_HAS_SYNC)
		read_ber(fe);
	else {
		p->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		p->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		p->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		p->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	return 0;
}

static int tune(struct dvb_frontend *fe, bool re_tune,
		unsigned int mode_flags,
		unsigned int *delay, enum fe_status *status)
{
	struct mxl *state = fe->demodulator_priv;
	int r = 0;

	*delay = HZ / 2;
	if (re_tune) {
		r = set_parameters(fe);
		if (r)
			return r;
		state->tune_time = jiffies;
	}

	return read_status(fe, status);
}

static enum fe_code_rate conv_fec(enum MXL_HYDRA_FEC_E fec)
{
	enum fe_code_rate fec2fec[11] = {
		FEC_NONE, FEC_1_2, FEC_3_5, FEC_2_3,
		FEC_3_4, FEC_4_5, FEC_5_6, FEC_6_7,
		FEC_7_8, FEC_8_9, FEC_9_10
	};

	if (fec > MXL_HYDRA_FEC_9_10)
		return FEC_NONE;
	return fec2fec[fec];
}

static int get_frontend(struct dvb_frontend *fe,
			struct dtv_frontend_properties *p)
{
	struct mxl *state = fe->demodulator_priv;
	u32 reg_data[MXL_DEMOD_CHAN_PARAMS_BUFF_SIZE];
	u32 freq;

	mutex_lock(&state->base->status_lock);
	HYDRA_DEMOD_STATUS_LOCK(state, state->demod);
	read_register_block(state,
		(HYDRA_DMD_STANDARD_ADDR_OFFSET +
		HYDRA_DMD_STATUS_OFFSET(state->demod)),
		(MXL_DEMOD_CHAN_PARAMS_BUFF_SIZE * 4), /* 25 * 4 bytes */
		(u8 *) &reg_data[0]);
	/* read demod channel parameters */
	read_register_block(state,
		(HYDRA_DMD_STATUS_CENTER_FREQ_IN_KHZ_ADDR +
		HYDRA_DMD_STATUS_OFFSET(state->demod)),
		(4), /* 4 bytes */
		(u8 *) &freq);
	HYDRA_DEMOD_STATUS_UNLOCK(state, state->demod);
	mutex_unlock(&state->base->status_lock);

	dev_dbg(state->i2cdev, "freq=%u delsys=%u srate=%u\n",
		freq * 1000, reg_data[DMD_STANDARD_ADDR],
		reg_data[DMD_SYMBOL_RATE_ADDR]);
	p->symbol_rate = reg_data[DMD_SYMBOL_RATE_ADDR];
	p->frequency = freq;
	/*
	 * p->delivery_system =
	 *	(MXL_HYDRA_BCAST_STD_E) regData[DMD_STANDARD_ADDR];
	 * p->inversion =
	 *	(MXL_HYDRA_SPECTRUM_E) regData[DMD_SPECTRUM_INVERSION_ADDR];
	 * freqSearchRangeKHz =
	 *	(regData[DMD_FREQ_SEARCH_RANGE_IN_KHZ_ADDR]);
	 */

	p->fec_inner = conv_fec(reg_data[DMD_FEC_CODE_RATE_ADDR]);
	switch (p->delivery_system) {
	case SYS_DSS:
		break;
	case SYS_DVBS2:
		switch ((enum MXL_HYDRA_PILOTS_E)
			reg_data[DMD_DVBS2_PILOT_ON_OFF_ADDR]) {
		case MXL_HYDRA_PILOTS_OFF:
			p->pilot = PILOT_OFF;
			break;
		case MXL_HYDRA_PILOTS_ON:
			p->pilot = PILOT_ON;
			break;
		default:
			break;
		}
		fallthrough;
	case SYS_DVBS:
		switch ((enum MXL_HYDRA_MODULATION_E)
			reg_data[DMD_MODULATION_SCHEME_ADDR]) {
		case MXL_HYDRA_MOD_QPSK:
			p->modulation = QPSK;
			break;
		case MXL_HYDRA_MOD_8PSK:
			p->modulation = PSK_8;
			break;
		default:
			break;
		}
		switch ((enum MXL_HYDRA_ROLLOFF_E)
			reg_data[DMD_SPECTRUM_ROLL_OFF_ADDR]) {
		case MXL_HYDRA_ROLLOFF_0_20:
			p->rolloff = ROLLOFF_20;
			break;
		case MXL_HYDRA_ROLLOFF_0_35:
			p->rolloff = ROLLOFF_35;
			break;
		case MXL_HYDRA_ROLLOFF_0_25:
			p->rolloff = ROLLOFF_25;
			break;
		default:
			break;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int set_input(struct dvb_frontend *fe, int input)
{
	struct mxl *state = fe->demodulator_priv;

	state->tuner = input;
	return 0;
}

static const struct dvb_frontend_ops mxl_ops = {
	.delsys = { SYS_DVBS, SYS_DVBS2, SYS_DSS },
	.info = {
		.name			= "MaxLinear MxL5xx DVB-S/S2 tuner-demodulator",
		.frequency_min_hz	=  300 * MHz,
		.frequency_max_hz	= 2350 * MHz,
		.symbol_rate_min	= 1000000,
		.symbol_rate_max	= 45000000,
		.caps			= FE_CAN_INVERSION_AUTO |
					  FE_CAN_FEC_AUTO       |
					  FE_CAN_QPSK           |
					  FE_CAN_2G_MODULATION
	},
	.init				= init,
	.release                        = release,
	.get_frontend_algo              = get_algo,
	.tune                           = tune,
	.read_status			= read_status,
	.sleep				= sleep,
	.get_frontend                   = get_frontend,
	.diseqc_send_master_cmd		= send_master_cmd,
};

static struct mxl_base *match_base(struct i2c_adapter  *i2c, u8 adr)
{
	struct mxl_base *p;

	list_for_each_entry(p, &mxllist, mxllist)
		if (p->i2c == i2c && p->adr == adr)
			return p;
	return NULL;
}

static void cfg_dev_xtal(struct mxl *state, u32 freq, u32 cap, u32 enable)
{
	if (state->base->can_clkout || !enable)
		update_by_mnemonic(state, 0x90200054, 23, 1, enable);

	if (freq == 24000000)
		write_register(state, HYDRA_CRYSTAL_SETTING, 0);
	else
		write_register(state, HYDRA_CRYSTAL_SETTING, 1);

	write_register(state, HYDRA_CRYSTAL_CAP, cap);
}

static u32 get_big_endian(u8 num_of_bits, const u8 buf[])
{
	u32 ret_value = 0;

	switch (num_of_bits) {
	case 24:
		ret_value = (((u32) buf[0]) << 16) |
			(((u32) buf[1]) << 8) | buf[2];
		break;
	case 32:
		ret_value = (((u32) buf[0]) << 24) |
			(((u32) buf[1]) << 16) |
			(((u32) buf[2]) << 8) | buf[3];
		break;
	default:
		break;
	}

	return ret_value;
}

static int write_fw_segment(struct mxl *state,
			    u32 mem_addr, u32 total_size, u8 *data_ptr)
{
	int status;
	u32 data_count = 0;
	u32 size = 0;
	u32 orig_size = 0;
	u8 *w_buf_ptr = NULL;
	u32 block_size = ((MXL_HYDRA_OEM_MAX_BLOCK_WRITE_LENGTH -
			 (MXL_HYDRA_I2C_HDR_SIZE +
			  MXL_HYDRA_REG_SIZE_IN_BYTES)) / 4) * 4;
	u8 w_msg_buffer[MXL_HYDRA_OEM_MAX_BLOCK_WRITE_LENGTH -
		      (MXL_HYDRA_I2C_HDR_SIZE + MXL_HYDRA_REG_SIZE_IN_BYTES)];

	do {
		size = orig_size = (((u32)(data_count + block_size)) > total_size) ?
			(total_size - data_count) : block_size;

		if (orig_size & 3)
			size = (orig_size + 4) & ~3;
		w_buf_ptr = &w_msg_buffer[0];
		memset((void *) w_buf_ptr, 0, size);
		memcpy((void *) w_buf_ptr, (void *) data_ptr, orig_size);
		convert_endian(1, size, w_buf_ptr);
		status  = write_firmware_block(state, mem_addr, size, w_buf_ptr);
		if (status)
			return status;
		data_count += size;
		mem_addr   += size;
		data_ptr   += size;
	} while (data_count < total_size);

	return status;
}

static int do_firmware_download(struct mxl *state, u8 *mbin_buffer_ptr,
				u32 mbin_buffer_size)

{
	int status;
	u32 index = 0;
	u32 seg_length = 0;
	u32 seg_address = 0;
	struct MBIN_FILE_T *mbin_ptr  = (struct MBIN_FILE_T *)mbin_buffer_ptr;
	struct MBIN_SEGMENT_T *segment_ptr;
	enum MXL_BOOL_E xcpu_fw_flag = MXL_FALSE;

	if (mbin_ptr->header.id != MBIN_FILE_HEADER_ID) {
		dev_err(state->i2cdev, "%s: Invalid file header ID (%c)\n",
		       __func__, mbin_ptr->header.id);
		return -EINVAL;
	}
	status = write_register(state, FW_DL_SIGN_ADDR, 0);
	if (status)
		return status;
	segment_ptr = (struct MBIN_SEGMENT_T *) (&mbin_ptr->data[0]);
	for (index = 0; index < mbin_ptr->header.num_segments; index++) {
		if (segment_ptr->header.id != MBIN_SEGMENT_HEADER_ID) {
			dev_err(state->i2cdev, "%s: Invalid segment header ID (%c)\n",
			       __func__, segment_ptr->header.id);
			return -EINVAL;
		}
		seg_length  = get_big_endian(24,
					    &(segment_ptr->header.len24[0]));
		seg_address = get_big_endian(32,
					    &(segment_ptr->header.address[0]));

		if (state->base->type == MXL_HYDRA_DEVICE_568) {
			if ((((seg_address & 0x90760000) == 0x90760000) ||
			     ((seg_address & 0x90740000) == 0x90740000)) &&
			    (xcpu_fw_flag == MXL_FALSE)) {
				update_by_mnemonic(state, 0x8003003C, 0, 1, 1);
				msleep(200);
				write_register(state, 0x90720000, 0);
				usleep_range(10000, 11000);
				xcpu_fw_flag = MXL_TRUE;
			}
			status = write_fw_segment(state, seg_address,
						  seg_length,
						  (u8 *) segment_ptr->data);
		} else {
			if (((seg_address & 0x90760000) != 0x90760000) &&
			    ((seg_address & 0x90740000) != 0x90740000))
				status = write_fw_segment(state, seg_address,
					seg_length, (u8 *) segment_ptr->data);
		}
		if (status)
			return status;
		segment_ptr = (struct MBIN_SEGMENT_T *)
			&(segment_ptr->data[((seg_length + 3) / 4) * 4]);
	}
	return status;
}

static int check_fw(struct mxl *state, u8 *mbin, u32 mbin_len)
{
	struct MBIN_FILE_HEADER_T *fh = (struct MBIN_FILE_HEADER_T *) mbin;
	u32 flen = (fh->image_size24[0] << 16) |
		(fh->image_size24[1] <<  8) | fh->image_size24[2];
	u8 *fw, cs = 0;
	u32 i;

	if (fh->id != 'M' || fh->fmt_version != '1' || flen > 0x3FFF0) {
		dev_info(state->i2cdev, "Invalid FW Header\n");
		return -1;
	}
	fw = mbin + sizeof(struct MBIN_FILE_HEADER_T);
	for (i = 0; i < flen; i += 1)
		cs += fw[i];
	if (cs != fh->image_checksum) {
		dev_info(state->i2cdev, "Invalid FW Checksum\n");
		return -1;
	}
	return 0;
}

static int firmware_download(struct mxl *state, u8 *mbin, u32 mbin_len)
{
	int status;
	u32 reg_data = 0;
	struct MXL_HYDRA_SKU_COMMAND_T dev_sku_cfg;
	u8 cmd_size = sizeof(struct MXL_HYDRA_SKU_COMMAND_T);
	u8 cmd_buff[sizeof(struct MXL_HYDRA_SKU_COMMAND_T) + 6];

	if (check_fw(state, mbin, mbin_len))
		return -1;

	/* put CPU into reset */
	status = update_by_mnemonic(state, 0x8003003C, 0, 1, 0);
	if (status)
		return status;
	usleep_range(1000, 2000);

	/* Reset TX FIFO's, BBAND, XBAR */
	status = write_register(state, HYDRA_RESET_TRANSPORT_FIFO_REG,
				HYDRA_RESET_TRANSPORT_FIFO_DATA);
	if (status)
		return status;
	status = write_register(state, HYDRA_RESET_BBAND_REG,
				HYDRA_RESET_BBAND_DATA);
	if (status)
		return status;
	status = write_register(state, HYDRA_RESET_XBAR_REG,
				HYDRA_RESET_XBAR_DATA);
	if (status)
		return status;

	/* Disable clock to Baseband, Wideband, SerDes,
	 * Alias ext & Transport modules
	 */
	status = write_register(state, HYDRA_MODULES_CLK_2_REG,
				HYDRA_DISABLE_CLK_2);
	if (status)
		return status;
	/* Clear Software & Host interrupt status - (Clear on read) */
	status = read_register(state, HYDRA_PRCM_ROOT_CLK_REG, &reg_data);
	if (status)
		return status;
	status = do_firmware_download(state, mbin, mbin_len);
	if (status)
		return status;

	if (state->base->type == MXL_HYDRA_DEVICE_568) {
		usleep_range(10000, 11000);

		/* bring XCPU out of reset */
		status = write_register(state, 0x90720000, 1);
		if (status)
			return status;
		msleep(500);

		/* Enable XCPU UART message processing in MCPU */
		status = write_register(state, 0x9076B510, 1);
		if (status)
			return status;
	} else {
		/* Bring CPU out of reset */
		status = update_by_mnemonic(state, 0x8003003C, 0, 1, 1);
		if (status)
			return status;
		/* Wait until FW boots */
		msleep(150);
	}

	/* Initialize XPT XBAR */
	status = write_register(state, XPT_DMD0_BASEADDR, 0x76543210);
	if (status)
		return status;

	if (!firmware_is_alive(state))
		return -1;

	dev_info(state->i2cdev, "Hydra FW alive. Hail!\n");

	/* sometimes register values are wrong shortly
	 * after first heart beats
	 */
	msleep(50);

	dev_sku_cfg.sku_type = state->base->sku_type;
	BUILD_HYDRA_CMD(MXL_HYDRA_DEV_CFG_SKU_CMD, MXL_CMD_WRITE,
			cmd_size, &dev_sku_cfg, cmd_buff);
	status = send_command(state, cmd_size + MXL_HYDRA_CMD_HEADER_SIZE,
			      &cmd_buff[0]);

	return status;
}

static int cfg_ts_pad_mux(struct mxl *state, enum MXL_BOOL_E enable_serial_ts)
{
	int status = 0;
	u32 pad_mux_value = 0;

	if (enable_serial_ts == MXL_TRUE) {
		pad_mux_value = 0;
		if ((state->base->type == MXL_HYDRA_DEVICE_541) ||
		    (state->base->type == MXL_HYDRA_DEVICE_541S))
			pad_mux_value = 2;
	} else {
		if ((state->base->type == MXL_HYDRA_DEVICE_581) ||
		    (state->base->type == MXL_HYDRA_DEVICE_581S))
			pad_mux_value = 2;
		else
			pad_mux_value = 3;
	}

	switch (state->base->type) {
	case MXL_HYDRA_DEVICE_561:
	case MXL_HYDRA_DEVICE_581:
	case MXL_HYDRA_DEVICE_541:
	case MXL_HYDRA_DEVICE_541S:
	case MXL_HYDRA_DEVICE_561S:
	case MXL_HYDRA_DEVICE_581S:
		status |= update_by_mnemonic(state, 0x90000170, 24, 3,
					     pad_mux_value);
		status |= update_by_mnemonic(state, 0x90000170, 28, 3,
					     pad_mux_value);
		status |= update_by_mnemonic(state, 0x90000174, 0, 3,
					     pad_mux_value);
		status |= update_by_mnemonic(state, 0x90000174, 4, 3,
					     pad_mux_value);
		status |= update_by_mnemonic(state, 0x90000174, 8, 3,
					     pad_mux_value);
		status |= update_by_mnemonic(state, 0x90000174, 12, 3,
					     pad_mux_value);
		status |= update_by_mnemonic(state, 0x90000174, 16, 3,
					     pad_mux_value);
		status |= update_by_mnemonic(state, 0x90000174, 20, 3,
					     pad_mux_value);
		status |= update_by_mnemonic(state, 0x90000174, 24, 3,
					     pad_mux_value);
		status |= update_by_mnemonic(state, 0x90000174, 28, 3,
					     pad_mux_value);
		status |= update_by_mnemonic(state, 0x90000178, 0, 3,
					     pad_mux_value);
		status |= update_by_mnemonic(state, 0x90000178, 4, 3,
					     pad_mux_value);
		status |= update_by_mnemonic(state, 0x90000178, 8, 3,
					     pad_mux_value);
		break;

	case MXL_HYDRA_DEVICE_544:
	case MXL_HYDRA_DEVICE_542:
		status |= update_by_mnemonic(state, 0x9000016C, 4, 3, 1);
		status |= update_by_mnemonic(state, 0x9000016C, 8, 3, 0);
		status |= update_by_mnemonic(state, 0x9000016C, 12, 3, 0);
		status |= update_by_mnemonic(state, 0x9000016C, 16, 3, 0);
		status |= update_by_mnemonic(state, 0x90000170, 0, 3, 0);
		status |= update_by_mnemonic(state, 0x90000178, 12, 3, 1);
		status |= update_by_mnemonic(state, 0x90000178, 16, 3, 1);
		status |= update_by_mnemonic(state, 0x90000178, 20, 3, 1);
		status |= update_by_mnemonic(state, 0x90000178, 24, 3, 1);
		status |= update_by_mnemonic(state, 0x9000017C, 0, 3, 1);
		status |= update_by_mnemonic(state, 0x9000017C, 4, 3, 1);
		if (enable_serial_ts == MXL_ENABLE) {
			status |= update_by_mnemonic(state,
				0x90000170, 4, 3, 0);
			status |= update_by_mnemonic(state,
				0x90000170, 8, 3, 0);
			status |= update_by_mnemonic(state,
				0x90000170, 12, 3, 0);
			status |= update_by_mnemonic(state,
				0x90000170, 16, 3, 0);
			status |= update_by_mnemonic(state,
				0x90000170, 20, 3, 1);
			status |= update_by_mnemonic(state,
				0x90000170, 24, 3, 1);
			status |= update_by_mnemonic(state,
				0x90000170, 28, 3, 2);
			status |= update_by_mnemonic(state,
				0x90000174, 0, 3, 2);
			status |= update_by_mnemonic(state,
				0x90000174, 4, 3, 2);
			status |= update_by_mnemonic(state,
				0x90000174, 8, 3, 2);
			status |= update_by_mnemonic(state,
				0x90000174, 12, 3, 2);
			status |= update_by_mnemonic(state,
				0x90000174, 16, 3, 2);
			status |= update_by_mnemonic(state,
				0x90000174, 20, 3, 2);
			status |= update_by_mnemonic(state,
				0x90000174, 24, 3, 2);
			status |= update_by_mnemonic(state,
				0x90000174, 28, 3, 2);
			status |= update_by_mnemonic(state,
				0x90000178, 0, 3, 2);
			status |= update_by_mnemonic(state,
				0x90000178, 4, 3, 2);
			status |= update_by_mnemonic(state,
				0x90000178, 8, 3, 2);
		} else {
			status |= update_by_mnemonic(state,
				0x90000170, 4, 3, 3);
			status |= update_by_mnemonic(state,
				0x90000170, 8, 3, 3);
			status |= update_by_mnemonic(state,
				0x90000170, 12, 3, 3);
			status |= update_by_mnemonic(state,
				0x90000170, 16, 3, 3);
			status |= update_by_mnemonic(state,
				0x90000170, 20, 3, 3);
			status |= update_by_mnemonic(state,
				0x90000170, 24, 3, 3);
			status |= update_by_mnemonic(state,
				0x90000170, 28, 3, 3);
			status |= update_by_mnemonic(state,
				0x90000174, 0, 3, 3);
			status |= update_by_mnemonic(state,
				0x90000174, 4, 3, 3);
			status |= update_by_mnemonic(state,
				0x90000174, 8, 3, 3);
			status |= update_by_mnemonic(state,
				0x90000174, 12, 3, 3);
			status |= update_by_mnemonic(state,
				0x90000174, 16, 3, 3);
			status |= update_by_mnemonic(state,
				0x90000174, 20, 3, 1);
			status |= update_by_mnemonic(state,
				0x90000174, 24, 3, 1);
			status |= update_by_mnemonic(state,
				0x90000174, 28, 3, 1);
			status |= update_by_mnemonic(state,
				0x90000178, 0, 3, 1);
			status |= update_by_mnemonic(state,
				0x90000178, 4, 3, 1);
			status |= update_by_mnemonic(state,
				0x90000178, 8, 3, 1);
		}
		break;

	case MXL_HYDRA_DEVICE_568:
		if (enable_serial_ts == MXL_FALSE) {
			status |= update_by_mnemonic(state,
				0x9000016C, 8, 3, 5);
			status |= update_by_mnemonic(state,
				0x9000016C, 12, 3, 5);
			status |= update_by_mnemonic(state,
				0x9000016C, 16, 3, 5);
			status |= update_by_mnemonic(state,
				0x9000016C, 20, 3, 5);
			status |= update_by_mnemonic(state,
				0x9000016C, 24, 3, 5);
			status |= update_by_mnemonic(state,
				0x9000016C, 28, 3, 5);
			status |= update_by_mnemonic(state,
				0x90000170, 0, 3, 5);
			status |= update_by_mnemonic(state,
				0x90000170, 4, 3, 5);
			status |= update_by_mnemonic(state,
				0x90000170, 8, 3, 5);
			status |= update_by_mnemonic(state,
				0x90000170, 12, 3, 5);
			status |= update_by_mnemonic(state,
				0x90000170, 16, 3, 5);
			status |= update_by_mnemonic(state,
				0x90000170, 20, 3, 5);

			status |= update_by_mnemonic(state,
				0x90000170, 24, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000174, 0, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000174, 4, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000174, 8, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000174, 12, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000174, 16, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000174, 20, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000174, 24, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000174, 28, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000178, 0, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000178, 4, 3, pad_mux_value);

			status |= update_by_mnemonic(state,
				0x90000178, 8, 3, 5);
			status |= update_by_mnemonic(state,
				0x90000178, 12, 3, 5);
			status |= update_by_mnemonic(state,
				0x90000178, 16, 3, 5);
			status |= update_by_mnemonic(state,
				0x90000178, 20, 3, 5);
			status |= update_by_mnemonic(state,
				0x90000178, 24, 3, 5);
			status |= update_by_mnemonic(state,
				0x90000178, 28, 3, 5);
			status |= update_by_mnemonic(state,
				0x9000017C, 0, 3, 5);
			status |= update_by_mnemonic(state,
				0x9000017C, 4, 3, 5);
		} else {
			status |= update_by_mnemonic(state,
				0x90000170, 4, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000170, 8, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000170, 12, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000170, 16, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000170, 20, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000170, 24, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000170, 28, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000174, 0, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000174, 4, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000174, 8, 3, pad_mux_value);
			status |= update_by_mnemonic(state,
				0x90000174, 12, 3, pad_mux_value);
		}
		break;


	case MXL_HYDRA_DEVICE_584:
	default:
		status |= update_by_mnemonic(state,
			0x90000170, 4, 3, pad_mux_value);
		status |= update_by_mnemonic(state,
			0x90000170, 8, 3, pad_mux_value);
		status |= update_by_mnemonic(state,
			0x90000170, 12, 3, pad_mux_value);
		status |= update_by_mnemonic(state,
			0x90000170, 16, 3, pad_mux_value);
		status |= update_by_mnemonic(state,
			0x90000170, 20, 3, pad_mux_value);
		status |= update_by_mnemonic(state,
			0x90000170, 24, 3, pad_mux_value);
		status |= update_by_mnemonic(state,
			0x90000170, 28, 3, pad_mux_value);
		status |= update_by_mnemonic(state,
			0x90000174, 0, 3, pad_mux_value);
		status |= update_by_mnemonic(state,
			0x90000174, 4, 3, pad_mux_value);
		status |= update_by_mnemonic(state,
			0x90000174, 8, 3, pad_mux_value);
		status |= update_by_mnemonic(state,
			0x90000174, 12, 3, pad_mux_value);
		break;
	}
	return status;
}

static int set_drive_strength(struct mxl *state,
		enum MXL_HYDRA_TS_DRIVE_STRENGTH_E ts_drive_strength)
{
	int stat = 0;
	u32 val;

	read_register(state, 0x90000194, &val);
	dev_info(state->i2cdev, "DIGIO = %08x\n", val);
	dev_info(state->i2cdev, "set drive_strength = %u\n", ts_drive_strength);


	stat |= update_by_mnemonic(state, 0x90000194, 0, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x90000194, 20, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x90000194, 24, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x90000198, 12, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x90000198, 16, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x90000198, 20, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x90000198, 24, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x9000019C, 0, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x9000019C, 4, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x9000019C, 8, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x9000019C, 24, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x9000019C, 28, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x900001A0, 0, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x900001A0, 4, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x900001A0, 20, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x900001A0, 24, 3, ts_drive_strength);
	stat |= update_by_mnemonic(state, 0x900001A0, 28, 3, ts_drive_strength);

	return stat;
}

static int enable_tuner(struct mxl *state, u32 tuner, u32 enable)
{
	int stat = 0;
	struct MXL_HYDRA_TUNER_CMD ctrl_tuner_cmd;
	u8 cmd_size = sizeof(ctrl_tuner_cmd);
	u8 cmd_buff[MXL_HYDRA_OEM_MAX_CMD_BUFF_LEN];
	u32 val, count = 10;

	ctrl_tuner_cmd.tuner_id = tuner;
	ctrl_tuner_cmd.enable = enable;
	BUILD_HYDRA_CMD(MXL_HYDRA_TUNER_ACTIVATE_CMD, MXL_CMD_WRITE,
			cmd_size, &ctrl_tuner_cmd, cmd_buff);
	stat = send_command(state, cmd_size + MXL_HYDRA_CMD_HEADER_SIZE,
			    &cmd_buff[0]);
	if (stat)
		return stat;
	read_register(state, HYDRA_TUNER_ENABLE_COMPLETE, &val);
	while (--count && ((val >> tuner) & 1) != enable) {
		msleep(20);
		read_register(state, HYDRA_TUNER_ENABLE_COMPLETE, &val);
	}
	if (!count)
		return -1;
	read_register(state, HYDRA_TUNER_ENABLE_COMPLETE, &val);
	dev_dbg(state->i2cdev, "tuner %u ready = %u\n",
		tuner, (val >> tuner) & 1);

	return 0;
}


static int config_ts(struct mxl *state, enum MXL_HYDRA_DEMOD_ID_E demod_id,
		     struct MXL_HYDRA_MPEGOUT_PARAM_T *mpeg_out_param_ptr)
{
	int status = 0;
	u32 nco_count_min = 0;
	u32 clk_type = 0;

	struct MXL_REG_FIELD_T xpt_sync_polarity[MXL_HYDRA_DEMOD_MAX] = {
		{0x90700010, 8, 1}, {0x90700010, 9, 1},
		{0x90700010, 10, 1}, {0x90700010, 11, 1},
		{0x90700010, 12, 1}, {0x90700010, 13, 1},
		{0x90700010, 14, 1}, {0x90700010, 15, 1} };
	struct MXL_REG_FIELD_T xpt_clock_polarity[MXL_HYDRA_DEMOD_MAX] = {
		{0x90700010, 16, 1}, {0x90700010, 17, 1},
		{0x90700010, 18, 1}, {0x90700010, 19, 1},
		{0x90700010, 20, 1}, {0x90700010, 21, 1},
		{0x90700010, 22, 1}, {0x90700010, 23, 1} };
	struct MXL_REG_FIELD_T xpt_valid_polarity[MXL_HYDRA_DEMOD_MAX] = {
		{0x90700014, 0, 1}, {0x90700014, 1, 1},
		{0x90700014, 2, 1}, {0x90700014, 3, 1},
		{0x90700014, 4, 1}, {0x90700014, 5, 1},
		{0x90700014, 6, 1}, {0x90700014, 7, 1} };
	struct MXL_REG_FIELD_T xpt_ts_clock_phase[MXL_HYDRA_DEMOD_MAX] = {
		{0x90700018, 0, 3}, {0x90700018, 4, 3},
		{0x90700018, 8, 3}, {0x90700018, 12, 3},
		{0x90700018, 16, 3}, {0x90700018, 20, 3},
		{0x90700018, 24, 3}, {0x90700018, 28, 3} };
	struct MXL_REG_FIELD_T xpt_lsb_first[MXL_HYDRA_DEMOD_MAX] = {
		{0x9070000C, 16, 1}, {0x9070000C, 17, 1},
		{0x9070000C, 18, 1}, {0x9070000C, 19, 1},
		{0x9070000C, 20, 1}, {0x9070000C, 21, 1},
		{0x9070000C, 22, 1}, {0x9070000C, 23, 1} };
	struct MXL_REG_FIELD_T xpt_sync_byte[MXL_HYDRA_DEMOD_MAX] = {
		{0x90700010, 0, 1}, {0x90700010, 1, 1},
		{0x90700010, 2, 1}, {0x90700010, 3, 1},
		{0x90700010, 4, 1}, {0x90700010, 5, 1},
		{0x90700010, 6, 1}, {0x90700010, 7, 1} };
	struct MXL_REG_FIELD_T xpt_enable_output[MXL_HYDRA_DEMOD_MAX] = {
		{0x9070000C, 0, 1}, {0x9070000C, 1, 1},
		{0x9070000C, 2, 1}, {0x9070000C, 3, 1},
		{0x9070000C, 4, 1}, {0x9070000C, 5, 1},
		{0x9070000C, 6, 1}, {0x9070000C, 7, 1} };
	struct MXL_REG_FIELD_T xpt_err_replace_sync[MXL_HYDRA_DEMOD_MAX] = {
		{0x9070000C, 24, 1}, {0x9070000C, 25, 1},
		{0x9070000C, 26, 1}, {0x9070000C, 27, 1},
		{0x9070000C, 28, 1}, {0x9070000C, 29, 1},
		{0x9070000C, 30, 1}, {0x9070000C, 31, 1} };
	struct MXL_REG_FIELD_T xpt_err_replace_valid[MXL_HYDRA_DEMOD_MAX] = {
		{0x90700014, 8, 1}, {0x90700014, 9, 1},
		{0x90700014, 10, 1}, {0x90700014, 11, 1},
		{0x90700014, 12, 1}, {0x90700014, 13, 1},
		{0x90700014, 14, 1}, {0x90700014, 15, 1} };
	struct MXL_REG_FIELD_T xpt_continuous_clock[MXL_HYDRA_DEMOD_MAX] = {
		{0x907001D4, 0, 1}, {0x907001D4, 1, 1},
		{0x907001D4, 2, 1}, {0x907001D4, 3, 1},
		{0x907001D4, 4, 1}, {0x907001D4, 5, 1},
		{0x907001D4, 6, 1}, {0x907001D4, 7, 1} };
	struct MXL_REG_FIELD_T xpt_nco_clock_rate[MXL_HYDRA_DEMOD_MAX] = {
		{0x90700044, 16, 80}, {0x90700044, 16, 81},
		{0x90700044, 16, 82}, {0x90700044, 16, 83},
		{0x90700044, 16, 84}, {0x90700044, 16, 85},
		{0x90700044, 16, 86}, {0x90700044, 16, 87} };

	demod_id = state->base->ts_map[demod_id];

	if (mpeg_out_param_ptr->enable == MXL_ENABLE) {
		if (mpeg_out_param_ptr->mpeg_mode ==
		    MXL_HYDRA_MPEG_MODE_PARALLEL) {
		} else {
			cfg_ts_pad_mux(state, MXL_TRUE);
			update_by_mnemonic(state,
				0x90700010, 27, 1, MXL_FALSE);
		}
	}

	nco_count_min =
		(u32)(MXL_HYDRA_NCO_CLK / mpeg_out_param_ptr->max_mpeg_clk_rate);

	if (state->base->chipversion >= 2) {
		status |= update_by_mnemonic(state,
			xpt_nco_clock_rate[demod_id].reg_addr, /* Reg Addr */
			xpt_nco_clock_rate[demod_id].lsb_pos, /* LSB pos */
			xpt_nco_clock_rate[demod_id].num_of_bits, /* Num of bits */
			nco_count_min); /* Data */
	} else
		update_by_mnemonic(state, 0x90700044, 16, 8, nco_count_min);

	if (mpeg_out_param_ptr->mpeg_clk_type == MXL_HYDRA_MPEG_CLK_CONTINUOUS)
		clk_type = 1;

	if (mpeg_out_param_ptr->mpeg_mode < MXL_HYDRA_MPEG_MODE_PARALLEL) {
		status |= update_by_mnemonic(state,
			xpt_continuous_clock[demod_id].reg_addr,
			xpt_continuous_clock[demod_id].lsb_pos,
			xpt_continuous_clock[demod_id].num_of_bits,
			clk_type);
	} else
		update_by_mnemonic(state, 0x907001D4, 8, 1, clk_type);

	status |= update_by_mnemonic(state,
		xpt_sync_polarity[demod_id].reg_addr,
		xpt_sync_polarity[demod_id].lsb_pos,
		xpt_sync_polarity[demod_id].num_of_bits,
		mpeg_out_param_ptr->mpeg_sync_pol);

	status |= update_by_mnemonic(state,
		xpt_valid_polarity[demod_id].reg_addr,
		xpt_valid_polarity[demod_id].lsb_pos,
		xpt_valid_polarity[demod_id].num_of_bits,
		mpeg_out_param_ptr->mpeg_valid_pol);

	status |= update_by_mnemonic(state,
		xpt_clock_polarity[demod_id].reg_addr,
		xpt_clock_polarity[demod_id].lsb_pos,
		xpt_clock_polarity[demod_id].num_of_bits,
		mpeg_out_param_ptr->mpeg_clk_pol);

	status |= update_by_mnemonic(state,
		xpt_sync_byte[demod_id].reg_addr,
		xpt_sync_byte[demod_id].lsb_pos,
		xpt_sync_byte[demod_id].num_of_bits,
		mpeg_out_param_ptr->mpeg_sync_pulse_width);

	status |= update_by_mnemonic(state,
		xpt_ts_clock_phase[demod_id].reg_addr,
		xpt_ts_clock_phase[demod_id].lsb_pos,
		xpt_ts_clock_phase[demod_id].num_of_bits,
		mpeg_out_param_ptr->mpeg_clk_phase);

	status |= update_by_mnemonic(state,
		xpt_lsb_first[demod_id].reg_addr,
		xpt_lsb_first[demod_id].lsb_pos,
		xpt_lsb_first[demod_id].num_of_bits,
		mpeg_out_param_ptr->lsb_or_msb_first);

	switch (mpeg_out_param_ptr->mpeg_error_indication) {
	case MXL_HYDRA_MPEG_ERR_REPLACE_SYNC:
		status |= update_by_mnemonic(state,
			xpt_err_replace_sync[demod_id].reg_addr,
			xpt_err_replace_sync[demod_id].lsb_pos,
			xpt_err_replace_sync[demod_id].num_of_bits,
			MXL_TRUE);
		status |= update_by_mnemonic(state,
			xpt_err_replace_valid[demod_id].reg_addr,
			xpt_err_replace_valid[demod_id].lsb_pos,
			xpt_err_replace_valid[demod_id].num_of_bits,
			MXL_FALSE);
		break;

	case MXL_HYDRA_MPEG_ERR_REPLACE_VALID:
		status |= update_by_mnemonic(state,
			xpt_err_replace_sync[demod_id].reg_addr,
			xpt_err_replace_sync[demod_id].lsb_pos,
			xpt_err_replace_sync[demod_id].num_of_bits,
			MXL_FALSE);

		status |= update_by_mnemonic(state,
			xpt_err_replace_valid[demod_id].reg_addr,
			xpt_err_replace_valid[demod_id].lsb_pos,
			xpt_err_replace_valid[demod_id].num_of_bits,
			MXL_TRUE);
		break;

	case MXL_HYDRA_MPEG_ERR_INDICATION_DISABLED:
	default:
		status |= update_by_mnemonic(state,
			xpt_err_replace_sync[demod_id].reg_addr,
			xpt_err_replace_sync[demod_id].lsb_pos,
			xpt_err_replace_sync[demod_id].num_of_bits,
			MXL_FALSE);

		status |= update_by_mnemonic(state,
			xpt_err_replace_valid[demod_id].reg_addr,
			xpt_err_replace_valid[demod_id].lsb_pos,
			xpt_err_replace_valid[demod_id].num_of_bits,
			MXL_FALSE);

		break;

	}

	if (mpeg_out_param_ptr->mpeg_mode != MXL_HYDRA_MPEG_MODE_PARALLEL) {
		status |= update_by_mnemonic(state,
			xpt_enable_output[demod_id].reg_addr,
			xpt_enable_output[demod_id].lsb_pos,
			xpt_enable_output[demod_id].num_of_bits,
			mpeg_out_param_ptr->enable);
	}
	return status;
}

static int config_mux(struct mxl *state)
{
	update_by_mnemonic(state, 0x9070000C, 0, 1, 0);
	update_by_mnemonic(state, 0x9070000C, 1, 1, 0);
	update_by_mnemonic(state, 0x9070000C, 2, 1, 0);
	update_by_mnemonic(state, 0x9070000C, 3, 1, 0);
	update_by_mnemonic(state, 0x9070000C, 4, 1, 0);
	update_by_mnemonic(state, 0x9070000C, 5, 1, 0);
	update_by_mnemonic(state, 0x9070000C, 6, 1, 0);
	update_by_mnemonic(state, 0x9070000C, 7, 1, 0);
	update_by_mnemonic(state, 0x90700008, 0, 2, 1);
	update_by_mnemonic(state, 0x90700008, 2, 2, 1);
	return 0;
}

static int load_fw(struct mxl *state, struct mxl5xx_cfg *cfg)
{
	int stat = 0;
	u8 *buf;

	if (cfg->fw)
		return firmware_download(state, cfg->fw, cfg->fw_len);

	if (!cfg->fw_read)
		return -1;

	buf = vmalloc(0x40000);
	if (!buf)
		return -ENOMEM;

	cfg->fw_read(cfg->fw_priv, buf, 0x40000);
	stat = firmware_download(state, buf, 0x40000);
	vfree(buf);

	return stat;
}

static int validate_sku(struct mxl *state)
{
	u32 pad_mux_bond = 0, prcm_chip_id = 0, prcm_so_cid = 0;
	int status;
	u32 type = state->base->type;

	status = read_by_mnemonic(state, 0x90000190, 0, 3, &pad_mux_bond);
	status |= read_by_mnemonic(state, 0x80030000, 0, 12, &prcm_chip_id);
	status |= read_by_mnemonic(state, 0x80030004, 24, 8, &prcm_so_cid);
	if (status)
		return -1;

	dev_info(state->i2cdev, "padMuxBond=%08x, prcmChipId=%08x, prcmSoCId=%08x\n",
		pad_mux_bond, prcm_chip_id, prcm_so_cid);

	if (prcm_chip_id != 0x560) {
		switch (pad_mux_bond) {
		case MXL_HYDRA_SKU_ID_581:
			if (type == MXL_HYDRA_DEVICE_581)
				return 0;
			if (type == MXL_HYDRA_DEVICE_581S) {
				state->base->type = MXL_HYDRA_DEVICE_581;
				return 0;
			}
			break;
		case MXL_HYDRA_SKU_ID_584:
			if (type == MXL_HYDRA_DEVICE_584)
				return 0;
			break;
		case MXL_HYDRA_SKU_ID_544:
			if (type == MXL_HYDRA_DEVICE_544)
				return 0;
			if (type == MXL_HYDRA_DEVICE_542)
				return 0;
			break;
		case MXL_HYDRA_SKU_ID_582:
			if (type == MXL_HYDRA_DEVICE_582)
				return 0;
			break;
		default:
			return -1;
		}
	} else {

	}
	return -1;
}

static int get_fwinfo(struct mxl *state)
{
	int status;
	u32 val = 0;

	status = read_by_mnemonic(state, 0x90000190, 0, 3, &val);
	if (status)
		return status;
	dev_info(state->i2cdev, "chipID=%08x\n", val);

	status = read_by_mnemonic(state, 0x80030004, 8, 8, &val);
	if (status)
		return status;
	dev_info(state->i2cdev, "chipVer=%08x\n", val);

	status = read_register(state, HYDRA_FIRMWARE_VERSION, &val);
	if (status)
		return status;
	dev_info(state->i2cdev, "FWVer=%08x\n", val);

	state->base->fwversion = val;
	return status;
}


static u8 ts_map1_to_1[MXL_HYDRA_DEMOD_MAX] = {
	MXL_HYDRA_DEMOD_ID_0,
	MXL_HYDRA_DEMOD_ID_1,
	MXL_HYDRA_DEMOD_ID_2,
	MXL_HYDRA_DEMOD_ID_3,
	MXL_HYDRA_DEMOD_ID_4,
	MXL_HYDRA_DEMOD_ID_5,
	MXL_HYDRA_DEMOD_ID_6,
	MXL_HYDRA_DEMOD_ID_7,
};

static u8 ts_map54x[MXL_HYDRA_DEMOD_MAX] = {
	MXL_HYDRA_DEMOD_ID_2,
	MXL_HYDRA_DEMOD_ID_3,
	MXL_HYDRA_DEMOD_ID_4,
	MXL_HYDRA_DEMOD_ID_5,
	MXL_HYDRA_DEMOD_MAX,
	MXL_HYDRA_DEMOD_MAX,
	MXL_HYDRA_DEMOD_MAX,
	MXL_HYDRA_DEMOD_MAX,
};

static int probe(struct mxl *state, struct mxl5xx_cfg *cfg)
{
	u32 chipver;
	int fw, status, j;
	struct MXL_HYDRA_MPEGOUT_PARAM_T mpeg_interface_cfg;

	state->base->ts_map = ts_map1_to_1;

	switch (state->base->type) {
	case MXL_HYDRA_DEVICE_581:
	case MXL_HYDRA_DEVICE_581S:
		state->base->can_clkout = 1;
		state->base->demod_num = 8;
		state->base->tuner_num = 1;
		state->base->sku_type = MXL_HYDRA_SKU_TYPE_581;
		break;
	case MXL_HYDRA_DEVICE_582:
		state->base->can_clkout = 1;
		state->base->demod_num = 8;
		state->base->tuner_num = 3;
		state->base->sku_type = MXL_HYDRA_SKU_TYPE_582;
		break;
	case MXL_HYDRA_DEVICE_585:
		state->base->can_clkout = 0;
		state->base->demod_num = 8;
		state->base->tuner_num = 4;
		state->base->sku_type = MXL_HYDRA_SKU_TYPE_585;
		break;
	case MXL_HYDRA_DEVICE_544:
		state->base->can_clkout = 0;
		state->base->demod_num = 4;
		state->base->tuner_num = 4;
		state->base->sku_type = MXL_HYDRA_SKU_TYPE_544;
		state->base->ts_map = ts_map54x;
		break;
	case MXL_HYDRA_DEVICE_541:
	case MXL_HYDRA_DEVICE_541S:
		state->base->can_clkout = 0;
		state->base->demod_num = 4;
		state->base->tuner_num = 1;
		state->base->sku_type = MXL_HYDRA_SKU_TYPE_541;
		state->base->ts_map = ts_map54x;
		break;
	case MXL_HYDRA_DEVICE_561:
	case MXL_HYDRA_DEVICE_561S:
		state->base->can_clkout = 0;
		state->base->demod_num = 6;
		state->base->tuner_num = 1;
		state->base->sku_type = MXL_HYDRA_SKU_TYPE_561;
		break;
	case MXL_HYDRA_DEVICE_568:
		state->base->can_clkout = 0;
		state->base->demod_num = 8;
		state->base->tuner_num = 1;
		state->base->chan_bond = 1;
		state->base->sku_type = MXL_HYDRA_SKU_TYPE_568;
		break;
	case MXL_HYDRA_DEVICE_542:
		state->base->can_clkout = 1;
		state->base->demod_num = 4;
		state->base->tuner_num = 3;
		state->base->sku_type = MXL_HYDRA_SKU_TYPE_542;
		state->base->ts_map = ts_map54x;
		break;
	case MXL_HYDRA_DEVICE_TEST:
	case MXL_HYDRA_DEVICE_584:
	default:
		state->base->can_clkout = 0;
		state->base->demod_num = 8;
		state->base->tuner_num = 4;
		state->base->sku_type = MXL_HYDRA_SKU_TYPE_584;
		break;
	}

	status = validate_sku(state);
	if (status)
		return status;

	update_by_mnemonic(state, 0x80030014, 9, 1, 1);
	update_by_mnemonic(state, 0x8003003C, 12, 1, 1);
	status = read_by_mnemonic(state, 0x80030000, 12, 4, &chipver);
	if (status)
		state->base->chipversion = 0;
	else
		state->base->chipversion = (chipver == 2) ? 2 : 1;
	dev_info(state->i2cdev, "Hydra chip version %u\n",
		state->base->chipversion);

	cfg_dev_xtal(state, cfg->clk, cfg->cap, 0);

	fw = firmware_is_alive(state);
	if (!fw) {
		status = load_fw(state, cfg);
		if (status)
			return status;
	}
	get_fwinfo(state);

	config_mux(state);
	mpeg_interface_cfg.enable = MXL_ENABLE;
	mpeg_interface_cfg.lsb_or_msb_first = MXL_HYDRA_MPEG_SERIAL_MSB_1ST;
	/*  supports only (0-104&139)MHz */
	if (cfg->ts_clk)
		mpeg_interface_cfg.max_mpeg_clk_rate = cfg->ts_clk;
	else
		mpeg_interface_cfg.max_mpeg_clk_rate = 69; /* 139; */
	mpeg_interface_cfg.mpeg_clk_phase = MXL_HYDRA_MPEG_CLK_PHASE_SHIFT_0_DEG;
	mpeg_interface_cfg.mpeg_clk_pol = MXL_HYDRA_MPEG_CLK_IN_PHASE;
	/* MXL_HYDRA_MPEG_CLK_GAPPED; */
	mpeg_interface_cfg.mpeg_clk_type = MXL_HYDRA_MPEG_CLK_CONTINUOUS;
	mpeg_interface_cfg.mpeg_error_indication =
		MXL_HYDRA_MPEG_ERR_INDICATION_DISABLED;
	mpeg_interface_cfg.mpeg_mode = MXL_HYDRA_MPEG_MODE_SERIAL_3_WIRE;
	mpeg_interface_cfg.mpeg_sync_pol  = MXL_HYDRA_MPEG_ACTIVE_HIGH;
	mpeg_interface_cfg.mpeg_sync_pulse_width  = MXL_HYDRA_MPEG_SYNC_WIDTH_BIT;
	mpeg_interface_cfg.mpeg_valid_pol  = MXL_HYDRA_MPEG_ACTIVE_HIGH;

	for (j = 0; j < state->base->demod_num; j++) {
		status = config_ts(state, (enum MXL_HYDRA_DEMOD_ID_E) j,
				   &mpeg_interface_cfg);
		if (status)
			return status;
	}
	set_drive_strength(state, 1);
	return 0;
}

struct dvb_frontend *mxl5xx_attach(struct i2c_adapter *i2c,
	struct mxl5xx_cfg *cfg, u32 demod, u32 tuner,
	int (**fn_set_input)(struct dvb_frontend *, int))
{
	struct mxl *state;
	struct mxl_base *base;

	state = kzalloc(sizeof(struct mxl), GFP_KERNEL);
	if (!state)
		return NULL;

	state->demod = demod;
	state->tuner = tuner;
	state->tuner_in_use = 0xffffffff;
	state->i2cdev = &i2c->dev;

	base = match_base(i2c, cfg->adr);
	if (base) {
		base->count++;
		if (base->count > base->demod_num)
			goto fail;
		state->base = base;
	} else {
		base = kzalloc(sizeof(struct mxl_base), GFP_KERNEL);
		if (!base)
			goto fail;
		base->i2c = i2c;
		base->adr = cfg->adr;
		base->type = cfg->type;
		base->count = 1;
		mutex_init(&base->i2c_lock);
		mutex_init(&base->status_lock);
		mutex_init(&base->tune_lock);
		INIT_LIST_HEAD(&base->mxls);

		state->base = base;
		if (probe(state, cfg) < 0) {
			kfree(base);
			goto fail;
		}
		list_add(&base->mxllist, &mxllist);
	}
	state->fe.ops               = mxl_ops;
	state->xbar[0]              = 4;
	state->xbar[1]              = demod;
	state->xbar[2]              = 8;
	state->fe.demodulator_priv  = state;
	*fn_set_input               = set_input;

	list_add(&state->mxl, &base->mxls);
	return &state->fe;

fail:
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL_GPL(mxl5xx_attach);

MODULE_DESCRIPTION("MaxLinear MxL5xx DVB-S/S2 tuner-demodulator driver");
MODULE_AUTHOR("Ralph and Marcus Metzler, Metzler Brothers Systementwicklung GbR");
MODULE_LICENSE("GPL v2");
