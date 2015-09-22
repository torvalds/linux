/*
 * drxd_hard.c: DVB-T Demodulator Micronas DRX3975D-A2,DRX397xD-B1
 *
 * Copyright (C) 2003-2007 Micronas
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <asm/div64.h>

#include "dvb_frontend.h"
#include "drxd.h"
#include "drxd_firm.h"

#define DRX_FW_FILENAME_A2 "drxd-a2-1.1.fw"
#define DRX_FW_FILENAME_B1 "drxd-b1-1.1.fw"

#define CHUNK_SIZE 48

#define DRX_I2C_RMW           0x10
#define DRX_I2C_BROADCAST     0x20
#define DRX_I2C_CLEARCRC      0x80
#define DRX_I2C_SINGLE_MASTER 0xC0
#define DRX_I2C_MODEFLAGS     0xC0
#define DRX_I2C_FLAGS         0xF0

#define DEFAULT_LOCK_TIMEOUT    1100

#define DRX_CHANNEL_AUTO 0
#define DRX_CHANNEL_HIGH 1
#define DRX_CHANNEL_LOW  2

#define DRX_LOCK_MPEG  1
#define DRX_LOCK_FEC   2
#define DRX_LOCK_DEMOD 4

/****************************************************************************/

enum CSCDState {
	CSCD_INIT = 0,
	CSCD_SET,
	CSCD_SAVED
};

enum CDrxdState {
	DRXD_UNINITIALIZED = 0,
	DRXD_STOPPED,
	DRXD_STARTED
};

enum AGC_CTRL_MODE {
	AGC_CTRL_AUTO = 0,
	AGC_CTRL_USER,
	AGC_CTRL_OFF
};

enum OperationMode {
	OM_Default,
	OM_DVBT_Diversity_Front,
	OM_DVBT_Diversity_End
};

struct SCfgAgc {
	enum AGC_CTRL_MODE ctrlMode;
	u16 outputLevel;	/* range [0, ... , 1023], 1/n of fullscale range */
	u16 settleLevel;	/* range [0, ... , 1023], 1/n of fullscale range */
	u16 minOutputLevel;	/* range [0, ... , 1023], 1/n of fullscale range */
	u16 maxOutputLevel;	/* range [0, ... , 1023], 1/n of fullscale range */
	u16 speed;		/* range [0, ... , 1023], 1/n of fullscale range */

	u16 R1;
	u16 R2;
	u16 R3;
};

struct SNoiseCal {
	int cpOpt;
	short cpNexpOfs;
	short tdCal2k;
	short tdCal8k;
};

enum app_env {
	APPENV_STATIC = 0,
	APPENV_PORTABLE = 1,
	APPENV_MOBILE = 2
};

enum EIFFilter {
	IFFILTER_SAW = 0,
	IFFILTER_DISCRETE = 1
};

struct drxd_state {
	struct dvb_frontend frontend;
	struct dvb_frontend_ops ops;
	struct dtv_frontend_properties props;

	const struct firmware *fw;
	struct device *dev;

	struct i2c_adapter *i2c;
	void *priv;
	struct drxd_config config;

	int i2c_access;
	int init_done;
	struct mutex mutex;

	u8 chip_adr;
	u16 hi_cfg_timing_div;
	u16 hi_cfg_bridge_delay;
	u16 hi_cfg_wakeup_key;
	u16 hi_cfg_ctrl;

	u16 intermediate_freq;
	u16 osc_clock_freq;

	enum CSCDState cscd_state;
	enum CDrxdState drxd_state;

	u16 sys_clock_freq;
	s16 osc_clock_deviation;
	u16 expected_sys_clock_freq;

	u16 insert_rs_byte;
	u16 enable_parallel;

	int operation_mode;

	struct SCfgAgc if_agc_cfg;
	struct SCfgAgc rf_agc_cfg;

	struct SNoiseCal noise_cal;

	u32 fe_fs_add_incr;
	u32 org_fe_fs_add_incr;
	u16 current_fe_if_incr;

	u16 m_FeAgRegAgPwd;
	u16 m_FeAgRegAgAgcSio;

	u16 m_EcOcRegOcModeLop;
	u16 m_EcOcRegSncSncLvl;
	u8 *m_InitAtomicRead;
	u8 *m_HiI2cPatch;

	u8 *m_ResetCEFR;
	u8 *m_InitFE_1;
	u8 *m_InitFE_2;
	u8 *m_InitCP;
	u8 *m_InitCE;
	u8 *m_InitEQ;
	u8 *m_InitSC;
	u8 *m_InitEC;
	u8 *m_ResetECRAM;
	u8 *m_InitDiversityFront;
	u8 *m_InitDiversityEnd;
	u8 *m_DisableDiversity;
	u8 *m_StartDiversityFront;
	u8 *m_StartDiversityEnd;

	u8 *m_DiversityDelay8MHZ;
	u8 *m_DiversityDelay6MHZ;

	u8 *microcode;
	u32 microcode_length;

	int type_A;
	int PGA;
	int diversity;
	int tuner_mirrors;

	enum app_env app_env_default;
	enum app_env app_env_diversity;

};

/****************************************************************************/
/* I2C **********************************************************************/
/****************************************************************************/

static int i2c_write(struct i2c_adapter *adap, u8 adr, u8 * data, int len)
{
	struct i2c_msg msg = {.addr = adr, .flags = 0, .buf = data, .len = len };

	if (i2c_transfer(adap, &msg, 1) != 1)
		return -1;
	return 0;
}

static int i2c_read(struct i2c_adapter *adap,
		    u8 adr, u8 *msg, int len, u8 *answ, int alen)
{
	struct i2c_msg msgs[2] = {
		{
			.addr = adr, .flags = 0,
			.buf = msg, .len = len
		}, {
			.addr = adr, .flags = I2C_M_RD,
			.buf = answ, .len = alen
		}
	};
	if (i2c_transfer(adap, msgs, 2) != 2)
		return -1;
	return 0;
}

static inline u32 MulDiv32(u32 a, u32 b, u32 c)
{
	u64 tmp64;

	tmp64 = (u64)a * (u64)b;
	do_div(tmp64, c);

	return (u32) tmp64;
}

static int Read16(struct drxd_state *state, u32 reg, u16 *data, u8 flags)
{
	u8 adr = state->config.demod_address;
	u8 mm1[4] = { reg & 0xff, (reg >> 16) & 0xff,
		flags | ((reg >> 24) & 0xff), (reg >> 8) & 0xff
	};
	u8 mm2[2];
	if (i2c_read(state->i2c, adr, mm1, 4, mm2, 2) < 0)
		return -1;
	if (data)
		*data = mm2[0] | (mm2[1] << 8);
	return mm2[0] | (mm2[1] << 8);
}

static int Read32(struct drxd_state *state, u32 reg, u32 *data, u8 flags)
{
	u8 adr = state->config.demod_address;
	u8 mm1[4] = { reg & 0xff, (reg >> 16) & 0xff,
		flags | ((reg >> 24) & 0xff), (reg >> 8) & 0xff
	};
	u8 mm2[4];

	if (i2c_read(state->i2c, adr, mm1, 4, mm2, 4) < 0)
		return -1;
	if (data)
		*data =
		    mm2[0] | (mm2[1] << 8) | (mm2[2] << 16) | (mm2[3] << 24);
	return 0;
}

static int Write16(struct drxd_state *state, u32 reg, u16 data, u8 flags)
{
	u8 adr = state->config.demod_address;
	u8 mm[6] = { reg & 0xff, (reg >> 16) & 0xff,
		flags | ((reg >> 24) & 0xff), (reg >> 8) & 0xff,
		data & 0xff, (data >> 8) & 0xff
	};

	if (i2c_write(state->i2c, adr, mm, 6) < 0)
		return -1;
	return 0;
}

static int Write32(struct drxd_state *state, u32 reg, u32 data, u8 flags)
{
	u8 adr = state->config.demod_address;
	u8 mm[8] = { reg & 0xff, (reg >> 16) & 0xff,
		flags | ((reg >> 24) & 0xff), (reg >> 8) & 0xff,
		data & 0xff, (data >> 8) & 0xff,
		(data >> 16) & 0xff, (data >> 24) & 0xff
	};

	if (i2c_write(state->i2c, adr, mm, 8) < 0)
		return -1;
	return 0;
}

static int write_chunk(struct drxd_state *state,
		       u32 reg, u8 *data, u32 len, u8 flags)
{
	u8 adr = state->config.demod_address;
	u8 mm[CHUNK_SIZE + 4] = { reg & 0xff, (reg >> 16) & 0xff,
		flags | ((reg >> 24) & 0xff), (reg >> 8) & 0xff
	};
	int i;

	for (i = 0; i < len; i++)
		mm[4 + i] = data[i];
	if (i2c_write(state->i2c, adr, mm, 4 + len) < 0) {
		printk(KERN_ERR "error in write_chunk\n");
		return -1;
	}
	return 0;
}

static int WriteBlock(struct drxd_state *state,
		      u32 Address, u16 BlockSize, u8 *pBlock, u8 Flags)
{
	while (BlockSize > 0) {
		u16 Chunk = BlockSize > CHUNK_SIZE ? CHUNK_SIZE : BlockSize;

		if (write_chunk(state, Address, pBlock, Chunk, Flags) < 0)
			return -1;
		pBlock += Chunk;
		Address += (Chunk >> 1);
		BlockSize -= Chunk;
	}
	return 0;
}

static int WriteTable(struct drxd_state *state, u8 * pTable)
{
	int status = 0;

	if (pTable == NULL)
		return 0;

	while (!status) {
		u16 Length;
		u32 Address = pTable[0] | (pTable[1] << 8) |
		    (pTable[2] << 16) | (pTable[3] << 24);

		if (Address == 0xFFFFFFFF)
			break;
		pTable += sizeof(u32);

		Length = pTable[0] | (pTable[1] << 8);
		pTable += sizeof(u16);
		if (!Length)
			break;
		status = WriteBlock(state, Address, Length * 2, pTable, 0);
		pTable += (Length * 2);
	}
	return status;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static int ResetCEFR(struct drxd_state *state)
{
	return WriteTable(state, state->m_ResetCEFR);
}

static int InitCP(struct drxd_state *state)
{
	return WriteTable(state, state->m_InitCP);
}

static int InitCE(struct drxd_state *state)
{
	int status;
	enum app_env AppEnv = state->app_env_default;

	do {
		status = WriteTable(state, state->m_InitCE);
		if (status < 0)
			break;

		if (state->operation_mode == OM_DVBT_Diversity_Front ||
		    state->operation_mode == OM_DVBT_Diversity_End) {
			AppEnv = state->app_env_diversity;
		}
		if (AppEnv == APPENV_STATIC) {
			status = Write16(state, CE_REG_TAPSET__A, 0x0000, 0);
			if (status < 0)
				break;
		} else if (AppEnv == APPENV_PORTABLE) {
			status = Write16(state, CE_REG_TAPSET__A, 0x0001, 0);
			if (status < 0)
				break;
		} else if (AppEnv == APPENV_MOBILE && state->type_A) {
			status = Write16(state, CE_REG_TAPSET__A, 0x0002, 0);
			if (status < 0)
				break;
		} else if (AppEnv == APPENV_MOBILE && !state->type_A) {
			status = Write16(state, CE_REG_TAPSET__A, 0x0006, 0);
			if (status < 0)
				break;
		}

		/* start ce */
		status = Write16(state, B_CE_REG_COMM_EXEC__A, 0x0001, 0);
		if (status < 0)
			break;
	} while (0);
	return status;
}

static int StopOC(struct drxd_state *state)
{
	int status = 0;
	u16 ocSyncLvl = 0;
	u16 ocModeLop = state->m_EcOcRegOcModeLop;
	u16 dtoIncLop = 0;
	u16 dtoIncHip = 0;

	do {
		/* Store output configuration */
		status = Read16(state, EC_OC_REG_SNC_ISC_LVL__A, &ocSyncLvl, 0);
		if (status < 0)
			break;
		/* CHK_ERROR(Read16(EC_OC_REG_OC_MODE_LOP__A, &ocModeLop)); */
		state->m_EcOcRegSncSncLvl = ocSyncLvl;
		/* m_EcOcRegOcModeLop = ocModeLop; */

		/* Flush FIFO (byte-boundary) at fixed rate */
		status = Read16(state, EC_OC_REG_RCN_MAP_LOP__A, &dtoIncLop, 0);
		if (status < 0)
			break;
		status = Read16(state, EC_OC_REG_RCN_MAP_HIP__A, &dtoIncHip, 0);
		if (status < 0)
			break;
		status = Write16(state, EC_OC_REG_DTO_INC_LOP__A, dtoIncLop, 0);
		if (status < 0)
			break;
		status = Write16(state, EC_OC_REG_DTO_INC_HIP__A, dtoIncHip, 0);
		if (status < 0)
			break;
		ocModeLop &= ~(EC_OC_REG_OC_MODE_LOP_DTO_CTR_SRC__M);
		ocModeLop |= EC_OC_REG_OC_MODE_LOP_DTO_CTR_SRC_STATIC;
		status = Write16(state, EC_OC_REG_OC_MODE_LOP__A, ocModeLop, 0);
		if (status < 0)
			break;
		status = Write16(state, EC_OC_REG_COMM_EXEC__A, EC_OC_REG_COMM_EXEC_CTL_HOLD, 0);
		if (status < 0)
			break;

		msleep(1);
		/* Output pins to '0' */
		status = Write16(state, EC_OC_REG_OCR_MPG_UOS__A, EC_OC_REG_OCR_MPG_UOS__M, 0);
		if (status < 0)
			break;

		/* Force the OC out of sync */
		ocSyncLvl &= ~(EC_OC_REG_SNC_ISC_LVL_OSC__M);
		status = Write16(state, EC_OC_REG_SNC_ISC_LVL__A, ocSyncLvl, 0);
		if (status < 0)
			break;
		ocModeLop &= ~(EC_OC_REG_OC_MODE_LOP_PAR_ENA__M);
		ocModeLop |= EC_OC_REG_OC_MODE_LOP_PAR_ENA_ENABLE;
		ocModeLop |= 0x2;	/* Magically-out-of-sync */
		status = Write16(state, EC_OC_REG_OC_MODE_LOP__A, ocModeLop, 0);
		if (status < 0)
			break;
		status = Write16(state, EC_OC_REG_COMM_INT_STA__A, 0x0, 0);
		if (status < 0)
			break;
		status = Write16(state, EC_OC_REG_COMM_EXEC__A, EC_OC_REG_COMM_EXEC_CTL_ACTIVE, 0);
		if (status < 0)
			break;
	} while (0);

	return status;
}

static int StartOC(struct drxd_state *state)
{
	int status = 0;

	do {
		/* Stop OC */
		status = Write16(state, EC_OC_REG_COMM_EXEC__A, EC_OC_REG_COMM_EXEC_CTL_HOLD, 0);
		if (status < 0)
			break;

		/* Restore output configuration */
		status = Write16(state, EC_OC_REG_SNC_ISC_LVL__A, state->m_EcOcRegSncSncLvl, 0);
		if (status < 0)
			break;
		status = Write16(state, EC_OC_REG_OC_MODE_LOP__A, state->m_EcOcRegOcModeLop, 0);
		if (status < 0)
			break;

		/* Output pins active again */
		status = Write16(state, EC_OC_REG_OCR_MPG_UOS__A, EC_OC_REG_OCR_MPG_UOS_INIT, 0);
		if (status < 0)
			break;

		/* Start OC */
		status = Write16(state, EC_OC_REG_COMM_EXEC__A, EC_OC_REG_COMM_EXEC_CTL_ACTIVE, 0);
		if (status < 0)
			break;
	} while (0);
	return status;
}

static int InitEQ(struct drxd_state *state)
{
	return WriteTable(state, state->m_InitEQ);
}

static int InitEC(struct drxd_state *state)
{
	return WriteTable(state, state->m_InitEC);
}

static int InitSC(struct drxd_state *state)
{
	return WriteTable(state, state->m_InitSC);
}

static int InitAtomicRead(struct drxd_state *state)
{
	return WriteTable(state, state->m_InitAtomicRead);
}

static int CorrectSysClockDeviation(struct drxd_state *state);

static int DRX_GetLockStatus(struct drxd_state *state, u32 * pLockStatus)
{
	u16 ScRaRamLock = 0;
	const u16 mpeg_lock_mask = (SC_RA_RAM_LOCK_MPEG__M |
				    SC_RA_RAM_LOCK_FEC__M |
				    SC_RA_RAM_LOCK_DEMOD__M);
	const u16 fec_lock_mask = (SC_RA_RAM_LOCK_FEC__M |
				   SC_RA_RAM_LOCK_DEMOD__M);
	const u16 demod_lock_mask = SC_RA_RAM_LOCK_DEMOD__M;

	int status;

	*pLockStatus = 0;

	status = Read16(state, SC_RA_RAM_LOCK__A, &ScRaRamLock, 0x0000);
	if (status < 0) {
		printk(KERN_ERR "Can't read SC_RA_RAM_LOCK__A status = %08x\n", status);
		return status;
	}

	if (state->drxd_state != DRXD_STARTED)
		return 0;

	if ((ScRaRamLock & mpeg_lock_mask) == mpeg_lock_mask) {
		*pLockStatus |= DRX_LOCK_MPEG;
		CorrectSysClockDeviation(state);
	}

	if ((ScRaRamLock & fec_lock_mask) == fec_lock_mask)
		*pLockStatus |= DRX_LOCK_FEC;

	if ((ScRaRamLock & demod_lock_mask) == demod_lock_mask)
		*pLockStatus |= DRX_LOCK_DEMOD;
	return 0;
}

/****************************************************************************/

static int SetCfgIfAgc(struct drxd_state *state, struct SCfgAgc *cfg)
{
	int status;

	if (cfg->outputLevel > DRXD_FE_CTRL_MAX)
		return -1;

	if (cfg->ctrlMode == AGC_CTRL_USER) {
		do {
			u16 FeAgRegPm1AgcWri;
			u16 FeAgRegAgModeLop;

			status = Read16(state, FE_AG_REG_AG_MODE_LOP__A, &FeAgRegAgModeLop, 0);
			if (status < 0)
				break;
			FeAgRegAgModeLop &= (~FE_AG_REG_AG_MODE_LOP_MODE_4__M);
			FeAgRegAgModeLop |= FE_AG_REG_AG_MODE_LOP_MODE_4_STATIC;
			status = Write16(state, FE_AG_REG_AG_MODE_LOP__A, FeAgRegAgModeLop, 0);
			if (status < 0)
				break;

			FeAgRegPm1AgcWri = (u16) (cfg->outputLevel &
						  FE_AG_REG_PM1_AGC_WRI__M);
			status = Write16(state, FE_AG_REG_PM1_AGC_WRI__A, FeAgRegPm1AgcWri, 0);
			if (status < 0)
				break;
		} while (0);
	} else if (cfg->ctrlMode == AGC_CTRL_AUTO) {
		if (((cfg->maxOutputLevel) < (cfg->minOutputLevel)) ||
		    ((cfg->maxOutputLevel) > DRXD_FE_CTRL_MAX) ||
		    ((cfg->speed) > DRXD_FE_CTRL_MAX) ||
		    ((cfg->settleLevel) > DRXD_FE_CTRL_MAX)
		    )
			return -1;
		do {
			u16 FeAgRegAgModeLop;
			u16 FeAgRegEgcSetLvl;
			u16 slope, offset;

			/* == Mode == */

			status = Read16(state, FE_AG_REG_AG_MODE_LOP__A, &FeAgRegAgModeLop, 0);
			if (status < 0)
				break;
			FeAgRegAgModeLop &= (~FE_AG_REG_AG_MODE_LOP_MODE_4__M);
			FeAgRegAgModeLop |=
			    FE_AG_REG_AG_MODE_LOP_MODE_4_DYNAMIC;
			status = Write16(state, FE_AG_REG_AG_MODE_LOP__A, FeAgRegAgModeLop, 0);
			if (status < 0)
				break;

			/* == Settle level == */

			FeAgRegEgcSetLvl = (u16) ((cfg->settleLevel >> 1) &
						  FE_AG_REG_EGC_SET_LVL__M);
			status = Write16(state, FE_AG_REG_EGC_SET_LVL__A, FeAgRegEgcSetLvl, 0);
			if (status < 0)
				break;

			/* == Min/Max == */

			slope = (u16) ((cfg->maxOutputLevel -
					cfg->minOutputLevel) / 2);
			offset = (u16) ((cfg->maxOutputLevel +
					 cfg->minOutputLevel) / 2 - 511);

			status = Write16(state, FE_AG_REG_GC1_AGC_RIC__A, slope, 0);
			if (status < 0)
				break;
			status = Write16(state, FE_AG_REG_GC1_AGC_OFF__A, offset, 0);
			if (status < 0)
				break;

			/* == Speed == */
			{
				const u16 maxRur = 8;
				const u16 slowIncrDecLUT[] = { 3, 4, 4, 5, 6 };
				const u16 fastIncrDecLUT[] = { 14, 15, 15, 16,
					17, 18, 18, 19,
					20, 21, 22, 23,
					24, 26, 27, 28,
					29, 31
				};

				u16 fineSteps = (DRXD_FE_CTRL_MAX + 1) /
				    (maxRur + 1);
				u16 fineSpeed = (u16) (cfg->speed -
						       ((cfg->speed /
							 fineSteps) *
							fineSteps));
				u16 invRurCount = (u16) (cfg->speed /
							 fineSteps);
				u16 rurCount;
				if (invRurCount > maxRur) {
					rurCount = 0;
					fineSpeed += fineSteps;
				} else {
					rurCount = maxRur - invRurCount;
				}

				/*
				   fastInc = default *
				   (2^(fineSpeed/fineSteps))
				   => range[default...2*default>
				   slowInc = default *
				   (2^(fineSpeed/fineSteps))
				 */
				{
					u16 fastIncrDec =
					    fastIncrDecLUT[fineSpeed /
							   ((fineSteps /
							     (14 + 1)) + 1)];
					u16 slowIncrDec =
					    slowIncrDecLUT[fineSpeed /
							   (fineSteps /
							    (3 + 1))];

					status = Write16(state, FE_AG_REG_EGC_RUR_CNT__A, rurCount, 0);
					if (status < 0)
						break;
					status = Write16(state, FE_AG_REG_EGC_FAS_INC__A, fastIncrDec, 0);
					if (status < 0)
						break;
					status = Write16(state, FE_AG_REG_EGC_FAS_DEC__A, fastIncrDec, 0);
					if (status < 0)
						break;
					status = Write16(state, FE_AG_REG_EGC_SLO_INC__A, slowIncrDec, 0);
					if (status < 0)
						break;
					status = Write16(state, FE_AG_REG_EGC_SLO_DEC__A, slowIncrDec, 0);
					if (status < 0)
						break;
				}
			}
		} while (0);

	} else {
		/* No OFF mode for IF control */
		return -1;
	}
	return status;
}

static int SetCfgRfAgc(struct drxd_state *state, struct SCfgAgc *cfg)
{
	int status = 0;

	if (cfg->outputLevel > DRXD_FE_CTRL_MAX)
		return -1;

	if (cfg->ctrlMode == AGC_CTRL_USER) {
		do {
			u16 AgModeLop = 0;
			u16 level = (cfg->outputLevel);

			if (level == DRXD_FE_CTRL_MAX)
				level++;

			status = Write16(state, FE_AG_REG_PM2_AGC_WRI__A, level, 0x0000);
			if (status < 0)
				break;

			/*==== Mode ====*/

			/* Powerdown PD2, WRI source */
			state->m_FeAgRegAgPwd &= ~(FE_AG_REG_AG_PWD_PWD_PD2__M);
			state->m_FeAgRegAgPwd |=
			    FE_AG_REG_AG_PWD_PWD_PD2_DISABLE;
			status = Write16(state, FE_AG_REG_AG_PWD__A, state->m_FeAgRegAgPwd, 0x0000);
			if (status < 0)
				break;

			status = Read16(state, FE_AG_REG_AG_MODE_LOP__A, &AgModeLop, 0x0000);
			if (status < 0)
				break;
			AgModeLop &= (~(FE_AG_REG_AG_MODE_LOP_MODE_5__M |
					FE_AG_REG_AG_MODE_LOP_MODE_E__M));
			AgModeLop |= (FE_AG_REG_AG_MODE_LOP_MODE_5_STATIC |
				      FE_AG_REG_AG_MODE_LOP_MODE_E_STATIC);
			status = Write16(state, FE_AG_REG_AG_MODE_LOP__A, AgModeLop, 0x0000);
			if (status < 0)
				break;

			/* enable AGC2 pin */
			{
				u16 FeAgRegAgAgcSio = 0;
				status = Read16(state, FE_AG_REG_AG_AGC_SIO__A, &FeAgRegAgAgcSio, 0x0000);
				if (status < 0)
					break;
				FeAgRegAgAgcSio &=
				    ~(FE_AG_REG_AG_AGC_SIO_AGC_SIO_2__M);
				FeAgRegAgAgcSio |=
				    FE_AG_REG_AG_AGC_SIO_AGC_SIO_2_OUTPUT;
				status = Write16(state, FE_AG_REG_AG_AGC_SIO__A, FeAgRegAgAgcSio, 0x0000);
				if (status < 0)
					break;
			}

		} while (0);
	} else if (cfg->ctrlMode == AGC_CTRL_AUTO) {
		u16 AgModeLop = 0;

		do {
			u16 level;
			/* Automatic control */
			/* Powerup PD2, AGC2 as output, TGC source */
			(state->m_FeAgRegAgPwd) &=
			    ~(FE_AG_REG_AG_PWD_PWD_PD2__M);
			(state->m_FeAgRegAgPwd) |=
			    FE_AG_REG_AG_PWD_PWD_PD2_DISABLE;
			status = Write16(state, FE_AG_REG_AG_PWD__A, (state->m_FeAgRegAgPwd), 0x0000);
			if (status < 0)
				break;

			status = Read16(state, FE_AG_REG_AG_MODE_LOP__A, &AgModeLop, 0x0000);
			if (status < 0)
				break;
			AgModeLop &= (~(FE_AG_REG_AG_MODE_LOP_MODE_5__M |
					FE_AG_REG_AG_MODE_LOP_MODE_E__M));
			AgModeLop |= (FE_AG_REG_AG_MODE_LOP_MODE_5_STATIC |
				      FE_AG_REG_AG_MODE_LOP_MODE_E_DYNAMIC);
			status = Write16(state, FE_AG_REG_AG_MODE_LOP__A, AgModeLop, 0x0000);
			if (status < 0)
				break;
			/* Settle level */
			level = (((cfg->settleLevel) >> 4) &
				 FE_AG_REG_TGC_SET_LVL__M);
			status = Write16(state, FE_AG_REG_TGC_SET_LVL__A, level, 0x0000);
			if (status < 0)
				break;

			/* Min/max: don't care */

			/* Speed: TODO */

			/* enable AGC2 pin */
			{
				u16 FeAgRegAgAgcSio = 0;
				status = Read16(state, FE_AG_REG_AG_AGC_SIO__A, &FeAgRegAgAgcSio, 0x0000);
				if (status < 0)
					break;
				FeAgRegAgAgcSio &=
				    ~(FE_AG_REG_AG_AGC_SIO_AGC_SIO_2__M);
				FeAgRegAgAgcSio |=
				    FE_AG_REG_AG_AGC_SIO_AGC_SIO_2_OUTPUT;
				status = Write16(state, FE_AG_REG_AG_AGC_SIO__A, FeAgRegAgAgcSio, 0x0000);
				if (status < 0)
					break;
			}

		} while (0);
	} else {
		u16 AgModeLop = 0;

		do {
			/* No RF AGC control */
			/* Powerdown PD2, AGC2 as output, WRI source */
			(state->m_FeAgRegAgPwd) &=
			    ~(FE_AG_REG_AG_PWD_PWD_PD2__M);
			(state->m_FeAgRegAgPwd) |=
			    FE_AG_REG_AG_PWD_PWD_PD2_ENABLE;
			status = Write16(state, FE_AG_REG_AG_PWD__A, (state->m_FeAgRegAgPwd), 0x0000);
			if (status < 0)
				break;

			status = Read16(state, FE_AG_REG_AG_MODE_LOP__A, &AgModeLop, 0x0000);
			if (status < 0)
				break;
			AgModeLop &= (~(FE_AG_REG_AG_MODE_LOP_MODE_5__M |
					FE_AG_REG_AG_MODE_LOP_MODE_E__M));
			AgModeLop |= (FE_AG_REG_AG_MODE_LOP_MODE_5_STATIC |
				      FE_AG_REG_AG_MODE_LOP_MODE_E_STATIC);
			status = Write16(state, FE_AG_REG_AG_MODE_LOP__A, AgModeLop, 0x0000);
			if (status < 0)
				break;

			/* set FeAgRegAgAgcSio AGC2 (RF) as input */
			{
				u16 FeAgRegAgAgcSio = 0;
				status = Read16(state, FE_AG_REG_AG_AGC_SIO__A, &FeAgRegAgAgcSio, 0x0000);
				if (status < 0)
					break;
				FeAgRegAgAgcSio &=
				    ~(FE_AG_REG_AG_AGC_SIO_AGC_SIO_2__M);
				FeAgRegAgAgcSio |=
				    FE_AG_REG_AG_AGC_SIO_AGC_SIO_2_INPUT;
				status = Write16(state, FE_AG_REG_AG_AGC_SIO__A, FeAgRegAgAgcSio, 0x0000);
				if (status < 0)
					break;
			}
		} while (0);
	}
	return status;
}

static int ReadIFAgc(struct drxd_state *state, u32 * pValue)
{
	int status = 0;

	*pValue = 0;
	if (state->if_agc_cfg.ctrlMode != AGC_CTRL_OFF) {
		u16 Value;
		status = Read16(state, FE_AG_REG_GC1_AGC_DAT__A, &Value, 0);
		Value &= FE_AG_REG_GC1_AGC_DAT__M;
		if (status >= 0) {
			/*           3.3V
			   |
			   R1
			   |
			   Vin - R3 - * -- Vout
			   |
			   R2
			   |
			   GND
			 */
			u32 R1 = state->if_agc_cfg.R1;
			u32 R2 = state->if_agc_cfg.R2;
			u32 R3 = state->if_agc_cfg.R3;

			u32 Vmax, Rpar, Vmin, Vout;

			if (R2 == 0 && (R1 == 0 || R3 == 0))
				return 0;

			Vmax = (3300 * R2) / (R1 + R2);
			Rpar = (R2 * R3) / (R3 + R2);
			Vmin = (3300 * Rpar) / (R1 + Rpar);
			Vout = Vmin + ((Vmax - Vmin) * Value) / 1024;

			*pValue = Vout;
		}
	}
	return status;
}

static int load_firmware(struct drxd_state *state, const char *fw_name)
{
	const struct firmware *fw;

	if (request_firmware(&fw, fw_name, state->dev) < 0) {
		printk(KERN_ERR "drxd: firmware load failure [%s]\n", fw_name);
		return -EIO;
	}

	state->microcode = kmemdup(fw->data, fw->size, GFP_KERNEL);
	if (state->microcode == NULL) {
		release_firmware(fw);
		printk(KERN_ERR "drxd: firmware load failure: no memory\n");
		return -ENOMEM;
	}

	state->microcode_length = fw->size;
	release_firmware(fw);
	return 0;
}

static int DownloadMicrocode(struct drxd_state *state,
			     const u8 *pMCImage, u32 Length)
{
	u8 *pSrc;
	u32 Address;
	u16 nBlocks;
	u16 BlockSize;
	u32 offset = 0;
	int i, status = 0;

	pSrc = (u8 *) pMCImage;
	/* We're not using Flags */
	/* Flags = (pSrc[0] << 8) | pSrc[1]; */
	pSrc += sizeof(u16);
	offset += sizeof(u16);
	nBlocks = (pSrc[0] << 8) | pSrc[1];
	pSrc += sizeof(u16);
	offset += sizeof(u16);

	for (i = 0; i < nBlocks; i++) {
		Address = (pSrc[0] << 24) | (pSrc[1] << 16) |
		    (pSrc[2] << 8) | pSrc[3];
		pSrc += sizeof(u32);
		offset += sizeof(u32);

		BlockSize = ((pSrc[0] << 8) | pSrc[1]) * sizeof(u16);
		pSrc += sizeof(u16);
		offset += sizeof(u16);

		/* We're not using Flags */
		/* u16 Flags = (pSrc[0] << 8) | pSrc[1]; */
		pSrc += sizeof(u16);
		offset += sizeof(u16);

		/* We're not using BlockCRC */
		/* u16 BlockCRC = (pSrc[0] << 8) | pSrc[1]; */
		pSrc += sizeof(u16);
		offset += sizeof(u16);

		status = WriteBlock(state, Address, BlockSize,
				    pSrc, DRX_I2C_CLEARCRC);
		if (status < 0)
			break;
		pSrc += BlockSize;
		offset += BlockSize;
	}

	return status;
}

static int HI_Command(struct drxd_state *state, u16 cmd, u16 * pResult)
{
	u32 nrRetries = 0;
	u16 waitCmd;
	int status;

	status = Write16(state, HI_RA_RAM_SRV_CMD__A, cmd, 0);
	if (status < 0)
		return status;

	do {
		nrRetries += 1;
		if (nrRetries > DRXD_MAX_RETRIES) {
			status = -1;
			break;
		}
		status = Read16(state, HI_RA_RAM_SRV_CMD__A, &waitCmd, 0);
	} while (waitCmd != 0);

	if (status >= 0)
		status = Read16(state, HI_RA_RAM_SRV_RES__A, pResult, 0);
	return status;
}

static int HI_CfgCommand(struct drxd_state *state)
{
	int status = 0;

	mutex_lock(&state->mutex);
	Write16(state, HI_RA_RAM_SRV_CFG_KEY__A, HI_RA_RAM_SRV_RST_KEY_ACT, 0);
	Write16(state, HI_RA_RAM_SRV_CFG_DIV__A, state->hi_cfg_timing_div, 0);
	Write16(state, HI_RA_RAM_SRV_CFG_BDL__A, state->hi_cfg_bridge_delay, 0);
	Write16(state, HI_RA_RAM_SRV_CFG_WUP__A, state->hi_cfg_wakeup_key, 0);
	Write16(state, HI_RA_RAM_SRV_CFG_ACT__A, state->hi_cfg_ctrl, 0);

	Write16(state, HI_RA_RAM_SRV_CFG_KEY__A, HI_RA_RAM_SRV_RST_KEY_ACT, 0);

	if ((state->hi_cfg_ctrl & HI_RA_RAM_SRV_CFG_ACT_PWD_EXE) ==
	    HI_RA_RAM_SRV_CFG_ACT_PWD_EXE)
		status = Write16(state, HI_RA_RAM_SRV_CMD__A,
				 HI_RA_RAM_SRV_CMD_CONFIG, 0);
	else
		status = HI_Command(state, HI_RA_RAM_SRV_CMD_CONFIG, NULL);
	mutex_unlock(&state->mutex);
	return status;
}

static int InitHI(struct drxd_state *state)
{
	state->hi_cfg_wakeup_key = (state->chip_adr);
	/* port/bridge/power down ctrl */
	state->hi_cfg_ctrl = HI_RA_RAM_SRV_CFG_ACT_SLV0_ON;
	return HI_CfgCommand(state);
}

static int HI_ResetCommand(struct drxd_state *state)
{
	int status;

	mutex_lock(&state->mutex);
	status = Write16(state, HI_RA_RAM_SRV_RST_KEY__A,
			 HI_RA_RAM_SRV_RST_KEY_ACT, 0);
	if (status == 0)
		status = HI_Command(state, HI_RA_RAM_SRV_CMD_RESET, NULL);
	mutex_unlock(&state->mutex);
	msleep(1);
	return status;
}

static int DRX_ConfigureI2CBridge(struct drxd_state *state, int bEnableBridge)
{
	state->hi_cfg_ctrl &= (~HI_RA_RAM_SRV_CFG_ACT_BRD__M);
	if (bEnableBridge)
		state->hi_cfg_ctrl |= HI_RA_RAM_SRV_CFG_ACT_BRD_ON;
	else
		state->hi_cfg_ctrl |= HI_RA_RAM_SRV_CFG_ACT_BRD_OFF;

	return HI_CfgCommand(state);
}

#define HI_TR_WRITE      0x9
#define HI_TR_READ       0xA
#define HI_TR_READ_WRITE 0xB
#define HI_TR_BROADCAST  0x4

#if 0
static int AtomicReadBlock(struct drxd_state *state,
			   u32 Addr, u16 DataSize, u8 *pData, u8 Flags)
{
	int status;
	int i = 0;

	/* Parameter check */
	if ((!pData) || ((DataSize & 1) != 0))
		return -1;

	mutex_lock(&state->mutex);

	do {
		/* Instruct HI to read n bytes */
		/* TODO use proper names forthese egisters */
		status = Write16(state, HI_RA_RAM_SRV_CFG_KEY__A, (HI_TR_FUNC_ADDR & 0xFFFF), 0);
		if (status < 0)
			break;
		status = Write16(state, HI_RA_RAM_SRV_CFG_DIV__A, (u16) (Addr >> 16), 0);
		if (status < 0)
			break;
		status = Write16(state, HI_RA_RAM_SRV_CFG_BDL__A, (u16) (Addr & 0xFFFF), 0);
		if (status < 0)
			break;
		status = Write16(state, HI_RA_RAM_SRV_CFG_WUP__A, (u16) ((DataSize / 2) - 1), 0);
		if (status < 0)
			break;
		status = Write16(state, HI_RA_RAM_SRV_CFG_ACT__A, HI_TR_READ, 0);
		if (status < 0)
			break;

		status = HI_Command(state, HI_RA_RAM_SRV_CMD_EXECUTE, 0);
		if (status < 0)
			break;

	} while (0);

	if (status >= 0) {
		for (i = 0; i < (DataSize / 2); i += 1) {
			u16 word;

			status = Read16(state, (HI_RA_RAM_USR_BEGIN__A + i),
					&word, 0);
			if (status < 0)
				break;
			pData[2 * i] = (u8) (word & 0xFF);
			pData[(2 * i) + 1] = (u8) (word >> 8);
		}
	}
	mutex_unlock(&state->mutex);
	return status;
}

static int AtomicReadReg32(struct drxd_state *state,
			   u32 Addr, u32 *pData, u8 Flags)
{
	u8 buf[sizeof(u32)];
	int status;

	if (!pData)
		return -1;
	status = AtomicReadBlock(state, Addr, sizeof(u32), buf, Flags);
	*pData = (((u32) buf[0]) << 0) +
	    (((u32) buf[1]) << 8) +
	    (((u32) buf[2]) << 16) + (((u32) buf[3]) << 24);
	return status;
}
#endif

static int StopAllProcessors(struct drxd_state *state)
{
	return Write16(state, HI_COMM_EXEC__A,
		       SC_COMM_EXEC_CTL_STOP, DRX_I2C_BROADCAST);
}

static int EnableAndResetMB(struct drxd_state *state)
{
	if (state->type_A) {
		/* disable? monitor bus observe @ EC_OC */
		Write16(state, EC_OC_REG_OC_MON_SIO__A, 0x0000, 0x0000);
	}

	/* do inverse broadcast, followed by explicit write to HI */
	Write16(state, HI_COMM_MB__A, 0x0000, DRX_I2C_BROADCAST);
	Write16(state, HI_COMM_MB__A, 0x0000, 0x0000);
	return 0;
}

static int InitCC(struct drxd_state *state)
{
	if (state->osc_clock_freq == 0 ||
	    state->osc_clock_freq > 20000 ||
	    (state->osc_clock_freq % 4000) != 0) {
		printk(KERN_ERR "invalid osc frequency %d\n", state->osc_clock_freq);
		return -1;
	}

	Write16(state, CC_REG_OSC_MODE__A, CC_REG_OSC_MODE_M20, 0);
	Write16(state, CC_REG_PLL_MODE__A, CC_REG_PLL_MODE_BYPASS_PLL |
		CC_REG_PLL_MODE_PUMP_CUR_12, 0);
	Write16(state, CC_REG_REF_DIVIDE__A, state->osc_clock_freq / 4000, 0);
	Write16(state, CC_REG_PWD_MODE__A, CC_REG_PWD_MODE_DOWN_PLL, 0);
	Write16(state, CC_REG_UPDATE__A, CC_REG_UPDATE_KEY, 0);

	return 0;
}

static int ResetECOD(struct drxd_state *state)
{
	int status = 0;

	if (state->type_A)
		status = Write16(state, EC_OD_REG_SYNC__A, 0x0664, 0);
	else
		status = Write16(state, B_EC_OD_REG_SYNC__A, 0x0664, 0);

	if (!(status < 0))
		status = WriteTable(state, state->m_ResetECRAM);
	if (!(status < 0))
		status = Write16(state, EC_OD_REG_COMM_EXEC__A, 0x0001, 0);
	return status;
}

/* Configure PGA switch */

static int SetCfgPga(struct drxd_state *state, int pgaSwitch)
{
	int status;
	u16 AgModeLop = 0;
	u16 AgModeHip = 0;
	do {
		if (pgaSwitch) {
			/* PGA on */
			/* fine gain */
			status = Read16(state, B_FE_AG_REG_AG_MODE_LOP__A, &AgModeLop, 0x0000);
			if (status < 0)
				break;
			AgModeLop &= (~(B_FE_AG_REG_AG_MODE_LOP_MODE_C__M));
			AgModeLop |= B_FE_AG_REG_AG_MODE_LOP_MODE_C_DYNAMIC;
			status = Write16(state, B_FE_AG_REG_AG_MODE_LOP__A, AgModeLop, 0x0000);
			if (status < 0)
				break;

			/* coarse gain */
			status = Read16(state, B_FE_AG_REG_AG_MODE_HIP__A, &AgModeHip, 0x0000);
			if (status < 0)
				break;
			AgModeHip &= (~(B_FE_AG_REG_AG_MODE_HIP_MODE_J__M));
			AgModeHip |= B_FE_AG_REG_AG_MODE_HIP_MODE_J_DYNAMIC;
			status = Write16(state, B_FE_AG_REG_AG_MODE_HIP__A, AgModeHip, 0x0000);
			if (status < 0)
				break;

			/* enable fine and coarse gain, enable AAF,
			   no ext resistor */
			status = Write16(state, B_FE_AG_REG_AG_PGA_MODE__A, B_FE_AG_REG_AG_PGA_MODE_PFY_PCY_AFY_REN, 0x0000);
			if (status < 0)
				break;
		} else {
			/* PGA off, bypass */

			/* fine gain */
			status = Read16(state, B_FE_AG_REG_AG_MODE_LOP__A, &AgModeLop, 0x0000);
			if (status < 0)
				break;
			AgModeLop &= (~(B_FE_AG_REG_AG_MODE_LOP_MODE_C__M));
			AgModeLop |= B_FE_AG_REG_AG_MODE_LOP_MODE_C_STATIC;
			status = Write16(state, B_FE_AG_REG_AG_MODE_LOP__A, AgModeLop, 0x0000);
			if (status < 0)
				break;

			/* coarse gain */
			status = Read16(state, B_FE_AG_REG_AG_MODE_HIP__A, &AgModeHip, 0x0000);
			if (status < 0)
				break;
			AgModeHip &= (~(B_FE_AG_REG_AG_MODE_HIP_MODE_J__M));
			AgModeHip |= B_FE_AG_REG_AG_MODE_HIP_MODE_J_STATIC;
			status = Write16(state, B_FE_AG_REG_AG_MODE_HIP__A, AgModeHip, 0x0000);
			if (status < 0)
				break;

			/* disable fine and coarse gain, enable AAF,
			   no ext resistor */
			status = Write16(state, B_FE_AG_REG_AG_PGA_MODE__A, B_FE_AG_REG_AG_PGA_MODE_PFN_PCN_AFY_REN, 0x0000);
			if (status < 0)
				break;
		}
	} while (0);
	return status;
}

static int InitFE(struct drxd_state *state)
{
	int status;

	do {
		status = WriteTable(state, state->m_InitFE_1);
		if (status < 0)
			break;

		if (state->type_A) {
			status = Write16(state, FE_AG_REG_AG_PGA_MODE__A,
					 FE_AG_REG_AG_PGA_MODE_PFN_PCN_AFY_REN,
					 0);
		} else {
			if (state->PGA)
				status = SetCfgPga(state, 0);
			else
				status =
				    Write16(state, B_FE_AG_REG_AG_PGA_MODE__A,
					    B_FE_AG_REG_AG_PGA_MODE_PFN_PCN_AFY_REN,
					    0);
		}

		if (status < 0)
			break;
		status = Write16(state, FE_AG_REG_AG_AGC_SIO__A, state->m_FeAgRegAgAgcSio, 0x0000);
		if (status < 0)
			break;
		status = Write16(state, FE_AG_REG_AG_PWD__A, state->m_FeAgRegAgPwd, 0x0000);
		if (status < 0)
			break;

		status = WriteTable(state, state->m_InitFE_2);
		if (status < 0)
			break;

	} while (0);

	return status;
}

static int InitFT(struct drxd_state *state)
{
	/*
	   norm OFFSET,  MB says =2 voor 8K en =3 voor 2K waarschijnlijk
	   SC stuff
	 */
	return Write16(state, FT_REG_COMM_EXEC__A, 0x0001, 0x0000);
}

static int SC_WaitForReady(struct drxd_state *state)
{
	u16 curCmd;
	int i;

	for (i = 0; i < DRXD_MAX_RETRIES; i += 1) {
		int status = Read16(state, SC_RA_RAM_CMD__A, &curCmd, 0);
		if (status == 0 || curCmd == 0)
			return status;
	}
	return -1;
}

static int SC_SendCommand(struct drxd_state *state, u16 cmd)
{
	int status = 0;
	u16 errCode;

	Write16(state, SC_RA_RAM_CMD__A, cmd, 0);
	SC_WaitForReady(state);

	Read16(state, SC_RA_RAM_CMD_ADDR__A, &errCode, 0);

	if (errCode == 0xFFFF) {
		printk(KERN_ERR "Command Error\n");
		status = -1;
	}

	return status;
}

static int SC_ProcStartCommand(struct drxd_state *state,
			       u16 subCmd, u16 param0, u16 param1)
{
	int status = 0;
	u16 scExec;

	mutex_lock(&state->mutex);
	do {
		Read16(state, SC_COMM_EXEC__A, &scExec, 0);
		if (scExec != 1) {
			status = -1;
			break;
		}
		SC_WaitForReady(state);
		Write16(state, SC_RA_RAM_CMD_ADDR__A, subCmd, 0);
		Write16(state, SC_RA_RAM_PARAM1__A, param1, 0);
		Write16(state, SC_RA_RAM_PARAM0__A, param0, 0);

		SC_SendCommand(state, SC_RA_RAM_CMD_PROC_START);
	} while (0);
	mutex_unlock(&state->mutex);
	return status;
}

static int SC_SetPrefParamCommand(struct drxd_state *state,
				  u16 subCmd, u16 param0, u16 param1)
{
	int status;

	mutex_lock(&state->mutex);
	do {
		status = SC_WaitForReady(state);
		if (status < 0)
			break;
		status = Write16(state, SC_RA_RAM_CMD_ADDR__A, subCmd, 0);
		if (status < 0)
			break;
		status = Write16(state, SC_RA_RAM_PARAM1__A, param1, 0);
		if (status < 0)
			break;
		status = Write16(state, SC_RA_RAM_PARAM0__A, param0, 0);
		if (status < 0)
			break;

		status = SC_SendCommand(state, SC_RA_RAM_CMD_SET_PREF_PARAM);
		if (status < 0)
			break;
	} while (0);
	mutex_unlock(&state->mutex);
	return status;
}

#if 0
static int SC_GetOpParamCommand(struct drxd_state *state, u16 * result)
{
	int status = 0;

	mutex_lock(&state->mutex);
	do {
		status = SC_WaitForReady(state);
		if (status < 0)
			break;
		status = SC_SendCommand(state, SC_RA_RAM_CMD_GET_OP_PARAM);
		if (status < 0)
			break;
		status = Read16(state, SC_RA_RAM_PARAM0__A, result, 0);
		if (status < 0)
			break;
	} while (0);
	mutex_unlock(&state->mutex);
	return status;
}
#endif

static int ConfigureMPEGOutput(struct drxd_state *state, int bEnableOutput)
{
	int status;

	do {
		u16 EcOcRegIprInvMpg = 0;
		u16 EcOcRegOcModeLop = 0;
		u16 EcOcRegOcModeHip = 0;
		u16 EcOcRegOcMpgSio = 0;

		/*CHK_ERROR(Read16(state, EC_OC_REG_OC_MODE_LOP__A, &EcOcRegOcModeLop, 0)); */

		if (state->operation_mode == OM_DVBT_Diversity_Front) {
			if (bEnableOutput) {
				EcOcRegOcModeHip |=
				    B_EC_OC_REG_OC_MODE_HIP_MPG_BUS_SRC_MONITOR;
			} else
				EcOcRegOcMpgSio |= EC_OC_REG_OC_MPG_SIO__M;
			EcOcRegOcModeLop |=
			    EC_OC_REG_OC_MODE_LOP_PAR_ENA_DISABLE;
		} else {
			EcOcRegOcModeLop = state->m_EcOcRegOcModeLop;

			if (bEnableOutput)
				EcOcRegOcMpgSio &= (~(EC_OC_REG_OC_MPG_SIO__M));
			else
				EcOcRegOcMpgSio |= EC_OC_REG_OC_MPG_SIO__M;

			/* Don't Insert RS Byte */
			if (state->insert_rs_byte) {
				EcOcRegOcModeLop &=
				    (~(EC_OC_REG_OC_MODE_LOP_PAR_ENA__M));
				EcOcRegOcModeHip &=
				    (~EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL__M);
				EcOcRegOcModeHip |=
				    EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL_ENABLE;
			} else {
				EcOcRegOcModeLop |=
				    EC_OC_REG_OC_MODE_LOP_PAR_ENA_DISABLE;
				EcOcRegOcModeHip &=
				    (~EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL__M);
				EcOcRegOcModeHip |=
				    EC_OC_REG_OC_MODE_HIP_MPG_PAR_VAL_DISABLE;
			}

			/* Mode = Parallel */
			if (state->enable_parallel)
				EcOcRegOcModeLop &=
				    (~(EC_OC_REG_OC_MODE_LOP_MPG_TRM_MDE__M));
			else
				EcOcRegOcModeLop |=
				    EC_OC_REG_OC_MODE_LOP_MPG_TRM_MDE_SERIAL;
		}
		/* Invert Data */
		/* EcOcRegIprInvMpg |= 0x00FF; */
		EcOcRegIprInvMpg &= (~(0x00FF));

		/* Invert Error ( we don't use the pin ) */
		/*  EcOcRegIprInvMpg |= 0x0100; */
		EcOcRegIprInvMpg &= (~(0x0100));

		/* Invert Start ( we don't use the pin ) */
		/* EcOcRegIprInvMpg |= 0x0200; */
		EcOcRegIprInvMpg &= (~(0x0200));

		/* Invert Valid ( we don't use the pin ) */
		/* EcOcRegIprInvMpg |= 0x0400; */
		EcOcRegIprInvMpg &= (~(0x0400));

		/* Invert Clock */
		/* EcOcRegIprInvMpg |= 0x0800; */
		EcOcRegIprInvMpg &= (~(0x0800));

		/* EcOcRegOcModeLop =0x05; */
		status = Write16(state, EC_OC_REG_IPR_INV_MPG__A, EcOcRegIprInvMpg, 0);
		if (status < 0)
			break;
		status = Write16(state, EC_OC_REG_OC_MODE_LOP__A, EcOcRegOcModeLop, 0);
		if (status < 0)
			break;
		status = Write16(state, EC_OC_REG_OC_MODE_HIP__A, EcOcRegOcModeHip, 0x0000);
		if (status < 0)
			break;
		status = Write16(state, EC_OC_REG_OC_MPG_SIO__A, EcOcRegOcMpgSio, 0);
		if (status < 0)
			break;
	} while (0);
	return status;
}

static int SetDeviceTypeId(struct drxd_state *state)
{
	int status = 0;
	u16 deviceId = 0;

	do {
		status = Read16(state, CC_REG_JTAGID_L__A, &deviceId, 0);
		if (status < 0)
			break;
		/* TODO: why twice? */
		status = Read16(state, CC_REG_JTAGID_L__A, &deviceId, 0);
		if (status < 0)
			break;
		printk(KERN_INFO "drxd: deviceId = %04x\n", deviceId);

		state->type_A = 0;
		state->PGA = 0;
		state->diversity = 0;
		if (deviceId == 0) {	/* on A2 only 3975 available */
			state->type_A = 1;
			printk(KERN_INFO "DRX3975D-A2\n");
		} else {
			deviceId >>= 12;
			printk(KERN_INFO "DRX397%dD-B1\n", deviceId);
			switch (deviceId) {
			case 4:
				state->diversity = 1;
			case 3:
			case 7:
				state->PGA = 1;
				break;
			case 6:
				state->diversity = 1;
			case 5:
			case 8:
				break;
			default:
				status = -1;
				break;
			}
		}
	} while (0);

	if (status < 0)
		return status;

	/* Init Table selection */
	state->m_InitAtomicRead = DRXD_InitAtomicRead;
	state->m_InitSC = DRXD_InitSC;
	state->m_ResetECRAM = DRXD_ResetECRAM;
	if (state->type_A) {
		state->m_ResetCEFR = DRXD_ResetCEFR;
		state->m_InitFE_1 = DRXD_InitFEA2_1;
		state->m_InitFE_2 = DRXD_InitFEA2_2;
		state->m_InitCP = DRXD_InitCPA2;
		state->m_InitCE = DRXD_InitCEA2;
		state->m_InitEQ = DRXD_InitEQA2;
		state->m_InitEC = DRXD_InitECA2;
		if (load_firmware(state, DRX_FW_FILENAME_A2))
			return -EIO;
	} else {
		state->m_ResetCEFR = NULL;
		state->m_InitFE_1 = DRXD_InitFEB1_1;
		state->m_InitFE_2 = DRXD_InitFEB1_2;
		state->m_InitCP = DRXD_InitCPB1;
		state->m_InitCE = DRXD_InitCEB1;
		state->m_InitEQ = DRXD_InitEQB1;
		state->m_InitEC = DRXD_InitECB1;
		if (load_firmware(state, DRX_FW_FILENAME_B1))
			return -EIO;
	}
	if (state->diversity) {
		state->m_InitDiversityFront = DRXD_InitDiversityFront;
		state->m_InitDiversityEnd = DRXD_InitDiversityEnd;
		state->m_DisableDiversity = DRXD_DisableDiversity;
		state->m_StartDiversityFront = DRXD_StartDiversityFront;
		state->m_StartDiversityEnd = DRXD_StartDiversityEnd;
		state->m_DiversityDelay8MHZ = DRXD_DiversityDelay8MHZ;
		state->m_DiversityDelay6MHZ = DRXD_DiversityDelay6MHZ;
	} else {
		state->m_InitDiversityFront = NULL;
		state->m_InitDiversityEnd = NULL;
		state->m_DisableDiversity = NULL;
		state->m_StartDiversityFront = NULL;
		state->m_StartDiversityEnd = NULL;
		state->m_DiversityDelay8MHZ = NULL;
		state->m_DiversityDelay6MHZ = NULL;
	}

	return status;
}

static int CorrectSysClockDeviation(struct drxd_state *state)
{
	int status;
	s32 incr = 0;
	s32 nomincr = 0;
	u32 bandwidth = 0;
	u32 sysClockInHz = 0;
	u32 sysClockFreq = 0;	/* in kHz */
	s16 oscClockDeviation;
	s16 Diff;

	do {
		/* Retrieve bandwidth and incr, sanity check */

		/* These accesses should be AtomicReadReg32, but that
		   causes trouble (at least for diversity */
		status = Read32(state, LC_RA_RAM_IFINCR_NOM_L__A, ((u32 *) &nomincr), 0);
		if (status < 0)
			break;
		status = Read32(state, FE_IF_REG_INCR0__A, (u32 *) &incr, 0);
		if (status < 0)
			break;

		if (state->type_A) {
			if ((nomincr - incr < -500) || (nomincr - incr > 500))
				break;
		} else {
			if ((nomincr - incr < -2000) || (nomincr - incr > 2000))
				break;
		}

		switch (state->props.bandwidth_hz) {
		case 8000000:
			bandwidth = DRXD_BANDWIDTH_8MHZ_IN_HZ;
			break;
		case 7000000:
			bandwidth = DRXD_BANDWIDTH_7MHZ_IN_HZ;
			break;
		case 6000000:
			bandwidth = DRXD_BANDWIDTH_6MHZ_IN_HZ;
			break;
		default:
			return -1;
			break;
		}

		/* Compute new sysclock value
		   sysClockFreq = (((incr + 2^23)*bandwidth)/2^21)/1000 */
		incr += (1 << 23);
		sysClockInHz = MulDiv32(incr, bandwidth, 1 << 21);
		sysClockFreq = (u32) (sysClockInHz / 1000);
		/* rounding */
		if ((sysClockInHz % 1000) > 500)
			sysClockFreq++;

		/* Compute clock deviation in ppm */
		oscClockDeviation = (u16) ((((s32) (sysClockFreq) -
					     (s32)
					     (state->expected_sys_clock_freq)) *
					    1000000L) /
					   (s32)
					   (state->expected_sys_clock_freq));

		Diff = oscClockDeviation - state->osc_clock_deviation;
		/*printk(KERN_INFO "sysclockdiff=%d\n", Diff); */
		if (Diff >= -200 && Diff <= 200) {
			state->sys_clock_freq = (u16) sysClockFreq;
			if (oscClockDeviation != state->osc_clock_deviation) {
				if (state->config.osc_deviation) {
					state->config.osc_deviation(state->priv,
								    oscClockDeviation,
								    1);
					state->osc_clock_deviation =
					    oscClockDeviation;
				}
			}
			/* switch OFF SRMM scan in SC */
			status = Write16(state, SC_RA_RAM_SAMPLE_RATE_COUNT__A, DRXD_OSCDEV_DONT_SCAN, 0);
			if (status < 0)
				break;
			/* overrule FE_IF internal value for
			   proper re-locking */
			status = Write16(state, SC_RA_RAM_IF_SAVE__AX, state->current_fe_if_incr, 0);
			if (status < 0)
				break;
			state->cscd_state = CSCD_SAVED;
		}
	} while (0);

	return status;
}

static int DRX_Stop(struct drxd_state *state)
{
	int status;

	if (state->drxd_state != DRXD_STARTED)
		return 0;

	do {
		if (state->cscd_state != CSCD_SAVED) {
			u32 lock;
			status = DRX_GetLockStatus(state, &lock);
			if (status < 0)
				break;
		}

		status = StopOC(state);
		if (status < 0)
			break;

		state->drxd_state = DRXD_STOPPED;

		status = ConfigureMPEGOutput(state, 0);
		if (status < 0)
			break;

		if (state->type_A) {
			/* Stop relevant processors off the device */
			status = Write16(state, EC_OD_REG_COMM_EXEC__A, 0x0000, 0x0000);
			if (status < 0)
				break;

			status = Write16(state, SC_COMM_EXEC__A, SC_COMM_EXEC_CTL_STOP, 0);
			if (status < 0)
				break;
			status = Write16(state, LC_COMM_EXEC__A, SC_COMM_EXEC_CTL_STOP, 0);
			if (status < 0)
				break;
		} else {
			/* Stop all processors except HI & CC & FE */
			status = Write16(state, B_SC_COMM_EXEC__A, SC_COMM_EXEC_CTL_STOP, 0);
			if (status < 0)
				break;
			status = Write16(state, B_LC_COMM_EXEC__A, SC_COMM_EXEC_CTL_STOP, 0);
			if (status < 0)
				break;
			status = Write16(state, B_FT_COMM_EXEC__A, SC_COMM_EXEC_CTL_STOP, 0);
			if (status < 0)
				break;
			status = Write16(state, B_CP_COMM_EXEC__A, SC_COMM_EXEC_CTL_STOP, 0);
			if (status < 0)
				break;
			status = Write16(state, B_CE_COMM_EXEC__A, SC_COMM_EXEC_CTL_STOP, 0);
			if (status < 0)
				break;
			status = Write16(state, B_EQ_COMM_EXEC__A, SC_COMM_EXEC_CTL_STOP, 0);
			if (status < 0)
				break;
			status = Write16(state, EC_OD_REG_COMM_EXEC__A, 0x0000, 0);
			if (status < 0)
				break;
		}

	} while (0);
	return status;
}

#if 0	/* Currently unused */
static int SetOperationMode(struct drxd_state *state, int oMode)
{
	int status;

	do {
		if (state->drxd_state != DRXD_STOPPED) {
			status = -1;
			break;
		}

		if (oMode == state->operation_mode) {
			status = 0;
			break;
		}

		if (oMode != OM_Default && !state->diversity) {
			status = -1;
			break;
		}

		switch (oMode) {
		case OM_DVBT_Diversity_Front:
			status = WriteTable(state, state->m_InitDiversityFront);
			break;
		case OM_DVBT_Diversity_End:
			status = WriteTable(state, state->m_InitDiversityEnd);
			break;
		case OM_Default:
			/* We need to check how to
			   get DRXD out of diversity */
		default:
			status = WriteTable(state, state->m_DisableDiversity);
			break;
		}
	} while (0);

	if (!status)
		state->operation_mode = oMode;
	return status;
}
#endif

static int StartDiversity(struct drxd_state *state)
{
	int status = 0;
	u16 rcControl;

	do {
		if (state->operation_mode == OM_DVBT_Diversity_Front) {
			status = WriteTable(state, state->m_StartDiversityFront);
			if (status < 0)
				break;
		} else if (state->operation_mode == OM_DVBT_Diversity_End) {
			status = WriteTable(state, state->m_StartDiversityEnd);
			if (status < 0)
				break;
			if (state->props.bandwidth_hz == 8000000) {
				status = WriteTable(state, state->m_DiversityDelay8MHZ);
				if (status < 0)
					break;
			} else {
				status = WriteTable(state, state->m_DiversityDelay6MHZ);
				if (status < 0)
					break;
			}

			status = Read16(state, B_EQ_REG_RC_SEL_CAR__A, &rcControl, 0);
			if (status < 0)
				break;
			rcControl &= ~(B_EQ_REG_RC_SEL_CAR_FFTMODE__M);
			rcControl |= B_EQ_REG_RC_SEL_CAR_DIV_ON |
			    /*  combining enabled */
			    B_EQ_REG_RC_SEL_CAR_MEAS_A_CC |
			    B_EQ_REG_RC_SEL_CAR_PASS_A_CC |
			    B_EQ_REG_RC_SEL_CAR_LOCAL_A_CC;
			status = Write16(state, B_EQ_REG_RC_SEL_CAR__A, rcControl, 0);
			if (status < 0)
				break;
		}
	} while (0);
	return status;
}

static int SetFrequencyShift(struct drxd_state *state,
			     u32 offsetFreq, int channelMirrored)
{
	int negativeShift = (state->tuner_mirrors == channelMirrored);

	/* Handle all mirroring
	 *
	 * Note: ADC mirroring (aliasing) is implictly handled by limiting
	 * feFsRegAddInc to 28 bits below
	 * (if the result before masking is more than 28 bits, this means
	 *  that the ADC is mirroring.
	 * The masking is in fact the aliasing of the ADC)
	 *
	 */

	/* Compute register value, unsigned computation */
	state->fe_fs_add_incr = MulDiv32(state->intermediate_freq +
					 offsetFreq,
					 1 << 28, state->sys_clock_freq);
	/* Remove integer part */
	state->fe_fs_add_incr &= 0x0FFFFFFFL;
	if (negativeShift)
		state->fe_fs_add_incr = ((1 << 28) - state->fe_fs_add_incr);

	/* Save the frequency shift without tunerOffset compensation
	   for CtrlGetChannel. */
	state->org_fe_fs_add_incr = MulDiv32(state->intermediate_freq,
					     1 << 28, state->sys_clock_freq);
	/* Remove integer part */
	state->org_fe_fs_add_incr &= 0x0FFFFFFFL;
	if (negativeShift)
		state->org_fe_fs_add_incr = ((1L << 28) -
					     state->org_fe_fs_add_incr);

	return Write32(state, FE_FS_REG_ADD_INC_LOP__A,
		       state->fe_fs_add_incr, 0);
}

static int SetCfgNoiseCalibration(struct drxd_state *state,
				  struct SNoiseCal *noiseCal)
{
	u16 beOptEna;
	int status = 0;

	do {
		status = Read16(state, SC_RA_RAM_BE_OPT_ENA__A, &beOptEna, 0);
		if (status < 0)
			break;
		if (noiseCal->cpOpt) {
			beOptEna |= (1 << SC_RA_RAM_BE_OPT_ENA_CP_OPT);
		} else {
			beOptEna &= ~(1 << SC_RA_RAM_BE_OPT_ENA_CP_OPT);
			status = Write16(state, CP_REG_AC_NEXP_OFFS__A, noiseCal->cpNexpOfs, 0);
			if (status < 0)
				break;
		}
		status = Write16(state, SC_RA_RAM_BE_OPT_ENA__A, beOptEna, 0);
		if (status < 0)
			break;

		if (!state->type_A) {
			status = Write16(state, B_SC_RA_RAM_CO_TD_CAL_2K__A, noiseCal->tdCal2k, 0);
			if (status < 0)
				break;
			status = Write16(state, B_SC_RA_RAM_CO_TD_CAL_8K__A, noiseCal->tdCal8k, 0);
			if (status < 0)
				break;
		}
	} while (0);

	return status;
}

static int DRX_Start(struct drxd_state *state, s32 off)
{
	struct dtv_frontend_properties *p = &state->props;
	int status;

	u16 transmissionParams = 0;
	u16 operationMode = 0;
	u16 qpskTdTpsPwr = 0;
	u16 qam16TdTpsPwr = 0;
	u16 qam64TdTpsPwr = 0;
	u32 feIfIncr = 0;
	u32 bandwidth = 0;
	int mirrorFreqSpect;

	u16 qpskSnCeGain = 0;
	u16 qam16SnCeGain = 0;
	u16 qam64SnCeGain = 0;
	u16 qpskIsGainMan = 0;
	u16 qam16IsGainMan = 0;
	u16 qam64IsGainMan = 0;
	u16 qpskIsGainExp = 0;
	u16 qam16IsGainExp = 0;
	u16 qam64IsGainExp = 0;
	u16 bandwidthParam = 0;

	if (off < 0)
		off = (off - 500) / 1000;
	else
		off = (off + 500) / 1000;

	do {
		if (state->drxd_state != DRXD_STOPPED)
			return -1;
		status = ResetECOD(state);
		if (status < 0)
			break;
		if (state->type_A) {
			status = InitSC(state);
			if (status < 0)
				break;
		} else {
			status = InitFT(state);
			if (status < 0)
				break;
			status = InitCP(state);
			if (status < 0)
				break;
			status = InitCE(state);
			if (status < 0)
				break;
			status = InitEQ(state);
			if (status < 0)
				break;
			status = InitSC(state);
			if (status < 0)
				break;
		}

		/* Restore current IF & RF AGC settings */

		status = SetCfgIfAgc(state, &state->if_agc_cfg);
		if (status < 0)
			break;
		status = SetCfgRfAgc(state, &state->rf_agc_cfg);
		if (status < 0)
			break;

		mirrorFreqSpect = (state->props.inversion == INVERSION_ON);

		switch (p->transmission_mode) {
		default:	/* Not set, detect it automatically */
			operationMode |= SC_RA_RAM_OP_AUTO_MODE__M;
			/* fall through , try first guess DRX_FFTMODE_8K */
		case TRANSMISSION_MODE_8K:
			transmissionParams |= SC_RA_RAM_OP_PARAM_MODE_8K;
			if (state->type_A) {
				status = Write16(state, EC_SB_REG_TR_MODE__A, EC_SB_REG_TR_MODE_8K, 0x0000);
				if (status < 0)
					break;
				qpskSnCeGain = 99;
				qam16SnCeGain = 83;
				qam64SnCeGain = 67;
			}
			break;
		case TRANSMISSION_MODE_2K:
			transmissionParams |= SC_RA_RAM_OP_PARAM_MODE_2K;
			if (state->type_A) {
				status = Write16(state, EC_SB_REG_TR_MODE__A, EC_SB_REG_TR_MODE_2K, 0x0000);
				if (status < 0)
					break;
				qpskSnCeGain = 97;
				qam16SnCeGain = 71;
				qam64SnCeGain = 65;
			}
			break;
		}

		switch (p->guard_interval) {
		case GUARD_INTERVAL_1_4:
			transmissionParams |= SC_RA_RAM_OP_PARAM_GUARD_4;
			break;
		case GUARD_INTERVAL_1_8:
			transmissionParams |= SC_RA_RAM_OP_PARAM_GUARD_8;
			break;
		case GUARD_INTERVAL_1_16:
			transmissionParams |= SC_RA_RAM_OP_PARAM_GUARD_16;
			break;
		case GUARD_INTERVAL_1_32:
			transmissionParams |= SC_RA_RAM_OP_PARAM_GUARD_32;
			break;
		default:	/* Not set, detect it automatically */
			operationMode |= SC_RA_RAM_OP_AUTO_GUARD__M;
			/* try first guess 1/4 */
			transmissionParams |= SC_RA_RAM_OP_PARAM_GUARD_4;
			break;
		}

		switch (p->hierarchy) {
		case HIERARCHY_1:
			transmissionParams |= SC_RA_RAM_OP_PARAM_HIER_A1;
			if (state->type_A) {
				status = Write16(state, EQ_REG_OT_ALPHA__A, 0x0001, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_ALPHA__A, 0x0001, 0x0000);
				if (status < 0)
					break;

				qpskTdTpsPwr = EQ_TD_TPS_PWR_UNKNOWN;
				qam16TdTpsPwr = EQ_TD_TPS_PWR_QAM16_ALPHA1;
				qam64TdTpsPwr = EQ_TD_TPS_PWR_QAM64_ALPHA1;

				qpskIsGainMan =
				    SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_MAN__PRE;
				qam16IsGainMan =
				    SC_RA_RAM_EQ_IS_GAIN_16QAM_MAN__PRE;
				qam64IsGainMan =
				    SC_RA_RAM_EQ_IS_GAIN_64QAM_MAN__PRE;

				qpskIsGainExp =
				    SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_EXP__PRE;
				qam16IsGainExp =
				    SC_RA_RAM_EQ_IS_GAIN_16QAM_EXP__PRE;
				qam64IsGainExp =
				    SC_RA_RAM_EQ_IS_GAIN_64QAM_EXP__PRE;
			}
			break;

		case HIERARCHY_2:
			transmissionParams |= SC_RA_RAM_OP_PARAM_HIER_A2;
			if (state->type_A) {
				status = Write16(state, EQ_REG_OT_ALPHA__A, 0x0002, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_ALPHA__A, 0x0002, 0x0000);
				if (status < 0)
					break;

				qpskTdTpsPwr = EQ_TD_TPS_PWR_UNKNOWN;
				qam16TdTpsPwr = EQ_TD_TPS_PWR_QAM16_ALPHA2;
				qam64TdTpsPwr = EQ_TD_TPS_PWR_QAM64_ALPHA2;

				qpskIsGainMan =
				    SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_MAN__PRE;
				qam16IsGainMan =
				    SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_MAN__PRE;
				qam64IsGainMan =
				    SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_MAN__PRE;

				qpskIsGainExp =
				    SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_EXP__PRE;
				qam16IsGainExp =
				    SC_RA_RAM_EQ_IS_GAIN_16QAM_A2_EXP__PRE;
				qam64IsGainExp =
				    SC_RA_RAM_EQ_IS_GAIN_64QAM_A2_EXP__PRE;
			}
			break;
		case HIERARCHY_4:
			transmissionParams |= SC_RA_RAM_OP_PARAM_HIER_A4;
			if (state->type_A) {
				status = Write16(state, EQ_REG_OT_ALPHA__A, 0x0003, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_ALPHA__A, 0x0003, 0x0000);
				if (status < 0)
					break;

				qpskTdTpsPwr = EQ_TD_TPS_PWR_UNKNOWN;
				qam16TdTpsPwr = EQ_TD_TPS_PWR_QAM16_ALPHA4;
				qam64TdTpsPwr = EQ_TD_TPS_PWR_QAM64_ALPHA4;

				qpskIsGainMan =
				    SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_MAN__PRE;
				qam16IsGainMan =
				    SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_MAN__PRE;
				qam64IsGainMan =
				    SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_MAN__PRE;

				qpskIsGainExp =
				    SC_RA_RAM_EQ_IS_GAIN_UNKNOWN_EXP__PRE;
				qam16IsGainExp =
				    SC_RA_RAM_EQ_IS_GAIN_16QAM_A4_EXP__PRE;
				qam64IsGainExp =
				    SC_RA_RAM_EQ_IS_GAIN_64QAM_A4_EXP__PRE;
			}
			break;
		case HIERARCHY_AUTO:
		default:
			/* Not set, detect it automatically, start with none */
			operationMode |= SC_RA_RAM_OP_AUTO_HIER__M;
			transmissionParams |= SC_RA_RAM_OP_PARAM_HIER_NO;
			if (state->type_A) {
				status = Write16(state, EQ_REG_OT_ALPHA__A, 0x0000, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_ALPHA__A, 0x0000, 0x0000);
				if (status < 0)
					break;

				qpskTdTpsPwr = EQ_TD_TPS_PWR_QPSK;
				qam16TdTpsPwr = EQ_TD_TPS_PWR_QAM16_ALPHAN;
				qam64TdTpsPwr = EQ_TD_TPS_PWR_QAM64_ALPHAN;

				qpskIsGainMan =
				    SC_RA_RAM_EQ_IS_GAIN_QPSK_MAN__PRE;
				qam16IsGainMan =
				    SC_RA_RAM_EQ_IS_GAIN_16QAM_MAN__PRE;
				qam64IsGainMan =
				    SC_RA_RAM_EQ_IS_GAIN_64QAM_MAN__PRE;

				qpskIsGainExp =
				    SC_RA_RAM_EQ_IS_GAIN_QPSK_EXP__PRE;
				qam16IsGainExp =
				    SC_RA_RAM_EQ_IS_GAIN_16QAM_EXP__PRE;
				qam64IsGainExp =
				    SC_RA_RAM_EQ_IS_GAIN_64QAM_EXP__PRE;
			}
			break;
		}
		status = status;
		if (status < 0)
			break;

		switch (p->modulation) {
		default:
			operationMode |= SC_RA_RAM_OP_AUTO_CONST__M;
			/* fall through , try first guess
			   DRX_CONSTELLATION_QAM64 */
		case QAM_64:
			transmissionParams |= SC_RA_RAM_OP_PARAM_CONST_QAM64;
			if (state->type_A) {
				status = Write16(state, EQ_REG_OT_CONST__A, 0x0002, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_CONST__A, EC_SB_REG_CONST_64QAM, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_SCALE_MSB__A, 0x0020, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_SCALE_BIT2__A, 0x0008, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_SCALE_LSB__A, 0x0002, 0x0000);
				if (status < 0)
					break;

				status = Write16(state, EQ_REG_TD_TPS_PWR_OFS__A, qam64TdTpsPwr, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EQ_REG_SN_CEGAIN__A, qam64SnCeGain, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EQ_REG_IS_GAIN_MAN__A, qam64IsGainMan, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EQ_REG_IS_GAIN_EXP__A, qam64IsGainExp, 0x0000);
				if (status < 0)
					break;
			}
			break;
		case QPSK:
			transmissionParams |= SC_RA_RAM_OP_PARAM_CONST_QPSK;
			if (state->type_A) {
				status = Write16(state, EQ_REG_OT_CONST__A, 0x0000, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_CONST__A, EC_SB_REG_CONST_QPSK, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_SCALE_MSB__A, 0x0010, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_SCALE_BIT2__A, 0x0000, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_SCALE_LSB__A, 0x0000, 0x0000);
				if (status < 0)
					break;

				status = Write16(state, EQ_REG_TD_TPS_PWR_OFS__A, qpskTdTpsPwr, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EQ_REG_SN_CEGAIN__A, qpskSnCeGain, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EQ_REG_IS_GAIN_MAN__A, qpskIsGainMan, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EQ_REG_IS_GAIN_EXP__A, qpskIsGainExp, 0x0000);
				if (status < 0)
					break;
			}
			break;

		case QAM_16:
			transmissionParams |= SC_RA_RAM_OP_PARAM_CONST_QAM16;
			if (state->type_A) {
				status = Write16(state, EQ_REG_OT_CONST__A, 0x0001, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_CONST__A, EC_SB_REG_CONST_16QAM, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_SCALE_MSB__A, 0x0010, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_SCALE_BIT2__A, 0x0004, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EC_SB_REG_SCALE_LSB__A, 0x0000, 0x0000);
				if (status < 0)
					break;

				status = Write16(state, EQ_REG_TD_TPS_PWR_OFS__A, qam16TdTpsPwr, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EQ_REG_SN_CEGAIN__A, qam16SnCeGain, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EQ_REG_IS_GAIN_MAN__A, qam16IsGainMan, 0x0000);
				if (status < 0)
					break;
				status = Write16(state, EQ_REG_IS_GAIN_EXP__A, qam16IsGainExp, 0x0000);
				if (status < 0)
					break;
			}
			break;

		}
		status = status;
		if (status < 0)
			break;

		switch (DRX_CHANNEL_HIGH) {
		default:
		case DRX_CHANNEL_AUTO:
		case DRX_CHANNEL_LOW:
			transmissionParams |= SC_RA_RAM_OP_PARAM_PRIO_LO;
			status = Write16(state, EC_SB_REG_PRIOR__A, EC_SB_REG_PRIOR_LO, 0x0000);
			if (status < 0)
				break;
			break;
		case DRX_CHANNEL_HIGH:
			transmissionParams |= SC_RA_RAM_OP_PARAM_PRIO_HI;
			status = Write16(state, EC_SB_REG_PRIOR__A, EC_SB_REG_PRIOR_HI, 0x0000);
			if (status < 0)
				break;
			break;

		}

		switch (p->code_rate_HP) {
		case FEC_1_2:
			transmissionParams |= SC_RA_RAM_OP_PARAM_RATE_1_2;
			if (state->type_A) {
				status = Write16(state, EC_VD_REG_SET_CODERATE__A, EC_VD_REG_SET_CODERATE_C1_2, 0x0000);
				if (status < 0)
					break;
			}
			break;
		default:
			operationMode |= SC_RA_RAM_OP_AUTO_RATE__M;
		case FEC_2_3:
			transmissionParams |= SC_RA_RAM_OP_PARAM_RATE_2_3;
			if (state->type_A) {
				status = Write16(state, EC_VD_REG_SET_CODERATE__A, EC_VD_REG_SET_CODERATE_C2_3, 0x0000);
				if (status < 0)
					break;
			}
			break;
		case FEC_3_4:
			transmissionParams |= SC_RA_RAM_OP_PARAM_RATE_3_4;
			if (state->type_A) {
				status = Write16(state, EC_VD_REG_SET_CODERATE__A, EC_VD_REG_SET_CODERATE_C3_4, 0x0000);
				if (status < 0)
					break;
			}
			break;
		case FEC_5_6:
			transmissionParams |= SC_RA_RAM_OP_PARAM_RATE_5_6;
			if (state->type_A) {
				status = Write16(state, EC_VD_REG_SET_CODERATE__A, EC_VD_REG_SET_CODERATE_C5_6, 0x0000);
				if (status < 0)
					break;
			}
			break;
		case FEC_7_8:
			transmissionParams |= SC_RA_RAM_OP_PARAM_RATE_7_8;
			if (state->type_A) {
				status = Write16(state, EC_VD_REG_SET_CODERATE__A, EC_VD_REG_SET_CODERATE_C7_8, 0x0000);
				if (status < 0)
					break;
			}
			break;
		}
		status = status;
		if (status < 0)
			break;

		/* First determine real bandwidth (Hz) */
		/* Also set delay for impulse noise cruncher (only A2) */
		/* Also set parameters for EC_OC fix, note
		   EC_OC_REG_TMD_HIL_MAR is changed
		   by SC for fix for some 8K,1/8 guard but is restored by
		   InitEC and ResetEC
		   functions */
		switch (p->bandwidth_hz) {
		case 0:
			p->bandwidth_hz = 8000000;
			/* fall through */
		case 8000000:
			/* (64/7)*(8/8)*1000000 */
			bandwidth = DRXD_BANDWIDTH_8MHZ_IN_HZ;

			bandwidthParam = 0;
			status = Write16(state,
					 FE_AG_REG_IND_DEL__A, 50, 0x0000);
			break;
		case 7000000:
			/* (64/7)*(7/8)*1000000 */
			bandwidth = DRXD_BANDWIDTH_7MHZ_IN_HZ;
			bandwidthParam = 0x4807;	/*binary:0100 1000 0000 0111 */
			status = Write16(state,
					 FE_AG_REG_IND_DEL__A, 59, 0x0000);
			break;
		case 6000000:
			/* (64/7)*(6/8)*1000000 */
			bandwidth = DRXD_BANDWIDTH_6MHZ_IN_HZ;
			bandwidthParam = 0x0F07;	/*binary: 0000 1111 0000 0111 */
			status = Write16(state,
					 FE_AG_REG_IND_DEL__A, 71, 0x0000);
			break;
		default:
			status = -EINVAL;
		}
		if (status < 0)
			break;

		status = Write16(state, SC_RA_RAM_BAND__A, bandwidthParam, 0x0000);
		if (status < 0)
			break;

		{
			u16 sc_config;
			status = Read16(state, SC_RA_RAM_CONFIG__A, &sc_config, 0);
			if (status < 0)
				break;

			/* enable SLAVE mode in 2k 1/32 to
			   prevent timing change glitches */
			if ((p->transmission_mode == TRANSMISSION_MODE_2K) &&
			    (p->guard_interval == GUARD_INTERVAL_1_32)) {
				/* enable slave */
				sc_config |= SC_RA_RAM_CONFIG_SLAVE__M;
			} else {
				/* disable slave */
				sc_config &= ~SC_RA_RAM_CONFIG_SLAVE__M;
			}
			status = Write16(state, SC_RA_RAM_CONFIG__A, sc_config, 0);
			if (status < 0)
				break;
		}

		status = SetCfgNoiseCalibration(state, &state->noise_cal);
		if (status < 0)
			break;

		if (state->cscd_state == CSCD_INIT) {
			/* switch on SRMM scan in SC */
			status = Write16(state, SC_RA_RAM_SAMPLE_RATE_COUNT__A, DRXD_OSCDEV_DO_SCAN, 0x0000);
			if (status < 0)
				break;
/*            CHK_ERROR(Write16(SC_RA_RAM_SAMPLE_RATE_STEP__A, DRXD_OSCDEV_STEP, 0x0000));*/
			state->cscd_state = CSCD_SET;
		}

		/* Now compute FE_IF_REG_INCR */
		/*((( SysFreq/BandWidth)/2)/2) -1) * 2^23) =>
		   ((SysFreq / BandWidth) * (2^21) ) - (2^23) */
		feIfIncr = MulDiv32(state->sys_clock_freq * 1000,
				    (1ULL << 21), bandwidth) - (1 << 23);
		status = Write16(state, FE_IF_REG_INCR0__A, (u16) (feIfIncr & FE_IF_REG_INCR0__M), 0x0000);
		if (status < 0)
			break;
		status = Write16(state, FE_IF_REG_INCR1__A, (u16) ((feIfIncr >> FE_IF_REG_INCR0__W) & FE_IF_REG_INCR1__M), 0x0000);
		if (status < 0)
			break;
		/* Bandwidth setting done */

		/* Mirror & frequency offset */
		SetFrequencyShift(state, off, mirrorFreqSpect);

		/* Start SC, write channel settings to SC */

		/* Enable SC after setting all other parameters */
		status = Write16(state, SC_COMM_STATE__A, 0, 0x0000);
		if (status < 0)
			break;
		status = Write16(state, SC_COMM_EXEC__A, 1, 0x0000);
		if (status < 0)
			break;

		/* Write SC parameter registers, operation mode */
#if 1
		operationMode = (SC_RA_RAM_OP_AUTO_MODE__M |
				 SC_RA_RAM_OP_AUTO_GUARD__M |
				 SC_RA_RAM_OP_AUTO_CONST__M |
				 SC_RA_RAM_OP_AUTO_HIER__M |
				 SC_RA_RAM_OP_AUTO_RATE__M);
#endif
		status = SC_SetPrefParamCommand(state, 0x0000, transmissionParams, operationMode);
		if (status < 0)
			break;

		/* Start correct processes to get in lock */
		status = SC_ProcStartCommand(state, SC_RA_RAM_PROC_LOCKTRACK, SC_RA_RAM_SW_EVENT_RUN_NMASK__M, SC_RA_RAM_LOCKTRACK_MIN);
		if (status < 0)
			break;

		status = StartOC(state);
		if (status < 0)
			break;

		if (state->operation_mode != OM_Default) {
			status = StartDiversity(state);
			if (status < 0)
				break;
		}

		state->drxd_state = DRXD_STARTED;
	} while (0);

	return status;
}

static int CDRXD(struct drxd_state *state, u32 IntermediateFrequency)
{
	u32 ulRfAgcOutputLevel = 0xffffffff;
	u32 ulRfAgcSettleLevel = 528;	/* Optimum value for MT2060 */
	u32 ulRfAgcMinLevel = 0;	/* Currently unused */
	u32 ulRfAgcMaxLevel = DRXD_FE_CTRL_MAX;	/* Currently unused */
	u32 ulRfAgcSpeed = 0;	/* Currently unused */
	u32 ulRfAgcMode = 0;	/*2;   Off */
	u32 ulRfAgcR1 = 820;
	u32 ulRfAgcR2 = 2200;
	u32 ulRfAgcR3 = 150;
	u32 ulIfAgcMode = 0;	/* Auto */
	u32 ulIfAgcOutputLevel = 0xffffffff;
	u32 ulIfAgcSettleLevel = 0xffffffff;
	u32 ulIfAgcMinLevel = 0xffffffff;
	u32 ulIfAgcMaxLevel = 0xffffffff;
	u32 ulIfAgcSpeed = 0xffffffff;
	u32 ulIfAgcR1 = 820;
	u32 ulIfAgcR2 = 2200;
	u32 ulIfAgcR3 = 150;
	u32 ulClock = state->config.clock;
	u32 ulSerialMode = 0;
	u32 ulEcOcRegOcModeLop = 4;	/* Dynamic DTO source */
	u32 ulHiI2cDelay = HI_I2C_DELAY;
	u32 ulHiI2cBridgeDelay = HI_I2C_BRIDGE_DELAY;
	u32 ulHiI2cPatch = 0;
	u32 ulEnvironment = APPENV_PORTABLE;
	u32 ulEnvironmentDiversity = APPENV_MOBILE;
	u32 ulIFFilter = IFFILTER_SAW;

	state->if_agc_cfg.ctrlMode = AGC_CTRL_AUTO;
	state->if_agc_cfg.outputLevel = 0;
	state->if_agc_cfg.settleLevel = 140;
	state->if_agc_cfg.minOutputLevel = 0;
	state->if_agc_cfg.maxOutputLevel = 1023;
	state->if_agc_cfg.speed = 904;

	if (ulIfAgcMode == 1 && ulIfAgcOutputLevel <= DRXD_FE_CTRL_MAX) {
		state->if_agc_cfg.ctrlMode = AGC_CTRL_USER;
		state->if_agc_cfg.outputLevel = (u16) (ulIfAgcOutputLevel);
	}

	if (ulIfAgcMode == 0 &&
	    ulIfAgcSettleLevel <= DRXD_FE_CTRL_MAX &&
	    ulIfAgcMinLevel <= DRXD_FE_CTRL_MAX &&
	    ulIfAgcMaxLevel <= DRXD_FE_CTRL_MAX &&
	    ulIfAgcSpeed <= DRXD_FE_CTRL_MAX) {
		state->if_agc_cfg.ctrlMode = AGC_CTRL_AUTO;
		state->if_agc_cfg.settleLevel = (u16) (ulIfAgcSettleLevel);
		state->if_agc_cfg.minOutputLevel = (u16) (ulIfAgcMinLevel);
		state->if_agc_cfg.maxOutputLevel = (u16) (ulIfAgcMaxLevel);
		state->if_agc_cfg.speed = (u16) (ulIfAgcSpeed);
	}

	state->if_agc_cfg.R1 = (u16) (ulIfAgcR1);
	state->if_agc_cfg.R2 = (u16) (ulIfAgcR2);
	state->if_agc_cfg.R3 = (u16) (ulIfAgcR3);

	state->rf_agc_cfg.R1 = (u16) (ulRfAgcR1);
	state->rf_agc_cfg.R2 = (u16) (ulRfAgcR2);
	state->rf_agc_cfg.R3 = (u16) (ulRfAgcR3);

	state->rf_agc_cfg.ctrlMode = AGC_CTRL_AUTO;
	/* rest of the RFAgcCfg structure currently unused */
	if (ulRfAgcMode == 1 && ulRfAgcOutputLevel <= DRXD_FE_CTRL_MAX) {
		state->rf_agc_cfg.ctrlMode = AGC_CTRL_USER;
		state->rf_agc_cfg.outputLevel = (u16) (ulRfAgcOutputLevel);
	}

	if (ulRfAgcMode == 0 &&
	    ulRfAgcSettleLevel <= DRXD_FE_CTRL_MAX &&
	    ulRfAgcMinLevel <= DRXD_FE_CTRL_MAX &&
	    ulRfAgcMaxLevel <= DRXD_FE_CTRL_MAX &&
	    ulRfAgcSpeed <= DRXD_FE_CTRL_MAX) {
		state->rf_agc_cfg.ctrlMode = AGC_CTRL_AUTO;
		state->rf_agc_cfg.settleLevel = (u16) (ulRfAgcSettleLevel);
		state->rf_agc_cfg.minOutputLevel = (u16) (ulRfAgcMinLevel);
		state->rf_agc_cfg.maxOutputLevel = (u16) (ulRfAgcMaxLevel);
		state->rf_agc_cfg.speed = (u16) (ulRfAgcSpeed);
	}

	if (ulRfAgcMode == 2)
		state->rf_agc_cfg.ctrlMode = AGC_CTRL_OFF;

	if (ulEnvironment <= 2)
		state->app_env_default = (enum app_env)
		    (ulEnvironment);
	if (ulEnvironmentDiversity <= 2)
		state->app_env_diversity = (enum app_env)
		    (ulEnvironmentDiversity);

	if (ulIFFilter == IFFILTER_DISCRETE) {
		/* discrete filter */
		state->noise_cal.cpOpt = 0;
		state->noise_cal.cpNexpOfs = 40;
		state->noise_cal.tdCal2k = -40;
		state->noise_cal.tdCal8k = -24;
	} else {
		/* SAW filter */
		state->noise_cal.cpOpt = 1;
		state->noise_cal.cpNexpOfs = 0;
		state->noise_cal.tdCal2k = -21;
		state->noise_cal.tdCal8k = -24;
	}
	state->m_EcOcRegOcModeLop = (u16) (ulEcOcRegOcModeLop);

	state->chip_adr = (state->config.demod_address << 1) | 1;
	switch (ulHiI2cPatch) {
	case 1:
		state->m_HiI2cPatch = DRXD_HiI2cPatch_1;
		break;
	case 3:
		state->m_HiI2cPatch = DRXD_HiI2cPatch_3;
		break;
	default:
		state->m_HiI2cPatch = NULL;
	}

	/* modify tuner and clock attributes */
	state->intermediate_freq = (u16) (IntermediateFrequency / 1000);
	/* expected system clock frequency in kHz */
	state->expected_sys_clock_freq = 48000;
	/* real system clock frequency in kHz */
	state->sys_clock_freq = 48000;
	state->osc_clock_freq = (u16) ulClock;
	state->osc_clock_deviation = 0;
	state->cscd_state = CSCD_INIT;
	state->drxd_state = DRXD_UNINITIALIZED;

	state->PGA = 0;
	state->type_A = 0;
	state->tuner_mirrors = 0;

	/* modify MPEG output attributes */
	state->insert_rs_byte = state->config.insert_rs_byte;
	state->enable_parallel = (ulSerialMode != 1);

	/* Timing div, 250ns/Psys */
	/* Timing div, = ( delay (nano seconds) * sysclk (kHz) )/ 1000 */

	state->hi_cfg_timing_div = (u16) ((state->sys_clock_freq / 1000) *
					  ulHiI2cDelay) / 1000;
	/* Bridge delay, uses oscilator clock */
	/* Delay = ( delay (nano seconds) * oscclk (kHz) )/ 1000 */
	state->hi_cfg_bridge_delay = (u16) ((state->osc_clock_freq / 1000) *
					    ulHiI2cBridgeDelay) / 1000;

	state->m_FeAgRegAgPwd = DRXD_DEF_AG_PWD_CONSUMER;
	/* state->m_FeAgRegAgPwd = DRXD_DEF_AG_PWD_PRO; */
	state->m_FeAgRegAgAgcSio = DRXD_DEF_AG_AGC_SIO;
	return 0;
}

static int DRXD_init(struct drxd_state *state, const u8 *fw, u32 fw_size)
{
	int status = 0;
	u32 driverVersion;

	if (state->init_done)
		return 0;

	CDRXD(state, state->config.IF ? state->config.IF : 36000000);

	do {
		state->operation_mode = OM_Default;

		status = SetDeviceTypeId(state);
		if (status < 0)
			break;

		/* Apply I2c address patch to B1 */
		if (!state->type_A && state->m_HiI2cPatch != NULL) {
			status = WriteTable(state, state->m_HiI2cPatch);
			if (status < 0)
				break;
		}

		if (state->type_A) {
			/* HI firmware patch for UIO readout,
			   avoid clearing of result register */
			status = Write16(state, 0x43012D, 0x047f, 0);
			if (status < 0)
				break;
		}

		status = HI_ResetCommand(state);
		if (status < 0)
			break;

		status = StopAllProcessors(state);
		if (status < 0)
			break;
		status = InitCC(state);
		if (status < 0)
			break;

		state->osc_clock_deviation = 0;

		if (state->config.osc_deviation)
			state->osc_clock_deviation =
			    state->config.osc_deviation(state->priv, 0, 0);
		{
			/* Handle clock deviation */
			s32 devB;
			s32 devA = (s32) (state->osc_clock_deviation) *
			    (s32) (state->expected_sys_clock_freq);
			/* deviation in kHz */
			s32 deviation = (devA / (1000000L));
			/* rounding, signed */
			if (devA > 0)
				devB = (2);
			else
				devB = (-2);
			if ((devB * (devA % 1000000L) > 1000000L)) {
				/* add +1 or -1 */
				deviation += (devB / 2);
			}

			state->sys_clock_freq =
			    (u16) ((state->expected_sys_clock_freq) +
				   deviation);
		}
		status = InitHI(state);
		if (status < 0)
			break;
		status = InitAtomicRead(state);
		if (status < 0)
			break;

		status = EnableAndResetMB(state);
		if (status < 0)
			break;
		if (state->type_A) {
			status = ResetCEFR(state);
			if (status < 0)
				break;
		}
		if (fw) {
			status = DownloadMicrocode(state, fw, fw_size);
			if (status < 0)
				break;
		} else {
			status = DownloadMicrocode(state, state->microcode, state->microcode_length);
			if (status < 0)
				break;
		}

		if (state->PGA) {
			state->m_FeAgRegAgPwd = DRXD_DEF_AG_PWD_PRO;
			SetCfgPga(state, 0);	/* PGA = 0 dB */
		} else {
			state->m_FeAgRegAgPwd = DRXD_DEF_AG_PWD_CONSUMER;
		}

		state->m_FeAgRegAgAgcSio = DRXD_DEF_AG_AGC_SIO;

		status = InitFE(state);
		if (status < 0)
			break;
		status = InitFT(state);
		if (status < 0)
			break;
		status = InitCP(state);
		if (status < 0)
			break;
		status = InitCE(state);
		if (status < 0)
			break;
		status = InitEQ(state);
		if (status < 0)
			break;
		status = InitEC(state);
		if (status < 0)
			break;
		status = InitSC(state);
		if (status < 0)
			break;

		status = SetCfgIfAgc(state, &state->if_agc_cfg);
		if (status < 0)
			break;
		status = SetCfgRfAgc(state, &state->rf_agc_cfg);
		if (status < 0)
			break;

		state->cscd_state = CSCD_INIT;
		status = Write16(state, SC_COMM_EXEC__A, SC_COMM_EXEC_CTL_STOP, 0);
		if (status < 0)
			break;
		status = Write16(state, LC_COMM_EXEC__A, SC_COMM_EXEC_CTL_STOP, 0);
		if (status < 0)
			break;

		driverVersion = (((VERSION_MAJOR / 10) << 4) +
				 (VERSION_MAJOR % 10)) << 24;
		driverVersion += (((VERSION_MINOR / 10) << 4) +
				  (VERSION_MINOR % 10)) << 16;
		driverVersion += ((VERSION_PATCH / 1000) << 12) +
		    ((VERSION_PATCH / 100) << 8) +
		    ((VERSION_PATCH / 10) << 4) + (VERSION_PATCH % 10);

		status = Write32(state, SC_RA_RAM_DRIVER_VERSION__AX, driverVersion, 0);
		if (status < 0)
			break;

		status = StopOC(state);
		if (status < 0)
			break;

		state->drxd_state = DRXD_STOPPED;
		state->init_done = 1;
		status = 0;
	} while (0);
	return status;
}

static int DRXD_status(struct drxd_state *state, u32 *pLockStatus)
{
	DRX_GetLockStatus(state, pLockStatus);

	/*if (*pLockStatus&DRX_LOCK_MPEG) */
	if (*pLockStatus & DRX_LOCK_FEC) {
		ConfigureMPEGOutput(state, 1);
		/* Get status again, in case we have MPEG lock now */
		/*DRX_GetLockStatus(state, pLockStatus); */
	}

	return 0;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static int drxd_read_signal_strength(struct dvb_frontend *fe, u16 * strength)
{
	struct drxd_state *state = fe->demodulator_priv;
	u32 value;
	int res;

	res = ReadIFAgc(state, &value);
	if (res < 0)
		*strength = 0;
	else
		*strength = 0xffff - (value << 4);
	return 0;
}

static int drxd_read_status(struct dvb_frontend *fe, enum fe_status *status)
{
	struct drxd_state *state = fe->demodulator_priv;
	u32 lock;

	DRXD_status(state, &lock);
	*status = 0;
	/* No MPEG lock in V255 firmware, bug ? */
#if 1
	if (lock & DRX_LOCK_MPEG)
		*status |= FE_HAS_LOCK;
#else
	if (lock & DRX_LOCK_FEC)
		*status |= FE_HAS_LOCK;
#endif
	if (lock & DRX_LOCK_FEC)
		*status |= FE_HAS_VITERBI | FE_HAS_SYNC;
	if (lock & DRX_LOCK_DEMOD)
		*status |= FE_HAS_CARRIER | FE_HAS_SIGNAL;

	return 0;
}

static int drxd_init(struct dvb_frontend *fe)
{
	struct drxd_state *state = fe->demodulator_priv;

	return DRXD_init(state, NULL, 0);
}

static int drxd_config_i2c(struct dvb_frontend *fe, int onoff)
{
	struct drxd_state *state = fe->demodulator_priv;

	if (state->config.disable_i2c_gate_ctrl == 1)
		return 0;

	return DRX_ConfigureI2CBridge(state, onoff);
}

static int drxd_get_tune_settings(struct dvb_frontend *fe,
				  struct dvb_frontend_tune_settings *sets)
{
	sets->min_delay_ms = 10000;
	sets->max_drift = 0;
	sets->step_size = 0;
	return 0;
}

static int drxd_read_ber(struct dvb_frontend *fe, u32 * ber)
{
	*ber = 0;
	return 0;
}

static int drxd_read_snr(struct dvb_frontend *fe, u16 * snr)
{
	*snr = 0;
	return 0;
}

static int drxd_read_ucblocks(struct dvb_frontend *fe, u32 * ucblocks)
{
	*ucblocks = 0;
	return 0;
}

static int drxd_sleep(struct dvb_frontend *fe)
{
	struct drxd_state *state = fe->demodulator_priv;

	ConfigureMPEGOutput(state, 0);
	return 0;
}

static int drxd_i2c_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	return drxd_config_i2c(fe, enable);
}

static int drxd_set_frontend(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *p = &fe->dtv_property_cache;
	struct drxd_state *state = fe->demodulator_priv;
	s32 off = 0;

	state->props = *p;
	DRX_Stop(state);

	if (fe->ops.tuner_ops.set_params) {
		fe->ops.tuner_ops.set_params(fe);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	msleep(200);

	return DRX_Start(state, off);
}

static void drxd_release(struct dvb_frontend *fe)
{
	struct drxd_state *state = fe->demodulator_priv;

	kfree(state);
}

static struct dvb_frontend_ops drxd_ops = {
	.delsys = { SYS_DVBT},
	.info = {
		 .name = "Micronas DRXD DVB-T",
		 .frequency_min = 47125000,
		 .frequency_max = 855250000,
		 .frequency_stepsize = 166667,
		 .frequency_tolerance = 0,
		 .caps = FE_CAN_FEC_1_2 | FE_CAN_FEC_2_3 |
		 FE_CAN_FEC_3_4 | FE_CAN_FEC_5_6 | FE_CAN_FEC_7_8 |
		 FE_CAN_FEC_AUTO |
		 FE_CAN_QAM_16 | FE_CAN_QAM_64 |
		 FE_CAN_QAM_AUTO |
		 FE_CAN_TRANSMISSION_MODE_AUTO |
		 FE_CAN_GUARD_INTERVAL_AUTO |
		 FE_CAN_HIERARCHY_AUTO | FE_CAN_RECOVER | FE_CAN_MUTE_TS},

	.release = drxd_release,
	.init = drxd_init,
	.sleep = drxd_sleep,
	.i2c_gate_ctrl = drxd_i2c_gate_ctrl,

	.set_frontend = drxd_set_frontend,
	.get_tune_settings = drxd_get_tune_settings,

	.read_status = drxd_read_status,
	.read_ber = drxd_read_ber,
	.read_signal_strength = drxd_read_signal_strength,
	.read_snr = drxd_read_snr,
	.read_ucblocks = drxd_read_ucblocks,
};

struct dvb_frontend *drxd_attach(const struct drxd_config *config,
				 void *priv, struct i2c_adapter *i2c,
				 struct device *dev)
{
	struct drxd_state *state = NULL;

	state = kmalloc(sizeof(struct drxd_state), GFP_KERNEL);
	if (!state)
		return NULL;
	memset(state, 0, sizeof(*state));

	state->ops = drxd_ops;
	state->dev = dev;
	state->config = *config;
	state->i2c = i2c;
	state->priv = priv;

	mutex_init(&state->mutex);

	if (Read16(state, 0, NULL, 0) < 0)
		goto error;

	state->frontend.ops = drxd_ops;
	state->frontend.demodulator_priv = state;
	ConfigureMPEGOutput(state, 0);
	/* add few initialization to allow gate control */
	CDRXD(state, state->config.IF ? state->config.IF : 36000000);
	InitHI(state);

	return &state->frontend;

error:
	printk(KERN_ERR "drxd: not found\n");
	kfree(state);
	return NULL;
}
EXPORT_SYMBOL(drxd_attach);

MODULE_DESCRIPTION("DRXD driver");
MODULE_AUTHOR("Micronas");
MODULE_LICENSE("GPL");
