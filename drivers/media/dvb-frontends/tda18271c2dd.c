/*
 * tda18271c2dd: Driver for the TDA18271C2 tuner
 *
 * Copyright (C) 2010 Digital Devices GmbH
 *
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
#include "tda18271c2dd.h"

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  64

struct SStandardParam {
	s32   m_IFFrequency;
	u32   m_BandWidth;
	u8    m_EP3_4_0;
	u8    m_EB22;
};

struct SMap {
	u32   m_Frequency;
	u8    m_Param;
};

struct SMapI {
	u32   m_Frequency;
	s32    m_Param;
};

struct SMap2 {
	u32   m_Frequency;
	u8    m_Param1;
	u8    m_Param2;
};

struct SRFBandMap {
	u32   m_RF_max;
	u32   m_RF1_Default;
	u32   m_RF2_Default;
	u32   m_RF3_Default;
};

enum ERegister {
	ID = 0,
	TM,
	PL,
	EP1, EP2, EP3, EP4, EP5,
	CPD, CD1, CD2, CD3,
	MPD, MD1, MD2, MD3,
	EB1, EB2, EB3, EB4, EB5, EB6, EB7, EB8, EB9, EB10,
	EB11, EB12, EB13, EB14, EB15, EB16, EB17, EB18, EB19, EB20,
	EB21, EB22, EB23,
	NUM_REGS
};

struct tda_state {
	struct i2c_adapter *i2c;
	u8 adr;

	u32   m_Frequency;
	u32   IF;

	u8    m_IFLevelAnalog;
	u8    m_IFLevelDigital;
	u8    m_IFLevelDVBC;
	u8    m_IFLevelDVBT;

	u8    m_EP4;
	u8    m_EP3_Standby;

	bool  m_bMaster;

	s32   m_SettlingTime;

	u8    m_Regs[NUM_REGS];

	/* Tracking filter settings for band 0..6 */
	u32   m_RF1[7];
	s32   m_RF_A1[7];
	s32   m_RF_B1[7];
	u32   m_RF2[7];
	s32   m_RF_A2[7];
	s32   m_RF_B2[7];
	u32   m_RF3[7];

	u8    m_TMValue_RFCal;    /* Calibration temperatur */

	bool  m_bFMInput;         /* true to use Pin 8 for FM Radio */

};

static int PowerScan(struct tda_state *state,
		     u8 RFBand, u32 RF_in,
		     u32 *pRF_Out, bool *pbcal);

static int i2c_readn(struct i2c_adapter *adapter, u8 adr, u8 *data, int len)
{
	struct i2c_msg msgs[1] = {{.addr = adr,  .flags = I2C_M_RD,
				   .buf  = data, .len   = len} };
	return (i2c_transfer(adapter, msgs, 1) == 1) ? 0 : -1;
}

static int i2c_write(struct i2c_adapter *adap, u8 adr, u8 *data, int len)
{
	struct i2c_msg msg = {.addr = adr, .flags = 0,
			      .buf = data, .len = len};

	if (i2c_transfer(adap, &msg, 1) != 1) {
		printk(KERN_ERR "tda18271c2dd: i2c write error at addr %i\n", adr);
		return -1;
	}
	return 0;
}

static int WriteRegs(struct tda_state *state,
		     u8 SubAddr, u8 *Regs, u16 nRegs)
{
	u8 data[MAX_XFER_SIZE];

	if (1 + nRegs > sizeof(data)) {
		printk(KERN_WARNING
		       "%s: i2c wr: len=%d is too big!\n",
		       KBUILD_MODNAME, nRegs);
		return -EINVAL;
	}

	data[0] = SubAddr;
	memcpy(data + 1, Regs, nRegs);
	return i2c_write(state->i2c, state->adr, data, nRegs + 1);
}

static int WriteReg(struct tda_state *state, u8 SubAddr, u8 Reg)
{
	u8 msg[2] = {SubAddr, Reg};

	return i2c_write(state->i2c, state->adr, msg, 2);
}

static int Read(struct tda_state *state, u8 * Regs)
{
	return i2c_readn(state->i2c, state->adr, Regs, 16);
}

static int ReadExtented(struct tda_state *state, u8 * Regs)
{
	return i2c_readn(state->i2c, state->adr, Regs, NUM_REGS);
}

static int UpdateRegs(struct tda_state *state, u8 RegFrom, u8 RegTo)
{
	return WriteRegs(state, RegFrom,
			 &state->m_Regs[RegFrom], RegTo-RegFrom+1);
}
static int UpdateReg(struct tda_state *state, u8 Reg)
{
	return WriteReg(state, Reg, state->m_Regs[Reg]);
}

#include "tda18271c2dd_maps.h"

static void reset(struct tda_state *state)
{
	u32   ulIFLevelAnalog = 0;
	u32   ulIFLevelDigital = 2;
	u32   ulIFLevelDVBC = 7;
	u32   ulIFLevelDVBT = 6;
	u32   ulXTOut = 0;
	u32   ulStandbyMode = 0x06;    /* Send in stdb, but leave osc on */
	u32   ulSlave = 0;
	u32   ulFMInput = 0;
	u32   ulSettlingTime = 100;

	state->m_Frequency         = 0;
	state->m_SettlingTime = 100;
	state->m_IFLevelAnalog = (ulIFLevelAnalog & 0x07) << 2;
	state->m_IFLevelDigital = (ulIFLevelDigital & 0x07) << 2;
	state->m_IFLevelDVBC = (ulIFLevelDVBC & 0x07) << 2;
	state->m_IFLevelDVBT = (ulIFLevelDVBT & 0x07) << 2;

	state->m_EP4 = 0x20;
	if (ulXTOut != 0)
		state->m_EP4 |= 0x40;

	state->m_EP3_Standby = ((ulStandbyMode & 0x07) << 5) | 0x0F;
	state->m_bMaster = (ulSlave == 0);

	state->m_SettlingTime = ulSettlingTime;

	state->m_bFMInput = (ulFMInput == 2);
}

static bool SearchMap1(struct SMap Map[],
		       u32 Frequency, u8 *pParam)
{
	int i = 0;

	while ((Map[i].m_Frequency != 0) && (Frequency > Map[i].m_Frequency))
		i += 1;
	if (Map[i].m_Frequency == 0)
		return false;
	*pParam = Map[i].m_Param;
	return true;
}

static bool SearchMap2(struct SMapI Map[],
		       u32 Frequency, s32 *pParam)
{
	int i = 0;

	while ((Map[i].m_Frequency != 0) &&
	       (Frequency > Map[i].m_Frequency))
		i += 1;
	if (Map[i].m_Frequency == 0)
		return false;
	*pParam = Map[i].m_Param;
	return true;
}

static bool SearchMap3(struct SMap2 Map[], u32 Frequency,
		       u8 *pParam1, u8 *pParam2)
{
	int i = 0;

	while ((Map[i].m_Frequency != 0) &&
	       (Frequency > Map[i].m_Frequency))
		i += 1;
	if (Map[i].m_Frequency == 0)
		return false;
	*pParam1 = Map[i].m_Param1;
	*pParam2 = Map[i].m_Param2;
	return true;
}

static bool SearchMap4(struct SRFBandMap Map[],
		       u32 Frequency, u8 *pRFBand)
{
	int i = 0;

	while (i < 7 && (Frequency > Map[i].m_RF_max))
		i += 1;
	if (i == 7)
		return false;
	*pRFBand = i;
	return true;
}

static int ThermometerRead(struct tda_state *state, u8 *pTM_Value)
{
	int status = 0;

	do {
		u8 Regs[16];
		state->m_Regs[TM] |= 0x10;
		status = UpdateReg(state, TM);
		if (status < 0)
			break;
		status = Read(state, Regs);
		if (status < 0)
			break;
		if (((Regs[TM] & 0x0F) == 0 && (Regs[TM] & 0x20) == 0x20) ||
		    ((Regs[TM] & 0x0F) == 8 && (Regs[TM] & 0x20) == 0x00)) {
			state->m_Regs[TM] ^= 0x20;
			status = UpdateReg(state, TM);
			if (status < 0)
				break;
			msleep(10);
			status = Read(state, Regs);
			if (status < 0)
				break;
		}
		*pTM_Value = (Regs[TM] & 0x20)
				? m_Thermometer_Map_2[Regs[TM] & 0x0F]
				: m_Thermometer_Map_1[Regs[TM] & 0x0F] ;
		state->m_Regs[TM] &= ~0x10;        /* Thermometer off */
		status = UpdateReg(state, TM);
		if (status < 0)
			break;
		state->m_Regs[EP4] &= ~0x03;       /* CAL_mode = 0 ????????? */
		status = UpdateReg(state, EP4);
		if (status < 0)
			break;
	} while (0);

	return status;
}

static int StandBy(struct tda_state *state)
{
	int status = 0;
	do {
		state->m_Regs[EB12] &= ~0x20;  /* PD_AGC1_Det = 0 */
		status = UpdateReg(state, EB12);
		if (status < 0)
			break;
		state->m_Regs[EB18] &= ~0x83;  /* AGC1_loop_off = 0, AGC1_Gain = 6 dB */
		status = UpdateReg(state, EB18);
		if (status < 0)
			break;
		state->m_Regs[EB21] |= 0x03; /* AGC2_Gain = -6 dB */
		state->m_Regs[EP3] = state->m_EP3_Standby;
		status = UpdateReg(state, EP3);
		if (status < 0)
			break;
		state->m_Regs[EB23] &= ~0x06; /* ForceLP_Fc2_En = 0, LP_Fc[2] = 0 */
		status = UpdateRegs(state, EB21, EB23);
		if (status < 0)
			break;
	} while (0);
	return status;
}

static int CalcMainPLL(struct tda_state *state, u32 freq)
{

	u8  PostDiv;
	u8  Div;
	u64 OscFreq;
	u32 MainDiv;

	if (!SearchMap3(m_Main_PLL_Map, freq, &PostDiv, &Div))
		return -EINVAL;

	OscFreq = (u64) freq * (u64) Div;
	OscFreq *= (u64) 16384;
	do_div(OscFreq, (u64)16000000);
	MainDiv = OscFreq;

	state->m_Regs[MPD] = PostDiv & 0x77;
	state->m_Regs[MD1] = ((MainDiv >> 16) & 0x7F);
	state->m_Regs[MD2] = ((MainDiv >>  8) & 0xFF);
	state->m_Regs[MD3] = (MainDiv & 0xFF);

	return UpdateRegs(state, MPD, MD3);
}

static int CalcCalPLL(struct tda_state *state, u32 freq)
{
	u8 PostDiv;
	u8 Div;
	u64 OscFreq;
	u32 CalDiv;

	if (!SearchMap3(m_Cal_PLL_Map, freq, &PostDiv, &Div))
		return -EINVAL;

	OscFreq = (u64)freq * (u64)Div;
	/* CalDiv = u32( OscFreq * 16384 / 16000000 ); */
	OscFreq *= (u64)16384;
	do_div(OscFreq, (u64)16000000);
	CalDiv = OscFreq;

	state->m_Regs[CPD] = PostDiv;
	state->m_Regs[CD1] = ((CalDiv >> 16) & 0xFF);
	state->m_Regs[CD2] = ((CalDiv >>  8) & 0xFF);
	state->m_Regs[CD3] = (CalDiv & 0xFF);

	return UpdateRegs(state, CPD, CD3);
}

static int CalibrateRF(struct tda_state *state,
		       u8 RFBand, u32 freq, s32 *pCprog)
{
	int status = 0;
	u8 Regs[NUM_REGS];
	do {
		u8 BP_Filter = 0;
		u8 GainTaper = 0;
		u8 RFC_K = 0;
		u8 RFC_M = 0;

		state->m_Regs[EP4] &= ~0x03; /* CAL_mode = 0 */
		status = UpdateReg(state, EP4);
		if (status < 0)
			break;
		state->m_Regs[EB18] |= 0x03;  /* AGC1_Gain = 3 */
		status = UpdateReg(state, EB18);
		if (status < 0)
			break;

		/* Switching off LT (as datasheet says) causes calibration on C1 to fail */
		/* (Readout of Cprog is allways 255) */
		if (state->m_Regs[ID] != 0x83)    /* C1: ID == 83, C2: ID == 84 */
			state->m_Regs[EP3] |= 0x40; /* SM_LT = 1 */

		if (!(SearchMap1(m_BP_Filter_Map, freq, &BP_Filter) &&
			SearchMap1(m_GainTaper_Map, freq, &GainTaper) &&
			SearchMap3(m_KM_Map, freq, &RFC_K, &RFC_M)))
			return -EINVAL;

		state->m_Regs[EP1] = (state->m_Regs[EP1] & ~0x07) | BP_Filter;
		state->m_Regs[EP2] = (RFBand << 5) | GainTaper;

		state->m_Regs[EB13] = (state->m_Regs[EB13] & ~0x7C) | (RFC_K << 4) | (RFC_M << 2);

		status = UpdateRegs(state, EP1, EP3);
		if (status < 0)
			break;
		status = UpdateReg(state, EB13);
		if (status < 0)
			break;

		state->m_Regs[EB4] |= 0x20;    /* LO_ForceSrce = 1 */
		status = UpdateReg(state, EB4);
		if (status < 0)
			break;

		state->m_Regs[EB7] |= 0x20;    /* CAL_ForceSrce = 1 */
		status = UpdateReg(state, EB7);
		if (status < 0)
			break;

		state->m_Regs[EB14] = 0; /* RFC_Cprog = 0 */
		status = UpdateReg(state, EB14);
		if (status < 0)
			break;

		state->m_Regs[EB20] &= ~0x20;  /* ForceLock = 0; */
		status = UpdateReg(state, EB20);
		if (status < 0)
			break;

		state->m_Regs[EP4] |= 0x03;  /* CAL_Mode = 3 */
		status = UpdateRegs(state, EP4, EP5);
		if (status < 0)
			break;

		status = CalcCalPLL(state, freq);
		if (status < 0)
			break;
		status = CalcMainPLL(state, freq + 1000000);
		if (status < 0)
			break;

		msleep(5);
		status = UpdateReg(state, EP2);
		if (status < 0)
			break;
		status = UpdateReg(state, EP1);
		if (status < 0)
			break;
		status = UpdateReg(state, EP2);
		if (status < 0)
			break;
		status = UpdateReg(state, EP1);
		if (status < 0)
			break;

		state->m_Regs[EB4] &= ~0x20;    /* LO_ForceSrce = 0 */
		status = UpdateReg(state, EB4);
		if (status < 0)
			break;

		state->m_Regs[EB7] &= ~0x20;    /* CAL_ForceSrce = 0 */
		status = UpdateReg(state, EB7);
		if (status < 0)
			break;
		msleep(10);

		state->m_Regs[EB20] |= 0x20;  /* ForceLock = 1; */
		status = UpdateReg(state, EB20);
		if (status < 0)
			break;
		msleep(60);

		state->m_Regs[EP4] &= ~0x03;  /* CAL_Mode = 0 */
		state->m_Regs[EP3] &= ~0x40; /* SM_LT = 0 */
		state->m_Regs[EB18] &= ~0x03;  /* AGC1_Gain = 0 */
		status = UpdateReg(state, EB18);
		if (status < 0)
			break;
		status = UpdateRegs(state, EP3, EP4);
		if (status < 0)
			break;
		status = UpdateReg(state, EP1);
		if (status < 0)
			break;

		status = ReadExtented(state, Regs);
		if (status < 0)
			break;

		*pCprog = Regs[EB14];

	} while (0);
	return status;
}

static int RFTrackingFiltersInit(struct tda_state *state,
				 u8 RFBand)
{
	int status = 0;

	u32   RF1 = m_RF_Band_Map[RFBand].m_RF1_Default;
	u32   RF2 = m_RF_Band_Map[RFBand].m_RF2_Default;
	u32   RF3 = m_RF_Band_Map[RFBand].m_RF3_Default;
	bool    bcal = false;

	s32    Cprog_cal1 = 0;
	s32    Cprog_table1 = 0;
	s32    Cprog_cal2 = 0;
	s32    Cprog_table2 = 0;
	s32    Cprog_cal3 = 0;
	s32    Cprog_table3 = 0;

	state->m_RF_A1[RFBand] = 0;
	state->m_RF_B1[RFBand] = 0;
	state->m_RF_A2[RFBand] = 0;
	state->m_RF_B2[RFBand] = 0;

	do {
		status = PowerScan(state, RFBand, RF1, &RF1, &bcal);
		if (status < 0)
			break;
		if (bcal) {
			status = CalibrateRF(state, RFBand, RF1, &Cprog_cal1);
			if (status < 0)
				break;
		}
		SearchMap2(m_RF_Cal_Map, RF1, &Cprog_table1);
		if (!bcal)
			Cprog_cal1 = Cprog_table1;
		state->m_RF_B1[RFBand] = Cprog_cal1 - Cprog_table1;
		/* state->m_RF_A1[RF_Band] = ???? */

		if (RF2 == 0)
			break;

		status = PowerScan(state, RFBand, RF2, &RF2, &bcal);
		if (status < 0)
			break;
		if (bcal) {
			status = CalibrateRF(state, RFBand, RF2, &Cprog_cal2);
			if (status < 0)
				break;
		}
		SearchMap2(m_RF_Cal_Map, RF2, &Cprog_table2);
		if (!bcal)
			Cprog_cal2 = Cprog_table2;

		state->m_RF_A1[RFBand] =
			(Cprog_cal2 - Cprog_table2 - Cprog_cal1 + Cprog_table1) /
			((s32)(RF2) - (s32)(RF1));

		if (RF3 == 0)
			break;

		status = PowerScan(state, RFBand, RF3, &RF3, &bcal);
		if (status < 0)
			break;
		if (bcal) {
			status = CalibrateRF(state, RFBand, RF3, &Cprog_cal3);
			if (status < 0)
				break;
		}
		SearchMap2(m_RF_Cal_Map, RF3, &Cprog_table3);
		if (!bcal)
			Cprog_cal3 = Cprog_table3;
		state->m_RF_A2[RFBand] = (Cprog_cal3 - Cprog_table3 - Cprog_cal2 + Cprog_table2) / ((s32)(RF3) - (s32)(RF2));
		state->m_RF_B2[RFBand] = Cprog_cal2 - Cprog_table2;

	} while (0);

	state->m_RF1[RFBand] = RF1;
	state->m_RF2[RFBand] = RF2;
	state->m_RF3[RFBand] = RF3;

#if 0
	printk(KERN_ERR "tda18271c2dd: %s %d RF1 = %d A1 = %d B1 = %d RF2 = %d A2 = %d B2 = %d RF3 = %d\n", __func__,
	       RFBand, RF1, state->m_RF_A1[RFBand], state->m_RF_B1[RFBand], RF2,
	       state->m_RF_A2[RFBand], state->m_RF_B2[RFBand], RF3);
#endif

	return status;
}

static int PowerScan(struct tda_state *state,
		     u8 RFBand, u32 RF_in, u32 *pRF_Out, bool *pbcal)
{
	int status = 0;
	do {
		u8   Gain_Taper = 0;
		s32  RFC_Cprog = 0;
		u8   CID_Target = 0;
		u8   CountLimit = 0;
		u32  freq_MainPLL;
		u8   Regs[NUM_REGS];
		u8   CID_Gain;
		s32  Count = 0;
		int  sign  = 1;
		bool wait = false;

		if (!(SearchMap2(m_RF_Cal_Map, RF_in, &RFC_Cprog) &&
		      SearchMap1(m_GainTaper_Map, RF_in, &Gain_Taper) &&
		      SearchMap3(m_CID_Target_Map, RF_in, &CID_Target, &CountLimit))) {

			printk(KERN_ERR "tda18271c2dd: %s Search map failed\n", __func__);
			return -EINVAL;
		}

		state->m_Regs[EP2] = (RFBand << 5) | Gain_Taper;
		state->m_Regs[EB14] = (RFC_Cprog);
		status = UpdateReg(state, EP2);
		if (status < 0)
			break;
		status = UpdateReg(state, EB14);
		if (status < 0)
			break;

		freq_MainPLL = RF_in + 1000000;
		status = CalcMainPLL(state, freq_MainPLL);
		if (status < 0)
			break;
		msleep(5);
		state->m_Regs[EP4] = (state->m_Regs[EP4] & ~0x03) | 1;    /* CAL_mode = 1 */
		status = UpdateReg(state, EP4);
		if (status < 0)
			break;
		status = UpdateReg(state, EP2);  /* Launch power measurement */
		if (status < 0)
			break;
		status = ReadExtented(state, Regs);
		if (status < 0)
			break;
		CID_Gain = Regs[EB10] & 0x3F;
		state->m_Regs[ID] = Regs[ID];  /* Chip version, (needed for C1 workarround in CalibrateRF) */

		*pRF_Out = RF_in;

		while (CID_Gain < CID_Target) {
			freq_MainPLL = RF_in + sign * Count + 1000000;
			status = CalcMainPLL(state, freq_MainPLL);
			if (status < 0)
				break;
			msleep(wait ? 5 : 1);
			wait = false;
			status = UpdateReg(state, EP2);  /* Launch power measurement */
			if (status < 0)
				break;
			status = ReadExtented(state, Regs);
			if (status < 0)
				break;
			CID_Gain = Regs[EB10] & 0x3F;
			Count += 200000;

			if (Count < CountLimit * 100000)
				continue;
			if (sign < 0)
				break;

			sign = -sign;
			Count = 200000;
			wait = true;
		}
		status = status;
		if (status < 0)
			break;
		if (CID_Gain >= CID_Target) {
			*pbcal = true;
			*pRF_Out = freq_MainPLL - 1000000;
		} else
			*pbcal = false;
	} while (0);

	return status;
}

static int PowerScanInit(struct tda_state *state)
{
	int status = 0;
	do {
		state->m_Regs[EP3] = (state->m_Regs[EP3] & ~0x1F) | 0x12;
		state->m_Regs[EP4] = (state->m_Regs[EP4] & ~0x1F); /* If level = 0, Cal mode = 0 */
		status = UpdateRegs(state, EP3, EP4);
		if (status < 0)
			break;
		state->m_Regs[EB18] = (state->m_Regs[EB18] & ~0x03); /* AGC 1 Gain = 0 */
		status = UpdateReg(state, EB18);
		if (status < 0)
			break;
		state->m_Regs[EB21] = (state->m_Regs[EB21] & ~0x03); /* AGC 2 Gain = 0 (Datasheet = 3) */
		state->m_Regs[EB23] = (state->m_Regs[EB23] | 0x06); /* ForceLP_Fc2_En = 1, LPFc[2] = 1 */
		status = UpdateRegs(state, EB21, EB23);
		if (status < 0)
			break;
	} while (0);
	return status;
}

static int CalcRFFilterCurve(struct tda_state *state)
{
	int status = 0;
	do {
		msleep(200);      /* Temperature stabilisation */
		status = PowerScanInit(state);
		if (status < 0)
			break;
		status = RFTrackingFiltersInit(state, 0);
		if (status < 0)
			break;
		status = RFTrackingFiltersInit(state, 1);
		if (status < 0)
			break;
		status = RFTrackingFiltersInit(state, 2);
		if (status < 0)
			break;
		status = RFTrackingFiltersInit(state, 3);
		if (status < 0)
			break;
		status = RFTrackingFiltersInit(state, 4);
		if (status < 0)
			break;
		status = RFTrackingFiltersInit(state, 5);
		if (status < 0)
			break;
		status = RFTrackingFiltersInit(state, 6);
		if (status < 0)
			break;
		status = ThermometerRead(state, &state->m_TMValue_RFCal); /* also switches off Cal mode !!! */
		if (status < 0)
			break;
	} while (0);

	return status;
}

static int FixedContentsI2CUpdate(struct tda_state *state)
{
	static u8 InitRegs[] = {
		0x08, 0x80, 0xC6,
		0xDF, 0x16, 0x60, 0x80,
		0x80, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0xFC, 0x01, 0x84, 0x41,
		0x01, 0x84, 0x40, 0x07,
		0x00, 0x00, 0x96, 0x3F,
		0xC1, 0x00, 0x8F, 0x00,
		0x00, 0x8C, 0x00, 0x20,
		0xB3, 0x48, 0xB0,
	};
	int status = 0;
	memcpy(&state->m_Regs[TM], InitRegs, EB23 - TM + 1);
	do {
		status = UpdateRegs(state, TM, EB23);
		if (status < 0)
			break;

		/* AGC1 gain setup */
		state->m_Regs[EB17] = 0x00;
		status = UpdateReg(state, EB17);
		if (status < 0)
			break;
		state->m_Regs[EB17] = 0x03;
		status = UpdateReg(state, EB17);
		if (status < 0)
			break;
		state->m_Regs[EB17] = 0x43;
		status = UpdateReg(state, EB17);
		if (status < 0)
			break;
		state->m_Regs[EB17] = 0x4C;
		status = UpdateReg(state, EB17);
		if (status < 0)
			break;

		/* IRC Cal Low band */
		state->m_Regs[EP3] = 0x1F;
		state->m_Regs[EP4] = 0x66;
		state->m_Regs[EP5] = 0x81;
		state->m_Regs[CPD] = 0xCC;
		state->m_Regs[CD1] = 0x6C;
		state->m_Regs[CD2] = 0x00;
		state->m_Regs[CD3] = 0x00;
		state->m_Regs[MPD] = 0xC5;
		state->m_Regs[MD1] = 0x77;
		state->m_Regs[MD2] = 0x08;
		state->m_Regs[MD3] = 0x00;
		status = UpdateRegs(state, EP2, MD3); /* diff between sw and datasheet (ep3-md3) */
		if (status < 0)
			break;

#if 0
		state->m_Regs[EB4] = 0x61;          /* missing in sw */
		status = UpdateReg(state, EB4);
		if (status < 0)
			break;
		msleep(1);
		state->m_Regs[EB4] = 0x41;
		status = UpdateReg(state, EB4);
		if (status < 0)
			break;
#endif

		msleep(5);
		status = UpdateReg(state, EP1);
		if (status < 0)
			break;
		msleep(5);

		state->m_Regs[EP5] = 0x85;
		state->m_Regs[CPD] = 0xCB;
		state->m_Regs[CD1] = 0x66;
		state->m_Regs[CD2] = 0x70;
		status = UpdateRegs(state, EP3, CD3);
		if (status < 0)
			break;
		msleep(5);
		status = UpdateReg(state, EP2);
		if (status < 0)
			break;
		msleep(30);

		/* IRC Cal mid band */
		state->m_Regs[EP5] = 0x82;
		state->m_Regs[CPD] = 0xA8;
		state->m_Regs[CD2] = 0x00;
		state->m_Regs[MPD] = 0xA1; /* Datasheet = 0xA9 */
		state->m_Regs[MD1] = 0x73;
		state->m_Regs[MD2] = 0x1A;
		status = UpdateRegs(state, EP3, MD3);
		if (status < 0)
			break;

		msleep(5);
		status = UpdateReg(state, EP1);
		if (status < 0)
			break;
		msleep(5);

		state->m_Regs[EP5] = 0x86;
		state->m_Regs[CPD] = 0xA8;
		state->m_Regs[CD1] = 0x66;
		state->m_Regs[CD2] = 0xA0;
		status = UpdateRegs(state, EP3, CD3);
		if (status < 0)
			break;
		msleep(5);
		status = UpdateReg(state, EP2);
		if (status < 0)
			break;
		msleep(30);

		/* IRC Cal high band */
		state->m_Regs[EP5] = 0x83;
		state->m_Regs[CPD] = 0x98;
		state->m_Regs[CD1] = 0x65;
		state->m_Regs[CD2] = 0x00;
		state->m_Regs[MPD] = 0x91;  /* Datasheet = 0x91 */
		state->m_Regs[MD1] = 0x71;
		state->m_Regs[MD2] = 0xCD;
		status = UpdateRegs(state, EP3, MD3);
		if (status < 0)
			break;
		msleep(5);
		status = UpdateReg(state, EP1);
		if (status < 0)
			break;
		msleep(5);
		state->m_Regs[EP5] = 0x87;
		state->m_Regs[CD1] = 0x65;
		state->m_Regs[CD2] = 0x50;
		status = UpdateRegs(state, EP3, CD3);
		if (status < 0)
			break;
		msleep(5);
		status = UpdateReg(state, EP2);
		if (status < 0)
			break;
		msleep(30);

		/* Back to normal */
		state->m_Regs[EP4] = 0x64;
		status = UpdateReg(state, EP4);
		if (status < 0)
			break;
		status = UpdateReg(state, EP1);
		if (status < 0)
			break;

	} while (0);
	return status;
}

static int InitCal(struct tda_state *state)
{
	int status = 0;

	do {
		status = FixedContentsI2CUpdate(state);
		if (status < 0)
			break;
		status = CalcRFFilterCurve(state);
		if (status < 0)
			break;
		status = StandBy(state);
		if (status < 0)
			break;
		/* m_bInitDone = true; */
	} while (0);
	return status;
};

static int RFTrackingFiltersCorrection(struct tda_state *state,
				       u32 Frequency)
{
	int status = 0;
	s32 Cprog_table;
	u8 RFBand;
	u8 dCoverdT;

	if (!SearchMap2(m_RF_Cal_Map, Frequency, &Cprog_table) ||
	    !SearchMap4(m_RF_Band_Map, Frequency, &RFBand) ||
	    !SearchMap1(m_RF_Cal_DC_Over_DT_Map, Frequency, &dCoverdT))

		return -EINVAL;

	do {
		u8 TMValue_Current;
		u32   RF1 = state->m_RF1[RFBand];
		u32   RF2 = state->m_RF1[RFBand];
		u32   RF3 = state->m_RF1[RFBand];
		s32    RF_A1 = state->m_RF_A1[RFBand];
		s32    RF_B1 = state->m_RF_B1[RFBand];
		s32    RF_A2 = state->m_RF_A2[RFBand];
		s32    RF_B2 = state->m_RF_B2[RFBand];
		s32 Capprox = 0;
		int TComp;

		state->m_Regs[EP3] &= ~0xE0;  /* Power up */
		status = UpdateReg(state, EP3);
		if (status < 0)
			break;

		status = ThermometerRead(state, &TMValue_Current);
		if (status < 0)
			break;

		if (RF3 == 0 || Frequency < RF2)
			Capprox = RF_A1 * ((s32)(Frequency) - (s32)(RF1)) + RF_B1 + Cprog_table;
		else
			Capprox = RF_A2 * ((s32)(Frequency) - (s32)(RF2)) + RF_B2 + Cprog_table;

		TComp = (int)(dCoverdT) * ((int)(TMValue_Current) - (int)(state->m_TMValue_RFCal))/1000;

		Capprox += TComp;

		if (Capprox < 0)
			Capprox = 0;
		else if (Capprox > 255)
			Capprox = 255;


		/* TODO Temperature compensation. There is defenitely a scale factor */
		/*      missing in the datasheet, so leave it out for now.           */
		state->m_Regs[EB14] = Capprox;

		status = UpdateReg(state, EB14);
		if (status < 0)
			break;

	} while (0);
	return status;
}

static int ChannelConfiguration(struct tda_state *state,
				u32 Frequency, int Standard)
{

	s32 IntermediateFrequency = m_StandardTable[Standard].m_IFFrequency;
	int status = 0;

	u8 BP_Filter = 0;
	u8 RF_Band = 0;
	u8 GainTaper = 0;
	u8 IR_Meas = 0;

	state->IF = IntermediateFrequency;
	/* printk("tda18271c2dd: %s Freq = %d Standard = %d IF = %d\n", __func__, Frequency, Standard, IntermediateFrequency); */
	/* get values from tables */

	if (!(SearchMap1(m_BP_Filter_Map, Frequency, &BP_Filter) &&
	       SearchMap1(m_GainTaper_Map, Frequency, &GainTaper) &&
	       SearchMap1(m_IR_Meas_Map, Frequency, &IR_Meas) &&
	       SearchMap4(m_RF_Band_Map, Frequency, &RF_Band))) {

		printk(KERN_ERR "tda18271c2dd: %s SearchMap failed\n", __func__);
		return -EINVAL;
	}

	do {
		state->m_Regs[EP3] = (state->m_Regs[EP3] & ~0x1F) | m_StandardTable[Standard].m_EP3_4_0;
		state->m_Regs[EP3] &= ~0x04;   /* switch RFAGC to high speed mode */

		/* m_EP4 default for XToutOn, CAL_Mode (0) */
		state->m_Regs[EP4] = state->m_EP4 | ((Standard > HF_AnalogMax) ? state->m_IFLevelDigital : state->m_IFLevelAnalog);
		/* state->m_Regs[EP4] = state->m_EP4 | state->m_IFLevelDigital; */
		if (Standard <= HF_AnalogMax)
			state->m_Regs[EP4] = state->m_EP4 | state->m_IFLevelAnalog;
		else if (Standard <= HF_ATSC)
			state->m_Regs[EP4] = state->m_EP4 | state->m_IFLevelDVBT;
		else if (Standard <= HF_DVBC)
			state->m_Regs[EP4] = state->m_EP4 | state->m_IFLevelDVBC;
		else
			state->m_Regs[EP4] = state->m_EP4 | state->m_IFLevelDigital;

		if ((Standard == HF_FM_Radio) && state->m_bFMInput)
			state->m_Regs[EP4] |= 0x80;

		state->m_Regs[MPD] &= ~0x80;
		if (Standard > HF_AnalogMax)
			state->m_Regs[MPD] |= 0x80; /* Add IF_notch for digital */

		state->m_Regs[EB22] = m_StandardTable[Standard].m_EB22;

		/* Note: This is missing from flowchart in TDA18271 specification ( 1.5 MHz cutoff for FM ) */
		if (Standard == HF_FM_Radio)
			state->m_Regs[EB23] |=  0x06; /* ForceLP_Fc2_En = 1, LPFc[2] = 1 */
		else
			state->m_Regs[EB23] &= ~0x06; /* ForceLP_Fc2_En = 0, LPFc[2] = 0 */

		status = UpdateRegs(state, EB22, EB23);
		if (status < 0)
			break;

		state->m_Regs[EP1] = (state->m_Regs[EP1] & ~0x07) | 0x40 | BP_Filter;   /* Dis_Power_level = 1, Filter */
		state->m_Regs[EP5] = (state->m_Regs[EP5] & ~0x07) | IR_Meas;
		state->m_Regs[EP2] = (RF_Band << 5) | GainTaper;

		state->m_Regs[EB1] = (state->m_Regs[EB1] & ~0x07) |
			(state->m_bMaster ? 0x04 : 0x00); /* CALVCO_FortLOn = MS */
		/* AGC1_always_master = 0 */
		/* AGC_firstn = 0 */
		status = UpdateReg(state, EB1);
		if (status < 0)
			break;

		if (state->m_bMaster) {
			status = CalcMainPLL(state, Frequency + IntermediateFrequency);
			if (status < 0)
				break;
			status = UpdateRegs(state, TM, EP5);
			if (status < 0)
				break;
			state->m_Regs[EB4] |= 0x20;    /* LO_forceSrce = 1 */
			status = UpdateReg(state, EB4);
			if (status < 0)
				break;
			msleep(1);
			state->m_Regs[EB4] &= ~0x20;   /* LO_forceSrce = 0 */
			status = UpdateReg(state, EB4);
			if (status < 0)
				break;
		} else {
			u8 PostDiv = 0;
			u8 Div;
			status = CalcCalPLL(state, Frequency + IntermediateFrequency);
			if (status < 0)
				break;

			SearchMap3(m_Cal_PLL_Map, Frequency + IntermediateFrequency, &PostDiv, &Div);
			state->m_Regs[MPD] = (state->m_Regs[MPD] & ~0x7F) | (PostDiv & 0x77);
			status = UpdateReg(state, MPD);
			if (status < 0)
				break;
			status = UpdateRegs(state, TM, EP5);
			if (status < 0)
				break;

			state->m_Regs[EB7] |= 0x20;    /* CAL_forceSrce = 1 */
			status = UpdateReg(state, EB7);
			if (status < 0)
				break;
			msleep(1);
			state->m_Regs[EB7] &= ~0x20;   /* CAL_forceSrce = 0 */
			status = UpdateReg(state, EB7);
			if (status < 0)
				break;
		}
		msleep(20);
		if (Standard != HF_FM_Radio)
			state->m_Regs[EP3] |= 0x04;    /* RFAGC to normal mode */
		status = UpdateReg(state, EP3);
		if (status < 0)
			break;

	} while (0);
	return status;
}

static int sleep(struct dvb_frontend *fe)
{
	struct tda_state *state = fe->tuner_priv;

	StandBy(state);
	return 0;
}

static int init(struct dvb_frontend *fe)
{
	return 0;
}

static void release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
}


static int set_params(struct dvb_frontend *fe)
{
	struct tda_state *state = fe->tuner_priv;
	int status = 0;
	int Standard;
	u32 bw = fe->dtv_property_cache.bandwidth_hz;
	u32 delsys  = fe->dtv_property_cache.delivery_system;

	state->m_Frequency = fe->dtv_property_cache.frequency;

	switch (delsys) {
	case  SYS_DVBT:
	case  SYS_DVBT2:
		switch (bw) {
		case 6000000:
			Standard = HF_DVBT_6MHZ;
			break;
		case 7000000:
			Standard = HF_DVBT_7MHZ;
			break;
		case 8000000:
			Standard = HF_DVBT_8MHZ;
			break;
		default:
			return -EINVAL;
		}
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_C:
		if (bw <= 6000000)
			Standard = HF_DVBC_6MHZ;
		else if (bw <= 7000000)
			Standard = HF_DVBC_7MHZ;
		else
			Standard = HF_DVBC_8MHZ;
		break;
	default:
		return -EINVAL;
	}
	do {
		status = RFTrackingFiltersCorrection(state, state->m_Frequency);
		if (status < 0)
			break;
		status = ChannelConfiguration(state, state->m_Frequency,
					      Standard);
		if (status < 0)
			break;

		msleep(state->m_SettlingTime);  /* Allow AGC's to settle down */
	} while (0);
	return status;
}

#if 0
static int GetSignalStrength(s32 *pSignalStrength, u32 RFAgc, u32 IFAgc)
{
	if (IFAgc < 500) {
		/* Scale this from 0 to 50000 */
		*pSignalStrength = IFAgc * 100;
	} else {
		/* Scale range 500-1500 to 50000-80000 */
		*pSignalStrength = 50000 + (IFAgc - 500) * 30;
	}

	return 0;
}
#endif

static int get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tda_state *state = fe->tuner_priv;

	*frequency = state->IF;
	return 0;
}

static int get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	/* struct tda_state *state = fe->tuner_priv; */
	/* *bandwidth = priv->bandwidth; */
	return 0;
}


static const struct dvb_tuner_ops tuner_ops = {
	.info = {
		.name = "NXP TDA18271C2D",
		.frequency_min  =  47125000,
		.frequency_max  = 865000000,
		.frequency_step =     62500
	},
	.init              = init,
	.sleep             = sleep,
	.set_params        = set_params,
	.release           = release,
	.get_if_frequency  = get_if_frequency,
	.get_bandwidth     = get_bandwidth,
};

struct dvb_frontend *tda18271c2dd_attach(struct dvb_frontend *fe,
					 struct i2c_adapter *i2c, u8 adr)
{
	struct tda_state *state;

	state = kzalloc(sizeof(struct tda_state), GFP_KERNEL);
	if (!state)
		return NULL;

	fe->tuner_priv = state;
	state->adr = adr;
	state->i2c = i2c;
	memcpy(&fe->ops.tuner_ops, &tuner_ops, sizeof(struct dvb_tuner_ops));
	reset(state);
	InitCal(state);

	return fe;
}
EXPORT_SYMBOL_GPL(tda18271c2dd_attach);

MODULE_DESCRIPTION("TDA18271C2 driver");
MODULE_AUTHOR("DD");
MODULE_LICENSE("GPL");
