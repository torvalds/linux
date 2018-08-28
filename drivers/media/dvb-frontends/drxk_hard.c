/*
 * drxk_hard: DRX-K DVB-C/T demodulator driver
 *
 * Copyright (C) 2010-2011 Digital Devices GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * To obtain the license, point your browser to
 * http://www.gnu.org/copyleft/gpl.html
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/hardirq.h>
#include <asm/div64.h>

#include <media/dvb_frontend.h>
#include "drxk.h"
#include "drxk_hard.h"
#include <media/dvb_math.h>

static int power_down_dvbt(struct drxk_state *state, bool set_power_mode);
static int power_down_qam(struct drxk_state *state);
static int set_dvbt_standard(struct drxk_state *state,
			   enum operation_mode o_mode);
static int set_qam_standard(struct drxk_state *state,
			  enum operation_mode o_mode);
static int set_qam(struct drxk_state *state, u16 intermediate_freqk_hz,
		  s32 tuner_freq_offset);
static int set_dvbt_standard(struct drxk_state *state,
			   enum operation_mode o_mode);
static int dvbt_start(struct drxk_state *state);
static int set_dvbt(struct drxk_state *state, u16 intermediate_freqk_hz,
		   s32 tuner_freq_offset);
static int get_qam_lock_status(struct drxk_state *state, u32 *p_lock_status);
static int get_dvbt_lock_status(struct drxk_state *state, u32 *p_lock_status);
static int switch_antenna_to_qam(struct drxk_state *state);
static int switch_antenna_to_dvbt(struct drxk_state *state);

static bool is_dvbt(struct drxk_state *state)
{
	return state->m_operation_mode == OM_DVBT;
}

static bool is_qam(struct drxk_state *state)
{
	return state->m_operation_mode == OM_QAM_ITU_A ||
	    state->m_operation_mode == OM_QAM_ITU_B ||
	    state->m_operation_mode == OM_QAM_ITU_C;
}

#define NOA1ROM 0

#define DRXDAP_FASI_SHORT_FORMAT(addr) (((addr) & 0xFC30FF80) == 0)
#define DRXDAP_FASI_LONG_FORMAT(addr)  (((addr) & 0xFC30FF80) != 0)

#define DEFAULT_MER_83  165
#define DEFAULT_MER_93  250

#ifndef DRXK_MPEG_SERIAL_OUTPUT_PIN_DRIVE_STRENGTH
#define DRXK_MPEG_SERIAL_OUTPUT_PIN_DRIVE_STRENGTH (0x02)
#endif

#ifndef DRXK_MPEG_PARALLEL_OUTPUT_PIN_DRIVE_STRENGTH
#define DRXK_MPEG_PARALLEL_OUTPUT_PIN_DRIVE_STRENGTH (0x03)
#endif

#define DEFAULT_DRXK_MPEG_LOCK_TIMEOUT 700
#define DEFAULT_DRXK_DEMOD_LOCK_TIMEOUT 500

#ifndef DRXK_KI_RAGC_ATV
#define DRXK_KI_RAGC_ATV   4
#endif
#ifndef DRXK_KI_IAGC_ATV
#define DRXK_KI_IAGC_ATV   6
#endif
#ifndef DRXK_KI_DAGC_ATV
#define DRXK_KI_DAGC_ATV   7
#endif

#ifndef DRXK_KI_RAGC_QAM
#define DRXK_KI_RAGC_QAM   3
#endif
#ifndef DRXK_KI_IAGC_QAM
#define DRXK_KI_IAGC_QAM   4
#endif
#ifndef DRXK_KI_DAGC_QAM
#define DRXK_KI_DAGC_QAM   7
#endif
#ifndef DRXK_KI_RAGC_DVBT
#define DRXK_KI_RAGC_DVBT  (IsA1WithPatchCode(state) ? 3 : 2)
#endif
#ifndef DRXK_KI_IAGC_DVBT
#define DRXK_KI_IAGC_DVBT  (IsA1WithPatchCode(state) ? 4 : 2)
#endif
#ifndef DRXK_KI_DAGC_DVBT
#define DRXK_KI_DAGC_DVBT  (IsA1WithPatchCode(state) ? 10 : 7)
#endif

#ifndef DRXK_AGC_DAC_OFFSET
#define DRXK_AGC_DAC_OFFSET (0x800)
#endif

#ifndef DRXK_BANDWIDTH_8MHZ_IN_HZ
#define DRXK_BANDWIDTH_8MHZ_IN_HZ  (0x8B8249L)
#endif

#ifndef DRXK_BANDWIDTH_7MHZ_IN_HZ
#define DRXK_BANDWIDTH_7MHZ_IN_HZ  (0x7A1200L)
#endif

#ifndef DRXK_BANDWIDTH_6MHZ_IN_HZ
#define DRXK_BANDWIDTH_6MHZ_IN_HZ  (0x68A1B6L)
#endif

#ifndef DRXK_QAM_SYMBOLRATE_MAX
#define DRXK_QAM_SYMBOLRATE_MAX         (7233000)
#endif

#define DRXK_BL_ROM_OFFSET_TAPS_DVBT    56
#define DRXK_BL_ROM_OFFSET_TAPS_ITU_A   64
#define DRXK_BL_ROM_OFFSET_TAPS_ITU_C   0x5FE0
#define DRXK_BL_ROM_OFFSET_TAPS_BG      24
#define DRXK_BL_ROM_OFFSET_TAPS_DKILLP  32
#define DRXK_BL_ROM_OFFSET_TAPS_NTSC    40
#define DRXK_BL_ROM_OFFSET_TAPS_FM      48
#define DRXK_BL_ROM_OFFSET_UCODE        0

#define DRXK_BLC_TIMEOUT                100

#define DRXK_BLCC_NR_ELEMENTS_TAPS      2
#define DRXK_BLCC_NR_ELEMENTS_UCODE     6

#define DRXK_BLDC_NR_ELEMENTS_TAPS      28

#ifndef DRXK_OFDM_NE_NOTCH_WIDTH
#define DRXK_OFDM_NE_NOTCH_WIDTH             (4)
#endif

#define DRXK_QAM_SL_SIG_POWER_QAM16       (40960)
#define DRXK_QAM_SL_SIG_POWER_QAM32       (20480)
#define DRXK_QAM_SL_SIG_POWER_QAM64       (43008)
#define DRXK_QAM_SL_SIG_POWER_QAM128      (20992)
#define DRXK_QAM_SL_SIG_POWER_QAM256      (43520)

static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages");

#define dprintk(level, fmt, arg...) do {				\
if (debug >= level)							\
	printk(KERN_DEBUG KBUILD_MODNAME ": %s " fmt, __func__, ##arg);	\
} while (0)


static inline u32 MulDiv32(u32 a, u32 b, u32 c)
{
	u64 tmp64;

	tmp64 = (u64) a * (u64) b;
	do_div(tmp64, c);

	return (u32) tmp64;
}

static inline u32 Frac28a(u32 a, u32 c)
{
	int i = 0;
	u32 Q1 = 0;
	u32 R0 = 0;

	R0 = (a % c) << 4;	/* 32-28 == 4 shifts possible at max */
	Q1 = a / c;		/*
				 * integer part, only the 4 least significant
				 * bits will be visible in the result
				 */

	/* division using radix 16, 7 nibbles in the result */
	for (i = 0; i < 7; i++) {
		Q1 = (Q1 << 4) | (R0 / c);
		R0 = (R0 % c) << 4;
	}
	/* rounding */
	if ((R0 >> 3) >= c)
		Q1++;

	return Q1;
}

static inline u32 log10times100(u32 value)
{
	return (100L * intlog10(value)) >> 24;
}

/***************************************************************************/
/* I2C **********************************************************************/
/***************************************************************************/

static int drxk_i2c_lock(struct drxk_state *state)
{
	i2c_lock_bus(state->i2c, I2C_LOCK_SEGMENT);
	state->drxk_i2c_exclusive_lock = true;

	return 0;
}

static void drxk_i2c_unlock(struct drxk_state *state)
{
	if (!state->drxk_i2c_exclusive_lock)
		return;

	i2c_unlock_bus(state->i2c, I2C_LOCK_SEGMENT);
	state->drxk_i2c_exclusive_lock = false;
}

static int drxk_i2c_transfer(struct drxk_state *state, struct i2c_msg *msgs,
			     unsigned len)
{
	if (state->drxk_i2c_exclusive_lock)
		return __i2c_transfer(state->i2c, msgs, len);
	else
		return i2c_transfer(state->i2c, msgs, len);
}

static int i2c_read1(struct drxk_state *state, u8 adr, u8 *val)
{
	struct i2c_msg msgs[1] = { {.addr = adr, .flags = I2C_M_RD,
				    .buf = val, .len = 1}
	};

	return drxk_i2c_transfer(state, msgs, 1);
}

static int i2c_write(struct drxk_state *state, u8 adr, u8 *data, int len)
{
	int status;
	struct i2c_msg msg = {
	    .addr = adr, .flags = 0, .buf = data, .len = len };

	dprintk(3, ":");
	if (debug > 2) {
		int i;
		for (i = 0; i < len; i++)
			pr_cont(" %02x", data[i]);
		pr_cont("\n");
	}
	status = drxk_i2c_transfer(state, &msg, 1);
	if (status >= 0 && status != 1)
		status = -EIO;

	if (status < 0)
		pr_err("i2c write error at addr 0x%02x\n", adr);

	return status;
}

static int i2c_read(struct drxk_state *state,
		    u8 adr, u8 *msg, int len, u8 *answ, int alen)
{
	int status;
	struct i2c_msg msgs[2] = {
		{.addr = adr, .flags = 0,
				    .buf = msg, .len = len},
		{.addr = adr, .flags = I2C_M_RD,
		 .buf = answ, .len = alen}
	};

	status = drxk_i2c_transfer(state, msgs, 2);
	if (status != 2) {
		if (debug > 2)
			pr_cont(": ERROR!\n");
		if (status >= 0)
			status = -EIO;

		pr_err("i2c read error at addr 0x%02x\n", adr);
		return status;
	}
	if (debug > 2) {
		int i;
		dprintk(2, ": read from");
		for (i = 0; i < len; i++)
			pr_cont(" %02x", msg[i]);
		pr_cont(", value = ");
		for (i = 0; i < alen; i++)
			pr_cont(" %02x", answ[i]);
		pr_cont("\n");
	}
	return 0;
}

static int read16_flags(struct drxk_state *state, u32 reg, u16 *data, u8 flags)
{
	int status;
	u8 adr = state->demod_address, mm1[4], mm2[2], len;

	if (state->single_master)
		flags |= 0xC0;

	if (DRXDAP_FASI_LONG_FORMAT(reg) || (flags != 0)) {
		mm1[0] = (((reg << 1) & 0xFF) | 0x01);
		mm1[1] = ((reg >> 16) & 0xFF);
		mm1[2] = ((reg >> 24) & 0xFF) | flags;
		mm1[3] = ((reg >> 7) & 0xFF);
		len = 4;
	} else {
		mm1[0] = ((reg << 1) & 0xFF);
		mm1[1] = (((reg >> 16) & 0x0F) | ((reg >> 18) & 0xF0));
		len = 2;
	}
	dprintk(2, "(0x%08x, 0x%02x)\n", reg, flags);
	status = i2c_read(state, adr, mm1, len, mm2, 2);
	if (status < 0)
		return status;
	if (data)
		*data = mm2[0] | (mm2[1] << 8);

	return 0;
}

static int read16(struct drxk_state *state, u32 reg, u16 *data)
{
	return read16_flags(state, reg, data, 0);
}

static int read32_flags(struct drxk_state *state, u32 reg, u32 *data, u8 flags)
{
	int status;
	u8 adr = state->demod_address, mm1[4], mm2[4], len;

	if (state->single_master)
		flags |= 0xC0;

	if (DRXDAP_FASI_LONG_FORMAT(reg) || (flags != 0)) {
		mm1[0] = (((reg << 1) & 0xFF) | 0x01);
		mm1[1] = ((reg >> 16) & 0xFF);
		mm1[2] = ((reg >> 24) & 0xFF) | flags;
		mm1[3] = ((reg >> 7) & 0xFF);
		len = 4;
	} else {
		mm1[0] = ((reg << 1) & 0xFF);
		mm1[1] = (((reg >> 16) & 0x0F) | ((reg >> 18) & 0xF0));
		len = 2;
	}
	dprintk(2, "(0x%08x, 0x%02x)\n", reg, flags);
	status = i2c_read(state, adr, mm1, len, mm2, 4);
	if (status < 0)
		return status;
	if (data)
		*data = mm2[0] | (mm2[1] << 8) |
		    (mm2[2] << 16) | (mm2[3] << 24);

	return 0;
}

static int read32(struct drxk_state *state, u32 reg, u32 *data)
{
	return read32_flags(state, reg, data, 0);
}

static int write16_flags(struct drxk_state *state, u32 reg, u16 data, u8 flags)
{
	u8 adr = state->demod_address, mm[6], len;

	if (state->single_master)
		flags |= 0xC0;
	if (DRXDAP_FASI_LONG_FORMAT(reg) || (flags != 0)) {
		mm[0] = (((reg << 1) & 0xFF) | 0x01);
		mm[1] = ((reg >> 16) & 0xFF);
		mm[2] = ((reg >> 24) & 0xFF) | flags;
		mm[3] = ((reg >> 7) & 0xFF);
		len = 4;
	} else {
		mm[0] = ((reg << 1) & 0xFF);
		mm[1] = (((reg >> 16) & 0x0F) | ((reg >> 18) & 0xF0));
		len = 2;
	}
	mm[len] = data & 0xff;
	mm[len + 1] = (data >> 8) & 0xff;

	dprintk(2, "(0x%08x, 0x%04x, 0x%02x)\n", reg, data, flags);
	return i2c_write(state, adr, mm, len + 2);
}

static int write16(struct drxk_state *state, u32 reg, u16 data)
{
	return write16_flags(state, reg, data, 0);
}

static int write32_flags(struct drxk_state *state, u32 reg, u32 data, u8 flags)
{
	u8 adr = state->demod_address, mm[8], len;

	if (state->single_master)
		flags |= 0xC0;
	if (DRXDAP_FASI_LONG_FORMAT(reg) || (flags != 0)) {
		mm[0] = (((reg << 1) & 0xFF) | 0x01);
		mm[1] = ((reg >> 16) & 0xFF);
		mm[2] = ((reg >> 24) & 0xFF) | flags;
		mm[3] = ((reg >> 7) & 0xFF);
		len = 4;
	} else {
		mm[0] = ((reg << 1) & 0xFF);
		mm[1] = (((reg >> 16) & 0x0F) | ((reg >> 18) & 0xF0));
		len = 2;
	}
	mm[len] = data & 0xff;
	mm[len + 1] = (data >> 8) & 0xff;
	mm[len + 2] = (data >> 16) & 0xff;
	mm[len + 3] = (data >> 24) & 0xff;
	dprintk(2, "(0x%08x, 0x%08x, 0x%02x)\n", reg, data, flags);

	return i2c_write(state, adr, mm, len + 4);
}

static int write32(struct drxk_state *state, u32 reg, u32 data)
{
	return write32_flags(state, reg, data, 0);
}

static int write_block(struct drxk_state *state, u32 address,
		      const int block_size, const u8 p_block[])
{
	int status = 0, blk_size = block_size;
	u8 flags = 0;

	if (state->single_master)
		flags |= 0xC0;

	while (blk_size > 0) {
		int chunk = blk_size > state->m_chunk_size ?
		    state->m_chunk_size : blk_size;
		u8 *adr_buf = &state->chunk[0];
		u32 adr_length = 0;

		if (DRXDAP_FASI_LONG_FORMAT(address) || (flags != 0)) {
			adr_buf[0] = (((address << 1) & 0xFF) | 0x01);
			adr_buf[1] = ((address >> 16) & 0xFF);
			adr_buf[2] = ((address >> 24) & 0xFF);
			adr_buf[3] = ((address >> 7) & 0xFF);
			adr_buf[2] |= flags;
			adr_length = 4;
			if (chunk == state->m_chunk_size)
				chunk -= 2;
		} else {
			adr_buf[0] = ((address << 1) & 0xFF);
			adr_buf[1] = (((address >> 16) & 0x0F) |
				     ((address >> 18) & 0xF0));
			adr_length = 2;
		}
		memcpy(&state->chunk[adr_length], p_block, chunk);
		dprintk(2, "(0x%08x, 0x%02x)\n", address, flags);
		if (debug > 1) {
			int i;
			if (p_block)
				for (i = 0; i < chunk; i++)
					pr_cont(" %02x", p_block[i]);
			pr_cont("\n");
		}
		status = i2c_write(state, state->demod_address,
				   &state->chunk[0], chunk + adr_length);
		if (status < 0) {
			pr_err("%s: i2c write error at addr 0x%02x\n",
			       __func__, address);
			break;
		}
		p_block += chunk;
		address += (chunk >> 1);
		blk_size -= chunk;
	}
	return status;
}

#ifndef DRXK_MAX_RETRIES_POWERUP
#define DRXK_MAX_RETRIES_POWERUP 20
#endif

static int power_up_device(struct drxk_state *state)
{
	int status;
	u8 data = 0;
	u16 retry_count = 0;

	dprintk(1, "\n");

	status = i2c_read1(state, state->demod_address, &data);
	if (status < 0) {
		do {
			data = 0;
			status = i2c_write(state, state->demod_address,
					   &data, 1);
			usleep_range(10000, 11000);
			retry_count++;
			if (status < 0)
				continue;
			status = i2c_read1(state, state->demod_address,
					   &data);
		} while (status < 0 &&
			 (retry_count < DRXK_MAX_RETRIES_POWERUP));
		if (status < 0 && retry_count >= DRXK_MAX_RETRIES_POWERUP)
			goto error;
	}

	/* Make sure all clk domains are active */
	status = write16(state, SIO_CC_PWD_MODE__A, SIO_CC_PWD_MODE_LEVEL_NONE);
	if (status < 0)
		goto error;
	status = write16(state, SIO_CC_UPDATE__A, SIO_CC_UPDATE_KEY);
	if (status < 0)
		goto error;
	/* Enable pll lock tests */
	status = write16(state, SIO_CC_PLL_LOCK__A, 1);
	if (status < 0)
		goto error;

	state->m_current_power_mode = DRX_POWER_UP;

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	return status;
}


static int init_state(struct drxk_state *state)
{
	/*
	 * FIXME: most (all?) of the values below should be moved into
	 * struct drxk_config, as they are probably board-specific
	 */
	u32 ul_vsb_if_agc_mode = DRXK_AGC_CTRL_AUTO;
	u32 ul_vsb_if_agc_output_level = 0;
	u32 ul_vsb_if_agc_min_level = 0;
	u32 ul_vsb_if_agc_max_level = 0x7FFF;
	u32 ul_vsb_if_agc_speed = 3;

	u32 ul_vsb_rf_agc_mode = DRXK_AGC_CTRL_AUTO;
	u32 ul_vsb_rf_agc_output_level = 0;
	u32 ul_vsb_rf_agc_min_level = 0;
	u32 ul_vsb_rf_agc_max_level = 0x7FFF;
	u32 ul_vsb_rf_agc_speed = 3;
	u32 ul_vsb_rf_agc_top = 9500;
	u32 ul_vsb_rf_agc_cut_off_current = 4000;

	u32 ul_atv_if_agc_mode = DRXK_AGC_CTRL_AUTO;
	u32 ul_atv_if_agc_output_level = 0;
	u32 ul_atv_if_agc_min_level = 0;
	u32 ul_atv_if_agc_max_level = 0;
	u32 ul_atv_if_agc_speed = 3;

	u32 ul_atv_rf_agc_mode = DRXK_AGC_CTRL_OFF;
	u32 ul_atv_rf_agc_output_level = 0;
	u32 ul_atv_rf_agc_min_level = 0;
	u32 ul_atv_rf_agc_max_level = 0;
	u32 ul_atv_rf_agc_top = 9500;
	u32 ul_atv_rf_agc_cut_off_current = 4000;
	u32 ul_atv_rf_agc_speed = 3;

	u32 ulQual83 = DEFAULT_MER_83;
	u32 ulQual93 = DEFAULT_MER_93;

	u32 ul_mpeg_lock_time_out = DEFAULT_DRXK_MPEG_LOCK_TIMEOUT;
	u32 ul_demod_lock_time_out = DEFAULT_DRXK_DEMOD_LOCK_TIMEOUT;

	/* io_pad_cfg register (8 bit reg.) MSB bit is 1 (default value) */
	/* io_pad_cfg_mode output mode is drive always */
	/* io_pad_cfg_drive is set to power 2 (23 mA) */
	u32 ul_gpio_cfg = 0x0113;
	u32 ul_invert_ts_clock = 0;
	u32 ul_ts_data_strength = DRXK_MPEG_SERIAL_OUTPUT_PIN_DRIVE_STRENGTH;
	u32 ul_dvbt_bitrate = 50000000;
	u32 ul_dvbc_bitrate = DRXK_QAM_SYMBOLRATE_MAX * 8;

	u32 ul_insert_rs_byte = 0;

	u32 ul_rf_mirror = 1;
	u32 ul_power_down = 0;

	dprintk(1, "\n");

	state->m_has_lna = false;
	state->m_has_dvbt = false;
	state->m_has_dvbc = false;
	state->m_has_atv = false;
	state->m_has_oob = false;
	state->m_has_audio = false;

	if (!state->m_chunk_size)
		state->m_chunk_size = 124;

	state->m_osc_clock_freq = 0;
	state->m_smart_ant_inverted = false;
	state->m_b_p_down_open_bridge = false;

	/* real system clock frequency in kHz */
	state->m_sys_clock_freq = 151875;
	/* Timing div, 250ns/Psys */
	/* Timing div, = (delay (nano seconds) * sysclk (kHz))/ 1000 */
	state->m_hi_cfg_timing_div = ((state->m_sys_clock_freq / 1000) *
				   HI_I2C_DELAY) / 1000;
	/* Clipping */
	if (state->m_hi_cfg_timing_div > SIO_HI_RA_RAM_PAR_2_CFG_DIV__M)
		state->m_hi_cfg_timing_div = SIO_HI_RA_RAM_PAR_2_CFG_DIV__M;
	state->m_hi_cfg_wake_up_key = (state->demod_address << 1);
	/* port/bridge/power down ctrl */
	state->m_hi_cfg_ctrl = SIO_HI_RA_RAM_PAR_5_CFG_SLV0_SLAVE;

	state->m_b_power_down = (ul_power_down != 0);

	state->m_drxk_a3_patch_code = false;

	/* Init AGC and PGA parameters */
	/* VSB IF */
	state->m_vsb_if_agc_cfg.ctrl_mode = ul_vsb_if_agc_mode;
	state->m_vsb_if_agc_cfg.output_level = ul_vsb_if_agc_output_level;
	state->m_vsb_if_agc_cfg.min_output_level = ul_vsb_if_agc_min_level;
	state->m_vsb_if_agc_cfg.max_output_level = ul_vsb_if_agc_max_level;
	state->m_vsb_if_agc_cfg.speed = ul_vsb_if_agc_speed;
	state->m_vsb_pga_cfg = 140;

	/* VSB RF */
	state->m_vsb_rf_agc_cfg.ctrl_mode = ul_vsb_rf_agc_mode;
	state->m_vsb_rf_agc_cfg.output_level = ul_vsb_rf_agc_output_level;
	state->m_vsb_rf_agc_cfg.min_output_level = ul_vsb_rf_agc_min_level;
	state->m_vsb_rf_agc_cfg.max_output_level = ul_vsb_rf_agc_max_level;
	state->m_vsb_rf_agc_cfg.speed = ul_vsb_rf_agc_speed;
	state->m_vsb_rf_agc_cfg.top = ul_vsb_rf_agc_top;
	state->m_vsb_rf_agc_cfg.cut_off_current = ul_vsb_rf_agc_cut_off_current;
	state->m_vsb_pre_saw_cfg.reference = 0x07;
	state->m_vsb_pre_saw_cfg.use_pre_saw = true;

	state->m_Quality83percent = DEFAULT_MER_83;
	state->m_Quality93percent = DEFAULT_MER_93;
	if (ulQual93 <= 500 && ulQual83 < ulQual93) {
		state->m_Quality83percent = ulQual83;
		state->m_Quality93percent = ulQual93;
	}

	/* ATV IF */
	state->m_atv_if_agc_cfg.ctrl_mode = ul_atv_if_agc_mode;
	state->m_atv_if_agc_cfg.output_level = ul_atv_if_agc_output_level;
	state->m_atv_if_agc_cfg.min_output_level = ul_atv_if_agc_min_level;
	state->m_atv_if_agc_cfg.max_output_level = ul_atv_if_agc_max_level;
	state->m_atv_if_agc_cfg.speed = ul_atv_if_agc_speed;

	/* ATV RF */
	state->m_atv_rf_agc_cfg.ctrl_mode = ul_atv_rf_agc_mode;
	state->m_atv_rf_agc_cfg.output_level = ul_atv_rf_agc_output_level;
	state->m_atv_rf_agc_cfg.min_output_level = ul_atv_rf_agc_min_level;
	state->m_atv_rf_agc_cfg.max_output_level = ul_atv_rf_agc_max_level;
	state->m_atv_rf_agc_cfg.speed = ul_atv_rf_agc_speed;
	state->m_atv_rf_agc_cfg.top = ul_atv_rf_agc_top;
	state->m_atv_rf_agc_cfg.cut_off_current = ul_atv_rf_agc_cut_off_current;
	state->m_atv_pre_saw_cfg.reference = 0x04;
	state->m_atv_pre_saw_cfg.use_pre_saw = true;


	/* DVBT RF */
	state->m_dvbt_rf_agc_cfg.ctrl_mode = DRXK_AGC_CTRL_OFF;
	state->m_dvbt_rf_agc_cfg.output_level = 0;
	state->m_dvbt_rf_agc_cfg.min_output_level = 0;
	state->m_dvbt_rf_agc_cfg.max_output_level = 0xFFFF;
	state->m_dvbt_rf_agc_cfg.top = 0x2100;
	state->m_dvbt_rf_agc_cfg.cut_off_current = 4000;
	state->m_dvbt_rf_agc_cfg.speed = 1;


	/* DVBT IF */
	state->m_dvbt_if_agc_cfg.ctrl_mode = DRXK_AGC_CTRL_AUTO;
	state->m_dvbt_if_agc_cfg.output_level = 0;
	state->m_dvbt_if_agc_cfg.min_output_level = 0;
	state->m_dvbt_if_agc_cfg.max_output_level = 9000;
	state->m_dvbt_if_agc_cfg.top = 13424;
	state->m_dvbt_if_agc_cfg.cut_off_current = 0;
	state->m_dvbt_if_agc_cfg.speed = 3;
	state->m_dvbt_if_agc_cfg.fast_clip_ctrl_delay = 30;
	state->m_dvbt_if_agc_cfg.ingain_tgt_max = 30000;
	/* state->m_dvbtPgaCfg = 140; */

	state->m_dvbt_pre_saw_cfg.reference = 4;
	state->m_dvbt_pre_saw_cfg.use_pre_saw = false;

	/* QAM RF */
	state->m_qam_rf_agc_cfg.ctrl_mode = DRXK_AGC_CTRL_OFF;
	state->m_qam_rf_agc_cfg.output_level = 0;
	state->m_qam_rf_agc_cfg.min_output_level = 6023;
	state->m_qam_rf_agc_cfg.max_output_level = 27000;
	state->m_qam_rf_agc_cfg.top = 0x2380;
	state->m_qam_rf_agc_cfg.cut_off_current = 4000;
	state->m_qam_rf_agc_cfg.speed = 3;

	/* QAM IF */
	state->m_qam_if_agc_cfg.ctrl_mode = DRXK_AGC_CTRL_AUTO;
	state->m_qam_if_agc_cfg.output_level = 0;
	state->m_qam_if_agc_cfg.min_output_level = 0;
	state->m_qam_if_agc_cfg.max_output_level = 9000;
	state->m_qam_if_agc_cfg.top = 0x0511;
	state->m_qam_if_agc_cfg.cut_off_current = 0;
	state->m_qam_if_agc_cfg.speed = 3;
	state->m_qam_if_agc_cfg.ingain_tgt_max = 5119;
	state->m_qam_if_agc_cfg.fast_clip_ctrl_delay = 50;

	state->m_qam_pga_cfg = 140;
	state->m_qam_pre_saw_cfg.reference = 4;
	state->m_qam_pre_saw_cfg.use_pre_saw = false;

	state->m_operation_mode = OM_NONE;
	state->m_drxk_state = DRXK_UNINITIALIZED;

	/* MPEG output configuration */
	state->m_enable_mpeg_output = true;	/* If TRUE; enable MPEG ouput */
	state->m_insert_rs_byte = false;	/* If TRUE; insert RS byte */
	state->m_invert_data = false;	/* If TRUE; invert DATA signals */
	state->m_invert_err = false;	/* If TRUE; invert ERR signal */
	state->m_invert_str = false;	/* If TRUE; invert STR signals */
	state->m_invert_val = false;	/* If TRUE; invert VAL signals */
	state->m_invert_clk = (ul_invert_ts_clock != 0);	/* If TRUE; invert CLK signals */

	/* If TRUE; static MPEG clockrate will be used;
	   otherwise clockrate will adapt to the bitrate of the TS */

	state->m_dvbt_bitrate = ul_dvbt_bitrate;
	state->m_dvbc_bitrate = ul_dvbc_bitrate;

	state->m_ts_data_strength = (ul_ts_data_strength & 0x07);

	/* Maximum bitrate in b/s in case static clockrate is selected */
	state->m_mpeg_ts_static_bitrate = 19392658;
	state->m_disable_te_ihandling = false;

	if (ul_insert_rs_byte)
		state->m_insert_rs_byte = true;

	state->m_mpeg_lock_time_out = DEFAULT_DRXK_MPEG_LOCK_TIMEOUT;
	if (ul_mpeg_lock_time_out < 10000)
		state->m_mpeg_lock_time_out = ul_mpeg_lock_time_out;
	state->m_demod_lock_time_out = DEFAULT_DRXK_DEMOD_LOCK_TIMEOUT;
	if (ul_demod_lock_time_out < 10000)
		state->m_demod_lock_time_out = ul_demod_lock_time_out;

	/* QAM defaults */
	state->m_constellation = DRX_CONSTELLATION_AUTO;
	state->m_qam_interleave_mode = DRXK_QAM_I12_J17;
	state->m_fec_rs_plen = 204 * 8;	/* fecRsPlen  annex A */
	state->m_fec_rs_prescale = 1;

	state->m_sqi_speed = DRXK_DVBT_SQI_SPEED_MEDIUM;
	state->m_agcfast_clip_ctrl_delay = 0;

	state->m_gpio_cfg = ul_gpio_cfg;

	state->m_b_power_down = false;
	state->m_current_power_mode = DRX_POWER_DOWN;

	state->m_rfmirror = (ul_rf_mirror == 0);
	state->m_if_agc_pol = false;
	return 0;
}

static int drxx_open(struct drxk_state *state)
{
	int status = 0;
	u32 jtag = 0;
	u16 bid = 0;
	u16 key = 0;

	dprintk(1, "\n");
	/* stop lock indicator process */
	status = write16(state, SCU_RAM_GPIO__A,
			 SCU_RAM_GPIO_HW_LOCK_IND_DISABLE);
	if (status < 0)
		goto error;
	/* Check device id */
	status = read16(state, SIO_TOP_COMM_KEY__A, &key);
	if (status < 0)
		goto error;
	status = write16(state, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY);
	if (status < 0)
		goto error;
	status = read32(state, SIO_TOP_JTAGID_LO__A, &jtag);
	if (status < 0)
		goto error;
	status = read16(state, SIO_PDR_UIO_IN_HI__A, &bid);
	if (status < 0)
		goto error;
	status = write16(state, SIO_TOP_COMM_KEY__A, key);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int get_device_capabilities(struct drxk_state *state)
{
	u16 sio_pdr_ohw_cfg = 0;
	u32 sio_top_jtagid_lo = 0;
	int status;
	const char *spin = "";

	dprintk(1, "\n");

	/* driver 0.9.0 */
	/* stop lock indicator process */
	status = write16(state, SCU_RAM_GPIO__A,
			 SCU_RAM_GPIO_HW_LOCK_IND_DISABLE);
	if (status < 0)
		goto error;
	status = write16(state, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY);
	if (status < 0)
		goto error;
	status = read16(state, SIO_PDR_OHW_CFG__A, &sio_pdr_ohw_cfg);
	if (status < 0)
		goto error;
	status = write16(state, SIO_TOP_COMM_KEY__A, 0x0000);
	if (status < 0)
		goto error;

	switch ((sio_pdr_ohw_cfg & SIO_PDR_OHW_CFG_FREF_SEL__M)) {
	case 0:
		/* ignore (bypass ?) */
		break;
	case 1:
		/* 27 MHz */
		state->m_osc_clock_freq = 27000;
		break;
	case 2:
		/* 20.25 MHz */
		state->m_osc_clock_freq = 20250;
		break;
	case 3:
		/* 4 MHz */
		state->m_osc_clock_freq = 20250;
		break;
	default:
		pr_err("Clock Frequency is unknown\n");
		return -EINVAL;
	}
	/*
		Determine device capabilities
		Based on pinning v14
		*/
	status = read32(state, SIO_TOP_JTAGID_LO__A, &sio_top_jtagid_lo);
	if (status < 0)
		goto error;

	pr_info("status = 0x%08x\n", sio_top_jtagid_lo);

	/* driver 0.9.0 */
	switch ((sio_top_jtagid_lo >> 29) & 0xF) {
	case 0:
		state->m_device_spin = DRXK_SPIN_A1;
		spin = "A1";
		break;
	case 2:
		state->m_device_spin = DRXK_SPIN_A2;
		spin = "A2";
		break;
	case 3:
		state->m_device_spin = DRXK_SPIN_A3;
		spin = "A3";
		break;
	default:
		state->m_device_spin = DRXK_SPIN_UNKNOWN;
		status = -EINVAL;
		pr_err("Spin %d unknown\n", (sio_top_jtagid_lo >> 29) & 0xF);
		goto error2;
	}
	switch ((sio_top_jtagid_lo >> 12) & 0xFF) {
	case 0x13:
		/* typeId = DRX3913K_TYPE_ID */
		state->m_has_lna = false;
		state->m_has_oob = false;
		state->m_has_atv = false;
		state->m_has_audio = false;
		state->m_has_dvbt = true;
		state->m_has_dvbc = true;
		state->m_has_sawsw = true;
		state->m_has_gpio2 = false;
		state->m_has_gpio1 = false;
		state->m_has_irqn = false;
		break;
	case 0x15:
		/* typeId = DRX3915K_TYPE_ID */
		state->m_has_lna = false;
		state->m_has_oob = false;
		state->m_has_atv = true;
		state->m_has_audio = false;
		state->m_has_dvbt = true;
		state->m_has_dvbc = false;
		state->m_has_sawsw = true;
		state->m_has_gpio2 = true;
		state->m_has_gpio1 = true;
		state->m_has_irqn = false;
		break;
	case 0x16:
		/* typeId = DRX3916K_TYPE_ID */
		state->m_has_lna = false;
		state->m_has_oob = false;
		state->m_has_atv = true;
		state->m_has_audio = false;
		state->m_has_dvbt = true;
		state->m_has_dvbc = false;
		state->m_has_sawsw = true;
		state->m_has_gpio2 = true;
		state->m_has_gpio1 = true;
		state->m_has_irqn = false;
		break;
	case 0x18:
		/* typeId = DRX3918K_TYPE_ID */
		state->m_has_lna = false;
		state->m_has_oob = false;
		state->m_has_atv = true;
		state->m_has_audio = true;
		state->m_has_dvbt = true;
		state->m_has_dvbc = false;
		state->m_has_sawsw = true;
		state->m_has_gpio2 = true;
		state->m_has_gpio1 = true;
		state->m_has_irqn = false;
		break;
	case 0x21:
		/* typeId = DRX3921K_TYPE_ID */
		state->m_has_lna = false;
		state->m_has_oob = false;
		state->m_has_atv = true;
		state->m_has_audio = true;
		state->m_has_dvbt = true;
		state->m_has_dvbc = true;
		state->m_has_sawsw = true;
		state->m_has_gpio2 = true;
		state->m_has_gpio1 = true;
		state->m_has_irqn = false;
		break;
	case 0x23:
		/* typeId = DRX3923K_TYPE_ID */
		state->m_has_lna = false;
		state->m_has_oob = false;
		state->m_has_atv = true;
		state->m_has_audio = true;
		state->m_has_dvbt = true;
		state->m_has_dvbc = true;
		state->m_has_sawsw = true;
		state->m_has_gpio2 = true;
		state->m_has_gpio1 = true;
		state->m_has_irqn = false;
		break;
	case 0x25:
		/* typeId = DRX3925K_TYPE_ID */
		state->m_has_lna = false;
		state->m_has_oob = false;
		state->m_has_atv = true;
		state->m_has_audio = true;
		state->m_has_dvbt = true;
		state->m_has_dvbc = true;
		state->m_has_sawsw = true;
		state->m_has_gpio2 = true;
		state->m_has_gpio1 = true;
		state->m_has_irqn = false;
		break;
	case 0x26:
		/* typeId = DRX3926K_TYPE_ID */
		state->m_has_lna = false;
		state->m_has_oob = false;
		state->m_has_atv = true;
		state->m_has_audio = false;
		state->m_has_dvbt = true;
		state->m_has_dvbc = true;
		state->m_has_sawsw = true;
		state->m_has_gpio2 = true;
		state->m_has_gpio1 = true;
		state->m_has_irqn = false;
		break;
	default:
		pr_err("DeviceID 0x%02x not supported\n",
			((sio_top_jtagid_lo >> 12) & 0xFF));
		status = -EINVAL;
		goto error2;
	}

	pr_info("detected a drx-39%02xk, spin %s, xtal %d.%03d MHz\n",
	       ((sio_top_jtagid_lo >> 12) & 0xFF), spin,
	       state->m_osc_clock_freq / 1000,
	       state->m_osc_clock_freq % 1000);

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

error2:
	return status;
}

static int hi_command(struct drxk_state *state, u16 cmd, u16 *p_result)
{
	int status;
	bool powerdown_cmd;

	dprintk(1, "\n");

	/* Write command */
	status = write16(state, SIO_HI_RA_RAM_CMD__A, cmd);
	if (status < 0)
		goto error;
	if (cmd == SIO_HI_RA_RAM_CMD_RESET)
		usleep_range(1000, 2000);

	powerdown_cmd =
	    (bool) ((cmd == SIO_HI_RA_RAM_CMD_CONFIG) &&
		    ((state->m_hi_cfg_ctrl) &
		     SIO_HI_RA_RAM_PAR_5_CFG_SLEEP__M) ==
		    SIO_HI_RA_RAM_PAR_5_CFG_SLEEP_ZZZ);
	if (!powerdown_cmd) {
		/* Wait until command rdy */
		u32 retry_count = 0;
		u16 wait_cmd;

		do {
			usleep_range(1000, 2000);
			retry_count += 1;
			status = read16(state, SIO_HI_RA_RAM_CMD__A,
					  &wait_cmd);
		} while ((status < 0) && (retry_count < DRXK_MAX_RETRIES)
			 && (wait_cmd != 0));
		if (status < 0)
			goto error;
		status = read16(state, SIO_HI_RA_RAM_RES__A, p_result);
	}
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	return status;
}

static int hi_cfg_command(struct drxk_state *state)
{
	int status;

	dprintk(1, "\n");

	mutex_lock(&state->mutex);

	status = write16(state, SIO_HI_RA_RAM_PAR_6__A,
			 state->m_hi_cfg_timeout);
	if (status < 0)
		goto error;
	status = write16(state, SIO_HI_RA_RAM_PAR_5__A,
			 state->m_hi_cfg_ctrl);
	if (status < 0)
		goto error;
	status = write16(state, SIO_HI_RA_RAM_PAR_4__A,
			 state->m_hi_cfg_wake_up_key);
	if (status < 0)
		goto error;
	status = write16(state, SIO_HI_RA_RAM_PAR_3__A,
			 state->m_hi_cfg_bridge_delay);
	if (status < 0)
		goto error;
	status = write16(state, SIO_HI_RA_RAM_PAR_2__A,
			 state->m_hi_cfg_timing_div);
	if (status < 0)
		goto error;
	status = write16(state, SIO_HI_RA_RAM_PAR_1__A,
			 SIO_HI_RA_RAM_PAR_1_PAR1_SEC_KEY);
	if (status < 0)
		goto error;
	status = hi_command(state, SIO_HI_RA_RAM_CMD_CONFIG, NULL);
	if (status < 0)
		goto error;

	state->m_hi_cfg_ctrl &= ~SIO_HI_RA_RAM_PAR_5_CFG_SLEEP_ZZZ;
error:
	mutex_unlock(&state->mutex);
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int init_hi(struct drxk_state *state)
{
	dprintk(1, "\n");

	state->m_hi_cfg_wake_up_key = (state->demod_address << 1);
	state->m_hi_cfg_timeout = 0x96FF;
	/* port/bridge/power down ctrl */
	state->m_hi_cfg_ctrl = SIO_HI_RA_RAM_PAR_5_CFG_SLV0_SLAVE;

	return hi_cfg_command(state);
}

static int mpegts_configure_pins(struct drxk_state *state, bool mpeg_enable)
{
	int status = -1;
	u16 sio_pdr_mclk_cfg = 0;
	u16 sio_pdr_mdx_cfg = 0;
	u16 err_cfg = 0;

	dprintk(1, ": mpeg %s, %s mode\n",
		mpeg_enable ? "enable" : "disable",
		state->m_enable_parallel ? "parallel" : "serial");

	/* stop lock indicator process */
	status = write16(state, SCU_RAM_GPIO__A,
			 SCU_RAM_GPIO_HW_LOCK_IND_DISABLE);
	if (status < 0)
		goto error;

	/*  MPEG TS pad configuration */
	status = write16(state, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY);
	if (status < 0)
		goto error;

	if (!mpeg_enable) {
		/*  Set MPEG TS pads to inputmode */
		status = write16(state, SIO_PDR_MSTRT_CFG__A, 0x0000);
		if (status < 0)
			goto error;
		status = write16(state, SIO_PDR_MERR_CFG__A, 0x0000);
		if (status < 0)
			goto error;
		status = write16(state, SIO_PDR_MCLK_CFG__A, 0x0000);
		if (status < 0)
			goto error;
		status = write16(state, SIO_PDR_MVAL_CFG__A, 0x0000);
		if (status < 0)
			goto error;
		status = write16(state, SIO_PDR_MD0_CFG__A, 0x0000);
		if (status < 0)
			goto error;
		status = write16(state, SIO_PDR_MD1_CFG__A, 0x0000);
		if (status < 0)
			goto error;
		status = write16(state, SIO_PDR_MD2_CFG__A, 0x0000);
		if (status < 0)
			goto error;
		status = write16(state, SIO_PDR_MD3_CFG__A, 0x0000);
		if (status < 0)
			goto error;
		status = write16(state, SIO_PDR_MD4_CFG__A, 0x0000);
		if (status < 0)
			goto error;
		status = write16(state, SIO_PDR_MD5_CFG__A, 0x0000);
		if (status < 0)
			goto error;
		status = write16(state, SIO_PDR_MD6_CFG__A, 0x0000);
		if (status < 0)
			goto error;
		status = write16(state, SIO_PDR_MD7_CFG__A, 0x0000);
		if (status < 0)
			goto error;
	} else {
		/* Enable MPEG output */
		sio_pdr_mdx_cfg =
			((state->m_ts_data_strength <<
			SIO_PDR_MD0_CFG_DRIVE__B) | 0x0003);
		sio_pdr_mclk_cfg = ((state->m_ts_clockk_strength <<
					SIO_PDR_MCLK_CFG_DRIVE__B) |
					0x0003);

		status = write16(state, SIO_PDR_MSTRT_CFG__A, sio_pdr_mdx_cfg);
		if (status < 0)
			goto error;

		if (state->enable_merr_cfg)
			err_cfg = sio_pdr_mdx_cfg;

		status = write16(state, SIO_PDR_MERR_CFG__A, err_cfg);
		if (status < 0)
			goto error;
		status = write16(state, SIO_PDR_MVAL_CFG__A, err_cfg);
		if (status < 0)
			goto error;

		if (state->m_enable_parallel) {
			/* parallel -> enable MD1 to MD7 */
			status = write16(state, SIO_PDR_MD1_CFG__A,
					 sio_pdr_mdx_cfg);
			if (status < 0)
				goto error;
			status = write16(state, SIO_PDR_MD2_CFG__A,
					 sio_pdr_mdx_cfg);
			if (status < 0)
				goto error;
			status = write16(state, SIO_PDR_MD3_CFG__A,
					 sio_pdr_mdx_cfg);
			if (status < 0)
				goto error;
			status = write16(state, SIO_PDR_MD4_CFG__A,
					 sio_pdr_mdx_cfg);
			if (status < 0)
				goto error;
			status = write16(state, SIO_PDR_MD5_CFG__A,
					 sio_pdr_mdx_cfg);
			if (status < 0)
				goto error;
			status = write16(state, SIO_PDR_MD6_CFG__A,
					 sio_pdr_mdx_cfg);
			if (status < 0)
				goto error;
			status = write16(state, SIO_PDR_MD7_CFG__A,
					 sio_pdr_mdx_cfg);
			if (status < 0)
				goto error;
		} else {
			sio_pdr_mdx_cfg = ((state->m_ts_data_strength <<
						SIO_PDR_MD0_CFG_DRIVE__B)
					| 0x0003);
			/* serial -> disable MD1 to MD7 */
			status = write16(state, SIO_PDR_MD1_CFG__A, 0x0000);
			if (status < 0)
				goto error;
			status = write16(state, SIO_PDR_MD2_CFG__A, 0x0000);
			if (status < 0)
				goto error;
			status = write16(state, SIO_PDR_MD3_CFG__A, 0x0000);
			if (status < 0)
				goto error;
			status = write16(state, SIO_PDR_MD4_CFG__A, 0x0000);
			if (status < 0)
				goto error;
			status = write16(state, SIO_PDR_MD5_CFG__A, 0x0000);
			if (status < 0)
				goto error;
			status = write16(state, SIO_PDR_MD6_CFG__A, 0x0000);
			if (status < 0)
				goto error;
			status = write16(state, SIO_PDR_MD7_CFG__A, 0x0000);
			if (status < 0)
				goto error;
		}
		status = write16(state, SIO_PDR_MCLK_CFG__A, sio_pdr_mclk_cfg);
		if (status < 0)
			goto error;
		status = write16(state, SIO_PDR_MD0_CFG__A, sio_pdr_mdx_cfg);
		if (status < 0)
			goto error;
	}
	/*  Enable MB output over MPEG pads and ctl input */
	status = write16(state, SIO_PDR_MON_CFG__A, 0x0000);
	if (status < 0)
		goto error;
	/*  Write nomagic word to enable pdr reg write */
	status = write16(state, SIO_TOP_COMM_KEY__A, 0x0000);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int mpegts_disable(struct drxk_state *state)
{
	dprintk(1, "\n");

	return mpegts_configure_pins(state, false);
}

static int bl_chain_cmd(struct drxk_state *state,
		      u16 rom_offset, u16 nr_of_elements, u32 time_out)
{
	u16 bl_status = 0;
	int status;
	unsigned long end;

	dprintk(1, "\n");
	mutex_lock(&state->mutex);
	status = write16(state, SIO_BL_MODE__A, SIO_BL_MODE_CHAIN);
	if (status < 0)
		goto error;
	status = write16(state, SIO_BL_CHAIN_ADDR__A, rom_offset);
	if (status < 0)
		goto error;
	status = write16(state, SIO_BL_CHAIN_LEN__A, nr_of_elements);
	if (status < 0)
		goto error;
	status = write16(state, SIO_BL_ENABLE__A, SIO_BL_ENABLE_ON);
	if (status < 0)
		goto error;

	end = jiffies + msecs_to_jiffies(time_out);
	do {
		usleep_range(1000, 2000);
		status = read16(state, SIO_BL_STATUS__A, &bl_status);
		if (status < 0)
			goto error;
	} while ((bl_status == 0x1) &&
			((time_is_after_jiffies(end))));

	if (bl_status == 0x1) {
		pr_err("SIO not ready\n");
		status = -EINVAL;
		goto error2;
	}
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
error2:
	mutex_unlock(&state->mutex);
	return status;
}


static int download_microcode(struct drxk_state *state,
			     const u8 p_mc_image[], u32 length)
{
	const u8 *p_src = p_mc_image;
	u32 address;
	u16 n_blocks;
	u16 block_size;
	u32 offset = 0;
	u32 i;
	int status = 0;

	dprintk(1, "\n");

	/* down the drain (we don't care about MAGIC_WORD) */
#if 0
	/* For future reference */
	drain = (p_src[0] << 8) | p_src[1];
#endif
	p_src += sizeof(u16);
	offset += sizeof(u16);
	n_blocks = (p_src[0] << 8) | p_src[1];
	p_src += sizeof(u16);
	offset += sizeof(u16);

	for (i = 0; i < n_blocks; i += 1) {
		address = (p_src[0] << 24) | (p_src[1] << 16) |
		    (p_src[2] << 8) | p_src[3];
		p_src += sizeof(u32);
		offset += sizeof(u32);

		block_size = ((p_src[0] << 8) | p_src[1]) * sizeof(u16);
		p_src += sizeof(u16);
		offset += sizeof(u16);

#if 0
		/* For future reference */
		flags = (p_src[0] << 8) | p_src[1];
#endif
		p_src += sizeof(u16);
		offset += sizeof(u16);

#if 0
		/* For future reference */
		block_crc = (p_src[0] << 8) | p_src[1];
#endif
		p_src += sizeof(u16);
		offset += sizeof(u16);

		if (offset + block_size > length) {
			pr_err("Firmware is corrupted.\n");
			return -EINVAL;
		}

		status = write_block(state, address, block_size, p_src);
		if (status < 0) {
			pr_err("Error %d while loading firmware\n", status);
			break;
		}
		p_src += block_size;
		offset += block_size;
	}
	return status;
}

static int dvbt_enable_ofdm_token_ring(struct drxk_state *state, bool enable)
{
	int status;
	u16 data = 0;
	u16 desired_ctrl = SIO_OFDM_SH_OFDM_RING_ENABLE_ON;
	u16 desired_status = SIO_OFDM_SH_OFDM_RING_STATUS_ENABLED;
	unsigned long end;

	dprintk(1, "\n");

	if (!enable) {
		desired_ctrl = SIO_OFDM_SH_OFDM_RING_ENABLE_OFF;
		desired_status = SIO_OFDM_SH_OFDM_RING_STATUS_DOWN;
	}

	status = read16(state, SIO_OFDM_SH_OFDM_RING_STATUS__A, &data);
	if (status >= 0 && data == desired_status) {
		/* tokenring already has correct status */
		return status;
	}
	/* Disable/enable dvbt tokenring bridge   */
	status = write16(state, SIO_OFDM_SH_OFDM_RING_ENABLE__A, desired_ctrl);

	end = jiffies + msecs_to_jiffies(DRXK_OFDM_TR_SHUTDOWN_TIMEOUT);
	do {
		status = read16(state, SIO_OFDM_SH_OFDM_RING_STATUS__A, &data);
		if ((status >= 0 && data == desired_status)
		    || time_is_after_jiffies(end))
			break;
		usleep_range(1000, 2000);
	} while (1);
	if (data != desired_status) {
		pr_err("SIO not ready\n");
		return -EINVAL;
	}
	return status;
}

static int mpegts_stop(struct drxk_state *state)
{
	int status = 0;
	u16 fec_oc_snc_mode = 0;
	u16 fec_oc_ipr_mode = 0;

	dprintk(1, "\n");

	/* Graceful shutdown (byte boundaries) */
	status = read16(state, FEC_OC_SNC_MODE__A, &fec_oc_snc_mode);
	if (status < 0)
		goto error;
	fec_oc_snc_mode |= FEC_OC_SNC_MODE_SHUTDOWN__M;
	status = write16(state, FEC_OC_SNC_MODE__A, fec_oc_snc_mode);
	if (status < 0)
		goto error;

	/* Suppress MCLK during absence of data */
	status = read16(state, FEC_OC_IPR_MODE__A, &fec_oc_ipr_mode);
	if (status < 0)
		goto error;
	fec_oc_ipr_mode |= FEC_OC_IPR_MODE_MCLK_DIS_DAT_ABS__M;
	status = write16(state, FEC_OC_IPR_MODE__A, fec_oc_ipr_mode);

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	return status;
}

static int scu_command(struct drxk_state *state,
		       u16 cmd, u8 parameter_len,
		       u16 *parameter, u8 result_len, u16 *result)
{
#if (SCU_RAM_PARAM_0__A - SCU_RAM_PARAM_15__A) != 15
#error DRXK register mapping no longer compatible with this routine!
#endif
	u16 cur_cmd = 0;
	int status = -EINVAL;
	unsigned long end;
	u8 buffer[34];
	int cnt = 0, ii;
	const char *p;
	char errname[30];

	dprintk(1, "\n");

	if ((cmd == 0) || ((parameter_len > 0) && (parameter == NULL)) ||
	    ((result_len > 0) && (result == NULL))) {
		pr_err("Error %d on %s\n", status, __func__);
		return status;
	}

	mutex_lock(&state->mutex);

	/* assume that the command register is ready
		since it is checked afterwards */
	for (ii = parameter_len - 1; ii >= 0; ii -= 1) {
		buffer[cnt++] = (parameter[ii] & 0xFF);
		buffer[cnt++] = ((parameter[ii] >> 8) & 0xFF);
	}
	buffer[cnt++] = (cmd & 0xFF);
	buffer[cnt++] = ((cmd >> 8) & 0xFF);

	write_block(state, SCU_RAM_PARAM_0__A -
			(parameter_len - 1), cnt, buffer);
	/* Wait until SCU has processed command */
	end = jiffies + msecs_to_jiffies(DRXK_MAX_WAITTIME);
	do {
		usleep_range(1000, 2000);
		status = read16(state, SCU_RAM_COMMAND__A, &cur_cmd);
		if (status < 0)
			goto error;
	} while (!(cur_cmd == DRX_SCU_READY) && (time_is_after_jiffies(end)));
	if (cur_cmd != DRX_SCU_READY) {
		pr_err("SCU not ready\n");
		status = -EIO;
		goto error2;
	}
	/* read results */
	if ((result_len > 0) && (result != NULL)) {
		s16 err;
		int ii;

		for (ii = result_len - 1; ii >= 0; ii -= 1) {
			status = read16(state, SCU_RAM_PARAM_0__A - ii,
					&result[ii]);
			if (status < 0)
				goto error;
		}

		/* Check if an error was reported by SCU */
		err = (s16)result[0];
		if (err >= 0)
			goto error;

		/* check for the known error codes */
		switch (err) {
		case SCU_RESULT_UNKCMD:
			p = "SCU_RESULT_UNKCMD";
			break;
		case SCU_RESULT_UNKSTD:
			p = "SCU_RESULT_UNKSTD";
			break;
		case SCU_RESULT_SIZE:
			p = "SCU_RESULT_SIZE";
			break;
		case SCU_RESULT_INVPAR:
			p = "SCU_RESULT_INVPAR";
			break;
		default: /* Other negative values are errors */
			sprintf(errname, "ERROR: %d\n", err);
			p = errname;
		}
		pr_err("%s while sending cmd 0x%04x with params:", p, cmd);
		print_hex_dump_bytes("drxk: ", DUMP_PREFIX_NONE, buffer, cnt);
		status = -EINVAL;
		goto error2;
	}

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
error2:
	mutex_unlock(&state->mutex);
	return status;
}

static int set_iqm_af(struct drxk_state *state, bool active)
{
	u16 data = 0;
	int status;

	dprintk(1, "\n");

	/* Configure IQM */
	status = read16(state, IQM_AF_STDBY__A, &data);
	if (status < 0)
		goto error;

	if (!active) {
		data |= (IQM_AF_STDBY_STDBY_ADC_STANDBY
				| IQM_AF_STDBY_STDBY_AMP_STANDBY
				| IQM_AF_STDBY_STDBY_PD_STANDBY
				| IQM_AF_STDBY_STDBY_TAGC_IF_STANDBY
				| IQM_AF_STDBY_STDBY_TAGC_RF_STANDBY);
	} else {
		data &= ((~IQM_AF_STDBY_STDBY_ADC_STANDBY)
				& (~IQM_AF_STDBY_STDBY_AMP_STANDBY)
				& (~IQM_AF_STDBY_STDBY_PD_STANDBY)
				& (~IQM_AF_STDBY_STDBY_TAGC_IF_STANDBY)
				& (~IQM_AF_STDBY_STDBY_TAGC_RF_STANDBY)
			);
	}
	status = write16(state, IQM_AF_STDBY__A, data);

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int ctrl_power_mode(struct drxk_state *state, enum drx_power_mode *mode)
{
	int status = 0;
	u16 sio_cc_pwd_mode = 0;

	dprintk(1, "\n");

	/* Check arguments */
	if (mode == NULL)
		return -EINVAL;

	switch (*mode) {
	case DRX_POWER_UP:
		sio_cc_pwd_mode = SIO_CC_PWD_MODE_LEVEL_NONE;
		break;
	case DRXK_POWER_DOWN_OFDM:
		sio_cc_pwd_mode = SIO_CC_PWD_MODE_LEVEL_OFDM;
		break;
	case DRXK_POWER_DOWN_CORE:
		sio_cc_pwd_mode = SIO_CC_PWD_MODE_LEVEL_CLOCK;
		break;
	case DRXK_POWER_DOWN_PLL:
		sio_cc_pwd_mode = SIO_CC_PWD_MODE_LEVEL_PLL;
		break;
	case DRX_POWER_DOWN:
		sio_cc_pwd_mode = SIO_CC_PWD_MODE_LEVEL_OSC;
		break;
	default:
		/* Unknow sleep mode */
		return -EINVAL;
	}

	/* If already in requested power mode, do nothing */
	if (state->m_current_power_mode == *mode)
		return 0;

	/* For next steps make sure to start from DRX_POWER_UP mode */
	if (state->m_current_power_mode != DRX_POWER_UP) {
		status = power_up_device(state);
		if (status < 0)
			goto error;
		status = dvbt_enable_ofdm_token_ring(state, true);
		if (status < 0)
			goto error;
	}

	if (*mode == DRX_POWER_UP) {
		/* Restore analog & pin configuration */
	} else {
		/* Power down to requested mode */
		/* Backup some register settings */
		/* Set pins with possible pull-ups connected
		   to them in input mode */
		/* Analog power down */
		/* ADC power down */
		/* Power down device */
		/* stop all comm_exec */
		/* Stop and power down previous standard */
		switch (state->m_operation_mode) {
		case OM_DVBT:
			status = mpegts_stop(state);
			if (status < 0)
				goto error;
			status = power_down_dvbt(state, false);
			if (status < 0)
				goto error;
			break;
		case OM_QAM_ITU_A:
		case OM_QAM_ITU_C:
			status = mpegts_stop(state);
			if (status < 0)
				goto error;
			status = power_down_qam(state);
			if (status < 0)
				goto error;
			break;
		default:
			break;
		}
		status = dvbt_enable_ofdm_token_ring(state, false);
		if (status < 0)
			goto error;
		status = write16(state, SIO_CC_PWD_MODE__A, sio_cc_pwd_mode);
		if (status < 0)
			goto error;
		status = write16(state, SIO_CC_UPDATE__A, SIO_CC_UPDATE_KEY);
		if (status < 0)
			goto error;

		if (*mode != DRXK_POWER_DOWN_OFDM) {
			state->m_hi_cfg_ctrl |=
				SIO_HI_RA_RAM_PAR_5_CFG_SLEEP_ZZZ;
			status = hi_cfg_command(state);
			if (status < 0)
				goto error;
		}
	}
	state->m_current_power_mode = *mode;

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	return status;
}

static int power_down_dvbt(struct drxk_state *state, bool set_power_mode)
{
	enum drx_power_mode power_mode = DRXK_POWER_DOWN_OFDM;
	u16 cmd_result = 0;
	u16 data = 0;
	int status;

	dprintk(1, "\n");

	status = read16(state, SCU_COMM_EXEC__A, &data);
	if (status < 0)
		goto error;
	if (data == SCU_COMM_EXEC_ACTIVE) {
		/* Send OFDM stop command */
		status = scu_command(state,
				     SCU_RAM_COMMAND_STANDARD_OFDM
				     | SCU_RAM_COMMAND_CMD_DEMOD_STOP,
				     0, NULL, 1, &cmd_result);
		if (status < 0)
			goto error;
		/* Send OFDM reset command */
		status = scu_command(state,
				     SCU_RAM_COMMAND_STANDARD_OFDM
				     | SCU_RAM_COMMAND_CMD_DEMOD_RESET,
				     0, NULL, 1, &cmd_result);
		if (status < 0)
			goto error;
	}

	/* Reset datapath for OFDM, processors first */
	status = write16(state, OFDM_SC_COMM_EXEC__A, OFDM_SC_COMM_EXEC_STOP);
	if (status < 0)
		goto error;
	status = write16(state, OFDM_LC_COMM_EXEC__A, OFDM_LC_COMM_EXEC_STOP);
	if (status < 0)
		goto error;
	status = write16(state, IQM_COMM_EXEC__A, IQM_COMM_EXEC_B_STOP);
	if (status < 0)
		goto error;

	/* powerdown AFE                   */
	status = set_iqm_af(state, false);
	if (status < 0)
		goto error;

	/* powerdown to OFDM mode          */
	if (set_power_mode) {
		status = ctrl_power_mode(state, &power_mode);
		if (status < 0)
			goto error;
	}
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int setoperation_mode(struct drxk_state *state,
			    enum operation_mode o_mode)
{
	int status = 0;

	dprintk(1, "\n");
	/*
	   Stop and power down previous standard
	   TODO investigate total power down instead of partial
	   power down depending on "previous" standard.
	 */

	/* disable HW lock indicator */
	status = write16(state, SCU_RAM_GPIO__A,
			 SCU_RAM_GPIO_HW_LOCK_IND_DISABLE);
	if (status < 0)
		goto error;

	/* Device is already at the required mode */
	if (state->m_operation_mode == o_mode)
		return 0;

	switch (state->m_operation_mode) {
		/* OM_NONE was added for start up */
	case OM_NONE:
		break;
	case OM_DVBT:
		status = mpegts_stop(state);
		if (status < 0)
			goto error;
		status = power_down_dvbt(state, true);
		if (status < 0)
			goto error;
		state->m_operation_mode = OM_NONE;
		break;
	case OM_QAM_ITU_A:	/* fallthrough */
	case OM_QAM_ITU_C:
		status = mpegts_stop(state);
		if (status < 0)
			goto error;
		status = power_down_qam(state);
		if (status < 0)
			goto error;
		state->m_operation_mode = OM_NONE;
		break;
	case OM_QAM_ITU_B:
	default:
		status = -EINVAL;
		goto error;
	}

	/*
		Power up new standard
		*/
	switch (o_mode) {
	case OM_DVBT:
		dprintk(1, ": DVB-T\n");
		state->m_operation_mode = o_mode;
		status = set_dvbt_standard(state, o_mode);
		if (status < 0)
			goto error;
		break;
	case OM_QAM_ITU_A:	/* fallthrough */
	case OM_QAM_ITU_C:
		dprintk(1, ": DVB-C Annex %c\n",
			(state->m_operation_mode == OM_QAM_ITU_A) ? 'A' : 'C');
		state->m_operation_mode = o_mode;
		status = set_qam_standard(state, o_mode);
		if (status < 0)
			goto error;
		break;
	case OM_QAM_ITU_B:
	default:
		status = -EINVAL;
	}
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int start(struct drxk_state *state, s32 offset_freq,
		 s32 intermediate_frequency)
{
	int status = -EINVAL;

	u16 i_freqk_hz;
	s32 offsetk_hz = offset_freq / 1000;

	dprintk(1, "\n");
	if (state->m_drxk_state != DRXK_STOPPED &&
		state->m_drxk_state != DRXK_DTV_STARTED)
		goto error;

	state->m_b_mirror_freq_spect = (state->props.inversion == INVERSION_ON);

	if (intermediate_frequency < 0) {
		state->m_b_mirror_freq_spect = !state->m_b_mirror_freq_spect;
		intermediate_frequency = -intermediate_frequency;
	}

	switch (state->m_operation_mode) {
	case OM_QAM_ITU_A:
	case OM_QAM_ITU_C:
		i_freqk_hz = (intermediate_frequency / 1000);
		status = set_qam(state, i_freqk_hz, offsetk_hz);
		if (status < 0)
			goto error;
		state->m_drxk_state = DRXK_DTV_STARTED;
		break;
	case OM_DVBT:
		i_freqk_hz = (intermediate_frequency / 1000);
		status = mpegts_stop(state);
		if (status < 0)
			goto error;
		status = set_dvbt(state, i_freqk_hz, offsetk_hz);
		if (status < 0)
			goto error;
		status = dvbt_start(state);
		if (status < 0)
			goto error;
		state->m_drxk_state = DRXK_DTV_STARTED;
		break;
	default:
		break;
	}
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int shut_down(struct drxk_state *state)
{
	dprintk(1, "\n");

	mpegts_stop(state);
	return 0;
}

static int get_lock_status(struct drxk_state *state, u32 *p_lock_status)
{
	int status = -EINVAL;

	dprintk(1, "\n");

	if (p_lock_status == NULL)
		goto error;

	*p_lock_status = NOT_LOCKED;

	/* define the SCU command code */
	switch (state->m_operation_mode) {
	case OM_QAM_ITU_A:
	case OM_QAM_ITU_B:
	case OM_QAM_ITU_C:
		status = get_qam_lock_status(state, p_lock_status);
		break;
	case OM_DVBT:
		status = get_dvbt_lock_status(state, p_lock_status);
		break;
	default:
		pr_debug("Unsupported operation mode %d in %s\n",
			state->m_operation_mode, __func__);
		return 0;
	}
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int mpegts_start(struct drxk_state *state)
{
	int status;

	u16 fec_oc_snc_mode = 0;

	/* Allow OC to sync again */
	status = read16(state, FEC_OC_SNC_MODE__A, &fec_oc_snc_mode);
	if (status < 0)
		goto error;
	fec_oc_snc_mode &= ~FEC_OC_SNC_MODE_SHUTDOWN__M;
	status = write16(state, FEC_OC_SNC_MODE__A, fec_oc_snc_mode);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_SNC_UNLOCK__A, 1);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int mpegts_dto_init(struct drxk_state *state)
{
	int status;

	dprintk(1, "\n");

	/* Rate integration settings */
	status = write16(state, FEC_OC_RCN_CTL_STEP_LO__A, 0x0000);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_RCN_CTL_STEP_HI__A, 0x000C);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_RCN_GAIN__A, 0x000A);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_AVR_PARM_A__A, 0x0008);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_AVR_PARM_B__A, 0x0006);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_TMD_HI_MARGIN__A, 0x0680);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_TMD_LO_MARGIN__A, 0x0080);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_TMD_COUNT__A, 0x03F4);
	if (status < 0)
		goto error;

	/* Additional configuration */
	status = write16(state, FEC_OC_OCR_INVERT__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_SNC_LWM__A, 2);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_SNC_HWM__A, 12);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	return status;
}

static int mpegts_dto_setup(struct drxk_state *state,
			  enum operation_mode o_mode)
{
	int status;

	u16 fec_oc_reg_mode = 0;	/* FEC_OC_MODE       register value */
	u16 fec_oc_reg_ipr_mode = 0;	/* FEC_OC_IPR_MODE   register value */
	u16 fec_oc_dto_mode = 0;	/* FEC_OC_IPR_INVERT register value */
	u16 fec_oc_fct_mode = 0;	/* FEC_OC_IPR_INVERT register value */
	u16 fec_oc_dto_period = 2;	/* FEC_OC_IPR_INVERT register value */
	u16 fec_oc_dto_burst_len = 188;	/* FEC_OC_IPR_INVERT register value */
	u32 fec_oc_rcn_ctl_rate = 0;	/* FEC_OC_IPR_INVERT register value */
	u16 fec_oc_tmd_mode = 0;
	u16 fec_oc_tmd_int_upd_rate = 0;
	u32 max_bit_rate = 0;
	bool static_clk = false;

	dprintk(1, "\n");

	/* Check insertion of the Reed-Solomon parity bytes */
	status = read16(state, FEC_OC_MODE__A, &fec_oc_reg_mode);
	if (status < 0)
		goto error;
	status = read16(state, FEC_OC_IPR_MODE__A, &fec_oc_reg_ipr_mode);
	if (status < 0)
		goto error;
	fec_oc_reg_mode &= (~FEC_OC_MODE_PARITY__M);
	fec_oc_reg_ipr_mode &= (~FEC_OC_IPR_MODE_MVAL_DIS_PAR__M);
	if (state->m_insert_rs_byte) {
		/* enable parity symbol forward */
		fec_oc_reg_mode |= FEC_OC_MODE_PARITY__M;
		/* MVAL disable during parity bytes */
		fec_oc_reg_ipr_mode |= FEC_OC_IPR_MODE_MVAL_DIS_PAR__M;
		/* TS burst length to 204 */
		fec_oc_dto_burst_len = 204;
	}

	/* Check serial or parallel output */
	fec_oc_reg_ipr_mode &= (~(FEC_OC_IPR_MODE_SERIAL__M));
	if (!state->m_enable_parallel) {
		/* MPEG data output is serial -> set ipr_mode[0] */
		fec_oc_reg_ipr_mode |= FEC_OC_IPR_MODE_SERIAL__M;
	}

	switch (o_mode) {
	case OM_DVBT:
		max_bit_rate = state->m_dvbt_bitrate;
		fec_oc_tmd_mode = 3;
		fec_oc_rcn_ctl_rate = 0xC00000;
		static_clk = state->m_dvbt_static_clk;
		break;
	case OM_QAM_ITU_A:	/* fallthrough */
	case OM_QAM_ITU_C:
		fec_oc_tmd_mode = 0x0004;
		fec_oc_rcn_ctl_rate = 0xD2B4EE;	/* good for >63 Mb/s */
		max_bit_rate = state->m_dvbc_bitrate;
		static_clk = state->m_dvbc_static_clk;
		break;
	default:
		status = -EINVAL;
	}		/* switch (standard) */
	if (status < 0)
		goto error;

	/* Configure DTO's */
	if (static_clk) {
		u32 bit_rate = 0;

		/* Rational DTO for MCLK source (static MCLK rate),
			Dynamic DTO for optimal grouping
			(avoid intra-packet gaps),
			DTO offset enable to sync TS burst with MSTRT */
		fec_oc_dto_mode = (FEC_OC_DTO_MODE_DYNAMIC__M |
				FEC_OC_DTO_MODE_OFFSET_ENABLE__M);
		fec_oc_fct_mode = (FEC_OC_FCT_MODE_RAT_ENA__M |
				FEC_OC_FCT_MODE_VIRT_ENA__M);

		/* Check user defined bitrate */
		bit_rate = max_bit_rate;
		if (bit_rate > 75900000UL) {	/* max is 75.9 Mb/s */
			bit_rate = 75900000UL;
		}
		/* Rational DTO period:
			dto_period = (Fsys / bitrate) - 2

			result should be floored,
			to make sure >= requested bitrate
			*/
		fec_oc_dto_period = (u16) (((state->m_sys_clock_freq)
						* 1000) / bit_rate);
		if (fec_oc_dto_period <= 2)
			fec_oc_dto_period = 0;
		else
			fec_oc_dto_period -= 2;
		fec_oc_tmd_int_upd_rate = 8;
	} else {
		/* (commonAttr->static_clk == false) => dynamic mode */
		fec_oc_dto_mode = FEC_OC_DTO_MODE_DYNAMIC__M;
		fec_oc_fct_mode = FEC_OC_FCT_MODE__PRE;
		fec_oc_tmd_int_upd_rate = 5;
	}

	/* Write appropriate registers with requested configuration */
	status = write16(state, FEC_OC_DTO_BURST_LEN__A, fec_oc_dto_burst_len);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_DTO_PERIOD__A, fec_oc_dto_period);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_DTO_MODE__A, fec_oc_dto_mode);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_FCT_MODE__A, fec_oc_fct_mode);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_MODE__A, fec_oc_reg_mode);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_IPR_MODE__A, fec_oc_reg_ipr_mode);
	if (status < 0)
		goto error;

	/* Rate integration settings */
	status = write32(state, FEC_OC_RCN_CTL_RATE_LO__A, fec_oc_rcn_ctl_rate);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_TMD_INT_UPD_RATE__A,
			 fec_oc_tmd_int_upd_rate);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_TMD_MODE__A, fec_oc_tmd_mode);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int mpegts_configure_polarity(struct drxk_state *state)
{
	u16 fec_oc_reg_ipr_invert = 0;

	/* Data mask for the output data byte */
	u16 invert_data_mask =
	    FEC_OC_IPR_INVERT_MD7__M | FEC_OC_IPR_INVERT_MD6__M |
	    FEC_OC_IPR_INVERT_MD5__M | FEC_OC_IPR_INVERT_MD4__M |
	    FEC_OC_IPR_INVERT_MD3__M | FEC_OC_IPR_INVERT_MD2__M |
	    FEC_OC_IPR_INVERT_MD1__M | FEC_OC_IPR_INVERT_MD0__M;

	dprintk(1, "\n");

	/* Control selective inversion of output bits */
	fec_oc_reg_ipr_invert &= (~(invert_data_mask));
	if (state->m_invert_data)
		fec_oc_reg_ipr_invert |= invert_data_mask;
	fec_oc_reg_ipr_invert &= (~(FEC_OC_IPR_INVERT_MERR__M));
	if (state->m_invert_err)
		fec_oc_reg_ipr_invert |= FEC_OC_IPR_INVERT_MERR__M;
	fec_oc_reg_ipr_invert &= (~(FEC_OC_IPR_INVERT_MSTRT__M));
	if (state->m_invert_str)
		fec_oc_reg_ipr_invert |= FEC_OC_IPR_INVERT_MSTRT__M;
	fec_oc_reg_ipr_invert &= (~(FEC_OC_IPR_INVERT_MVAL__M));
	if (state->m_invert_val)
		fec_oc_reg_ipr_invert |= FEC_OC_IPR_INVERT_MVAL__M;
	fec_oc_reg_ipr_invert &= (~(FEC_OC_IPR_INVERT_MCLK__M));
	if (state->m_invert_clk)
		fec_oc_reg_ipr_invert |= FEC_OC_IPR_INVERT_MCLK__M;

	return write16(state, FEC_OC_IPR_INVERT__A, fec_oc_reg_ipr_invert);
}

#define   SCU_RAM_AGC_KI_INV_RF_POL__M 0x4000

static int set_agc_rf(struct drxk_state *state,
		    struct s_cfg_agc *p_agc_cfg, bool is_dtv)
{
	int status = -EINVAL;
	u16 data = 0;
	struct s_cfg_agc *p_if_agc_settings;

	dprintk(1, "\n");

	if (p_agc_cfg == NULL)
		goto error;

	switch (p_agc_cfg->ctrl_mode) {
	case DRXK_AGC_CTRL_AUTO:
		/* Enable RF AGC DAC */
		status = read16(state, IQM_AF_STDBY__A, &data);
		if (status < 0)
			goto error;
		data &= ~IQM_AF_STDBY_STDBY_TAGC_RF_STANDBY;
		status = write16(state, IQM_AF_STDBY__A, data);
		if (status < 0)
			goto error;
		status = read16(state, SCU_RAM_AGC_CONFIG__A, &data);
		if (status < 0)
			goto error;

		/* Enable SCU RF AGC loop */
		data &= ~SCU_RAM_AGC_CONFIG_DISABLE_RF_AGC__M;

		/* Polarity */
		if (state->m_rf_agc_pol)
			data |= SCU_RAM_AGC_CONFIG_INV_RF_POL__M;
		else
			data &= ~SCU_RAM_AGC_CONFIG_INV_RF_POL__M;
		status = write16(state, SCU_RAM_AGC_CONFIG__A, data);
		if (status < 0)
			goto error;

		/* Set speed (using complementary reduction value) */
		status = read16(state, SCU_RAM_AGC_KI_RED__A, &data);
		if (status < 0)
			goto error;

		data &= ~SCU_RAM_AGC_KI_RED_RAGC_RED__M;
		data |= (~(p_agc_cfg->speed <<
				SCU_RAM_AGC_KI_RED_RAGC_RED__B)
				& SCU_RAM_AGC_KI_RED_RAGC_RED__M);

		status = write16(state, SCU_RAM_AGC_KI_RED__A, data);
		if (status < 0)
			goto error;

		if (is_dvbt(state))
			p_if_agc_settings = &state->m_dvbt_if_agc_cfg;
		else if (is_qam(state))
			p_if_agc_settings = &state->m_qam_if_agc_cfg;
		else
			p_if_agc_settings = &state->m_atv_if_agc_cfg;
		if (p_if_agc_settings == NULL) {
			status = -EINVAL;
			goto error;
		}

		/* Set TOP, only if IF-AGC is in AUTO mode */
		if (p_if_agc_settings->ctrl_mode == DRXK_AGC_CTRL_AUTO) {
			status = write16(state,
					 SCU_RAM_AGC_IF_IACCU_HI_TGT_MAX__A,
					 p_agc_cfg->top);
			if (status < 0)
				goto error;
		}

		/* Cut-Off current */
		status = write16(state, SCU_RAM_AGC_RF_IACCU_HI_CO__A,
				 p_agc_cfg->cut_off_current);
		if (status < 0)
			goto error;

		/* Max. output level */
		status = write16(state, SCU_RAM_AGC_RF_MAX__A,
				 p_agc_cfg->max_output_level);
		if (status < 0)
			goto error;

		break;

	case DRXK_AGC_CTRL_USER:
		/* Enable RF AGC DAC */
		status = read16(state, IQM_AF_STDBY__A, &data);
		if (status < 0)
			goto error;
		data &= ~IQM_AF_STDBY_STDBY_TAGC_RF_STANDBY;
		status = write16(state, IQM_AF_STDBY__A, data);
		if (status < 0)
			goto error;

		/* Disable SCU RF AGC loop */
		status = read16(state, SCU_RAM_AGC_CONFIG__A, &data);
		if (status < 0)
			goto error;
		data |= SCU_RAM_AGC_CONFIG_DISABLE_RF_AGC__M;
		if (state->m_rf_agc_pol)
			data |= SCU_RAM_AGC_CONFIG_INV_RF_POL__M;
		else
			data &= ~SCU_RAM_AGC_CONFIG_INV_RF_POL__M;
		status = write16(state, SCU_RAM_AGC_CONFIG__A, data);
		if (status < 0)
			goto error;

		/* SCU c.o.c. to 0, enabling full control range */
		status = write16(state, SCU_RAM_AGC_RF_IACCU_HI_CO__A, 0);
		if (status < 0)
			goto error;

		/* Write value to output pin */
		status = write16(state, SCU_RAM_AGC_RF_IACCU_HI__A,
				 p_agc_cfg->output_level);
		if (status < 0)
			goto error;
		break;

	case DRXK_AGC_CTRL_OFF:
		/* Disable RF AGC DAC */
		status = read16(state, IQM_AF_STDBY__A, &data);
		if (status < 0)
			goto error;
		data |= IQM_AF_STDBY_STDBY_TAGC_RF_STANDBY;
		status = write16(state, IQM_AF_STDBY__A, data);
		if (status < 0)
			goto error;

		/* Disable SCU RF AGC loop */
		status = read16(state, SCU_RAM_AGC_CONFIG__A, &data);
		if (status < 0)
			goto error;
		data |= SCU_RAM_AGC_CONFIG_DISABLE_RF_AGC__M;
		status = write16(state, SCU_RAM_AGC_CONFIG__A, data);
		if (status < 0)
			goto error;
		break;

	default:
		status = -EINVAL;

	}
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

#define SCU_RAM_AGC_KI_INV_IF_POL__M 0x2000

static int set_agc_if(struct drxk_state *state,
		    struct s_cfg_agc *p_agc_cfg, bool is_dtv)
{
	u16 data = 0;
	int status = 0;
	struct s_cfg_agc *p_rf_agc_settings;

	dprintk(1, "\n");

	switch (p_agc_cfg->ctrl_mode) {
	case DRXK_AGC_CTRL_AUTO:

		/* Enable IF AGC DAC */
		status = read16(state, IQM_AF_STDBY__A, &data);
		if (status < 0)
			goto error;
		data &= ~IQM_AF_STDBY_STDBY_TAGC_IF_STANDBY;
		status = write16(state, IQM_AF_STDBY__A, data);
		if (status < 0)
			goto error;

		status = read16(state, SCU_RAM_AGC_CONFIG__A, &data);
		if (status < 0)
			goto error;

		/* Enable SCU IF AGC loop */
		data &= ~SCU_RAM_AGC_CONFIG_DISABLE_IF_AGC__M;

		/* Polarity */
		if (state->m_if_agc_pol)
			data |= SCU_RAM_AGC_CONFIG_INV_IF_POL__M;
		else
			data &= ~SCU_RAM_AGC_CONFIG_INV_IF_POL__M;
		status = write16(state, SCU_RAM_AGC_CONFIG__A, data);
		if (status < 0)
			goto error;

		/* Set speed (using complementary reduction value) */
		status = read16(state, SCU_RAM_AGC_KI_RED__A, &data);
		if (status < 0)
			goto error;
		data &= ~SCU_RAM_AGC_KI_RED_IAGC_RED__M;
		data |= (~(p_agc_cfg->speed <<
				SCU_RAM_AGC_KI_RED_IAGC_RED__B)
				& SCU_RAM_AGC_KI_RED_IAGC_RED__M);

		status = write16(state, SCU_RAM_AGC_KI_RED__A, data);
		if (status < 0)
			goto error;

		if (is_qam(state))
			p_rf_agc_settings = &state->m_qam_rf_agc_cfg;
		else
			p_rf_agc_settings = &state->m_atv_rf_agc_cfg;
		if (p_rf_agc_settings == NULL)
			return -1;
		/* Restore TOP */
		status = write16(state, SCU_RAM_AGC_IF_IACCU_HI_TGT_MAX__A,
				 p_rf_agc_settings->top);
		if (status < 0)
			goto error;
		break;

	case DRXK_AGC_CTRL_USER:

		/* Enable IF AGC DAC */
		status = read16(state, IQM_AF_STDBY__A, &data);
		if (status < 0)
			goto error;
		data &= ~IQM_AF_STDBY_STDBY_TAGC_IF_STANDBY;
		status = write16(state, IQM_AF_STDBY__A, data);
		if (status < 0)
			goto error;

		status = read16(state, SCU_RAM_AGC_CONFIG__A, &data);
		if (status < 0)
			goto error;

		/* Disable SCU IF AGC loop */
		data |= SCU_RAM_AGC_CONFIG_DISABLE_IF_AGC__M;

		/* Polarity */
		if (state->m_if_agc_pol)
			data |= SCU_RAM_AGC_CONFIG_INV_IF_POL__M;
		else
			data &= ~SCU_RAM_AGC_CONFIG_INV_IF_POL__M;
		status = write16(state, SCU_RAM_AGC_CONFIG__A, data);
		if (status < 0)
			goto error;

		/* Write value to output pin */
		status = write16(state, SCU_RAM_AGC_IF_IACCU_HI_TGT_MAX__A,
				 p_agc_cfg->output_level);
		if (status < 0)
			goto error;
		break;

	case DRXK_AGC_CTRL_OFF:

		/* Disable If AGC DAC */
		status = read16(state, IQM_AF_STDBY__A, &data);
		if (status < 0)
			goto error;
		data |= IQM_AF_STDBY_STDBY_TAGC_IF_STANDBY;
		status = write16(state, IQM_AF_STDBY__A, data);
		if (status < 0)
			goto error;

		/* Disable SCU IF AGC loop */
		status = read16(state, SCU_RAM_AGC_CONFIG__A, &data);
		if (status < 0)
			goto error;
		data |= SCU_RAM_AGC_CONFIG_DISABLE_IF_AGC__M;
		status = write16(state, SCU_RAM_AGC_CONFIG__A, data);
		if (status < 0)
			goto error;
		break;
	}		/* switch (agcSettingsIf->ctrl_mode) */

	/* always set the top to support
		configurations without if-loop */
	status = write16(state, SCU_RAM_AGC_INGAIN_TGT_MIN__A, p_agc_cfg->top);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int get_qam_signal_to_noise(struct drxk_state *state,
			       s32 *p_signal_to_noise)
{
	int status = 0;
	u16 qam_sl_err_power = 0;	/* accum. error between
					raw and sliced symbols */
	u32 qam_sl_sig_power = 0;	/* used for MER, depends of
					QAM modulation */
	u32 qam_sl_mer = 0;	/* QAM MER */

	dprintk(1, "\n");

	/* MER calculation */

	/* get the register value needed for MER */
	status = read16(state, QAM_SL_ERR_POWER__A, &qam_sl_err_power);
	if (status < 0) {
		pr_err("Error %d on %s\n", status, __func__);
		return -EINVAL;
	}

	switch (state->props.modulation) {
	case QAM_16:
		qam_sl_sig_power = DRXK_QAM_SL_SIG_POWER_QAM16 << 2;
		break;
	case QAM_32:
		qam_sl_sig_power = DRXK_QAM_SL_SIG_POWER_QAM32 << 2;
		break;
	case QAM_64:
		qam_sl_sig_power = DRXK_QAM_SL_SIG_POWER_QAM64 << 2;
		break;
	case QAM_128:
		qam_sl_sig_power = DRXK_QAM_SL_SIG_POWER_QAM128 << 2;
		break;
	default:
	case QAM_256:
		qam_sl_sig_power = DRXK_QAM_SL_SIG_POWER_QAM256 << 2;
		break;
	}

	if (qam_sl_err_power > 0) {
		qam_sl_mer = log10times100(qam_sl_sig_power) -
			log10times100((u32) qam_sl_err_power);
	}
	*p_signal_to_noise = qam_sl_mer;

	return status;
}

static int get_dvbt_signal_to_noise(struct drxk_state *state,
				s32 *p_signal_to_noise)
{
	int status;
	u16 reg_data = 0;
	u32 eq_reg_td_sqr_err_i = 0;
	u32 eq_reg_td_sqr_err_q = 0;
	u16 eq_reg_td_sqr_err_exp = 0;
	u16 eq_reg_td_tps_pwr_ofs = 0;
	u16 eq_reg_td_req_smb_cnt = 0;
	u32 tps_cnt = 0;
	u32 sqr_err_iq = 0;
	u32 a = 0;
	u32 b = 0;
	u32 c = 0;
	u32 i_mer = 0;
	u16 transmission_params = 0;

	dprintk(1, "\n");

	status = read16(state, OFDM_EQ_TOP_TD_TPS_PWR_OFS__A,
			&eq_reg_td_tps_pwr_ofs);
	if (status < 0)
		goto error;
	status = read16(state, OFDM_EQ_TOP_TD_REQ_SMB_CNT__A,
			&eq_reg_td_req_smb_cnt);
	if (status < 0)
		goto error;
	status = read16(state, OFDM_EQ_TOP_TD_SQR_ERR_EXP__A,
			&eq_reg_td_sqr_err_exp);
	if (status < 0)
		goto error;
	status = read16(state, OFDM_EQ_TOP_TD_SQR_ERR_I__A,
			&reg_data);
	if (status < 0)
		goto error;
	/* Extend SQR_ERR_I operational range */
	eq_reg_td_sqr_err_i = (u32) reg_data;
	if ((eq_reg_td_sqr_err_exp > 11) &&
		(eq_reg_td_sqr_err_i < 0x00000FFFUL)) {
		eq_reg_td_sqr_err_i += 0x00010000UL;
	}
	status = read16(state, OFDM_EQ_TOP_TD_SQR_ERR_Q__A, &reg_data);
	if (status < 0)
		goto error;
	/* Extend SQR_ERR_Q operational range */
	eq_reg_td_sqr_err_q = (u32) reg_data;
	if ((eq_reg_td_sqr_err_exp > 11) &&
		(eq_reg_td_sqr_err_q < 0x00000FFFUL))
		eq_reg_td_sqr_err_q += 0x00010000UL;

	status = read16(state, OFDM_SC_RA_RAM_OP_PARAM__A,
			&transmission_params);
	if (status < 0)
		goto error;

	/* Check input data for MER */

	/* MER calculation (in 0.1 dB) without math.h */
	if ((eq_reg_td_tps_pwr_ofs == 0) || (eq_reg_td_req_smb_cnt == 0))
		i_mer = 0;
	else if ((eq_reg_td_sqr_err_i + eq_reg_td_sqr_err_q) == 0) {
		/* No error at all, this must be the HW reset value
			* Apparently no first measurement yet
			* Set MER to 0.0 */
		i_mer = 0;
	} else {
		sqr_err_iq = (eq_reg_td_sqr_err_i + eq_reg_td_sqr_err_q) <<
			eq_reg_td_sqr_err_exp;
		if ((transmission_params &
			OFDM_SC_RA_RAM_OP_PARAM_MODE__M)
			== OFDM_SC_RA_RAM_OP_PARAM_MODE_2K)
			tps_cnt = 17;
		else
			tps_cnt = 68;

		/* IMER = 100 * log10 (x)
			where x = (eq_reg_td_tps_pwr_ofs^2 *
			eq_reg_td_req_smb_cnt * tps_cnt)/sqr_err_iq

			=> IMER = a + b -c
			where a = 100 * log10 (eq_reg_td_tps_pwr_ofs^2)
			b = 100 * log10 (eq_reg_td_req_smb_cnt * tps_cnt)
			c = 100 * log10 (sqr_err_iq)
			*/

		/* log(x) x = 9bits * 9bits->18 bits  */
		a = log10times100(eq_reg_td_tps_pwr_ofs *
					eq_reg_td_tps_pwr_ofs);
		/* log(x) x = 16bits * 7bits->23 bits  */
		b = log10times100(eq_reg_td_req_smb_cnt * tps_cnt);
		/* log(x) x = (16bits + 16bits) << 15 ->32 bits  */
		c = log10times100(sqr_err_iq);

		i_mer = a + b - c;
	}
	*p_signal_to_noise = i_mer;

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int get_signal_to_noise(struct drxk_state *state, s32 *p_signal_to_noise)
{
	dprintk(1, "\n");

	*p_signal_to_noise = 0;
	switch (state->m_operation_mode) {
	case OM_DVBT:
		return get_dvbt_signal_to_noise(state, p_signal_to_noise);
	case OM_QAM_ITU_A:
	case OM_QAM_ITU_C:
		return get_qam_signal_to_noise(state, p_signal_to_noise);
	default:
		break;
	}
	return 0;
}

#if 0
static int get_dvbt_quality(struct drxk_state *state, s32 *p_quality)
{
	/* SNR Values for quasi errorfree reception rom Nordig 2.2 */
	int status = 0;

	dprintk(1, "\n");

	static s32 QE_SN[] = {
		51,		/* QPSK 1/2 */
		69,		/* QPSK 2/3 */
		79,		/* QPSK 3/4 */
		89,		/* QPSK 5/6 */
		97,		/* QPSK 7/8 */
		108,		/* 16-QAM 1/2 */
		131,		/* 16-QAM 2/3 */
		146,		/* 16-QAM 3/4 */
		156,		/* 16-QAM 5/6 */
		160,		/* 16-QAM 7/8 */
		165,		/* 64-QAM 1/2 */
		187,		/* 64-QAM 2/3 */
		202,		/* 64-QAM 3/4 */
		216,		/* 64-QAM 5/6 */
		225,		/* 64-QAM 7/8 */
	};

	*p_quality = 0;

	do {
		s32 signal_to_noise = 0;
		u16 constellation = 0;
		u16 code_rate = 0;
		u32 signal_to_noise_rel;
		u32 ber_quality;

		status = get_dvbt_signal_to_noise(state, &signal_to_noise);
		if (status < 0)
			break;
		status = read16(state, OFDM_EQ_TOP_TD_TPS_CONST__A,
				&constellation);
		if (status < 0)
			break;
		constellation &= OFDM_EQ_TOP_TD_TPS_CONST__M;

		status = read16(state, OFDM_EQ_TOP_TD_TPS_CODE_HP__A,
				&code_rate);
		if (status < 0)
			break;
		code_rate &= OFDM_EQ_TOP_TD_TPS_CODE_HP__M;

		if (constellation > OFDM_EQ_TOP_TD_TPS_CONST_64QAM ||
		    code_rate > OFDM_EQ_TOP_TD_TPS_CODE_LP_7_8)
			break;
		signal_to_noise_rel = signal_to_noise -
		    QE_SN[constellation * 5 + code_rate];
		ber_quality = 100;

		if (signal_to_noise_rel < -70)
			*p_quality = 0;
		else if (signal_to_noise_rel < 30)
			*p_quality = ((signal_to_noise_rel + 70) *
				     ber_quality) / 100;
		else
			*p_quality = ber_quality;
	} while (0);
	return 0;
};

static int get_dvbc_quality(struct drxk_state *state, s32 *p_quality)
{
	int status = 0;
	*p_quality = 0;

	dprintk(1, "\n");

	do {
		u32 signal_to_noise = 0;
		u32 ber_quality = 100;
		u32 signal_to_noise_rel = 0;

		status = get_qam_signal_to_noise(state, &signal_to_noise);
		if (status < 0)
			break;

		switch (state->props.modulation) {
		case QAM_16:
			signal_to_noise_rel = signal_to_noise - 200;
			break;
		case QAM_32:
			signal_to_noise_rel = signal_to_noise - 230;
			break;	/* Not in NorDig */
		case QAM_64:
			signal_to_noise_rel = signal_to_noise - 260;
			break;
		case QAM_128:
			signal_to_noise_rel = signal_to_noise - 290;
			break;
		default:
		case QAM_256:
			signal_to_noise_rel = signal_to_noise - 320;
			break;
		}

		if (signal_to_noise_rel < -70)
			*p_quality = 0;
		else if (signal_to_noise_rel < 30)
			*p_quality = ((signal_to_noise_rel + 70) *
				     ber_quality) / 100;
		else
			*p_quality = ber_quality;
	} while (0);

	return status;
}

static int get_quality(struct drxk_state *state, s32 *p_quality)
{
	dprintk(1, "\n");

	switch (state->m_operation_mode) {
	case OM_DVBT:
		return get_dvbt_quality(state, p_quality);
	case OM_QAM_ITU_A:
		return get_dvbc_quality(state, p_quality);
	default:
		break;
	}

	return 0;
}
#endif

/* Free data ram in SIO HI */
#define SIO_HI_RA_RAM_USR_BEGIN__A 0x420040
#define SIO_HI_RA_RAM_USR_END__A   0x420060

#define DRXK_HI_ATOMIC_BUF_START (SIO_HI_RA_RAM_USR_BEGIN__A)
#define DRXK_HI_ATOMIC_BUF_END   (SIO_HI_RA_RAM_USR_BEGIN__A + 7)
#define DRXK_HI_ATOMIC_READ      SIO_HI_RA_RAM_PAR_3_ACP_RW_READ
#define DRXK_HI_ATOMIC_WRITE     SIO_HI_RA_RAM_PAR_3_ACP_RW_WRITE

#define DRXDAP_FASI_ADDR2BLOCK(addr)  (((addr) >> 22) & 0x3F)
#define DRXDAP_FASI_ADDR2BANK(addr)   (((addr) >> 16) & 0x3F)
#define DRXDAP_FASI_ADDR2OFFSET(addr) ((addr) & 0x7FFF)

static int ConfigureI2CBridge(struct drxk_state *state, bool b_enable_bridge)
{
	int status = -EINVAL;

	dprintk(1, "\n");

	if (state->m_drxk_state == DRXK_UNINITIALIZED)
		return 0;
	if (state->m_drxk_state == DRXK_POWERED_DOWN)
		goto error;

	if (state->no_i2c_bridge)
		return 0;

	status = write16(state, SIO_HI_RA_RAM_PAR_1__A,
			 SIO_HI_RA_RAM_PAR_1_PAR1_SEC_KEY);
	if (status < 0)
		goto error;
	if (b_enable_bridge) {
		status = write16(state, SIO_HI_RA_RAM_PAR_2__A,
				 SIO_HI_RA_RAM_PAR_2_BRD_CFG_CLOSED);
		if (status < 0)
			goto error;
	} else {
		status = write16(state, SIO_HI_RA_RAM_PAR_2__A,
				 SIO_HI_RA_RAM_PAR_2_BRD_CFG_OPEN);
		if (status < 0)
			goto error;
	}

	status = hi_command(state, SIO_HI_RA_RAM_CMD_BRDCTRL, NULL);

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int set_pre_saw(struct drxk_state *state,
		     struct s_cfg_pre_saw *p_pre_saw_cfg)
{
	int status = -EINVAL;

	dprintk(1, "\n");

	if ((p_pre_saw_cfg == NULL)
	    || (p_pre_saw_cfg->reference > IQM_AF_PDREF__M))
		goto error;

	status = write16(state, IQM_AF_PDREF__A, p_pre_saw_cfg->reference);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int bl_direct_cmd(struct drxk_state *state, u32 target_addr,
		       u16 rom_offset, u16 nr_of_elements, u32 time_out)
{
	u16 bl_status = 0;
	u16 offset = (u16) ((target_addr >> 0) & 0x00FFFF);
	u16 blockbank = (u16) ((target_addr >> 16) & 0x000FFF);
	int status;
	unsigned long end;

	dprintk(1, "\n");

	mutex_lock(&state->mutex);
	status = write16(state, SIO_BL_MODE__A, SIO_BL_MODE_DIRECT);
	if (status < 0)
		goto error;
	status = write16(state, SIO_BL_TGT_HDR__A, blockbank);
	if (status < 0)
		goto error;
	status = write16(state, SIO_BL_TGT_ADDR__A, offset);
	if (status < 0)
		goto error;
	status = write16(state, SIO_BL_SRC_ADDR__A, rom_offset);
	if (status < 0)
		goto error;
	status = write16(state, SIO_BL_SRC_LEN__A, nr_of_elements);
	if (status < 0)
		goto error;
	status = write16(state, SIO_BL_ENABLE__A, SIO_BL_ENABLE_ON);
	if (status < 0)
		goto error;

	end = jiffies + msecs_to_jiffies(time_out);
	do {
		status = read16(state, SIO_BL_STATUS__A, &bl_status);
		if (status < 0)
			goto error;
	} while ((bl_status == 0x1) && time_is_after_jiffies(end));
	if (bl_status == 0x1) {
		pr_err("SIO not ready\n");
		status = -EINVAL;
		goto error2;
	}
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
error2:
	mutex_unlock(&state->mutex);
	return status;

}

static int adc_sync_measurement(struct drxk_state *state, u16 *count)
{
	u16 data = 0;
	int status;

	dprintk(1, "\n");

	/* start measurement */
	status = write16(state, IQM_AF_COMM_EXEC__A, IQM_AF_COMM_EXEC_ACTIVE);
	if (status < 0)
		goto error;
	status = write16(state, IQM_AF_START_LOCK__A, 1);
	if (status < 0)
		goto error;

	*count = 0;
	status = read16(state, IQM_AF_PHASE0__A, &data);
	if (status < 0)
		goto error;
	if (data == 127)
		*count = *count + 1;
	status = read16(state, IQM_AF_PHASE1__A, &data);
	if (status < 0)
		goto error;
	if (data == 127)
		*count = *count + 1;
	status = read16(state, IQM_AF_PHASE2__A, &data);
	if (status < 0)
		goto error;
	if (data == 127)
		*count = *count + 1;

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int adc_synchronization(struct drxk_state *state)
{
	u16 count = 0;
	int status;

	dprintk(1, "\n");

	status = adc_sync_measurement(state, &count);
	if (status < 0)
		goto error;

	if (count == 1) {
		/* Try sampling on a different edge */
		u16 clk_neg = 0;

		status = read16(state, IQM_AF_CLKNEG__A, &clk_neg);
		if (status < 0)
			goto error;
		if ((clk_neg & IQM_AF_CLKNEG_CLKNEGDATA__M) ==
			IQM_AF_CLKNEG_CLKNEGDATA_CLK_ADC_DATA_POS) {
			clk_neg &= (~(IQM_AF_CLKNEG_CLKNEGDATA__M));
			clk_neg |=
				IQM_AF_CLKNEG_CLKNEGDATA_CLK_ADC_DATA_NEG;
		} else {
			clk_neg &= (~(IQM_AF_CLKNEG_CLKNEGDATA__M));
			clk_neg |=
				IQM_AF_CLKNEG_CLKNEGDATA_CLK_ADC_DATA_POS;
		}
		status = write16(state, IQM_AF_CLKNEG__A, clk_neg);
		if (status < 0)
			goto error;
		status = adc_sync_measurement(state, &count);
		if (status < 0)
			goto error;
	}

	if (count < 2)
		status = -EINVAL;
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int set_frequency_shifter(struct drxk_state *state,
			       u16 intermediate_freqk_hz,
			       s32 tuner_freq_offset, bool is_dtv)
{
	bool select_pos_image = false;
	u32 rf_freq_residual = tuner_freq_offset;
	u32 fm_frequency_shift = 0;
	bool tuner_mirror = !state->m_b_mirror_freq_spect;
	u32 adc_freq;
	bool adc_flip;
	int status;
	u32 if_freq_actual;
	u32 sampling_frequency = (u32) (state->m_sys_clock_freq / 3);
	u32 frequency_shift;
	bool image_to_select;

	dprintk(1, "\n");

	/*
	   Program frequency shifter
	   No need to account for mirroring on RF
	 */
	if (is_dtv) {
		if ((state->m_operation_mode == OM_QAM_ITU_A) ||
		    (state->m_operation_mode == OM_QAM_ITU_C) ||
		    (state->m_operation_mode == OM_DVBT))
			select_pos_image = true;
		else
			select_pos_image = false;
	}
	if (tuner_mirror)
		/* tuner doesn't mirror */
		if_freq_actual = intermediate_freqk_hz +
		    rf_freq_residual + fm_frequency_shift;
	else
		/* tuner mirrors */
		if_freq_actual = intermediate_freqk_hz -
		    rf_freq_residual - fm_frequency_shift;
	if (if_freq_actual > sampling_frequency / 2) {
		/* adc mirrors */
		adc_freq = sampling_frequency - if_freq_actual;
		adc_flip = true;
	} else {
		/* adc doesn't mirror */
		adc_freq = if_freq_actual;
		adc_flip = false;
	}

	frequency_shift = adc_freq;
	image_to_select = state->m_rfmirror ^ tuner_mirror ^
	    adc_flip ^ select_pos_image;
	state->m_iqm_fs_rate_ofs =
	    Frac28a((frequency_shift), sampling_frequency);

	if (image_to_select)
		state->m_iqm_fs_rate_ofs = ~state->m_iqm_fs_rate_ofs + 1;

	/* Program frequency shifter with tuner offset compensation */
	/* frequency_shift += tuner_freq_offset; TODO */
	status = write32(state, IQM_FS_RATE_OFS_LO__A,
			 state->m_iqm_fs_rate_ofs);
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int init_agc(struct drxk_state *state, bool is_dtv)
{
	u16 ingain_tgt = 0;
	u16 ingain_tgt_min = 0;
	u16 ingain_tgt_max = 0;
	u16 clp_cyclen = 0;
	u16 clp_sum_min = 0;
	u16 clp_dir_to = 0;
	u16 sns_sum_min = 0;
	u16 sns_sum_max = 0;
	u16 clp_sum_max = 0;
	u16 sns_dir_to = 0;
	u16 ki_innergain_min = 0;
	u16 if_iaccu_hi_tgt = 0;
	u16 if_iaccu_hi_tgt_min = 0;
	u16 if_iaccu_hi_tgt_max = 0;
	u16 data = 0;
	u16 fast_clp_ctrl_delay = 0;
	u16 clp_ctrl_mode = 0;
	int status = 0;

	dprintk(1, "\n");

	/* Common settings */
	sns_sum_max = 1023;
	if_iaccu_hi_tgt_min = 2047;
	clp_cyclen = 500;
	clp_sum_max = 1023;

	/* AGCInit() not available for DVBT; init done in microcode */
	if (!is_qam(state)) {
		pr_err("%s: mode %d is not DVB-C\n",
		       __func__, state->m_operation_mode);
		return -EINVAL;
	}

	/* FIXME: Analog TV AGC require different settings */

	/* Standard specific settings */
	clp_sum_min = 8;
	clp_dir_to = (u16) -9;
	clp_ctrl_mode = 0;
	sns_sum_min = 8;
	sns_dir_to = (u16) -9;
	ki_innergain_min = (u16) -1030;
	if_iaccu_hi_tgt_max = 0x2380;
	if_iaccu_hi_tgt = 0x2380;
	ingain_tgt_min = 0x0511;
	ingain_tgt = 0x0511;
	ingain_tgt_max = 5119;
	fast_clp_ctrl_delay = state->m_qam_if_agc_cfg.fast_clip_ctrl_delay;

	status = write16(state, SCU_RAM_AGC_FAST_CLP_CTRL_DELAY__A,
			 fast_clp_ctrl_delay);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_AGC_CLP_CTRL_MODE__A, clp_ctrl_mode);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_INGAIN_TGT__A, ingain_tgt);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_INGAIN_TGT_MIN__A, ingain_tgt_min);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_INGAIN_TGT_MAX__A, ingain_tgt_max);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_IF_IACCU_HI_TGT_MIN__A,
			 if_iaccu_hi_tgt_min);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_IF_IACCU_HI_TGT_MAX__A,
			 if_iaccu_hi_tgt_max);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_IF_IACCU_HI__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_IF_IACCU_LO__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_RF_IACCU_HI__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_RF_IACCU_LO__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_CLP_SUM_MAX__A, clp_sum_max);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_SNS_SUM_MAX__A, sns_sum_max);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_AGC_KI_INNERGAIN_MIN__A,
			 ki_innergain_min);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_IF_IACCU_HI_TGT__A,
			 if_iaccu_hi_tgt);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_CLP_CYCLEN__A, clp_cyclen);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_AGC_RF_SNS_DEV_MAX__A, 1023);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_RF_SNS_DEV_MIN__A, (u16) -1023);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_FAST_SNS_CTRL_DELAY__A, 50);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_AGC_KI_MAXMINGAIN_TH__A, 20);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_CLP_SUM_MIN__A, clp_sum_min);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_SNS_SUM_MIN__A, sns_sum_min);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_CLP_DIR_TO__A, clp_dir_to);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_SNS_DIR_TO__A, sns_dir_to);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_KI_MINGAIN__A, 0x7fff);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_KI_MAXGAIN__A, 0x0);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_KI_MIN__A, 0x0117);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_KI_MAX__A, 0x0657);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_CLP_SUM__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_CLP_CYCCNT__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_CLP_DIR_WD__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_CLP_DIR_STP__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_SNS_SUM__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_SNS_CYCCNT__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_SNS_DIR_WD__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_SNS_DIR_STP__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_SNS_CYCLEN__A, 500);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_KI_CYCLEN__A, 500);
	if (status < 0)
		goto error;

	/* Initialize inner-loop KI gain factors */
	status = read16(state, SCU_RAM_AGC_KI__A, &data);
	if (status < 0)
		goto error;

	data = 0x0657;
	data &= ~SCU_RAM_AGC_KI_RF__M;
	data |= (DRXK_KI_RAGC_QAM << SCU_RAM_AGC_KI_RF__B);
	data &= ~SCU_RAM_AGC_KI_IF__M;
	data |= (DRXK_KI_IAGC_QAM << SCU_RAM_AGC_KI_IF__B);

	status = write16(state, SCU_RAM_AGC_KI__A, data);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int dvbtqam_get_acc_pkt_err(struct drxk_state *state, u16 *packet_err)
{
	int status;

	dprintk(1, "\n");
	if (packet_err == NULL)
		status = write16(state, SCU_RAM_FEC_ACCUM_PKT_FAILURES__A, 0);
	else
		status = read16(state, SCU_RAM_FEC_ACCUM_PKT_FAILURES__A,
				packet_err);
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int dvbt_sc_command(struct drxk_state *state,
			 u16 cmd, u16 subcmd,
			 u16 param0, u16 param1, u16 param2,
			 u16 param3, u16 param4)
{
	u16 cur_cmd = 0;
	u16 err_code = 0;
	u16 retry_cnt = 0;
	u16 sc_exec = 0;
	int status;

	dprintk(1, "\n");
	status = read16(state, OFDM_SC_COMM_EXEC__A, &sc_exec);
	if (sc_exec != 1) {
		/* SC is not running */
		status = -EINVAL;
	}
	if (status < 0)
		goto error;

	/* Wait until sc is ready to receive command */
	retry_cnt = 0;
	do {
		usleep_range(1000, 2000);
		status = read16(state, OFDM_SC_RA_RAM_CMD__A, &cur_cmd);
		retry_cnt++;
	} while ((cur_cmd != 0) && (retry_cnt < DRXK_MAX_RETRIES));
	if (retry_cnt >= DRXK_MAX_RETRIES && (status < 0))
		goto error;

	/* Write sub-command */
	switch (cmd) {
		/* All commands using sub-cmd */
	case OFDM_SC_RA_RAM_CMD_PROC_START:
	case OFDM_SC_RA_RAM_CMD_SET_PREF_PARAM:
	case OFDM_SC_RA_RAM_CMD_PROGRAM_PARAM:
		status = write16(state, OFDM_SC_RA_RAM_CMD_ADDR__A, subcmd);
		if (status < 0)
			goto error;
		break;
	default:
		/* Do nothing */
		break;
	}

	/* Write needed parameters and the command */
	status = 0;
	switch (cmd) {
		/* All commands using 5 parameters */
		/* All commands using 4 parameters */
		/* All commands using 3 parameters */
		/* All commands using 2 parameters */
	case OFDM_SC_RA_RAM_CMD_PROC_START:
	case OFDM_SC_RA_RAM_CMD_SET_PREF_PARAM:
	case OFDM_SC_RA_RAM_CMD_PROGRAM_PARAM:
		status |= write16(state, OFDM_SC_RA_RAM_PARAM1__A, param1);
		/* fall through - All commands using 1 parameters */
	case OFDM_SC_RA_RAM_CMD_SET_ECHO_TIMING:
	case OFDM_SC_RA_RAM_CMD_USER_IO:
		status |= write16(state, OFDM_SC_RA_RAM_PARAM0__A, param0);
		/* fall through - All commands using 0 parameters */
	case OFDM_SC_RA_RAM_CMD_GET_OP_PARAM:
	case OFDM_SC_RA_RAM_CMD_NULL:
		/* Write command */
		status |= write16(state, OFDM_SC_RA_RAM_CMD__A, cmd);
		break;
	default:
		/* Unknown command */
		status = -EINVAL;
	}
	if (status < 0)
		goto error;

	/* Wait until sc is ready processing command */
	retry_cnt = 0;
	do {
		usleep_range(1000, 2000);
		status = read16(state, OFDM_SC_RA_RAM_CMD__A, &cur_cmd);
		retry_cnt++;
	} while ((cur_cmd != 0) && (retry_cnt < DRXK_MAX_RETRIES));
	if (retry_cnt >= DRXK_MAX_RETRIES && (status < 0))
		goto error;

	/* Check for illegal cmd */
	status = read16(state, OFDM_SC_RA_RAM_CMD_ADDR__A, &err_code);
	if (err_code == 0xFFFF) {
		/* illegal command */
		status = -EINVAL;
	}
	if (status < 0)
		goto error;

	/* Retrieve results parameters from SC */
	switch (cmd) {
		/* All commands yielding 5 results */
		/* All commands yielding 4 results */
		/* All commands yielding 3 results */
		/* All commands yielding 2 results */
		/* All commands yielding 1 result */
	case OFDM_SC_RA_RAM_CMD_USER_IO:
	case OFDM_SC_RA_RAM_CMD_GET_OP_PARAM:
		status = read16(state, OFDM_SC_RA_RAM_PARAM0__A, &(param0));
		/* All commands yielding 0 results */
	case OFDM_SC_RA_RAM_CMD_SET_ECHO_TIMING:
	case OFDM_SC_RA_RAM_CMD_SET_TIMER:
	case OFDM_SC_RA_RAM_CMD_PROC_START:
	case OFDM_SC_RA_RAM_CMD_SET_PREF_PARAM:
	case OFDM_SC_RA_RAM_CMD_PROGRAM_PARAM:
	case OFDM_SC_RA_RAM_CMD_NULL:
		break;
	default:
		/* Unknown command */
		status = -EINVAL;
		break;
	}			/* switch (cmd->cmd) */
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int power_up_dvbt(struct drxk_state *state)
{
	enum drx_power_mode power_mode = DRX_POWER_UP;
	int status;

	dprintk(1, "\n");
	status = ctrl_power_mode(state, &power_mode);
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int dvbt_ctrl_set_inc_enable(struct drxk_state *state, bool *enabled)
{
	int status;

	dprintk(1, "\n");
	if (*enabled)
		status = write16(state, IQM_CF_BYPASSDET__A, 0);
	else
		status = write16(state, IQM_CF_BYPASSDET__A, 1);
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

#define DEFAULT_FR_THRES_8K     4000
static int dvbt_ctrl_set_fr_enable(struct drxk_state *state, bool *enabled)
{

	int status;

	dprintk(1, "\n");
	if (*enabled) {
		/* write mask to 1 */
		status = write16(state, OFDM_SC_RA_RAM_FR_THRES_8K__A,
				   DEFAULT_FR_THRES_8K);
	} else {
		/* write mask to 0 */
		status = write16(state, OFDM_SC_RA_RAM_FR_THRES_8K__A, 0);
	}
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	return status;
}

static int dvbt_ctrl_set_echo_threshold(struct drxk_state *state,
				struct drxk_cfg_dvbt_echo_thres_t *echo_thres)
{
	u16 data = 0;
	int status;

	dprintk(1, "\n");
	status = read16(state, OFDM_SC_RA_RAM_ECHO_THRES__A, &data);
	if (status < 0)
		goto error;

	switch (echo_thres->fft_mode) {
	case DRX_FFTMODE_2K:
		data &= ~OFDM_SC_RA_RAM_ECHO_THRES_2K__M;
		data |= ((echo_thres->threshold <<
			OFDM_SC_RA_RAM_ECHO_THRES_2K__B)
			& (OFDM_SC_RA_RAM_ECHO_THRES_2K__M));
		break;
	case DRX_FFTMODE_8K:
		data &= ~OFDM_SC_RA_RAM_ECHO_THRES_8K__M;
		data |= ((echo_thres->threshold <<
			OFDM_SC_RA_RAM_ECHO_THRES_8K__B)
			& (OFDM_SC_RA_RAM_ECHO_THRES_8K__M));
		break;
	default:
		return -EINVAL;
	}

	status = write16(state, OFDM_SC_RA_RAM_ECHO_THRES__A, data);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int dvbt_ctrl_set_sqi_speed(struct drxk_state *state,
			       enum drxk_cfg_dvbt_sqi_speed *speed)
{
	int status = -EINVAL;

	dprintk(1, "\n");

	switch (*speed) {
	case DRXK_DVBT_SQI_SPEED_FAST:
	case DRXK_DVBT_SQI_SPEED_MEDIUM:
	case DRXK_DVBT_SQI_SPEED_SLOW:
		break;
	default:
		goto error;
	}
	status = write16(state, SCU_RAM_FEC_PRE_RS_BER_FILTER_SH__A,
			   (u16) *speed);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

/*============================================================================*/

/*
* \brief Activate DVBT specific presets
* \param demod instance of demodulator.
* \return DRXStatus_t.
*
* Called in DVBTSetStandard
*
*/
static int dvbt_activate_presets(struct drxk_state *state)
{
	int status;
	bool setincenable = false;
	bool setfrenable = true;

	struct drxk_cfg_dvbt_echo_thres_t echo_thres2k = { 0, DRX_FFTMODE_2K };
	struct drxk_cfg_dvbt_echo_thres_t echo_thres8k = { 0, DRX_FFTMODE_8K };

	dprintk(1, "\n");
	status = dvbt_ctrl_set_inc_enable(state, &setincenable);
	if (status < 0)
		goto error;
	status = dvbt_ctrl_set_fr_enable(state, &setfrenable);
	if (status < 0)
		goto error;
	status = dvbt_ctrl_set_echo_threshold(state, &echo_thres2k);
	if (status < 0)
		goto error;
	status = dvbt_ctrl_set_echo_threshold(state, &echo_thres8k);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_AGC_INGAIN_TGT_MAX__A,
			 state->m_dvbt_if_agc_cfg.ingain_tgt_max);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

/*============================================================================*/

/*
* \brief Initialize channelswitch-independent settings for DVBT.
* \param demod instance of demodulator.
* \return DRXStatus_t.
*
* For ROM code channel filter taps are loaded from the bootloader. For microcode
* the DVB-T taps from the drxk_filters.h are used.
*/
static int set_dvbt_standard(struct drxk_state *state,
			   enum operation_mode o_mode)
{
	u16 cmd_result = 0;
	u16 data = 0;
	int status;

	dprintk(1, "\n");

	power_up_dvbt(state);
	/* added antenna switch */
	switch_antenna_to_dvbt(state);
	/* send OFDM reset command */
	status = scu_command(state,
			     SCU_RAM_COMMAND_STANDARD_OFDM
			     | SCU_RAM_COMMAND_CMD_DEMOD_RESET,
			     0, NULL, 1, &cmd_result);
	if (status < 0)
		goto error;

	/* send OFDM setenv command */
	status = scu_command(state, SCU_RAM_COMMAND_STANDARD_OFDM
			     | SCU_RAM_COMMAND_CMD_DEMOD_SET_ENV,
			     0, NULL, 1, &cmd_result);
	if (status < 0)
		goto error;

	/* reset datapath for OFDM, processors first */
	status = write16(state, OFDM_SC_COMM_EXEC__A, OFDM_SC_COMM_EXEC_STOP);
	if (status < 0)
		goto error;
	status = write16(state, OFDM_LC_COMM_EXEC__A, OFDM_LC_COMM_EXEC_STOP);
	if (status < 0)
		goto error;
	status = write16(state, IQM_COMM_EXEC__A, IQM_COMM_EXEC_B_STOP);
	if (status < 0)
		goto error;

	/* IQM setup */
	/* synchronize on ofdstate->m_festart */
	status = write16(state, IQM_AF_UPD_SEL__A, 1);
	if (status < 0)
		goto error;
	/* window size for clipping ADC detection */
	status = write16(state, IQM_AF_CLP_LEN__A, 0);
	if (status < 0)
		goto error;
	/* window size for for sense pre-SAW detection */
	status = write16(state, IQM_AF_SNS_LEN__A, 0);
	if (status < 0)
		goto error;
	/* sense threshold for sense pre-SAW detection */
	status = write16(state, IQM_AF_AMUX__A, IQM_AF_AMUX_SIGNAL2ADC);
	if (status < 0)
		goto error;
	status = set_iqm_af(state, true);
	if (status < 0)
		goto error;

	status = write16(state, IQM_AF_AGC_RF__A, 0);
	if (status < 0)
		goto error;

	/* Impulse noise cruncher setup */
	status = write16(state, IQM_AF_INC_LCT__A, 0);	/* crunch in IQM_CF */
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_DET_LCT__A, 0);	/* detect in IQM_CF */
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_WND_LEN__A, 3);	/* peak detector window length */
	if (status < 0)
		goto error;

	status = write16(state, IQM_RC_STRETCH__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_OUT_ENA__A, 0x4); /* enable output 2 */
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_DS_ENA__A, 0x4);	/* decimate output 2 */
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_SCALE__A, 1600);
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_SCALE_SH__A, 0);
	if (status < 0)
		goto error;

	/* virtual clipping threshold for clipping ADC detection */
	status = write16(state, IQM_AF_CLP_TH__A, 448);
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_DATATH__A, 495);	/* crunching threshold */
	if (status < 0)
		goto error;

	status = bl_chain_cmd(state, DRXK_BL_ROM_OFFSET_TAPS_DVBT,
			      DRXK_BLCC_NR_ELEMENTS_TAPS, DRXK_BLC_TIMEOUT);
	if (status < 0)
		goto error;

	status = write16(state, IQM_CF_PKDTH__A, 2);	/* peak detector threshold */
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_POW_MEAS_LEN__A, 2);
	if (status < 0)
		goto error;
	/* enable power measurement interrupt */
	status = write16(state, IQM_CF_COMM_INT_MSK__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, IQM_COMM_EXEC__A, IQM_COMM_EXEC_B_ACTIVE);
	if (status < 0)
		goto error;

	/* IQM will not be reset from here, sync ADC and update/init AGC */
	status = adc_synchronization(state);
	if (status < 0)
		goto error;
	status = set_pre_saw(state, &state->m_dvbt_pre_saw_cfg);
	if (status < 0)
		goto error;

	/* Halt SCU to enable safe non-atomic accesses */
	status = write16(state, SCU_COMM_EXEC__A, SCU_COMM_EXEC_HOLD);
	if (status < 0)
		goto error;

	status = set_agc_rf(state, &state->m_dvbt_rf_agc_cfg, true);
	if (status < 0)
		goto error;
	status = set_agc_if(state, &state->m_dvbt_if_agc_cfg, true);
	if (status < 0)
		goto error;

	/* Set Noise Estimation notch width and enable DC fix */
	status = read16(state, OFDM_SC_RA_RAM_CONFIG__A, &data);
	if (status < 0)
		goto error;
	data |= OFDM_SC_RA_RAM_CONFIG_NE_FIX_ENABLE__M;
	status = write16(state, OFDM_SC_RA_RAM_CONFIG__A, data);
	if (status < 0)
		goto error;

	/* Activate SCU to enable SCU commands */
	status = write16(state, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE);
	if (status < 0)
		goto error;

	if (!state->m_drxk_a3_rom_code) {
		/* AGCInit() is not done for DVBT, so set agcfast_clip_ctrl_delay  */
		status = write16(state, SCU_RAM_AGC_FAST_CLP_CTRL_DELAY__A,
				 state->m_dvbt_if_agc_cfg.fast_clip_ctrl_delay);
		if (status < 0)
			goto error;
	}

	/* OFDM_SC setup */
#ifdef COMPILE_FOR_NONRT
	status = write16(state, OFDM_SC_RA_RAM_BE_OPT_DELAY__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, OFDM_SC_RA_RAM_BE_OPT_INIT_DELAY__A, 2);
	if (status < 0)
		goto error;
#endif

	/* FEC setup */
	status = write16(state, FEC_DI_INPUT_CTL__A, 1);	/* OFDM input */
	if (status < 0)
		goto error;


#ifdef COMPILE_FOR_NONRT
	status = write16(state, FEC_RS_MEASUREMENT_PERIOD__A, 0x400);
	if (status < 0)
		goto error;
#else
	status = write16(state, FEC_RS_MEASUREMENT_PERIOD__A, 0x1000);
	if (status < 0)
		goto error;
#endif
	status = write16(state, FEC_RS_MEASUREMENT_PRESCALE__A, 0x0001);
	if (status < 0)
		goto error;

	/* Setup MPEG bus */
	status = mpegts_dto_setup(state, OM_DVBT);
	if (status < 0)
		goto error;
	/* Set DVBT Presets */
	status = dvbt_activate_presets(state);
	if (status < 0)
		goto error;

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

/*============================================================================*/
/*
* \brief start dvbt demodulating for channel.
* \param demod instance of demodulator.
* \return DRXStatus_t.
*/
static int dvbt_start(struct drxk_state *state)
{
	u16 param1;
	int status;
	/* drxk_ofdm_sc_cmd_t scCmd; */

	dprintk(1, "\n");
	/* start correct processes to get in lock */
	/* DRXK: OFDM_SC_RA_RAM_PROC_LOCKTRACK is no longer in mapfile! */
	param1 = OFDM_SC_RA_RAM_LOCKTRACK_MIN;
	status = dvbt_sc_command(state, OFDM_SC_RA_RAM_CMD_PROC_START, 0,
				 OFDM_SC_RA_RAM_SW_EVENT_RUN_NMASK__M, param1,
				 0, 0, 0);
	if (status < 0)
		goto error;
	/* start FEC OC */
	status = mpegts_start(state);
	if (status < 0)
		goto error;
	status = write16(state, FEC_COMM_EXEC__A, FEC_COMM_EXEC_ACTIVE);
	if (status < 0)
		goto error;
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}


/*============================================================================*/

/*
* \brief Set up dvbt demodulator for channel.
* \param demod instance of demodulator.
* \return DRXStatus_t.
* // original DVBTSetChannel()
*/
static int set_dvbt(struct drxk_state *state, u16 intermediate_freqk_hz,
		   s32 tuner_freq_offset)
{
	u16 cmd_result = 0;
	u16 transmission_params = 0;
	u16 operation_mode = 0;
	u32 iqm_rc_rate_ofs = 0;
	u32 bandwidth = 0;
	u16 param1;
	int status;

	dprintk(1, "IF =%d, TFO = %d\n",
		intermediate_freqk_hz, tuner_freq_offset);

	status = scu_command(state, SCU_RAM_COMMAND_STANDARD_OFDM
			    | SCU_RAM_COMMAND_CMD_DEMOD_STOP,
			    0, NULL, 1, &cmd_result);
	if (status < 0)
		goto error;

	/* Halt SCU to enable safe non-atomic accesses */
	status = write16(state, SCU_COMM_EXEC__A, SCU_COMM_EXEC_HOLD);
	if (status < 0)
		goto error;

	/* Stop processors */
	status = write16(state, OFDM_SC_COMM_EXEC__A, OFDM_SC_COMM_EXEC_STOP);
	if (status < 0)
		goto error;
	status = write16(state, OFDM_LC_COMM_EXEC__A, OFDM_LC_COMM_EXEC_STOP);
	if (status < 0)
		goto error;

	/* Mandatory fix, always stop CP, required to set spl offset back to
		hardware default (is set to 0 by ucode during pilot detection */
	status = write16(state, OFDM_CP_COMM_EXEC__A, OFDM_CP_COMM_EXEC_STOP);
	if (status < 0)
		goto error;

	/*== Write channel settings to device ================================*/

	/* mode */
	switch (state->props.transmission_mode) {
	case TRANSMISSION_MODE_AUTO:
	default:
		operation_mode |= OFDM_SC_RA_RAM_OP_AUTO_MODE__M;
		/* fall through - try first guess DRX_FFTMODE_8K */
	case TRANSMISSION_MODE_8K:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_MODE_8K;
		break;
	case TRANSMISSION_MODE_2K:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_MODE_2K;
		break;
	}

	/* guard */
	switch (state->props.guard_interval) {
	default:
	case GUARD_INTERVAL_AUTO:
		operation_mode |= OFDM_SC_RA_RAM_OP_AUTO_GUARD__M;
		/* fall through - try first guess DRX_GUARD_1DIV4 */
	case GUARD_INTERVAL_1_4:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_GUARD_4;
		break;
	case GUARD_INTERVAL_1_32:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_GUARD_32;
		break;
	case GUARD_INTERVAL_1_16:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_GUARD_16;
		break;
	case GUARD_INTERVAL_1_8:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_GUARD_8;
		break;
	}

	/* hierarchy */
	switch (state->props.hierarchy) {
	case HIERARCHY_AUTO:
	case HIERARCHY_NONE:
	default:
		operation_mode |= OFDM_SC_RA_RAM_OP_AUTO_HIER__M;
		/* try first guess SC_RA_RAM_OP_PARAM_HIER_NO */
		/* transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_HIER_NO; */
		/* fall through */
	case HIERARCHY_1:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_HIER_A1;
		break;
	case HIERARCHY_2:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_HIER_A2;
		break;
	case HIERARCHY_4:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_HIER_A4;
		break;
	}


	/* modulation */
	switch (state->props.modulation) {
	case QAM_AUTO:
	default:
		operation_mode |= OFDM_SC_RA_RAM_OP_AUTO_CONST__M;
		/* fall through - try first guess DRX_CONSTELLATION_QAM64 */
	case QAM_64:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_CONST_QAM64;
		break;
	case QPSK:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_CONST_QPSK;
		break;
	case QAM_16:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_CONST_QAM16;
		break;
	}
#if 0
	/* No hierarchical channels support in BDA */
	/* Priority (only for hierarchical channels) */
	switch (channel->priority) {
	case DRX_PRIORITY_LOW:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_PRIO_LO;
		WR16(dev_addr, OFDM_EC_SB_PRIOR__A,
			OFDM_EC_SB_PRIOR_LO);
		break;
	case DRX_PRIORITY_HIGH:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_PRIO_HI;
		WR16(dev_addr, OFDM_EC_SB_PRIOR__A,
			OFDM_EC_SB_PRIOR_HI));
		break;
	case DRX_PRIORITY_UNKNOWN:	/* fall through */
	default:
		status = -EINVAL;
		goto error;
	}
#else
	/* Set Priorty high */
	transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_PRIO_HI;
	status = write16(state, OFDM_EC_SB_PRIOR__A, OFDM_EC_SB_PRIOR_HI);
	if (status < 0)
		goto error;
#endif

	/* coderate */
	switch (state->props.code_rate_HP) {
	case FEC_AUTO:
	default:
		operation_mode |= OFDM_SC_RA_RAM_OP_AUTO_RATE__M;
		/* fall through - try first guess DRX_CODERATE_2DIV3 */
	case FEC_2_3:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_RATE_2_3;
		break;
	case FEC_1_2:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_RATE_1_2;
		break;
	case FEC_3_4:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_RATE_3_4;
		break;
	case FEC_5_6:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_RATE_5_6;
		break;
	case FEC_7_8:
		transmission_params |= OFDM_SC_RA_RAM_OP_PARAM_RATE_7_8;
		break;
	}

	/*
	 * SAW filter selection: normaly not necesarry, but if wanted
	 * the application can select a SAW filter via the driver by
	 * using UIOs
	 */

	/* First determine real bandwidth (Hz) */
	/* Also set delay for impulse noise cruncher */
	/*
	 * Also set parameters for EC_OC fix, note EC_OC_REG_TMD_HIL_MAR is
	 * changed by SC for fix for some 8K,1/8 guard but is restored by
	 * InitEC and ResetEC functions
	 */
	switch (state->props.bandwidth_hz) {
	case 0:
		state->props.bandwidth_hz = 8000000;
		/* fall through */
	case 8000000:
		bandwidth = DRXK_BANDWIDTH_8MHZ_IN_HZ;
		status = write16(state, OFDM_SC_RA_RAM_SRMM_FIX_FACT_8K__A,
				 3052);
		if (status < 0)
			goto error;
		/* cochannel protection for PAL 8 MHz */
		status = write16(state, OFDM_SC_RA_RAM_NI_INIT_8K_PER_LEFT__A,
				 7);
		if (status < 0)
			goto error;
		status = write16(state, OFDM_SC_RA_RAM_NI_INIT_8K_PER_RIGHT__A,
				 7);
		if (status < 0)
			goto error;
		status = write16(state, OFDM_SC_RA_RAM_NI_INIT_2K_PER_LEFT__A,
				 7);
		if (status < 0)
			goto error;
		status = write16(state, OFDM_SC_RA_RAM_NI_INIT_2K_PER_RIGHT__A,
				 1);
		if (status < 0)
			goto error;
		break;
	case 7000000:
		bandwidth = DRXK_BANDWIDTH_7MHZ_IN_HZ;
		status = write16(state, OFDM_SC_RA_RAM_SRMM_FIX_FACT_8K__A,
				 3491);
		if (status < 0)
			goto error;
		/* cochannel protection for PAL 7 MHz */
		status = write16(state, OFDM_SC_RA_RAM_NI_INIT_8K_PER_LEFT__A,
				 8);
		if (status < 0)
			goto error;
		status = write16(state, OFDM_SC_RA_RAM_NI_INIT_8K_PER_RIGHT__A,
				 8);
		if (status < 0)
			goto error;
		status = write16(state, OFDM_SC_RA_RAM_NI_INIT_2K_PER_LEFT__A,
				 4);
		if (status < 0)
			goto error;
		status = write16(state, OFDM_SC_RA_RAM_NI_INIT_2K_PER_RIGHT__A,
				 1);
		if (status < 0)
			goto error;
		break;
	case 6000000:
		bandwidth = DRXK_BANDWIDTH_6MHZ_IN_HZ;
		status = write16(state, OFDM_SC_RA_RAM_SRMM_FIX_FACT_8K__A,
				 4073);
		if (status < 0)
			goto error;
		/* cochannel protection for NTSC 6 MHz */
		status = write16(state, OFDM_SC_RA_RAM_NI_INIT_8K_PER_LEFT__A,
				 19);
		if (status < 0)
			goto error;
		status = write16(state, OFDM_SC_RA_RAM_NI_INIT_8K_PER_RIGHT__A,
				 19);
		if (status < 0)
			goto error;
		status = write16(state, OFDM_SC_RA_RAM_NI_INIT_2K_PER_LEFT__A,
				 14);
		if (status < 0)
			goto error;
		status = write16(state, OFDM_SC_RA_RAM_NI_INIT_2K_PER_RIGHT__A,
				 1);
		if (status < 0)
			goto error;
		break;
	default:
		status = -EINVAL;
		goto error;
	}

	if (iqm_rc_rate_ofs == 0) {
		/* Now compute IQM_RC_RATE_OFS
			(((SysFreq/BandWidth)/2)/2) -1) * 2^23)
			=>
			((SysFreq / BandWidth) * (2^21)) - (2^23)
			*/
		/* (SysFreq / BandWidth) * (2^28)  */
		/*
		 * assert (MAX(sysClk)/MIN(bandwidth) < 16)
		 *	=> assert(MAX(sysClk) < 16*MIN(bandwidth))
		 *	=> assert(109714272 > 48000000) = true
		 * so Frac 28 can be used
		 */
		iqm_rc_rate_ofs = Frac28a((u32)
					((state->m_sys_clock_freq *
						1000) / 3), bandwidth);
		/* (SysFreq / BandWidth) * (2^21), rounding before truncating */
		if ((iqm_rc_rate_ofs & 0x7fL) >= 0x40)
			iqm_rc_rate_ofs += 0x80L;
		iqm_rc_rate_ofs = iqm_rc_rate_ofs >> 7;
		/* ((SysFreq / BandWidth) * (2^21)) - (2^23)  */
		iqm_rc_rate_ofs = iqm_rc_rate_ofs - (1 << 23);
	}

	iqm_rc_rate_ofs &=
		((((u32) IQM_RC_RATE_OFS_HI__M) <<
		IQM_RC_RATE_OFS_LO__W) | IQM_RC_RATE_OFS_LO__M);
	status = write32(state, IQM_RC_RATE_OFS_LO__A, iqm_rc_rate_ofs);
	if (status < 0)
		goto error;

	/* Bandwidth setting done */

#if 0
	status = dvbt_set_frequency_shift(demod, channel, tuner_offset);
	if (status < 0)
		goto error;
#endif
	status = set_frequency_shifter(state, intermediate_freqk_hz,
				       tuner_freq_offset, true);
	if (status < 0)
		goto error;

	/*== start SC, write channel settings to SC ==========================*/

	/* Activate SCU to enable SCU commands */
	status = write16(state, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE);
	if (status < 0)
		goto error;

	/* Enable SC after setting all other parameters */
	status = write16(state, OFDM_SC_COMM_STATE__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, OFDM_SC_COMM_EXEC__A, 1);
	if (status < 0)
		goto error;


	status = scu_command(state, SCU_RAM_COMMAND_STANDARD_OFDM
			     | SCU_RAM_COMMAND_CMD_DEMOD_START,
			     0, NULL, 1, &cmd_result);
	if (status < 0)
		goto error;

	/* Write SC parameter registers, set all AUTO flags in operation mode */
	param1 = (OFDM_SC_RA_RAM_OP_AUTO_MODE__M |
			OFDM_SC_RA_RAM_OP_AUTO_GUARD__M |
			OFDM_SC_RA_RAM_OP_AUTO_CONST__M |
			OFDM_SC_RA_RAM_OP_AUTO_HIER__M |
			OFDM_SC_RA_RAM_OP_AUTO_RATE__M);
	status = dvbt_sc_command(state, OFDM_SC_RA_RAM_CMD_SET_PREF_PARAM,
				0, transmission_params, param1, 0, 0, 0);
	if (status < 0)
		goto error;

	if (!state->m_drxk_a3_rom_code)
		status = dvbt_ctrl_set_sqi_speed(state, &state->m_sqi_speed);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	return status;
}


/*============================================================================*/

/*
* \brief Retrieve lock status .
* \param demod    Pointer to demodulator instance.
* \param lockStat Pointer to lock status structure.
* \return DRXStatus_t.
*
*/
static int get_dvbt_lock_status(struct drxk_state *state, u32 *p_lock_status)
{
	int status;
	const u16 mpeg_lock_mask = (OFDM_SC_RA_RAM_LOCK_MPEG__M |
				    OFDM_SC_RA_RAM_LOCK_FEC__M);
	const u16 fec_lock_mask = (OFDM_SC_RA_RAM_LOCK_FEC__M);
	const u16 demod_lock_mask = OFDM_SC_RA_RAM_LOCK_DEMOD__M;

	u16 sc_ra_ram_lock = 0;
	u16 sc_comm_exec = 0;

	dprintk(1, "\n");

	*p_lock_status = NOT_LOCKED;
	/* driver 0.9.0 */
	/* Check if SC is running */
	status = read16(state, OFDM_SC_COMM_EXEC__A, &sc_comm_exec);
	if (status < 0)
		goto end;
	if (sc_comm_exec == OFDM_SC_COMM_EXEC_STOP)
		goto end;

	status = read16(state, OFDM_SC_RA_RAM_LOCK__A, &sc_ra_ram_lock);
	if (status < 0)
		goto end;

	if ((sc_ra_ram_lock & mpeg_lock_mask) == mpeg_lock_mask)
		*p_lock_status = MPEG_LOCK;
	else if ((sc_ra_ram_lock & fec_lock_mask) == fec_lock_mask)
		*p_lock_status = FEC_LOCK;
	else if ((sc_ra_ram_lock & demod_lock_mask) == demod_lock_mask)
		*p_lock_status = DEMOD_LOCK;
	else if (sc_ra_ram_lock & OFDM_SC_RA_RAM_LOCK_NODVBT__M)
		*p_lock_status = NEVER_LOCK;
end:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	return status;
}

static int power_up_qam(struct drxk_state *state)
{
	enum drx_power_mode power_mode = DRXK_POWER_DOWN_OFDM;
	int status;

	dprintk(1, "\n");
	status = ctrl_power_mode(state, &power_mode);
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	return status;
}


/* Power Down QAM */
static int power_down_qam(struct drxk_state *state)
{
	u16 data = 0;
	u16 cmd_result;
	int status = 0;

	dprintk(1, "\n");
	status = read16(state, SCU_COMM_EXEC__A, &data);
	if (status < 0)
		goto error;
	if (data == SCU_COMM_EXEC_ACTIVE) {
		/*
			STOP demodulator
			QAM and HW blocks
			*/
		/* stop all comstate->m_exec */
		status = write16(state, QAM_COMM_EXEC__A, QAM_COMM_EXEC_STOP);
		if (status < 0)
			goto error;
		status = scu_command(state, SCU_RAM_COMMAND_STANDARD_QAM
				     | SCU_RAM_COMMAND_CMD_DEMOD_STOP,
				     0, NULL, 1, &cmd_result);
		if (status < 0)
			goto error;
	}
	/* powerdown AFE                   */
	status = set_iqm_af(state, false);

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	return status;
}

/*============================================================================*/

/*
* \brief Setup of the QAM Measurement intervals for signal quality
* \param demod instance of demod.
* \param modulation current modulation.
* \return DRXStatus_t.
*
*  NOTE:
*  Take into account that for certain settings the errorcounters can overflow.
*  The implementation does not check this.
*
*/
static int set_qam_measurement(struct drxk_state *state,
			     enum e_drxk_constellation modulation,
			     u32 symbol_rate)
{
	u32 fec_bits_desired = 0;	/* BER accounting period */
	u32 fec_rs_period_total = 0;	/* Total period */
	u16 fec_rs_prescale = 0;	/* ReedSolomon Measurement Prescale */
	u16 fec_rs_period = 0;	/* Value for corresponding I2C register */
	int status = 0;

	dprintk(1, "\n");

	fec_rs_prescale = 1;
	/* fec_bits_desired = symbol_rate [kHz] *
		FrameLenght [ms] *
		(modulation + 1) *
		SyncLoss (== 1) *
		ViterbiLoss (==1)
		*/
	switch (modulation) {
	case DRX_CONSTELLATION_QAM16:
		fec_bits_desired = 4 * symbol_rate;
		break;
	case DRX_CONSTELLATION_QAM32:
		fec_bits_desired = 5 * symbol_rate;
		break;
	case DRX_CONSTELLATION_QAM64:
		fec_bits_desired = 6 * symbol_rate;
		break;
	case DRX_CONSTELLATION_QAM128:
		fec_bits_desired = 7 * symbol_rate;
		break;
	case DRX_CONSTELLATION_QAM256:
		fec_bits_desired = 8 * symbol_rate;
		break;
	default:
		status = -EINVAL;
	}
	if (status < 0)
		goto error;

	fec_bits_desired /= 1000;	/* symbol_rate [Hz] -> symbol_rate [kHz] */
	fec_bits_desired *= 500;	/* meas. period [ms] */

	/* Annex A/C: bits/RsPeriod = 204 * 8 = 1632 */
	/* fec_rs_period_total = fec_bits_desired / 1632 */
	fec_rs_period_total = (fec_bits_desired / 1632UL) + 1;	/* roughly ceil */

	/* fec_rs_period_total =  fec_rs_prescale * fec_rs_period  */
	fec_rs_prescale = 1 + (u16) (fec_rs_period_total >> 16);
	if (fec_rs_prescale == 0) {
		/* Divide by zero (though impossible) */
		status = -EINVAL;
		if (status < 0)
			goto error;
	}
	fec_rs_period =
		((u16) fec_rs_period_total +
		(fec_rs_prescale >> 1)) / fec_rs_prescale;

	/* write corresponding registers */
	status = write16(state, FEC_RS_MEASUREMENT_PERIOD__A, fec_rs_period);
	if (status < 0)
		goto error;
	status = write16(state, FEC_RS_MEASUREMENT_PRESCALE__A,
			 fec_rs_prescale);
	if (status < 0)
		goto error;
	status = write16(state, FEC_OC_SNC_FAIL_PERIOD__A, fec_rs_period);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int set_qam16(struct drxk_state *state)
{
	int status = 0;

	dprintk(1, "\n");
	/* QAM Equalizer Setup */
	/* Equalizer */
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD0__A, 13517);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD1__A, 13517);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD2__A, 13517);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD3__A, 13517);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD4__A, 13517);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD5__A, 13517);
	if (status < 0)
		goto error;
	/* Decision Feedback Equalizer */
	status = write16(state, QAM_DQ_QUAL_FUN0__A, 2);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN1__A, 2);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN2__A, 2);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN3__A, 2);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN4__A, 2);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN5__A, 0);
	if (status < 0)
		goto error;

	status = write16(state, QAM_SY_SYNC_HWM__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, QAM_SY_SYNC_AWM__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, QAM_SY_SYNC_LWM__A, 3);
	if (status < 0)
		goto error;

	/* QAM Slicer Settings */
	status = write16(state, SCU_RAM_QAM_SL_SIG_POWER__A,
			 DRXK_QAM_SL_SIG_POWER_QAM16);
	if (status < 0)
		goto error;

	/* QAM Loop Controller Coeficients */
	status = write16(state, SCU_RAM_QAM_LC_CA_FINE__A, 15);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CA_COARSE__A, 40);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_FINE__A, 12);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_COARSE__A, 24);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_FINE__A, 12);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_COARSE__A, 16);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_QAM_LC_CP_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CP_MEDIUM__A, 20);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CP_COARSE__A, 80);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_MEDIUM__A, 20);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_COARSE__A, 50);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_FINE__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_MEDIUM__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_COARSE__A, 32);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 10);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_COARSE__A, 10);
	if (status < 0)
		goto error;


	/* QAM State Machine (FSM) Thresholds */

	status = write16(state, SCU_RAM_QAM_FSM_RTH__A, 140);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_FTH__A, 50);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_CTH__A, 95);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_PTH__A, 120);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_QTH__A, 230);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_MTH__A, 105);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_QAM_FSM_RATE_LIM__A, 40);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_COUNT_LIM__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_FREQ_LIM__A, 24);
	if (status < 0)
		goto error;


	/* QAM FSM Tracking Parameters */

	status = write16(state, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, (u16) 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, (u16) 220);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, (u16) 25);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, (u16) 6);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16) -24);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16) -65);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16) -127);
	if (status < 0)
		goto error;

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

/*============================================================================*/

/*
* \brief QAM32 specific setup
* \param demod instance of demod.
* \return DRXStatus_t.
*/
static int set_qam32(struct drxk_state *state)
{
	int status = 0;

	dprintk(1, "\n");

	/* QAM Equalizer Setup */
	/* Equalizer */
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD0__A, 6707);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD1__A, 6707);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD2__A, 6707);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD3__A, 6707);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD4__A, 6707);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD5__A, 6707);
	if (status < 0)
		goto error;

	/* Decision Feedback Equalizer */
	status = write16(state, QAM_DQ_QUAL_FUN0__A, 3);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN1__A, 3);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN2__A, 3);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN3__A, 3);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN4__A, 3);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN5__A, 0);
	if (status < 0)
		goto error;

	status = write16(state, QAM_SY_SYNC_HWM__A, 6);
	if (status < 0)
		goto error;
	status = write16(state, QAM_SY_SYNC_AWM__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, QAM_SY_SYNC_LWM__A, 3);
	if (status < 0)
		goto error;

	/* QAM Slicer Settings */

	status = write16(state, SCU_RAM_QAM_SL_SIG_POWER__A,
			 DRXK_QAM_SL_SIG_POWER_QAM32);
	if (status < 0)
		goto error;


	/* QAM Loop Controller Coeficients */

	status = write16(state, SCU_RAM_QAM_LC_CA_FINE__A, 15);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CA_COARSE__A, 40);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_FINE__A, 12);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_COARSE__A, 24);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_FINE__A, 12);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_COARSE__A, 16);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_QAM_LC_CP_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CP_MEDIUM__A, 20);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CP_COARSE__A, 80);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_MEDIUM__A, 20);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_COARSE__A, 50);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_FINE__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_MEDIUM__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_COARSE__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 10);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_COARSE__A, 0);
	if (status < 0)
		goto error;


	/* QAM State Machine (FSM) Thresholds */

	status = write16(state, SCU_RAM_QAM_FSM_RTH__A, 90);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_FTH__A, 50);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_CTH__A, 80);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_PTH__A, 100);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_QTH__A, 170);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_MTH__A, 100);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_QAM_FSM_RATE_LIM__A, 40);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_COUNT_LIM__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_FREQ_LIM__A, 10);
	if (status < 0)
		goto error;


	/* QAM FSM Tracking Parameters */

	status = write16(state, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, (u16) 12);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, (u16) 140);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, (u16) -8);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, (u16) -16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16) -26);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16) -56);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16) -86);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

/*============================================================================*/

/*
* \brief QAM64 specific setup
* \param demod instance of demod.
* \return DRXStatus_t.
*/
static int set_qam64(struct drxk_state *state)
{
	int status = 0;

	dprintk(1, "\n");
	/* QAM Equalizer Setup */
	/* Equalizer */
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD0__A, 13336);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD1__A, 12618);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD2__A, 11988);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD3__A, 13809);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD4__A, 13809);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD5__A, 15609);
	if (status < 0)
		goto error;

	/* Decision Feedback Equalizer */
	status = write16(state, QAM_DQ_QUAL_FUN0__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN1__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN2__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN3__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN4__A, 3);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN5__A, 0);
	if (status < 0)
		goto error;

	status = write16(state, QAM_SY_SYNC_HWM__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, QAM_SY_SYNC_AWM__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, QAM_SY_SYNC_LWM__A, 3);
	if (status < 0)
		goto error;

	/* QAM Slicer Settings */
	status = write16(state, SCU_RAM_QAM_SL_SIG_POWER__A,
			 DRXK_QAM_SL_SIG_POWER_QAM64);
	if (status < 0)
		goto error;


	/* QAM Loop Controller Coeficients */

	status = write16(state, SCU_RAM_QAM_LC_CA_FINE__A, 15);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CA_COARSE__A, 40);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_FINE__A, 12);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_COARSE__A, 24);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_FINE__A, 12);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_COARSE__A, 16);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_QAM_LC_CP_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CP_MEDIUM__A, 30);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CP_COARSE__A, 100);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_MEDIUM__A, 30);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_COARSE__A, 50);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_FINE__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_MEDIUM__A, 25);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_COARSE__A, 48);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 10);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_COARSE__A, 10);
	if (status < 0)
		goto error;


	/* QAM State Machine (FSM) Thresholds */

	status = write16(state, SCU_RAM_QAM_FSM_RTH__A, 100);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_FTH__A, 60);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_CTH__A, 80);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_PTH__A, 110);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_QTH__A, 200);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_MTH__A, 95);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_QAM_FSM_RATE_LIM__A, 40);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_COUNT_LIM__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_FREQ_LIM__A, 15);
	if (status < 0)
		goto error;


	/* QAM FSM Tracking Parameters */

	status = write16(state, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, (u16) 12);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, (u16) 141);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, (u16) 7);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, (u16) 0);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16) -15);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16) -45);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16) -80);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	return status;
}

/*============================================================================*/

/*
* \brief QAM128 specific setup
* \param demod: instance of demod.
* \return DRXStatus_t.
*/
static int set_qam128(struct drxk_state *state)
{
	int status = 0;

	dprintk(1, "\n");
	/* QAM Equalizer Setup */
	/* Equalizer */
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD0__A, 6564);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD1__A, 6598);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD2__A, 6394);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD3__A, 6409);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD4__A, 6656);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD5__A, 7238);
	if (status < 0)
		goto error;

	/* Decision Feedback Equalizer */
	status = write16(state, QAM_DQ_QUAL_FUN0__A, 6);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN1__A, 6);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN2__A, 6);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN3__A, 6);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN4__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN5__A, 0);
	if (status < 0)
		goto error;

	status = write16(state, QAM_SY_SYNC_HWM__A, 6);
	if (status < 0)
		goto error;
	status = write16(state, QAM_SY_SYNC_AWM__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, QAM_SY_SYNC_LWM__A, 3);
	if (status < 0)
		goto error;


	/* QAM Slicer Settings */

	status = write16(state, SCU_RAM_QAM_SL_SIG_POWER__A,
			 DRXK_QAM_SL_SIG_POWER_QAM128);
	if (status < 0)
		goto error;


	/* QAM Loop Controller Coeficients */

	status = write16(state, SCU_RAM_QAM_LC_CA_FINE__A, 15);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CA_COARSE__A, 40);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_FINE__A, 12);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_COARSE__A, 24);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_FINE__A, 12);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_COARSE__A, 16);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_QAM_LC_CP_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CP_MEDIUM__A, 40);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CP_COARSE__A, 120);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_MEDIUM__A, 40);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_COARSE__A, 60);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_FINE__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_MEDIUM__A, 25);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_COARSE__A, 64);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 10);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_COARSE__A, 0);
	if (status < 0)
		goto error;


	/* QAM State Machine (FSM) Thresholds */

	status = write16(state, SCU_RAM_QAM_FSM_RTH__A, 50);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_FTH__A, 60);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_CTH__A, 80);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_PTH__A, 100);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_QTH__A, 140);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_MTH__A, 100);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_QAM_FSM_RATE_LIM__A, 40);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_COUNT_LIM__A, 5);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_QAM_FSM_FREQ_LIM__A, 12);
	if (status < 0)
		goto error;

	/* QAM FSM Tracking Parameters */

	status = write16(state, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, (u16) 8);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, (u16) 65);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, (u16) 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, (u16) 3);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16) -1);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16) -12);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16) -23);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	return status;
}

/*============================================================================*/

/*
* \brief QAM256 specific setup
* \param demod: instance of demod.
* \return DRXStatus_t.
*/
static int set_qam256(struct drxk_state *state)
{
	int status = 0;

	dprintk(1, "\n");
	/* QAM Equalizer Setup */
	/* Equalizer */
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD0__A, 11502);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD1__A, 12084);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD2__A, 12543);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD3__A, 12931);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD4__A, 13629);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_EQ_CMA_RAD5__A, 15385);
	if (status < 0)
		goto error;

	/* Decision Feedback Equalizer */
	status = write16(state, QAM_DQ_QUAL_FUN0__A, 8);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN1__A, 8);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN2__A, 8);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN3__A, 8);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN4__A, 6);
	if (status < 0)
		goto error;
	status = write16(state, QAM_DQ_QUAL_FUN5__A, 0);
	if (status < 0)
		goto error;

	status = write16(state, QAM_SY_SYNC_HWM__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, QAM_SY_SYNC_AWM__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, QAM_SY_SYNC_LWM__A, 3);
	if (status < 0)
		goto error;

	/* QAM Slicer Settings */

	status = write16(state, SCU_RAM_QAM_SL_SIG_POWER__A,
			 DRXK_QAM_SL_SIG_POWER_QAM256);
	if (status < 0)
		goto error;


	/* QAM Loop Controller Coeficients */

	status = write16(state, SCU_RAM_QAM_LC_CA_FINE__A, 15);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CA_COARSE__A, 40);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_FINE__A, 12);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_MEDIUM__A, 24);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EP_COARSE__A, 24);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_FINE__A, 12);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_MEDIUM__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_EI_COARSE__A, 16);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_QAM_LC_CP_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CP_MEDIUM__A, 50);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CP_COARSE__A, 250);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_MEDIUM__A, 50);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CI_COARSE__A, 125);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_FINE__A, 16);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_MEDIUM__A, 25);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF_COARSE__A, 48);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_FINE__A, 5);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_MEDIUM__A, 10);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_LC_CF1_COARSE__A, 10);
	if (status < 0)
		goto error;


	/* QAM State Machine (FSM) Thresholds */

	status = write16(state, SCU_RAM_QAM_FSM_RTH__A, 50);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_FTH__A, 60);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_CTH__A, 80);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_PTH__A, 100);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_QTH__A, 150);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_MTH__A, 110);
	if (status < 0)
		goto error;

	status = write16(state, SCU_RAM_QAM_FSM_RATE_LIM__A, 40);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_COUNT_LIM__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_FREQ_LIM__A, 12);
	if (status < 0)
		goto error;


	/* QAM FSM Tracking Parameters */

	status = write16(state, SCU_RAM_QAM_FSM_MEDIAN_AV_MULT__A, (u16) 8);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_RADIUS_AV_LIMIT__A, (u16) 74);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET1__A, (u16) 18);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET2__A, (u16) 13);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET3__A, (u16) 7);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET4__A, (u16) 0);
	if (status < 0)
		goto error;
	status = write16(state, SCU_RAM_QAM_FSM_LCAVG_OFFSET5__A, (u16) -8);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}


/*============================================================================*/
/*
* \brief Reset QAM block.
* \param demod:   instance of demod.
* \param channel: pointer to channel data.
* \return DRXStatus_t.
*/
static int qam_reset_qam(struct drxk_state *state)
{
	int status;
	u16 cmd_result;

	dprintk(1, "\n");
	/* Stop QAM comstate->m_exec */
	status = write16(state, QAM_COMM_EXEC__A, QAM_COMM_EXEC_STOP);
	if (status < 0)
		goto error;

	status = scu_command(state, SCU_RAM_COMMAND_STANDARD_QAM
			     | SCU_RAM_COMMAND_CMD_DEMOD_RESET,
			     0, NULL, 1, &cmd_result);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

/*============================================================================*/

/*
* \brief Set QAM symbolrate.
* \param demod:   instance of demod.
* \param channel: pointer to channel data.
* \return DRXStatus_t.
*/
static int qam_set_symbolrate(struct drxk_state *state)
{
	u32 adc_frequency = 0;
	u32 symb_freq = 0;
	u32 iqm_rc_rate = 0;
	u16 ratesel = 0;
	u32 lc_symb_rate = 0;
	int status;

	dprintk(1, "\n");
	/* Select & calculate correct IQM rate */
	adc_frequency = (state->m_sys_clock_freq * 1000) / 3;
	ratesel = 0;
	if (state->props.symbol_rate <= 1188750)
		ratesel = 3;
	else if (state->props.symbol_rate <= 2377500)
		ratesel = 2;
	else if (state->props.symbol_rate <= 4755000)
		ratesel = 1;
	status = write16(state, IQM_FD_RATESEL__A, ratesel);
	if (status < 0)
		goto error;

	/*
		IqmRcRate = ((Fadc / (symbolrate * (4<<ratesel))) - 1) * (1<<23)
		*/
	symb_freq = state->props.symbol_rate * (1 << ratesel);
	if (symb_freq == 0) {
		/* Divide by zero */
		status = -EINVAL;
		goto error;
	}
	iqm_rc_rate = (adc_frequency / symb_freq) * (1 << 21) +
		(Frac28a((adc_frequency % symb_freq), symb_freq) >> 7) -
		(1 << 23);
	status = write32(state, IQM_RC_RATE_OFS_LO__A, iqm_rc_rate);
	if (status < 0)
		goto error;
	state->m_iqm_rc_rate = iqm_rc_rate;
	/*
		LcSymbFreq = round (.125 *  symbolrate / adc_freq * (1<<15))
		*/
	symb_freq = state->props.symbol_rate;
	if (adc_frequency == 0) {
		/* Divide by zero */
		status = -EINVAL;
		goto error;
	}
	lc_symb_rate = (symb_freq / adc_frequency) * (1 << 12) +
		(Frac28a((symb_freq % adc_frequency), adc_frequency) >>
		16);
	if (lc_symb_rate > 511)
		lc_symb_rate = 511;
	status = write16(state, QAM_LC_SYMBOL_FREQ__A, (u16) lc_symb_rate);

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

/*============================================================================*/

/*
* \brief Get QAM lock status.
* \param demod:   instance of demod.
* \param channel: pointer to channel data.
* \return DRXStatus_t.
*/

static int get_qam_lock_status(struct drxk_state *state, u32 *p_lock_status)
{
	int status;
	u16 result[2] = { 0, 0 };

	dprintk(1, "\n");
	*p_lock_status = NOT_LOCKED;
	status = scu_command(state,
			SCU_RAM_COMMAND_STANDARD_QAM |
			SCU_RAM_COMMAND_CMD_DEMOD_GET_LOCK, 0, NULL, 2,
			result);
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	if (result[1] < SCU_RAM_QAM_LOCKED_LOCKED_DEMOD_LOCKED) {
		/* 0x0000 NOT LOCKED */
	} else if (result[1] < SCU_RAM_QAM_LOCKED_LOCKED_LOCKED) {
		/* 0x4000 DEMOD LOCKED */
		*p_lock_status = DEMOD_LOCK;
	} else if (result[1] < SCU_RAM_QAM_LOCKED_LOCKED_NEVER_LOCK) {
		/* 0x8000 DEMOD + FEC LOCKED (system lock) */
		*p_lock_status = MPEG_LOCK;
	} else {
		/* 0xC000 NEVER LOCKED */
		/* (system will never be able to lock to the signal) */
		/*
		 * TODO: check this, intermediate & standard specific lock
		 * states are not taken into account here
		 */
		*p_lock_status = NEVER_LOCK;
	}
	return status;
}

#define QAM_MIRROR__M         0x03
#define QAM_MIRROR_NORMAL     0x00
#define QAM_MIRRORED          0x01
#define QAM_MIRROR_AUTO_ON    0x02
#define QAM_LOCKRANGE__M      0x10
#define QAM_LOCKRANGE_NORMAL  0x10

static int qam_demodulator_command(struct drxk_state *state,
				 int number_of_parameters)
{
	int status;
	u16 cmd_result;
	u16 set_param_parameters[4] = { 0, 0, 0, 0 };

	set_param_parameters[0] = state->m_constellation;	/* modulation     */
	set_param_parameters[1] = DRXK_QAM_I12_J17;	/* interleave mode   */

	if (number_of_parameters == 2) {
		u16 set_env_parameters[1] = { 0 };

		if (state->m_operation_mode == OM_QAM_ITU_C)
			set_env_parameters[0] = QAM_TOP_ANNEX_C;
		else
			set_env_parameters[0] = QAM_TOP_ANNEX_A;

		status = scu_command(state,
				     SCU_RAM_COMMAND_STANDARD_QAM
				     | SCU_RAM_COMMAND_CMD_DEMOD_SET_ENV,
				     1, set_env_parameters, 1, &cmd_result);
		if (status < 0)
			goto error;

		status = scu_command(state,
				     SCU_RAM_COMMAND_STANDARD_QAM
				     | SCU_RAM_COMMAND_CMD_DEMOD_SET_PARAM,
				     number_of_parameters, set_param_parameters,
				     1, &cmd_result);
	} else if (number_of_parameters == 4) {
		if (state->m_operation_mode == OM_QAM_ITU_C)
			set_param_parameters[2] = QAM_TOP_ANNEX_C;
		else
			set_param_parameters[2] = QAM_TOP_ANNEX_A;

		set_param_parameters[3] |= (QAM_MIRROR_AUTO_ON);
		/* Env parameters */
		/* check for LOCKRANGE Extented */
		/* set_param_parameters[3] |= QAM_LOCKRANGE_NORMAL; */

		status = scu_command(state,
				     SCU_RAM_COMMAND_STANDARD_QAM
				     | SCU_RAM_COMMAND_CMD_DEMOD_SET_PARAM,
				     number_of_parameters, set_param_parameters,
				     1, &cmd_result);
	} else {
		pr_warn("Unknown QAM demodulator parameter count %d\n",
			number_of_parameters);
		status = -EINVAL;
	}

error:
	if (status < 0)
		pr_warn("Warning %d on %s\n", status, __func__);
	return status;
}

static int set_qam(struct drxk_state *state, u16 intermediate_freqk_hz,
		  s32 tuner_freq_offset)
{
	int status;
	u16 cmd_result;
	int qam_demod_param_count = state->qam_demod_parameter_count;

	dprintk(1, "\n");
	/*
	 * STEP 1: reset demodulator
	 *	resets FEC DI and FEC RS
	 *	resets QAM block
	 *	resets SCU variables
	 */
	status = write16(state, FEC_DI_COMM_EXEC__A, FEC_DI_COMM_EXEC_STOP);
	if (status < 0)
		goto error;
	status = write16(state, FEC_RS_COMM_EXEC__A, FEC_RS_COMM_EXEC_STOP);
	if (status < 0)
		goto error;
	status = qam_reset_qam(state);
	if (status < 0)
		goto error;

	/*
	 * STEP 2: configure demodulator
	 *	-set params; resets IQM,QAM,FEC HW; initializes some
	 *       SCU variables
	 */
	status = qam_set_symbolrate(state);
	if (status < 0)
		goto error;

	/* Set params */
	switch (state->props.modulation) {
	case QAM_256:
		state->m_constellation = DRX_CONSTELLATION_QAM256;
		break;
	case QAM_AUTO:
	case QAM_64:
		state->m_constellation = DRX_CONSTELLATION_QAM64;
		break;
	case QAM_16:
		state->m_constellation = DRX_CONSTELLATION_QAM16;
		break;
	case QAM_32:
		state->m_constellation = DRX_CONSTELLATION_QAM32;
		break;
	case QAM_128:
		state->m_constellation = DRX_CONSTELLATION_QAM128;
		break;
	default:
		status = -EINVAL;
		break;
	}
	if (status < 0)
		goto error;

	/* Use the 4-parameter if it's requested or we're probing for
	 * the correct command. */
	if (state->qam_demod_parameter_count == 4
		|| !state->qam_demod_parameter_count) {
		qam_demod_param_count = 4;
		status = qam_demodulator_command(state, qam_demod_param_count);
	}

	/* Use the 2-parameter command if it was requested or if we're
	 * probing for the correct command and the 4-parameter command
	 * failed. */
	if (state->qam_demod_parameter_count == 2
		|| (!state->qam_demod_parameter_count && status < 0)) {
		qam_demod_param_count = 2;
		status = qam_demodulator_command(state, qam_demod_param_count);
	}

	if (status < 0) {
		dprintk(1, "Could not set demodulator parameters.\n");
		dprintk(1,
			"Make sure qam_demod_parameter_count (%d) is correct for your firmware (%s).\n",
			state->qam_demod_parameter_count,
			state->microcode_name);
		goto error;
	} else if (!state->qam_demod_parameter_count) {
		dprintk(1,
			"Auto-probing the QAM command parameters was successful - using %d parameters.\n",
			qam_demod_param_count);

		/*
		 * One of our commands was successful. We don't need to
		 * auto-probe anymore, now that we got the correct command.
		 */
		state->qam_demod_parameter_count = qam_demod_param_count;
	}

	/*
	 * STEP 3: enable the system in a mode where the ADC provides valid
	 * signal setup modulation independent registers
	 */
#if 0
	status = set_frequency(channel, tuner_freq_offset));
	if (status < 0)
		goto error;
#endif
	status = set_frequency_shifter(state, intermediate_freqk_hz,
				       tuner_freq_offset, true);
	if (status < 0)
		goto error;

	/* Setup BER measurement */
	status = set_qam_measurement(state, state->m_constellation,
				     state->props.symbol_rate);
	if (status < 0)
		goto error;

	/* Reset default values */
	status = write16(state, IQM_CF_SCALE_SH__A, IQM_CF_SCALE_SH__PRE);
	if (status < 0)
		goto error;
	status = write16(state, QAM_SY_TIMEOUT__A, QAM_SY_TIMEOUT__PRE);
	if (status < 0)
		goto error;

	/* Reset default LC values */
	status = write16(state, QAM_LC_RATE_LIMIT__A, 3);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_LPF_FACTORP__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_LPF_FACTORI__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_MODE__A, 7);
	if (status < 0)
		goto error;

	status = write16(state, QAM_LC_QUAL_TAB0__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB1__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB2__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB3__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB4__A, 2);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB5__A, 2);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB6__A, 2);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB8__A, 2);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB9__A, 2);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB10__A, 2);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB12__A, 2);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB15__A, 3);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB16__A, 3);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB20__A, 4);
	if (status < 0)
		goto error;
	status = write16(state, QAM_LC_QUAL_TAB25__A, 4);
	if (status < 0)
		goto error;

	/* Mirroring, QAM-block starting point not inverted */
	status = write16(state, QAM_SY_SP_INV__A,
			 QAM_SY_SP_INV_SPECTRUM_INV_DIS);
	if (status < 0)
		goto error;

	/* Halt SCU to enable safe non-atomic accesses */
	status = write16(state, SCU_COMM_EXEC__A, SCU_COMM_EXEC_HOLD);
	if (status < 0)
		goto error;

	/* STEP 4: modulation specific setup */
	switch (state->props.modulation) {
	case QAM_16:
		status = set_qam16(state);
		break;
	case QAM_32:
		status = set_qam32(state);
		break;
	case QAM_AUTO:
	case QAM_64:
		status = set_qam64(state);
		break;
	case QAM_128:
		status = set_qam128(state);
		break;
	case QAM_256:
		status = set_qam256(state);
		break;
	default:
		status = -EINVAL;
		break;
	}
	if (status < 0)
		goto error;

	/* Activate SCU to enable SCU commands */
	status = write16(state, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE);
	if (status < 0)
		goto error;

	/* Re-configure MPEG output, requires knowledge of channel bitrate */
	/* extAttr->currentChannel.modulation = channel->modulation; */
	/* extAttr->currentChannel.symbolrate    = channel->symbolrate; */
	status = mpegts_dto_setup(state, state->m_operation_mode);
	if (status < 0)
		goto error;

	/* start processes */
	status = mpegts_start(state);
	if (status < 0)
		goto error;
	status = write16(state, FEC_COMM_EXEC__A, FEC_COMM_EXEC_ACTIVE);
	if (status < 0)
		goto error;
	status = write16(state, QAM_COMM_EXEC__A, QAM_COMM_EXEC_ACTIVE);
	if (status < 0)
		goto error;
	status = write16(state, IQM_COMM_EXEC__A, IQM_COMM_EXEC_B_ACTIVE);
	if (status < 0)
		goto error;

	/* STEP 5: start QAM demodulator (starts FEC, QAM and IQM HW) */
	status = scu_command(state, SCU_RAM_COMMAND_STANDARD_QAM
			     | SCU_RAM_COMMAND_CMD_DEMOD_START,
			     0, NULL, 1, &cmd_result);
	if (status < 0)
		goto error;

	/* update global DRXK data container */
/*?     extAttr->qamInterleaveMode = DRXK_QAM_I12_J17; */

error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int set_qam_standard(struct drxk_state *state,
			  enum operation_mode o_mode)
{
	int status;
#ifdef DRXK_QAM_TAPS
#define DRXK_QAMA_TAPS_SELECT
#include "drxk_filters.h"
#undef DRXK_QAMA_TAPS_SELECT
#endif

	dprintk(1, "\n");

	/* added antenna switch */
	switch_antenna_to_qam(state);

	/* Ensure correct power-up mode */
	status = power_up_qam(state);
	if (status < 0)
		goto error;
	/* Reset QAM block */
	status = qam_reset_qam(state);
	if (status < 0)
		goto error;

	/* Setup IQM */

	status = write16(state, IQM_COMM_EXEC__A, IQM_COMM_EXEC_B_STOP);
	if (status < 0)
		goto error;
	status = write16(state, IQM_AF_AMUX__A, IQM_AF_AMUX_SIGNAL2ADC);
	if (status < 0)
		goto error;

	/* Upload IQM Channel Filter settings by
		boot loader from ROM table */
	switch (o_mode) {
	case OM_QAM_ITU_A:
		status = bl_chain_cmd(state, DRXK_BL_ROM_OFFSET_TAPS_ITU_A,
				      DRXK_BLCC_NR_ELEMENTS_TAPS,
			DRXK_BLC_TIMEOUT);
		break;
	case OM_QAM_ITU_C:
		status = bl_direct_cmd(state, IQM_CF_TAP_RE0__A,
				       DRXK_BL_ROM_OFFSET_TAPS_ITU_C,
				       DRXK_BLDC_NR_ELEMENTS_TAPS,
				       DRXK_BLC_TIMEOUT);
		if (status < 0)
			goto error;
		status = bl_direct_cmd(state,
				       IQM_CF_TAP_IM0__A,
				       DRXK_BL_ROM_OFFSET_TAPS_ITU_C,
				       DRXK_BLDC_NR_ELEMENTS_TAPS,
				       DRXK_BLC_TIMEOUT);
		break;
	default:
		status = -EINVAL;
	}
	if (status < 0)
		goto error;

	status = write16(state, IQM_CF_OUT_ENA__A, 1 << IQM_CF_OUT_ENA_QAM__B);
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_SYMMETRIC__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_MIDTAP__A,
		     ((1 << IQM_CF_MIDTAP_RE__B) | (1 << IQM_CF_MIDTAP_IM__B)));
	if (status < 0)
		goto error;

	status = write16(state, IQM_RC_STRETCH__A, 21);
	if (status < 0)
		goto error;
	status = write16(state, IQM_AF_CLP_LEN__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, IQM_AF_CLP_TH__A, 448);
	if (status < 0)
		goto error;
	status = write16(state, IQM_AF_SNS_LEN__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_POW_MEAS_LEN__A, 0);
	if (status < 0)
		goto error;

	status = write16(state, IQM_FS_ADJ_SEL__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, IQM_RC_ADJ_SEL__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_ADJ_SEL__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, IQM_AF_UPD_SEL__A, 0);
	if (status < 0)
		goto error;

	/* IQM Impulse Noise Processing Unit */
	status = write16(state, IQM_CF_CLP_VAL__A, 500);
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_DATATH__A, 1000);
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_BYPASSDET__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_DET_LCT__A, 0);
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_WND_LEN__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, IQM_CF_PKDTH__A, 1);
	if (status < 0)
		goto error;
	status = write16(state, IQM_AF_INC_BYPASS__A, 1);
	if (status < 0)
		goto error;

	/* turn on IQMAF. Must be done before setAgc**() */
	status = set_iqm_af(state, true);
	if (status < 0)
		goto error;
	status = write16(state, IQM_AF_START_LOCK__A, 0x01);
	if (status < 0)
		goto error;

	/* IQM will not be reset from here, sync ADC and update/init AGC */
	status = adc_synchronization(state);
	if (status < 0)
		goto error;

	/* Set the FSM step period */
	status = write16(state, SCU_RAM_QAM_FSM_STEP_PERIOD__A, 2000);
	if (status < 0)
		goto error;

	/* Halt SCU to enable safe non-atomic accesses */
	status = write16(state, SCU_COMM_EXEC__A, SCU_COMM_EXEC_HOLD);
	if (status < 0)
		goto error;

	/* No more resets of the IQM, current standard correctly set =>
		now AGCs can be configured. */

	status = init_agc(state, true);
	if (status < 0)
		goto error;
	status = set_pre_saw(state, &(state->m_qam_pre_saw_cfg));
	if (status < 0)
		goto error;

	/* Configure AGC's */
	status = set_agc_rf(state, &(state->m_qam_rf_agc_cfg), true);
	if (status < 0)
		goto error;
	status = set_agc_if(state, &(state->m_qam_if_agc_cfg), true);
	if (status < 0)
		goto error;

	/* Activate SCU to enable SCU commands */
	status = write16(state, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int write_gpio(struct drxk_state *state)
{
	int status;
	u16 value = 0;

	dprintk(1, "\n");
	/* stop lock indicator process */
	status = write16(state, SCU_RAM_GPIO__A,
			 SCU_RAM_GPIO_HW_LOCK_IND_DISABLE);
	if (status < 0)
		goto error;

	/*  Write magic word to enable pdr reg write               */
	status = write16(state, SIO_TOP_COMM_KEY__A, SIO_TOP_COMM_KEY_KEY);
	if (status < 0)
		goto error;

	if (state->m_has_sawsw) {
		if (state->uio_mask & 0x0001) { /* UIO-1 */
			/* write to io pad configuration register - output mode */
			status = write16(state, SIO_PDR_SMA_TX_CFG__A,
					 state->m_gpio_cfg);
			if (status < 0)
				goto error;

			/* use corresponding bit in io data output registar */
			status = read16(state, SIO_PDR_UIO_OUT_LO__A, &value);
			if (status < 0)
				goto error;
			if ((state->m_gpio & 0x0001) == 0)
				value &= 0x7FFF;	/* write zero to 15th bit - 1st UIO */
			else
				value |= 0x8000;	/* write one to 15th bit - 1st UIO */
			/* write back to io data output register */
			status = write16(state, SIO_PDR_UIO_OUT_LO__A, value);
			if (status < 0)
				goto error;
		}
		if (state->uio_mask & 0x0002) { /* UIO-2 */
			/* write to io pad configuration register - output mode */
			status = write16(state, SIO_PDR_SMA_RX_CFG__A,
					 state->m_gpio_cfg);
			if (status < 0)
				goto error;

			/* use corresponding bit in io data output registar */
			status = read16(state, SIO_PDR_UIO_OUT_LO__A, &value);
			if (status < 0)
				goto error;
			if ((state->m_gpio & 0x0002) == 0)
				value &= 0xBFFF;	/* write zero to 14th bit - 2st UIO */
			else
				value |= 0x4000;	/* write one to 14th bit - 2st UIO */
			/* write back to io data output register */
			status = write16(state, SIO_PDR_UIO_OUT_LO__A, value);
			if (status < 0)
				goto error;
		}
		if (state->uio_mask & 0x0004) { /* UIO-3 */
			/* write to io pad configuration register - output mode */
			status = write16(state, SIO_PDR_GPIO_CFG__A,
					 state->m_gpio_cfg);
			if (status < 0)
				goto error;

			/* use corresponding bit in io data output registar */
			status = read16(state, SIO_PDR_UIO_OUT_LO__A, &value);
			if (status < 0)
				goto error;
			if ((state->m_gpio & 0x0004) == 0)
				value &= 0xFFFB;            /* write zero to 2nd bit - 3rd UIO */
			else
				value |= 0x0004;            /* write one to 2nd bit - 3rd UIO */
			/* write back to io data output register */
			status = write16(state, SIO_PDR_UIO_OUT_LO__A, value);
			if (status < 0)
				goto error;
		}
	}
	/*  Write magic word to disable pdr reg write               */
	status = write16(state, SIO_TOP_COMM_KEY__A, 0x0000);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int switch_antenna_to_qam(struct drxk_state *state)
{
	int status = 0;
	bool gpio_state;

	dprintk(1, "\n");

	if (!state->antenna_gpio)
		return 0;

	gpio_state = state->m_gpio & state->antenna_gpio;

	if (state->antenna_dvbt ^ gpio_state) {
		/* Antenna is on DVB-T mode. Switch */
		if (state->antenna_dvbt)
			state->m_gpio &= ~state->antenna_gpio;
		else
			state->m_gpio |= state->antenna_gpio;
		status = write_gpio(state);
	}
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}

static int switch_antenna_to_dvbt(struct drxk_state *state)
{
	int status = 0;
	bool gpio_state;

	dprintk(1, "\n");

	if (!state->antenna_gpio)
		return 0;

	gpio_state = state->m_gpio & state->antenna_gpio;

	if (!(state->antenna_dvbt ^ gpio_state)) {
		/* Antenna is on DVB-C mode. Switch */
		if (state->antenna_dvbt)
			state->m_gpio |= state->antenna_gpio;
		else
			state->m_gpio &= ~state->antenna_gpio;
		status = write_gpio(state);
	}
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);
	return status;
}


static int power_down_device(struct drxk_state *state)
{
	/* Power down to requested mode */
	/* Backup some register settings */
	/* Set pins with possible pull-ups connected to them in input mode */
	/* Analog power down */
	/* ADC power down */
	/* Power down device */
	int status;

	dprintk(1, "\n");
	if (state->m_b_p_down_open_bridge) {
		/* Open I2C bridge before power down of DRXK */
		status = ConfigureI2CBridge(state, true);
		if (status < 0)
			goto error;
	}
	/* driver 0.9.0 */
	status = dvbt_enable_ofdm_token_ring(state, false);
	if (status < 0)
		goto error;

	status = write16(state, SIO_CC_PWD_MODE__A,
			 SIO_CC_PWD_MODE_LEVEL_CLOCK);
	if (status < 0)
		goto error;
	status = write16(state, SIO_CC_UPDATE__A, SIO_CC_UPDATE_KEY);
	if (status < 0)
		goto error;
	state->m_hi_cfg_ctrl |= SIO_HI_RA_RAM_PAR_5_CFG_SLEEP_ZZZ;
	status = hi_cfg_command(state);
error:
	if (status < 0)
		pr_err("Error %d on %s\n", status, __func__);

	return status;
}

static int init_drxk(struct drxk_state *state)
{
	int status = 0, n = 0;
	enum drx_power_mode power_mode = DRXK_POWER_DOWN_OFDM;
	u16 driver_version;

	dprintk(1, "\n");
	if (state->m_drxk_state == DRXK_UNINITIALIZED) {
		drxk_i2c_lock(state);
		status = power_up_device(state);
		if (status < 0)
			goto error;
		status = drxx_open(state);
		if (status < 0)
			goto error;
		/* Soft reset of OFDM-, sys- and osc-clockdomain */
		status = write16(state, SIO_CC_SOFT_RST__A,
				 SIO_CC_SOFT_RST_OFDM__M
				 | SIO_CC_SOFT_RST_SYS__M
				 | SIO_CC_SOFT_RST_OSC__M);
		if (status < 0)
			goto error;
		status = write16(state, SIO_CC_UPDATE__A, SIO_CC_UPDATE_KEY);
		if (status < 0)
			goto error;
		/*
		 * TODO is this needed? If yes, how much delay in
		 * worst case scenario
		 */
		usleep_range(1000, 2000);
		state->m_drxk_a3_patch_code = true;
		status = get_device_capabilities(state);
		if (status < 0)
			goto error;

		/* Bridge delay, uses oscilator clock */
		/* Delay = (delay (nano seconds) * oscclk (kHz))/ 1000 */
		/* SDA brdige delay */
		state->m_hi_cfg_bridge_delay =
			(u16) ((state->m_osc_clock_freq / 1000) *
				HI_I2C_BRIDGE_DELAY) / 1000;
		/* Clipping */
		if (state->m_hi_cfg_bridge_delay >
			SIO_HI_RA_RAM_PAR_3_CFG_DBL_SDA__M) {
			state->m_hi_cfg_bridge_delay =
				SIO_HI_RA_RAM_PAR_3_CFG_DBL_SDA__M;
		}
		/* SCL bridge delay, same as SDA for now */
		state->m_hi_cfg_bridge_delay +=
			state->m_hi_cfg_bridge_delay <<
			SIO_HI_RA_RAM_PAR_3_CFG_DBL_SCL__B;

		status = init_hi(state);
		if (status < 0)
			goto error;
		/* disable various processes */
#if NOA1ROM
		if (!(state->m_DRXK_A1_ROM_CODE)
			&& !(state->m_DRXK_A2_ROM_CODE))
#endif
		{
			status = write16(state, SCU_RAM_GPIO__A,
					 SCU_RAM_GPIO_HW_LOCK_IND_DISABLE);
			if (status < 0)
				goto error;
		}

		/* disable MPEG port */
		status = mpegts_disable(state);
		if (status < 0)
			goto error;

		/* Stop AUD and SCU */
		status = write16(state, AUD_COMM_EXEC__A, AUD_COMM_EXEC_STOP);
		if (status < 0)
			goto error;
		status = write16(state, SCU_COMM_EXEC__A, SCU_COMM_EXEC_STOP);
		if (status < 0)
			goto error;

		/* enable token-ring bus through OFDM block for possible ucode upload */
		status = write16(state, SIO_OFDM_SH_OFDM_RING_ENABLE__A,
				 SIO_OFDM_SH_OFDM_RING_ENABLE_ON);
		if (status < 0)
			goto error;

		/* include boot loader section */
		status = write16(state, SIO_BL_COMM_EXEC__A,
				 SIO_BL_COMM_EXEC_ACTIVE);
		if (status < 0)
			goto error;
		status = bl_chain_cmd(state, 0, 6, 100);
		if (status < 0)
			goto error;

		if (state->fw) {
			status = download_microcode(state, state->fw->data,
						   state->fw->size);
			if (status < 0)
				goto error;
		}

		/* disable token-ring bus through OFDM block for possible ucode upload */
		status = write16(state, SIO_OFDM_SH_OFDM_RING_ENABLE__A,
				 SIO_OFDM_SH_OFDM_RING_ENABLE_OFF);
		if (status < 0)
			goto error;

		/* Run SCU for a little while to initialize microcode version numbers */
		status = write16(state, SCU_COMM_EXEC__A, SCU_COMM_EXEC_ACTIVE);
		if (status < 0)
			goto error;
		status = drxx_open(state);
		if (status < 0)
			goto error;
		/* added for test */
		msleep(30);

		power_mode = DRXK_POWER_DOWN_OFDM;
		status = ctrl_power_mode(state, &power_mode);
		if (status < 0)
			goto error;

		/* Stamp driver version number in SCU data RAM in BCD code
			Done to enable field application engineers to retrieve drxdriver version
			via I2C from SCU RAM.
			Not using SCU command interface for SCU register access since no
			microcode may be present.
			*/
		driver_version =
			(((DRXK_VERSION_MAJOR / 100) % 10) << 12) +
			(((DRXK_VERSION_MAJOR / 10) % 10) << 8) +
			((DRXK_VERSION_MAJOR % 10) << 4) +
			(DRXK_VERSION_MINOR % 10);
		status = write16(state, SCU_RAM_DRIVER_VER_HI__A,
				 driver_version);
		if (status < 0)
			goto error;
		driver_version =
			(((DRXK_VERSION_PATCH / 1000) % 10) << 12) +
			(((DRXK_VERSION_PATCH / 100) % 10) << 8) +
			(((DRXK_VERSION_PATCH / 10) % 10) << 4) +
			(DRXK_VERSION_PATCH % 10);
		status = write16(state, SCU_RAM_DRIVER_VER_LO__A,
				 driver_version);
		if (status < 0)
			goto error;

		pr_info("DRXK driver version %d.%d.%d\n",
			DRXK_VERSION_MAJOR, DRXK_VERSION_MINOR,
			DRXK_VERSION_PATCH);

		/*
		 * Dirty fix of default values for ROM/PATCH microcode
		 * Dirty because this fix makes it impossible to setup
		 * suitable values before calling DRX_Open. This solution
		 * requires changes to RF AGC speed to be done via the CTRL
		 * function after calling DRX_Open
		 */

		/* m_dvbt_rf_agc_cfg.speed = 3; */

		/* Reset driver debug flags to 0 */
		status = write16(state, SCU_RAM_DRIVER_DEBUG__A, 0);
		if (status < 0)
			goto error;
		/* driver 0.9.0 */
		/* Setup FEC OC:
			NOTE: No more full FEC resets allowed afterwards!! */
		status = write16(state, FEC_COMM_EXEC__A, FEC_COMM_EXEC_STOP);
		if (status < 0)
			goto error;
		/* MPEGTS functions are still the same */
		status = mpegts_dto_init(state);
		if (status < 0)
			goto error;
		status = mpegts_stop(state);
		if (status < 0)
			goto error;
		status = mpegts_configure_polarity(state);
		if (status < 0)
			goto error;
		status = mpegts_configure_pins(state, state->m_enable_mpeg_output);
		if (status < 0)
			goto error;
		/* added: configure GPIO */
		status = write_gpio(state);
		if (status < 0)
			goto error;

		state->m_drxk_state = DRXK_STOPPED;

		if (state->m_b_power_down) {
			status = power_down_device(state);
			if (status < 0)
				goto error;
			state->m_drxk_state = DRXK_POWERED_DOWN;
		} else
			state->m_drxk_state = DRXK_STOPPED;

		/* Initialize the supported delivery systems */
		n = 0;
		if (state->m_has_dvbc) {
			state->frontend.ops.delsys[n++] = SYS_DVBC_ANNEX_A;
			state->frontend.ops.delsys[n++] = SYS_DVBC_ANNEX_C;
			strlcat(state->frontend.ops.info.name, " DVB-C",
				sizeof(state->frontend.ops.info.name));
		}
		if (state->m_has_dvbt) {
			state->frontend.ops.delsys[n++] = SYS_DVBT;
			strlcat(state->frontend.ops.info.name, " DVB-T",
				sizeof(state->frontend.ops.info.name));
		}
		drxk_i2c_unlock(state);
	}
error:
	if (status < 0) {
		state->m_drxk_state = DRXK_NO_DEV;
		drxk_i2c_unlock(state);
		pr_err("Error %d on %s\n", status, __func__);
	}

	return status;
}

static void load_firmware_cb(const struct firmware *fw,
			     void *context)
{
	struct drxk_state *state = context;

	dprintk(1, ": %s\n", fw ? "firmware loaded" : "firmware not loaded");
	if (!fw) {
		pr_err("Could not load firmware file %s.\n",
			state->microcode_name);
		pr_info("Copy %s to your hotplug directory!\n",
			state->microcode_name);
		state->microcode_name = NULL;

		/*
		 * As firmware is now load asynchronous, it is not possible
		 * anymore to fail at frontend attach. We might silently
		 * return here, and hope that the driver won't crash.
		 * We might also change all DVB callbacks to return -ENODEV
		 * if the device is not initialized.
		 * As the DRX-K devices have their own internal firmware,
		 * let's just hope that it will match a firmware revision
		 * compatible with this driver and proceed.
		 */
	}
	state->fw = fw;

	init_drxk(state);
}

static void drxk_release(struct dvb_frontend *fe)
{
	struct drxk_state *state = fe->demodulator_priv;

	dprintk(1, "\n");
	release_firmware(state->fw);

	kfree(state);
}

static int drxk_sleep(struct dvb_frontend *fe)
{
	struct drxk_state *state = fe->demodulator_priv;

	dprintk(1, "\n");

	if (state->m_drxk_state == DRXK_NO_DEV)
		return -ENODEV;
	if (state->m_drxk_state == DRXK_UNINITIALIZED)
		return 0;

	shut_down(state);
	return 0;
}

static int drxk_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct drxk_state *state = fe->demodulator_priv;

	dprintk(1, ": %s\n", enable ? "enable" : "disable");

	if (state->m_drxk_state == DRXK_NO_DEV)
		return -ENODEV;

	return ConfigureI2CBridge(state, enable ? true : false);
}

static int drxk_set_parameters(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	u32 delsys  = p->delivery_system, old_delsys;
	struct drxk_state *state = fe->demodulator_priv;
	u32 IF;

	dprintk(1, "\n");

	if (state->m_drxk_state == DRXK_NO_DEV)
		return -ENODEV;

	if (state->m_drxk_state == DRXK_UNINITIALIZED)
		return -EAGAIN;

	if (!fe->ops.tuner_ops.get_if_frequency) {
		pr_err("Error: get_if_frequency() not defined at tuner. Can't work without it!\n");
		return -EINVAL;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);
	if (fe->ops.tuner_ops.set_params)
		fe->ops.tuner_ops.set_params(fe);
	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	old_delsys = state->props.delivery_system;
	state->props = *p;

	if (old_delsys != delsys) {
		shut_down(state);
		switch (delsys) {
		case SYS_DVBC_ANNEX_A:
		case SYS_DVBC_ANNEX_C:
			if (!state->m_has_dvbc)
				return -EINVAL;
			state->m_itut_annex_c = (delsys == SYS_DVBC_ANNEX_C) ?
						true : false;
			if (state->m_itut_annex_c)
				setoperation_mode(state, OM_QAM_ITU_C);
			else
				setoperation_mode(state, OM_QAM_ITU_A);
			break;
		case SYS_DVBT:
			if (!state->m_has_dvbt)
				return -EINVAL;
			setoperation_mode(state, OM_DVBT);
			break;
		default:
			return -EINVAL;
		}
	}

	fe->ops.tuner_ops.get_if_frequency(fe, &IF);
	start(state, 0, IF);

	/* After set_frontend, stats aren't available */
	p->strength.stat[0].scale = FE_SCALE_RELATIVE;
	p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	/* printk(KERN_DEBUG "drxk: %s IF=%d done\n", __func__, IF); */

	return 0;
}

static int get_strength(struct drxk_state *state, u64 *strength)
{
	int status;
	struct s_cfg_agc   rf_agc, if_agc;
	u32          total_gain  = 0;
	u32          atten      = 0;
	u32          agc_range   = 0;
	u16            scu_lvl  = 0;
	u16            scu_coc  = 0;
	/* FIXME: those are part of the tuner presets */
	u16 tuner_rf_gain         = 50; /* Default value on az6007 driver */
	u16 tuner_if_gain         = 40; /* Default value on az6007 driver */

	*strength = 0;

	if (is_dvbt(state)) {
		rf_agc = state->m_dvbt_rf_agc_cfg;
		if_agc = state->m_dvbt_if_agc_cfg;
	} else if (is_qam(state)) {
		rf_agc = state->m_qam_rf_agc_cfg;
		if_agc = state->m_qam_if_agc_cfg;
	} else {
		rf_agc = state->m_atv_rf_agc_cfg;
		if_agc = state->m_atv_if_agc_cfg;
	}

	if (rf_agc.ctrl_mode == DRXK_AGC_CTRL_AUTO) {
		/* SCU output_level */
		status = read16(state, SCU_RAM_AGC_RF_IACCU_HI__A, &scu_lvl);
		if (status < 0)
			return status;

		/* SCU c.o.c. */
		status = read16(state, SCU_RAM_AGC_RF_IACCU_HI_CO__A, &scu_coc);
		if (status < 0)
			return status;

		if (((u32) scu_lvl + (u32) scu_coc) < 0xffff)
			rf_agc.output_level = scu_lvl + scu_coc;
		else
			rf_agc.output_level = 0xffff;

		/* Take RF gain into account */
		total_gain += tuner_rf_gain;

		/* clip output value */
		if (rf_agc.output_level < rf_agc.min_output_level)
			rf_agc.output_level = rf_agc.min_output_level;
		if (rf_agc.output_level > rf_agc.max_output_level)
			rf_agc.output_level = rf_agc.max_output_level;

		agc_range = (u32) (rf_agc.max_output_level - rf_agc.min_output_level);
		if (agc_range > 0) {
			atten += 100UL *
				((u32)(tuner_rf_gain)) *
				((u32)(rf_agc.output_level - rf_agc.min_output_level))
				/ agc_range;
		}
	}

	if (if_agc.ctrl_mode == DRXK_AGC_CTRL_AUTO) {
		status = read16(state, SCU_RAM_AGC_IF_IACCU_HI__A,
				&if_agc.output_level);
		if (status < 0)
			return status;

		status = read16(state, SCU_RAM_AGC_INGAIN_TGT_MIN__A,
				&if_agc.top);
		if (status < 0)
			return status;

		/* Take IF gain into account */
		total_gain += (u32) tuner_if_gain;

		/* clip output value */
		if (if_agc.output_level < if_agc.min_output_level)
			if_agc.output_level = if_agc.min_output_level;
		if (if_agc.output_level > if_agc.max_output_level)
			if_agc.output_level = if_agc.max_output_level;

		agc_range  = (u32)(if_agc.max_output_level - if_agc.min_output_level);
		if (agc_range > 0) {
			atten += 100UL *
				((u32)(tuner_if_gain)) *
				((u32)(if_agc.output_level - if_agc.min_output_level))
				/ agc_range;
		}
	}

	/*
	 * Convert to 0..65535 scale.
	 * If it can't be measured (AGC is disabled), just show 100%.
	 */
	if (total_gain > 0)
		*strength = (65535UL * atten / total_gain / 100);
	else
		*strength = 65535;

	return 0;
}

static int drxk_get_stats(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct drxk_state *state = fe->demodulator_priv;
	int status;
	u32 stat;
	u16 reg16;
	u32 post_bit_count;
	u32 post_bit_err_count;
	u32 post_bit_error_scale;
	u32 pre_bit_err_count;
	u32 pre_bit_count;
	u32 pkt_count;
	u32 pkt_error_count;
	s32 cnr;

	if (state->m_drxk_state == DRXK_NO_DEV)
		return -ENODEV;
	if (state->m_drxk_state == DRXK_UNINITIALIZED)
		return -EAGAIN;

	/* get status */
	state->fe_status = 0;
	get_lock_status(state, &stat);
	if (stat == MPEG_LOCK)
		state->fe_status |= 0x1f;
	if (stat == FEC_LOCK)
		state->fe_status |= 0x0f;
	if (stat == DEMOD_LOCK)
		state->fe_status |= 0x07;

	/*
	 * Estimate signal strength from AGC
	 */
	get_strength(state, &c->strength.stat[0].uvalue);
	c->strength.stat[0].scale = FE_SCALE_RELATIVE;


	if (stat >= DEMOD_LOCK) {
		get_signal_to_noise(state, &cnr);
		c->cnr.stat[0].svalue = cnr * 100;
		c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
	} else {
		c->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	}

	if (stat < FEC_LOCK) {
		c->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		c->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
		return 0;
	}

	/* Get post BER */

	/* BER measurement is valid if at least FEC lock is achieved */

	/*
	 * OFDM_EC_VD_REQ_SMB_CNT__A and/or OFDM_EC_VD_REQ_BIT_CNT can be
	 * written to set nr of symbols or bits over which to measure
	 * EC_VD_REG_ERR_BIT_CNT__A . See CtrlSetCfg().
	 */

	/* Read registers for post/preViterbi BER calculation */
	status = read16(state, OFDM_EC_VD_ERR_BIT_CNT__A, &reg16);
	if (status < 0)
		goto error;
	pre_bit_err_count = reg16;

	status = read16(state, OFDM_EC_VD_IN_BIT_CNT__A , &reg16);
	if (status < 0)
		goto error;
	pre_bit_count = reg16;

	/* Number of bit-errors */
	status = read16(state, FEC_RS_NR_BIT_ERRORS__A, &reg16);
	if (status < 0)
		goto error;
	post_bit_err_count = reg16;

	status = read16(state, FEC_RS_MEASUREMENT_PRESCALE__A, &reg16);
	if (status < 0)
		goto error;
	post_bit_error_scale = reg16;

	status = read16(state, FEC_RS_MEASUREMENT_PERIOD__A, &reg16);
	if (status < 0)
		goto error;
	pkt_count = reg16;

	status = read16(state, SCU_RAM_FEC_ACCUM_PKT_FAILURES__A, &reg16);
	if (status < 0)
		goto error;
	pkt_error_count = reg16;
	write16(state, SCU_RAM_FEC_ACCUM_PKT_FAILURES__A, 0);

	post_bit_err_count *= post_bit_error_scale;

	post_bit_count = pkt_count * 204 * 8;

	/* Store the results */
	c->block_error.stat[0].scale = FE_SCALE_COUNTER;
	c->block_error.stat[0].uvalue += pkt_error_count;
	c->block_count.stat[0].scale = FE_SCALE_COUNTER;
	c->block_count.stat[0].uvalue += pkt_count;

	c->pre_bit_error.stat[0].scale = FE_SCALE_COUNTER;
	c->pre_bit_error.stat[0].uvalue += pre_bit_err_count;
	c->pre_bit_count.stat[0].scale = FE_SCALE_COUNTER;
	c->pre_bit_count.stat[0].uvalue += pre_bit_count;

	c->post_bit_error.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_error.stat[0].uvalue += post_bit_err_count;
	c->post_bit_count.stat[0].scale = FE_SCALE_COUNTER;
	c->post_bit_count.stat[0].uvalue += post_bit_count;

error:
	return status;
}


static int drxk_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct drxk_state *state = fe->demodulator_priv;
	int rc;

	dprintk(1, "\n");

	rc = drxk_get_stats(fe);
	if (rc < 0)
		return rc;

	*status = state->fe_status;

	return 0;
}

static int drxk_read_signal_strength(struct dvb_frontend *fe,
				     u16 *strength)
{
	struct drxk_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	dprintk(1, "\n");

	if (state->m_drxk_state == DRXK_NO_DEV)
		return -ENODEV;
	if (state->m_drxk_state == DRXK_UNINITIALIZED)
		return -EAGAIN;

	*strength = c->strength.stat[0].uvalue;
	return 0;
}

static int drxk_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct drxk_state *state = fe->demodulator_priv;
	s32 snr2;

	dprintk(1, "\n");

	if (state->m_drxk_state == DRXK_NO_DEV)
		return -ENODEV;
	if (state->m_drxk_state == DRXK_UNINITIALIZED)
		return -EAGAIN;

	get_signal_to_noise(state, &snr2);

	/* No negative SNR, clip to zero */
	if (snr2 < 0)
		snr2 = 0;
	*snr = snr2 & 0xffff;
	return 0;
}

static int drxk_read_ucblocks(struct dvb_frontend *fe, u32 *ucblocks)
{
	struct drxk_state *state = fe->demodulator_priv;
	u16 err;

	dprintk(1, "\n");

	if (state->m_drxk_state == DRXK_NO_DEV)
		return -ENODEV;
	if (state->m_drxk_state == DRXK_UNINITIALIZED)
		return -EAGAIN;

	dvbtqam_get_acc_pkt_err(state, &err);
	*ucblocks = (u32) err;
	return 0;
}

static int drxk_get_tune_settings(struct dvb_frontend *fe,
				  struct dvb_frontend_tune_settings *sets)
{
	struct drxk_state *state = fe->demodulator_priv;
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;

	dprintk(1, "\n");

	if (state->m_drxk_state == DRXK_NO_DEV)
		return -ENODEV;
	if (state->m_drxk_state == DRXK_UNINITIALIZED)
		return -EAGAIN;

	switch (p->delivery_system) {
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_C:
	case SYS_DVBT:
		sets->min_delay_ms = 3000;
		sets->max_drift = 0;
		sets->step_size = 0;
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct dvb_frontend_ops drxk_ops = {
	/* .delsys will be filled dynamically */
	.info = {
		.name = "DRXK",
		.frequency_min_hz =  47 * MHz,
		.frequency_max_hz = 865 * MHz,
		 /* For DVB-C */
		.symbol_rate_min =   870000,
		.symbol_rate_max = 11700000,
		/* For DVB-T */
		.frequency_stepsize_hz = 166667,

		.caps = FE_CAN_QAM_16 | FE_CAN_QAM_32 | FE_CAN_QAM_64 |
			FE_CAN_QAM_128 | FE_CAN_QAM_256 | FE_CAN_FEC_AUTO |
			FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 | FE_CAN_FEC_3_4 |
			FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 | FE_CAN_MUTE_TS |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_RECOVER |
			FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_HIERARCHY_AUTO
	},

	.release = drxk_release,
	.sleep = drxk_sleep,
	.i2c_gate_ctrl = drxk_gate_ctrl,

	.set_frontend = drxk_set_parameters,
	.get_tune_settings = drxk_get_tune_settings,

	.read_status = drxk_read_status,
	.read_signal_strength = drxk_read_signal_strength,
	.read_snr = drxk_read_snr,
	.read_ucblocks = drxk_read_ucblocks,
};

struct dvb_frontend *drxk_attach(const struct drxk_config *config,
				 struct i2c_adapter *i2c)
{
	struct dtv_frontend_properties *p;
	struct drxk_state *state = NULL;
	u8 adr = config->adr;
	int status;

	dprintk(1, "\n");
	state = kzalloc(sizeof(struct drxk_state), GFP_KERNEL);
	if (!state)
		return NULL;

	state->i2c = i2c;
	state->demod_address = adr;
	state->single_master = config->single_master;
	state->microcode_name = config->microcode_name;
	state->qam_demod_parameter_count = config->qam_demod_parameter_count;
	state->no_i2c_bridge = config->no_i2c_bridge;
	state->antenna_gpio = config->antenna_gpio;
	state->antenna_dvbt = config->antenna_dvbt;
	state->m_chunk_size = config->chunk_size;
	state->enable_merr_cfg = config->enable_merr_cfg;

	if (config->dynamic_clk) {
		state->m_dvbt_static_clk = false;
		state->m_dvbc_static_clk = false;
	} else {
		state->m_dvbt_static_clk = true;
		state->m_dvbc_static_clk = true;
	}


	if (config->mpeg_out_clk_strength)
		state->m_ts_clockk_strength = config->mpeg_out_clk_strength & 0x07;
	else
		state->m_ts_clockk_strength = 0x06;

	if (config->parallel_ts)
		state->m_enable_parallel = true;
	else
		state->m_enable_parallel = false;

	/* NOTE: as more UIO bits will be used, add them to the mask */
	state->uio_mask = config->antenna_gpio;

	/* Default gpio to DVB-C */
	if (!state->antenna_dvbt && state->antenna_gpio)
		state->m_gpio |= state->antenna_gpio;
	else
		state->m_gpio &= ~state->antenna_gpio;

	mutex_init(&state->mutex);

	memcpy(&state->frontend.ops, &drxk_ops, sizeof(drxk_ops));
	state->frontend.demodulator_priv = state;

	init_state(state);

	/* Load firmware and initialize DRX-K */
	if (state->microcode_name) {
		const struct firmware *fw = NULL;

		status = request_firmware(&fw, state->microcode_name,
					  state->i2c->dev.parent);
		if (status < 0)
			fw = NULL;
		load_firmware_cb(fw, state);
	} else if (init_drxk(state) < 0)
		goto error;


	/* Initialize stats */
	p = &state->frontend.dtv_property_cache;
	p->strength.len = 1;
	p->cnr.len = 1;
	p->block_error.len = 1;
	p->block_count.len = 1;
	p->pre_bit_error.len = 1;
	p->pre_bit_count.len = 1;
	p->post_bit_error.len = 1;
	p->post_bit_count.len = 1;

	p->strength.stat[0].scale = FE_SCALE_RELATIVE;
	p->cnr.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->block_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->block_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->pre_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->pre_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_error.stat[0].scale = FE_SCALE_NOT_AVAILABLE;
	p->post_bit_count.stat[0].scale = FE_SCALE_NOT_AVAILABLE;

	pr_info("frontend initialized.\n");
	return &state->frontend;

error:
	pr_err("not found\n");
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(drxk_attach);

MODULE_DESCRIPTION("DRX-K driver");
MODULE_AUTHOR("Ralph Metzler");
MODULE_LICENSE("GPL");
