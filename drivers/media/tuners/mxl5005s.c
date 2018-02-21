/*
    MaxLinear MXL5005S VSB/QAM/DVBT tuner driver

    Copyright (C) 2008 MaxLinear
    Copyright (C) 2006 Steven Toth <stoth@linuxtv.org>
      Functions:
	mxl5005s_reset()
	mxl5005s_writereg()
	mxl5005s_writeregs()
	mxl5005s_init()
	mxl5005s_reconfigure()
	mxl5005s_AssignTunerMode()
	mxl5005s_set_params()
	mxl5005s_get_frequency()
	mxl5005s_get_bandwidth()
	mxl5005s_release()
	mxl5005s_attach()

    Copyright (C) 2008 Realtek
    Copyright (C) 2008 Jan Hoogenraad
      Functions:
	mxl5005s_SetRfFreqHz()

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

/*
    History of this driver (Steven Toth):
      I was given a public release of a linux driver that included
      support for the MaxLinear MXL5005S silicon tuner. Analysis of
      the tuner driver showed clearly three things.

      1. The tuner driver didn't support the LinuxTV tuner API
	 so the code Realtek added had to be removed.

      2. A significant amount of the driver is reference driver code
	 from MaxLinear, I felt it was important to identify and
	 preserve this.

      3. New code has to be added to interface correctly with the
	 LinuxTV API, as a regular kernel module.

      Other than the reference driver enum's, I've clearly marked
      sections of the code and retained the copyright of the
      respective owners.
*/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <media/dvb_frontend.h>
#include "mxl5005s.h"

static int debug;

#define dprintk(level, arg...) do {    \
	if (level <= debug)            \
		printk(arg);    \
	} while (0)

#define TUNER_REGS_NUM          104
#define INITCTRL_NUM            40

#ifdef _MXL_PRODUCTION
#define CHCTRL_NUM              39
#else
#define CHCTRL_NUM              36
#endif

#define MXLCTRL_NUM             189
#define MASTER_CONTROL_ADDR     9

/* Enumeration of Master Control Register State */
enum master_control_state {
	MC_LOAD_START = 1,
	MC_POWER_DOWN,
	MC_SYNTH_RESET,
	MC_SEQ_OFF
};

/* Enumeration of MXL5005 Tuner Modulation Type */
enum {
	MXL_DEFAULT_MODULATION = 0,
	MXL_DVBT,
	MXL_ATSC,
	MXL_QAM,
	MXL_ANALOG_CABLE,
	MXL_ANALOG_OTA
};

/* MXL5005 Tuner Register Struct */
struct TunerReg {
	u16 Reg_Num;	/* Tuner Register Address */
	u16 Reg_Val;	/* Current sw programmed value waiting to be written */
};

enum {
	/* Initialization Control Names */
	DN_IQTN_AMP_CUT = 1,       /* 1 */
	BB_MODE,                   /* 2 */
	BB_BUF,                    /* 3 */
	BB_BUF_OA,                 /* 4 */
	BB_ALPF_BANDSELECT,        /* 5 */
	BB_IQSWAP,                 /* 6 */
	BB_DLPF_BANDSEL,           /* 7 */
	RFSYN_CHP_GAIN,            /* 8 */
	RFSYN_EN_CHP_HIGAIN,       /* 9 */
	AGC_IF,                    /* 10 */
	AGC_RF,                    /* 11 */
	IF_DIVVAL,                 /* 12 */
	IF_VCO_BIAS,               /* 13 */
	CHCAL_INT_MOD_IF,          /* 14 */
	CHCAL_FRAC_MOD_IF,         /* 15 */
	DRV_RES_SEL,               /* 16 */
	I_DRIVER,                  /* 17 */
	EN_AAF,                    /* 18 */
	EN_3P,                     /* 19 */
	EN_AUX_3P,                 /* 20 */
	SEL_AAF_BAND,              /* 21 */
	SEQ_ENCLK16_CLK_OUT,       /* 22 */
	SEQ_SEL4_16B,              /* 23 */
	XTAL_CAPSELECT,            /* 24 */
	IF_SEL_DBL,                /* 25 */
	RFSYN_R_DIV,               /* 26 */
	SEQ_EXTSYNTHCALIF,         /* 27 */
	SEQ_EXTDCCAL,              /* 28 */
	AGC_EN_RSSI,               /* 29 */
	RFA_ENCLKRFAGC,            /* 30 */
	RFA_RSSI_REFH,             /* 31 */
	RFA_RSSI_REF,              /* 32 */
	RFA_RSSI_REFL,             /* 33 */
	RFA_FLR,                   /* 34 */
	RFA_CEIL,                  /* 35 */
	SEQ_EXTIQFSMPULSE,         /* 36 */
	OVERRIDE_1,                /* 37 */
	BB_INITSTATE_DLPF_TUNE,    /* 38 */
	TG_R_DIV,                  /* 39 */
	EN_CHP_LIN_B,              /* 40 */

	/* Channel Change Control Names */
	DN_POLY = 51,              /* 51 */
	DN_RFGAIN,                 /* 52 */
	DN_CAP_RFLPF,              /* 53 */
	DN_EN_VHFUHFBAR,           /* 54 */
	DN_GAIN_ADJUST,            /* 55 */
	DN_IQTNBUF_AMP,            /* 56 */
	DN_IQTNGNBFBIAS_BST,       /* 57 */
	RFSYN_EN_OUTMUX,           /* 58 */
	RFSYN_SEL_VCO_OUT,         /* 59 */
	RFSYN_SEL_VCO_HI,          /* 60 */
	RFSYN_SEL_DIVM,            /* 61 */
	RFSYN_RF_DIV_BIAS,         /* 62 */
	DN_SEL_FREQ,               /* 63 */
	RFSYN_VCO_BIAS,            /* 64 */
	CHCAL_INT_MOD_RF,          /* 65 */
	CHCAL_FRAC_MOD_RF,         /* 66 */
	RFSYN_LPF_R,               /* 67 */
	CHCAL_EN_INT_RF,           /* 68 */
	TG_LO_DIVVAL,              /* 69 */
	TG_LO_SELVAL,              /* 70 */
	TG_DIV_VAL,                /* 71 */
	TG_VCO_BIAS,               /* 72 */
	SEQ_EXTPOWERUP,            /* 73 */
	OVERRIDE_2,                /* 74 */
	OVERRIDE_3,                /* 75 */
	OVERRIDE_4,                /* 76 */
	SEQ_FSM_PULSE,             /* 77 */
	GPIO_4B,                   /* 78 */
	GPIO_3B,                   /* 79 */
	GPIO_4,                    /* 80 */
	GPIO_3,                    /* 81 */
	GPIO_1B,                   /* 82 */
	DAC_A_ENABLE,              /* 83 */
	DAC_B_ENABLE,              /* 84 */
	DAC_DIN_A,                 /* 85 */
	DAC_DIN_B,                 /* 86 */
#ifdef _MXL_PRODUCTION
	RFSYN_EN_DIV,              /* 87 */
	RFSYN_DIVM,                /* 88 */
	DN_BYPASS_AGC_I2C          /* 89 */
#endif
};

/*
 * The following context is source code provided by MaxLinear.
 * MaxLinear source code - Common_MXL.h (?)
 */

/* Constants */
#define MXL5005S_REG_WRITING_TABLE_LEN_MAX	104
#define MXL5005S_LATCH_BYTE			0xfe

/* Register address, MSB, and LSB */
#define MXL5005S_BB_IQSWAP_ADDR			59
#define MXL5005S_BB_IQSWAP_MSB			0
#define MXL5005S_BB_IQSWAP_LSB			0

#define MXL5005S_BB_DLPF_BANDSEL_ADDR		53
#define MXL5005S_BB_DLPF_BANDSEL_MSB		4
#define MXL5005S_BB_DLPF_BANDSEL_LSB		3

/* Standard modes */
enum {
	MXL5005S_STANDARD_DVBT,
	MXL5005S_STANDARD_ATSC,
};
#define MXL5005S_STANDARD_MODE_NUM		2

/* Bandwidth modes */
enum {
	MXL5005S_BANDWIDTH_6MHZ = 6000000,
	MXL5005S_BANDWIDTH_7MHZ = 7000000,
	MXL5005S_BANDWIDTH_8MHZ = 8000000,
};
#define MXL5005S_BANDWIDTH_MODE_NUM		3

/* MXL5005 Tuner Control Struct */
struct TunerControl {
	u16 Ctrl_Num;	/* Control Number */
	u16 size;	/* Number of bits to represent Value */
	u16 addr[25];	/* Array of Tuner Register Address for each bit pos */
	u16 bit[25];	/* Array of bit pos in Reg Addr for each bit pos */
	u16 val[25];	/* Binary representation of Value */
};

/* MXL5005 Tuner Struct */
struct mxl5005s_state {
	u8	Mode;		/* 0: Analog Mode ; 1: Digital Mode */
	u8	IF_Mode;	/* for Analog Mode, 0: zero IF; 1: low IF */
	u32	Chan_Bandwidth;	/* filter  channel bandwidth (6, 7, 8) */
	u32	IF_OUT;		/* Desired IF Out Frequency */
	u16	IF_OUT_LOAD;	/* IF Out Load Resistor (200/300 Ohms) */
	u32	RF_IN;		/* RF Input Frequency */
	u32	Fxtal;		/* XTAL Frequency */
	u8	AGC_Mode;	/* AGC Mode 0: Dual AGC; 1: Single AGC */
	u16	TOP;		/* Value: take over point */
	u8	CLOCK_OUT;	/* 0: turn off clk out; 1: turn on clock out */
	u8	DIV_OUT;	/* 4MHz or 16MHz */
	u8	CAPSELECT;	/* 0: disable On-Chip pulling cap; 1: enable */
	u8	EN_RSSI;	/* 0: disable RSSI; 1: enable RSSI */

	/* Modulation Type; */
	/* 0 - Default;	1 - DVB-T; 2 - ATSC; 3 - QAM; 4 - Analog Cable */
	u8	Mod_Type;

	/* Tracking Filter Type */
	/* 0 - Default; 1 - Off; 2 - Type C; 3 - Type C-H */
	u8	TF_Type;

	/* Calculated Settings */
	u32	RF_LO;		/* Synth RF LO Frequency */
	u32	IF_LO;		/* Synth IF LO Frequency */
	u32	TG_LO;		/* Synth TG_LO Frequency */

	/* Pointers to ControlName Arrays */
	u16	Init_Ctrl_Num;		/* Number of INIT Control Names */
	struct TunerControl
		Init_Ctrl[INITCTRL_NUM]; /* INIT Control Names Array Pointer */

	u16	CH_Ctrl_Num;		/* Number of CH Control Names */
	struct TunerControl
		CH_Ctrl[CHCTRL_NUM];	/* CH Control Name Array Pointer */

	u16	MXL_Ctrl_Num;		/* Number of MXL Control Names */
	struct TunerControl
		MXL_Ctrl[MXLCTRL_NUM];	/* MXL Control Name Array Pointer */

	/* Pointer to Tuner Register Array */
	u16	TunerRegs_Num;		/* Number of Tuner Registers */
	struct TunerReg
		TunerRegs[TUNER_REGS_NUM]; /* Tuner Register Array Pointer */

	/* Linux driver framework specific */
	struct mxl5005s_config *config;
	struct dvb_frontend *frontend;
	struct i2c_adapter *i2c;

	/* Cache values */
	u32 current_mode;

};

static u16 MXL_GetMasterControl(u8 *MasterReg, int state);
static u16 MXL_ControlWrite(struct dvb_frontend *fe, u16 ControlNum, u32 value);
static u16 MXL_ControlRead(struct dvb_frontend *fe, u16 controlNum, u32 *value);
static void MXL_RegWriteBit(struct dvb_frontend *fe, u8 address, u8 bit,
	u8 bitVal);
static u16 MXL_GetCHRegister(struct dvb_frontend *fe, u8 *RegNum,
	u8 *RegVal, int *count);
static u32 MXL_Ceiling(u32 value, u32 resolution);
static u16 MXL_RegRead(struct dvb_frontend *fe, u8 RegNum, u8 *RegVal);
static u16 MXL_ControlWrite_Group(struct dvb_frontend *fe, u16 controlNum,
	u32 value, u16 controlGroup);
static u16 MXL_SetGPIO(struct dvb_frontend *fe, u8 GPIO_Num, u8 GPIO_Val);
static u16 MXL_GetInitRegister(struct dvb_frontend *fe, u8 *RegNum,
	u8 *RegVal, int *count);
static u16 MXL_TuneRF(struct dvb_frontend *fe, u32 RF_Freq);
static void MXL_SynthIFLO_Calc(struct dvb_frontend *fe);
static void MXL_SynthRFTGLO_Calc(struct dvb_frontend *fe);
static u16 MXL_GetCHRegister_ZeroIF(struct dvb_frontend *fe, u8 *RegNum,
	u8 *RegVal, int *count);
static int mxl5005s_writeregs(struct dvb_frontend *fe, u8 *addrtable,
	u8 *datatable, u8 len);
static u16 MXL_IFSynthInit(struct dvb_frontend *fe);
static int mxl5005s_AssignTunerMode(struct dvb_frontend *fe, u32 mod_type,
	u32 bandwidth);
static int mxl5005s_reconfigure(struct dvb_frontend *fe, u32 mod_type,
	u32 bandwidth);

/* ----------------------------------------------------------------
 * Begin: Custom code salvaged from the Realtek driver.
 * Copyright (C) 2008 Realtek
 * Copyright (C) 2008 Jan Hoogenraad
 * This code is placed under the terms of the GNU General Public License
 *
 * Released by Realtek under GPLv2.
 * Thanks to Realtek for a lot of support we received !
 *
 *  Revision: 080314 - original version
 */

static int mxl5005s_SetRfFreqHz(struct dvb_frontend *fe, unsigned long RfFreqHz)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	unsigned char AddrTable[MXL5005S_REG_WRITING_TABLE_LEN_MAX];
	unsigned char ByteTable[MXL5005S_REG_WRITING_TABLE_LEN_MAX];
	int TableLen;

	u32 IfDivval = 0;
	unsigned char MasterControlByte;

	dprintk(1, "%s() freq=%ld\n", __func__, RfFreqHz);

	/* Set MxL5005S tuner RF frequency according to example code. */

	/* Tuner RF frequency setting stage 0 */
	MXL_GetMasterControl(ByteTable, MC_SYNTH_RESET);
	AddrTable[0] = MASTER_CONTROL_ADDR;
	ByteTable[0] |= state->config->AgcMasterByte;

	mxl5005s_writeregs(fe, AddrTable, ByteTable, 1);

	/* Tuner RF frequency setting stage 1 */
	MXL_TuneRF(fe, RfFreqHz);

	MXL_ControlRead(fe, IF_DIVVAL, &IfDivval);

	MXL_ControlWrite(fe, SEQ_FSM_PULSE, 0);
	MXL_ControlWrite(fe, SEQ_EXTPOWERUP, 1);
	MXL_ControlWrite(fe, IF_DIVVAL, 8);
	MXL_GetCHRegister(fe, AddrTable, ByteTable, &TableLen);

	MXL_GetMasterControl(&MasterControlByte, MC_LOAD_START);
	AddrTable[TableLen] = MASTER_CONTROL_ADDR ;
	ByteTable[TableLen] = MasterControlByte |
		state->config->AgcMasterByte;
	TableLen += 1;

	mxl5005s_writeregs(fe, AddrTable, ByteTable, TableLen);

	/* Wait 30 ms. */
	msleep(150);

	/* Tuner RF frequency setting stage 2 */
	MXL_ControlWrite(fe, SEQ_FSM_PULSE, 1);
	MXL_ControlWrite(fe, IF_DIVVAL, IfDivval);
	MXL_GetCHRegister_ZeroIF(fe, AddrTable, ByteTable, &TableLen);

	MXL_GetMasterControl(&MasterControlByte, MC_LOAD_START);
	AddrTable[TableLen] = MASTER_CONTROL_ADDR ;
	ByteTable[TableLen] = MasterControlByte |
		state->config->AgcMasterByte ;
	TableLen += 1;

	mxl5005s_writeregs(fe, AddrTable, ByteTable, TableLen);

	msleep(100);

	return 0;
}
/* End: Custom code taken from the Realtek driver */

/* ----------------------------------------------------------------
 * Begin: Reference driver code found in the Realtek driver.
 * Copyright (C) 2008 MaxLinear
 */
static u16 MXL5005_RegisterInit(struct dvb_frontend *fe)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	state->TunerRegs_Num = TUNER_REGS_NUM ;

	state->TunerRegs[0].Reg_Num = 9 ;
	state->TunerRegs[0].Reg_Val = 0x40 ;

	state->TunerRegs[1].Reg_Num = 11 ;
	state->TunerRegs[1].Reg_Val = 0x19 ;

	state->TunerRegs[2].Reg_Num = 12 ;
	state->TunerRegs[2].Reg_Val = 0x60 ;

	state->TunerRegs[3].Reg_Num = 13 ;
	state->TunerRegs[3].Reg_Val = 0x00 ;

	state->TunerRegs[4].Reg_Num = 14 ;
	state->TunerRegs[4].Reg_Val = 0x00 ;

	state->TunerRegs[5].Reg_Num = 15 ;
	state->TunerRegs[5].Reg_Val = 0xC0 ;

	state->TunerRegs[6].Reg_Num = 16 ;
	state->TunerRegs[6].Reg_Val = 0x00 ;

	state->TunerRegs[7].Reg_Num = 17 ;
	state->TunerRegs[7].Reg_Val = 0x00 ;

	state->TunerRegs[8].Reg_Num = 18 ;
	state->TunerRegs[8].Reg_Val = 0x00 ;

	state->TunerRegs[9].Reg_Num = 19 ;
	state->TunerRegs[9].Reg_Val = 0x34 ;

	state->TunerRegs[10].Reg_Num = 21 ;
	state->TunerRegs[10].Reg_Val = 0x00 ;

	state->TunerRegs[11].Reg_Num = 22 ;
	state->TunerRegs[11].Reg_Val = 0x6B ;

	state->TunerRegs[12].Reg_Num = 23 ;
	state->TunerRegs[12].Reg_Val = 0x35 ;

	state->TunerRegs[13].Reg_Num = 24 ;
	state->TunerRegs[13].Reg_Val = 0x70 ;

	state->TunerRegs[14].Reg_Num = 25 ;
	state->TunerRegs[14].Reg_Val = 0x3E ;

	state->TunerRegs[15].Reg_Num = 26 ;
	state->TunerRegs[15].Reg_Val = 0x82 ;

	state->TunerRegs[16].Reg_Num = 31 ;
	state->TunerRegs[16].Reg_Val = 0x00 ;

	state->TunerRegs[17].Reg_Num = 32 ;
	state->TunerRegs[17].Reg_Val = 0x40 ;

	state->TunerRegs[18].Reg_Num = 33 ;
	state->TunerRegs[18].Reg_Val = 0x53 ;

	state->TunerRegs[19].Reg_Num = 34 ;
	state->TunerRegs[19].Reg_Val = 0x81 ;

	state->TunerRegs[20].Reg_Num = 35 ;
	state->TunerRegs[20].Reg_Val = 0xC9 ;

	state->TunerRegs[21].Reg_Num = 36 ;
	state->TunerRegs[21].Reg_Val = 0x01 ;

	state->TunerRegs[22].Reg_Num = 37 ;
	state->TunerRegs[22].Reg_Val = 0x00 ;

	state->TunerRegs[23].Reg_Num = 41 ;
	state->TunerRegs[23].Reg_Val = 0x00 ;

	state->TunerRegs[24].Reg_Num = 42 ;
	state->TunerRegs[24].Reg_Val = 0xF8 ;

	state->TunerRegs[25].Reg_Num = 43 ;
	state->TunerRegs[25].Reg_Val = 0x43 ;

	state->TunerRegs[26].Reg_Num = 44 ;
	state->TunerRegs[26].Reg_Val = 0x20 ;

	state->TunerRegs[27].Reg_Num = 45 ;
	state->TunerRegs[27].Reg_Val = 0x80 ;

	state->TunerRegs[28].Reg_Num = 46 ;
	state->TunerRegs[28].Reg_Val = 0x88 ;

	state->TunerRegs[29].Reg_Num = 47 ;
	state->TunerRegs[29].Reg_Val = 0x86 ;

	state->TunerRegs[30].Reg_Num = 48 ;
	state->TunerRegs[30].Reg_Val = 0x00 ;

	state->TunerRegs[31].Reg_Num = 49 ;
	state->TunerRegs[31].Reg_Val = 0x00 ;

	state->TunerRegs[32].Reg_Num = 53 ;
	state->TunerRegs[32].Reg_Val = 0x94 ;

	state->TunerRegs[33].Reg_Num = 54 ;
	state->TunerRegs[33].Reg_Val = 0xFA ;

	state->TunerRegs[34].Reg_Num = 55 ;
	state->TunerRegs[34].Reg_Val = 0x92 ;

	state->TunerRegs[35].Reg_Num = 56 ;
	state->TunerRegs[35].Reg_Val = 0x80 ;

	state->TunerRegs[36].Reg_Num = 57 ;
	state->TunerRegs[36].Reg_Val = 0x41 ;

	state->TunerRegs[37].Reg_Num = 58 ;
	state->TunerRegs[37].Reg_Val = 0xDB ;

	state->TunerRegs[38].Reg_Num = 59 ;
	state->TunerRegs[38].Reg_Val = 0x00 ;

	state->TunerRegs[39].Reg_Num = 60 ;
	state->TunerRegs[39].Reg_Val = 0x00 ;

	state->TunerRegs[40].Reg_Num = 61 ;
	state->TunerRegs[40].Reg_Val = 0x00 ;

	state->TunerRegs[41].Reg_Num = 62 ;
	state->TunerRegs[41].Reg_Val = 0x00 ;

	state->TunerRegs[42].Reg_Num = 65 ;
	state->TunerRegs[42].Reg_Val = 0xF8 ;

	state->TunerRegs[43].Reg_Num = 66 ;
	state->TunerRegs[43].Reg_Val = 0xE4 ;

	state->TunerRegs[44].Reg_Num = 67 ;
	state->TunerRegs[44].Reg_Val = 0x90 ;

	state->TunerRegs[45].Reg_Num = 68 ;
	state->TunerRegs[45].Reg_Val = 0xC0 ;

	state->TunerRegs[46].Reg_Num = 69 ;
	state->TunerRegs[46].Reg_Val = 0x01 ;

	state->TunerRegs[47].Reg_Num = 70 ;
	state->TunerRegs[47].Reg_Val = 0x50 ;

	state->TunerRegs[48].Reg_Num = 71 ;
	state->TunerRegs[48].Reg_Val = 0x06 ;

	state->TunerRegs[49].Reg_Num = 72 ;
	state->TunerRegs[49].Reg_Val = 0x00 ;

	state->TunerRegs[50].Reg_Num = 73 ;
	state->TunerRegs[50].Reg_Val = 0x20 ;

	state->TunerRegs[51].Reg_Num = 76 ;
	state->TunerRegs[51].Reg_Val = 0xBB ;

	state->TunerRegs[52].Reg_Num = 77 ;
	state->TunerRegs[52].Reg_Val = 0x13 ;

	state->TunerRegs[53].Reg_Num = 81 ;
	state->TunerRegs[53].Reg_Val = 0x04 ;

	state->TunerRegs[54].Reg_Num = 82 ;
	state->TunerRegs[54].Reg_Val = 0x75 ;

	state->TunerRegs[55].Reg_Num = 83 ;
	state->TunerRegs[55].Reg_Val = 0x00 ;

	state->TunerRegs[56].Reg_Num = 84 ;
	state->TunerRegs[56].Reg_Val = 0x00 ;

	state->TunerRegs[57].Reg_Num = 85 ;
	state->TunerRegs[57].Reg_Val = 0x00 ;

	state->TunerRegs[58].Reg_Num = 91 ;
	state->TunerRegs[58].Reg_Val = 0x70 ;

	state->TunerRegs[59].Reg_Num = 92 ;
	state->TunerRegs[59].Reg_Val = 0x00 ;

	state->TunerRegs[60].Reg_Num = 93 ;
	state->TunerRegs[60].Reg_Val = 0x00 ;

	state->TunerRegs[61].Reg_Num = 94 ;
	state->TunerRegs[61].Reg_Val = 0x00 ;

	state->TunerRegs[62].Reg_Num = 95 ;
	state->TunerRegs[62].Reg_Val = 0x0C ;

	state->TunerRegs[63].Reg_Num = 96 ;
	state->TunerRegs[63].Reg_Val = 0x00 ;

	state->TunerRegs[64].Reg_Num = 97 ;
	state->TunerRegs[64].Reg_Val = 0x00 ;

	state->TunerRegs[65].Reg_Num = 98 ;
	state->TunerRegs[65].Reg_Val = 0xE2 ;

	state->TunerRegs[66].Reg_Num = 99 ;
	state->TunerRegs[66].Reg_Val = 0x00 ;

	state->TunerRegs[67].Reg_Num = 100 ;
	state->TunerRegs[67].Reg_Val = 0x00 ;

	state->TunerRegs[68].Reg_Num = 101 ;
	state->TunerRegs[68].Reg_Val = 0x12 ;

	state->TunerRegs[69].Reg_Num = 102 ;
	state->TunerRegs[69].Reg_Val = 0x80 ;

	state->TunerRegs[70].Reg_Num = 103 ;
	state->TunerRegs[70].Reg_Val = 0x32 ;

	state->TunerRegs[71].Reg_Num = 104 ;
	state->TunerRegs[71].Reg_Val = 0xB4 ;

	state->TunerRegs[72].Reg_Num = 105 ;
	state->TunerRegs[72].Reg_Val = 0x60 ;

	state->TunerRegs[73].Reg_Num = 106 ;
	state->TunerRegs[73].Reg_Val = 0x83 ;

	state->TunerRegs[74].Reg_Num = 107 ;
	state->TunerRegs[74].Reg_Val = 0x84 ;

	state->TunerRegs[75].Reg_Num = 108 ;
	state->TunerRegs[75].Reg_Val = 0x9C ;

	state->TunerRegs[76].Reg_Num = 109 ;
	state->TunerRegs[76].Reg_Val = 0x02 ;

	state->TunerRegs[77].Reg_Num = 110 ;
	state->TunerRegs[77].Reg_Val = 0x81 ;

	state->TunerRegs[78].Reg_Num = 111 ;
	state->TunerRegs[78].Reg_Val = 0xC0 ;

	state->TunerRegs[79].Reg_Num = 112 ;
	state->TunerRegs[79].Reg_Val = 0x10 ;

	state->TunerRegs[80].Reg_Num = 131 ;
	state->TunerRegs[80].Reg_Val = 0x8A ;

	state->TunerRegs[81].Reg_Num = 132 ;
	state->TunerRegs[81].Reg_Val = 0x10 ;

	state->TunerRegs[82].Reg_Num = 133 ;
	state->TunerRegs[82].Reg_Val = 0x24 ;

	state->TunerRegs[83].Reg_Num = 134 ;
	state->TunerRegs[83].Reg_Val = 0x00 ;

	state->TunerRegs[84].Reg_Num = 135 ;
	state->TunerRegs[84].Reg_Val = 0x00 ;

	state->TunerRegs[85].Reg_Num = 136 ;
	state->TunerRegs[85].Reg_Val = 0x7E ;

	state->TunerRegs[86].Reg_Num = 137 ;
	state->TunerRegs[86].Reg_Val = 0x40 ;

	state->TunerRegs[87].Reg_Num = 138 ;
	state->TunerRegs[87].Reg_Val = 0x38 ;

	state->TunerRegs[88].Reg_Num = 146 ;
	state->TunerRegs[88].Reg_Val = 0xF6 ;

	state->TunerRegs[89].Reg_Num = 147 ;
	state->TunerRegs[89].Reg_Val = 0x1A ;

	state->TunerRegs[90].Reg_Num = 148 ;
	state->TunerRegs[90].Reg_Val = 0x62 ;

	state->TunerRegs[91].Reg_Num = 149 ;
	state->TunerRegs[91].Reg_Val = 0x33 ;

	state->TunerRegs[92].Reg_Num = 150 ;
	state->TunerRegs[92].Reg_Val = 0x80 ;

	state->TunerRegs[93].Reg_Num = 156 ;
	state->TunerRegs[93].Reg_Val = 0x56 ;

	state->TunerRegs[94].Reg_Num = 157 ;
	state->TunerRegs[94].Reg_Val = 0x17 ;

	state->TunerRegs[95].Reg_Num = 158 ;
	state->TunerRegs[95].Reg_Val = 0xA9 ;

	state->TunerRegs[96].Reg_Num = 159 ;
	state->TunerRegs[96].Reg_Val = 0x00 ;

	state->TunerRegs[97].Reg_Num = 160 ;
	state->TunerRegs[97].Reg_Val = 0x00 ;

	state->TunerRegs[98].Reg_Num = 161 ;
	state->TunerRegs[98].Reg_Val = 0x00 ;

	state->TunerRegs[99].Reg_Num = 162 ;
	state->TunerRegs[99].Reg_Val = 0x40 ;

	state->TunerRegs[100].Reg_Num = 166 ;
	state->TunerRegs[100].Reg_Val = 0xAE ;

	state->TunerRegs[101].Reg_Num = 167 ;
	state->TunerRegs[101].Reg_Val = 0x1B ;

	state->TunerRegs[102].Reg_Num = 168 ;
	state->TunerRegs[102].Reg_Val = 0xF2 ;

	state->TunerRegs[103].Reg_Num = 195 ;
	state->TunerRegs[103].Reg_Val = 0x00 ;

	return 0 ;
}

static u16 MXL5005_ControlInit(struct dvb_frontend *fe)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	state->Init_Ctrl_Num = INITCTRL_NUM;

	state->Init_Ctrl[0].Ctrl_Num = DN_IQTN_AMP_CUT ;
	state->Init_Ctrl[0].size = 1 ;
	state->Init_Ctrl[0].addr[0] = 73;
	state->Init_Ctrl[0].bit[0] = 7;
	state->Init_Ctrl[0].val[0] = 0;

	state->Init_Ctrl[1].Ctrl_Num = BB_MODE ;
	state->Init_Ctrl[1].size = 1 ;
	state->Init_Ctrl[1].addr[0] = 53;
	state->Init_Ctrl[1].bit[0] = 2;
	state->Init_Ctrl[1].val[0] = 1;

	state->Init_Ctrl[2].Ctrl_Num = BB_BUF ;
	state->Init_Ctrl[2].size = 2 ;
	state->Init_Ctrl[2].addr[0] = 53;
	state->Init_Ctrl[2].bit[0] = 1;
	state->Init_Ctrl[2].val[0] = 0;
	state->Init_Ctrl[2].addr[1] = 57;
	state->Init_Ctrl[2].bit[1] = 0;
	state->Init_Ctrl[2].val[1] = 1;

	state->Init_Ctrl[3].Ctrl_Num = BB_BUF_OA ;
	state->Init_Ctrl[3].size = 1 ;
	state->Init_Ctrl[3].addr[0] = 53;
	state->Init_Ctrl[3].bit[0] = 0;
	state->Init_Ctrl[3].val[0] = 0;

	state->Init_Ctrl[4].Ctrl_Num = BB_ALPF_BANDSELECT ;
	state->Init_Ctrl[4].size = 3 ;
	state->Init_Ctrl[4].addr[0] = 53;
	state->Init_Ctrl[4].bit[0] = 5;
	state->Init_Ctrl[4].val[0] = 0;
	state->Init_Ctrl[4].addr[1] = 53;
	state->Init_Ctrl[4].bit[1] = 6;
	state->Init_Ctrl[4].val[1] = 0;
	state->Init_Ctrl[4].addr[2] = 53;
	state->Init_Ctrl[4].bit[2] = 7;
	state->Init_Ctrl[4].val[2] = 1;

	state->Init_Ctrl[5].Ctrl_Num = BB_IQSWAP ;
	state->Init_Ctrl[5].size = 1 ;
	state->Init_Ctrl[5].addr[0] = 59;
	state->Init_Ctrl[5].bit[0] = 0;
	state->Init_Ctrl[5].val[0] = 0;

	state->Init_Ctrl[6].Ctrl_Num = BB_DLPF_BANDSEL ;
	state->Init_Ctrl[6].size = 2 ;
	state->Init_Ctrl[6].addr[0] = 53;
	state->Init_Ctrl[6].bit[0] = 3;
	state->Init_Ctrl[6].val[0] = 0;
	state->Init_Ctrl[6].addr[1] = 53;
	state->Init_Ctrl[6].bit[1] = 4;
	state->Init_Ctrl[6].val[1] = 1;

	state->Init_Ctrl[7].Ctrl_Num = RFSYN_CHP_GAIN ;
	state->Init_Ctrl[7].size = 4 ;
	state->Init_Ctrl[7].addr[0] = 22;
	state->Init_Ctrl[7].bit[0] = 4;
	state->Init_Ctrl[7].val[0] = 0;
	state->Init_Ctrl[7].addr[1] = 22;
	state->Init_Ctrl[7].bit[1] = 5;
	state->Init_Ctrl[7].val[1] = 1;
	state->Init_Ctrl[7].addr[2] = 22;
	state->Init_Ctrl[7].bit[2] = 6;
	state->Init_Ctrl[7].val[2] = 1;
	state->Init_Ctrl[7].addr[3] = 22;
	state->Init_Ctrl[7].bit[3] = 7;
	state->Init_Ctrl[7].val[3] = 0;

	state->Init_Ctrl[8].Ctrl_Num = RFSYN_EN_CHP_HIGAIN ;
	state->Init_Ctrl[8].size = 1 ;
	state->Init_Ctrl[8].addr[0] = 22;
	state->Init_Ctrl[8].bit[0] = 2;
	state->Init_Ctrl[8].val[0] = 0;

	state->Init_Ctrl[9].Ctrl_Num = AGC_IF ;
	state->Init_Ctrl[9].size = 4 ;
	state->Init_Ctrl[9].addr[0] = 76;
	state->Init_Ctrl[9].bit[0] = 0;
	state->Init_Ctrl[9].val[0] = 1;
	state->Init_Ctrl[9].addr[1] = 76;
	state->Init_Ctrl[9].bit[1] = 1;
	state->Init_Ctrl[9].val[1] = 1;
	state->Init_Ctrl[9].addr[2] = 76;
	state->Init_Ctrl[9].bit[2] = 2;
	state->Init_Ctrl[9].val[2] = 0;
	state->Init_Ctrl[9].addr[3] = 76;
	state->Init_Ctrl[9].bit[3] = 3;
	state->Init_Ctrl[9].val[3] = 1;

	state->Init_Ctrl[10].Ctrl_Num = AGC_RF ;
	state->Init_Ctrl[10].size = 4 ;
	state->Init_Ctrl[10].addr[0] = 76;
	state->Init_Ctrl[10].bit[0] = 4;
	state->Init_Ctrl[10].val[0] = 1;
	state->Init_Ctrl[10].addr[1] = 76;
	state->Init_Ctrl[10].bit[1] = 5;
	state->Init_Ctrl[10].val[1] = 1;
	state->Init_Ctrl[10].addr[2] = 76;
	state->Init_Ctrl[10].bit[2] = 6;
	state->Init_Ctrl[10].val[2] = 0;
	state->Init_Ctrl[10].addr[3] = 76;
	state->Init_Ctrl[10].bit[3] = 7;
	state->Init_Ctrl[10].val[3] = 1;

	state->Init_Ctrl[11].Ctrl_Num = IF_DIVVAL ;
	state->Init_Ctrl[11].size = 5 ;
	state->Init_Ctrl[11].addr[0] = 43;
	state->Init_Ctrl[11].bit[0] = 3;
	state->Init_Ctrl[11].val[0] = 0;
	state->Init_Ctrl[11].addr[1] = 43;
	state->Init_Ctrl[11].bit[1] = 4;
	state->Init_Ctrl[11].val[1] = 0;
	state->Init_Ctrl[11].addr[2] = 43;
	state->Init_Ctrl[11].bit[2] = 5;
	state->Init_Ctrl[11].val[2] = 0;
	state->Init_Ctrl[11].addr[3] = 43;
	state->Init_Ctrl[11].bit[3] = 6;
	state->Init_Ctrl[11].val[3] = 1;
	state->Init_Ctrl[11].addr[4] = 43;
	state->Init_Ctrl[11].bit[4] = 7;
	state->Init_Ctrl[11].val[4] = 0;

	state->Init_Ctrl[12].Ctrl_Num = IF_VCO_BIAS ;
	state->Init_Ctrl[12].size = 6 ;
	state->Init_Ctrl[12].addr[0] = 44;
	state->Init_Ctrl[12].bit[0] = 2;
	state->Init_Ctrl[12].val[0] = 0;
	state->Init_Ctrl[12].addr[1] = 44;
	state->Init_Ctrl[12].bit[1] = 3;
	state->Init_Ctrl[12].val[1] = 0;
	state->Init_Ctrl[12].addr[2] = 44;
	state->Init_Ctrl[12].bit[2] = 4;
	state->Init_Ctrl[12].val[2] = 0;
	state->Init_Ctrl[12].addr[3] = 44;
	state->Init_Ctrl[12].bit[3] = 5;
	state->Init_Ctrl[12].val[3] = 1;
	state->Init_Ctrl[12].addr[4] = 44;
	state->Init_Ctrl[12].bit[4] = 6;
	state->Init_Ctrl[12].val[4] = 0;
	state->Init_Ctrl[12].addr[5] = 44;
	state->Init_Ctrl[12].bit[5] = 7;
	state->Init_Ctrl[12].val[5] = 0;

	state->Init_Ctrl[13].Ctrl_Num = CHCAL_INT_MOD_IF ;
	state->Init_Ctrl[13].size = 7 ;
	state->Init_Ctrl[13].addr[0] = 11;
	state->Init_Ctrl[13].bit[0] = 0;
	state->Init_Ctrl[13].val[0] = 1;
	state->Init_Ctrl[13].addr[1] = 11;
	state->Init_Ctrl[13].bit[1] = 1;
	state->Init_Ctrl[13].val[1] = 0;
	state->Init_Ctrl[13].addr[2] = 11;
	state->Init_Ctrl[13].bit[2] = 2;
	state->Init_Ctrl[13].val[2] = 0;
	state->Init_Ctrl[13].addr[3] = 11;
	state->Init_Ctrl[13].bit[3] = 3;
	state->Init_Ctrl[13].val[3] = 1;
	state->Init_Ctrl[13].addr[4] = 11;
	state->Init_Ctrl[13].bit[4] = 4;
	state->Init_Ctrl[13].val[4] = 1;
	state->Init_Ctrl[13].addr[5] = 11;
	state->Init_Ctrl[13].bit[5] = 5;
	state->Init_Ctrl[13].val[5] = 0;
	state->Init_Ctrl[13].addr[6] = 11;
	state->Init_Ctrl[13].bit[6] = 6;
	state->Init_Ctrl[13].val[6] = 0;

	state->Init_Ctrl[14].Ctrl_Num = CHCAL_FRAC_MOD_IF ;
	state->Init_Ctrl[14].size = 16 ;
	state->Init_Ctrl[14].addr[0] = 13;
	state->Init_Ctrl[14].bit[0] = 0;
	state->Init_Ctrl[14].val[0] = 0;
	state->Init_Ctrl[14].addr[1] = 13;
	state->Init_Ctrl[14].bit[1] = 1;
	state->Init_Ctrl[14].val[1] = 0;
	state->Init_Ctrl[14].addr[2] = 13;
	state->Init_Ctrl[14].bit[2] = 2;
	state->Init_Ctrl[14].val[2] = 0;
	state->Init_Ctrl[14].addr[3] = 13;
	state->Init_Ctrl[14].bit[3] = 3;
	state->Init_Ctrl[14].val[3] = 0;
	state->Init_Ctrl[14].addr[4] = 13;
	state->Init_Ctrl[14].bit[4] = 4;
	state->Init_Ctrl[14].val[4] = 0;
	state->Init_Ctrl[14].addr[5] = 13;
	state->Init_Ctrl[14].bit[5] = 5;
	state->Init_Ctrl[14].val[5] = 0;
	state->Init_Ctrl[14].addr[6] = 13;
	state->Init_Ctrl[14].bit[6] = 6;
	state->Init_Ctrl[14].val[6] = 0;
	state->Init_Ctrl[14].addr[7] = 13;
	state->Init_Ctrl[14].bit[7] = 7;
	state->Init_Ctrl[14].val[7] = 0;
	state->Init_Ctrl[14].addr[8] = 12;
	state->Init_Ctrl[14].bit[8] = 0;
	state->Init_Ctrl[14].val[8] = 0;
	state->Init_Ctrl[14].addr[9] = 12;
	state->Init_Ctrl[14].bit[9] = 1;
	state->Init_Ctrl[14].val[9] = 0;
	state->Init_Ctrl[14].addr[10] = 12;
	state->Init_Ctrl[14].bit[10] = 2;
	state->Init_Ctrl[14].val[10] = 0;
	state->Init_Ctrl[14].addr[11] = 12;
	state->Init_Ctrl[14].bit[11] = 3;
	state->Init_Ctrl[14].val[11] = 0;
	state->Init_Ctrl[14].addr[12] = 12;
	state->Init_Ctrl[14].bit[12] = 4;
	state->Init_Ctrl[14].val[12] = 0;
	state->Init_Ctrl[14].addr[13] = 12;
	state->Init_Ctrl[14].bit[13] = 5;
	state->Init_Ctrl[14].val[13] = 1;
	state->Init_Ctrl[14].addr[14] = 12;
	state->Init_Ctrl[14].bit[14] = 6;
	state->Init_Ctrl[14].val[14] = 1;
	state->Init_Ctrl[14].addr[15] = 12;
	state->Init_Ctrl[14].bit[15] = 7;
	state->Init_Ctrl[14].val[15] = 0;

	state->Init_Ctrl[15].Ctrl_Num = DRV_RES_SEL ;
	state->Init_Ctrl[15].size = 3 ;
	state->Init_Ctrl[15].addr[0] = 147;
	state->Init_Ctrl[15].bit[0] = 2;
	state->Init_Ctrl[15].val[0] = 0;
	state->Init_Ctrl[15].addr[1] = 147;
	state->Init_Ctrl[15].bit[1] = 3;
	state->Init_Ctrl[15].val[1] = 1;
	state->Init_Ctrl[15].addr[2] = 147;
	state->Init_Ctrl[15].bit[2] = 4;
	state->Init_Ctrl[15].val[2] = 1;

	state->Init_Ctrl[16].Ctrl_Num = I_DRIVER ;
	state->Init_Ctrl[16].size = 2 ;
	state->Init_Ctrl[16].addr[0] = 147;
	state->Init_Ctrl[16].bit[0] = 0;
	state->Init_Ctrl[16].val[0] = 0;
	state->Init_Ctrl[16].addr[1] = 147;
	state->Init_Ctrl[16].bit[1] = 1;
	state->Init_Ctrl[16].val[1] = 1;

	state->Init_Ctrl[17].Ctrl_Num = EN_AAF ;
	state->Init_Ctrl[17].size = 1 ;
	state->Init_Ctrl[17].addr[0] = 147;
	state->Init_Ctrl[17].bit[0] = 7;
	state->Init_Ctrl[17].val[0] = 0;

	state->Init_Ctrl[18].Ctrl_Num = EN_3P ;
	state->Init_Ctrl[18].size = 1 ;
	state->Init_Ctrl[18].addr[0] = 147;
	state->Init_Ctrl[18].bit[0] = 6;
	state->Init_Ctrl[18].val[0] = 0;

	state->Init_Ctrl[19].Ctrl_Num = EN_AUX_3P ;
	state->Init_Ctrl[19].size = 1 ;
	state->Init_Ctrl[19].addr[0] = 156;
	state->Init_Ctrl[19].bit[0] = 0;
	state->Init_Ctrl[19].val[0] = 0;

	state->Init_Ctrl[20].Ctrl_Num = SEL_AAF_BAND ;
	state->Init_Ctrl[20].size = 1 ;
	state->Init_Ctrl[20].addr[0] = 147;
	state->Init_Ctrl[20].bit[0] = 5;
	state->Init_Ctrl[20].val[0] = 0;

	state->Init_Ctrl[21].Ctrl_Num = SEQ_ENCLK16_CLK_OUT ;
	state->Init_Ctrl[21].size = 1 ;
	state->Init_Ctrl[21].addr[0] = 137;
	state->Init_Ctrl[21].bit[0] = 4;
	state->Init_Ctrl[21].val[0] = 0;

	state->Init_Ctrl[22].Ctrl_Num = SEQ_SEL4_16B ;
	state->Init_Ctrl[22].size = 1 ;
	state->Init_Ctrl[22].addr[0] = 137;
	state->Init_Ctrl[22].bit[0] = 7;
	state->Init_Ctrl[22].val[0] = 0;

	state->Init_Ctrl[23].Ctrl_Num = XTAL_CAPSELECT ;
	state->Init_Ctrl[23].size = 1 ;
	state->Init_Ctrl[23].addr[0] = 91;
	state->Init_Ctrl[23].bit[0] = 5;
	state->Init_Ctrl[23].val[0] = 1;

	state->Init_Ctrl[24].Ctrl_Num = IF_SEL_DBL ;
	state->Init_Ctrl[24].size = 1 ;
	state->Init_Ctrl[24].addr[0] = 43;
	state->Init_Ctrl[24].bit[0] = 0;
	state->Init_Ctrl[24].val[0] = 1;

	state->Init_Ctrl[25].Ctrl_Num = RFSYN_R_DIV ;
	state->Init_Ctrl[25].size = 2 ;
	state->Init_Ctrl[25].addr[0] = 22;
	state->Init_Ctrl[25].bit[0] = 0;
	state->Init_Ctrl[25].val[0] = 1;
	state->Init_Ctrl[25].addr[1] = 22;
	state->Init_Ctrl[25].bit[1] = 1;
	state->Init_Ctrl[25].val[1] = 1;

	state->Init_Ctrl[26].Ctrl_Num = SEQ_EXTSYNTHCALIF ;
	state->Init_Ctrl[26].size = 1 ;
	state->Init_Ctrl[26].addr[0] = 134;
	state->Init_Ctrl[26].bit[0] = 2;
	state->Init_Ctrl[26].val[0] = 0;

	state->Init_Ctrl[27].Ctrl_Num = SEQ_EXTDCCAL ;
	state->Init_Ctrl[27].size = 1 ;
	state->Init_Ctrl[27].addr[0] = 137;
	state->Init_Ctrl[27].bit[0] = 3;
	state->Init_Ctrl[27].val[0] = 0;

	state->Init_Ctrl[28].Ctrl_Num = AGC_EN_RSSI ;
	state->Init_Ctrl[28].size = 1 ;
	state->Init_Ctrl[28].addr[0] = 77;
	state->Init_Ctrl[28].bit[0] = 7;
	state->Init_Ctrl[28].val[0] = 0;

	state->Init_Ctrl[29].Ctrl_Num = RFA_ENCLKRFAGC ;
	state->Init_Ctrl[29].size = 1 ;
	state->Init_Ctrl[29].addr[0] = 166;
	state->Init_Ctrl[29].bit[0] = 7;
	state->Init_Ctrl[29].val[0] = 1;

	state->Init_Ctrl[30].Ctrl_Num = RFA_RSSI_REFH ;
	state->Init_Ctrl[30].size = 3 ;
	state->Init_Ctrl[30].addr[0] = 166;
	state->Init_Ctrl[30].bit[0] = 0;
	state->Init_Ctrl[30].val[0] = 0;
	state->Init_Ctrl[30].addr[1] = 166;
	state->Init_Ctrl[30].bit[1] = 1;
	state->Init_Ctrl[30].val[1] = 1;
	state->Init_Ctrl[30].addr[2] = 166;
	state->Init_Ctrl[30].bit[2] = 2;
	state->Init_Ctrl[30].val[2] = 1;

	state->Init_Ctrl[31].Ctrl_Num = RFA_RSSI_REF ;
	state->Init_Ctrl[31].size = 3 ;
	state->Init_Ctrl[31].addr[0] = 166;
	state->Init_Ctrl[31].bit[0] = 3;
	state->Init_Ctrl[31].val[0] = 1;
	state->Init_Ctrl[31].addr[1] = 166;
	state->Init_Ctrl[31].bit[1] = 4;
	state->Init_Ctrl[31].val[1] = 0;
	state->Init_Ctrl[31].addr[2] = 166;
	state->Init_Ctrl[31].bit[2] = 5;
	state->Init_Ctrl[31].val[2] = 1;

	state->Init_Ctrl[32].Ctrl_Num = RFA_RSSI_REFL ;
	state->Init_Ctrl[32].size = 3 ;
	state->Init_Ctrl[32].addr[0] = 167;
	state->Init_Ctrl[32].bit[0] = 0;
	state->Init_Ctrl[32].val[0] = 1;
	state->Init_Ctrl[32].addr[1] = 167;
	state->Init_Ctrl[32].bit[1] = 1;
	state->Init_Ctrl[32].val[1] = 1;
	state->Init_Ctrl[32].addr[2] = 167;
	state->Init_Ctrl[32].bit[2] = 2;
	state->Init_Ctrl[32].val[2] = 0;

	state->Init_Ctrl[33].Ctrl_Num = RFA_FLR ;
	state->Init_Ctrl[33].size = 4 ;
	state->Init_Ctrl[33].addr[0] = 168;
	state->Init_Ctrl[33].bit[0] = 0;
	state->Init_Ctrl[33].val[0] = 0;
	state->Init_Ctrl[33].addr[1] = 168;
	state->Init_Ctrl[33].bit[1] = 1;
	state->Init_Ctrl[33].val[1] = 1;
	state->Init_Ctrl[33].addr[2] = 168;
	state->Init_Ctrl[33].bit[2] = 2;
	state->Init_Ctrl[33].val[2] = 0;
	state->Init_Ctrl[33].addr[3] = 168;
	state->Init_Ctrl[33].bit[3] = 3;
	state->Init_Ctrl[33].val[3] = 0;

	state->Init_Ctrl[34].Ctrl_Num = RFA_CEIL ;
	state->Init_Ctrl[34].size = 4 ;
	state->Init_Ctrl[34].addr[0] = 168;
	state->Init_Ctrl[34].bit[0] = 4;
	state->Init_Ctrl[34].val[0] = 1;
	state->Init_Ctrl[34].addr[1] = 168;
	state->Init_Ctrl[34].bit[1] = 5;
	state->Init_Ctrl[34].val[1] = 1;
	state->Init_Ctrl[34].addr[2] = 168;
	state->Init_Ctrl[34].bit[2] = 6;
	state->Init_Ctrl[34].val[2] = 1;
	state->Init_Ctrl[34].addr[3] = 168;
	state->Init_Ctrl[34].bit[3] = 7;
	state->Init_Ctrl[34].val[3] = 1;

	state->Init_Ctrl[35].Ctrl_Num = SEQ_EXTIQFSMPULSE ;
	state->Init_Ctrl[35].size = 1 ;
	state->Init_Ctrl[35].addr[0] = 135;
	state->Init_Ctrl[35].bit[0] = 0;
	state->Init_Ctrl[35].val[0] = 0;

	state->Init_Ctrl[36].Ctrl_Num = OVERRIDE_1 ;
	state->Init_Ctrl[36].size = 1 ;
	state->Init_Ctrl[36].addr[0] = 56;
	state->Init_Ctrl[36].bit[0] = 3;
	state->Init_Ctrl[36].val[0] = 0;

	state->Init_Ctrl[37].Ctrl_Num = BB_INITSTATE_DLPF_TUNE ;
	state->Init_Ctrl[37].size = 7 ;
	state->Init_Ctrl[37].addr[0] = 59;
	state->Init_Ctrl[37].bit[0] = 1;
	state->Init_Ctrl[37].val[0] = 0;
	state->Init_Ctrl[37].addr[1] = 59;
	state->Init_Ctrl[37].bit[1] = 2;
	state->Init_Ctrl[37].val[1] = 0;
	state->Init_Ctrl[37].addr[2] = 59;
	state->Init_Ctrl[37].bit[2] = 3;
	state->Init_Ctrl[37].val[2] = 0;
	state->Init_Ctrl[37].addr[3] = 59;
	state->Init_Ctrl[37].bit[3] = 4;
	state->Init_Ctrl[37].val[3] = 0;
	state->Init_Ctrl[37].addr[4] = 59;
	state->Init_Ctrl[37].bit[4] = 5;
	state->Init_Ctrl[37].val[4] = 0;
	state->Init_Ctrl[37].addr[5] = 59;
	state->Init_Ctrl[37].bit[5] = 6;
	state->Init_Ctrl[37].val[5] = 0;
	state->Init_Ctrl[37].addr[6] = 59;
	state->Init_Ctrl[37].bit[6] = 7;
	state->Init_Ctrl[37].val[6] = 0;

	state->Init_Ctrl[38].Ctrl_Num = TG_R_DIV ;
	state->Init_Ctrl[38].size = 6 ;
	state->Init_Ctrl[38].addr[0] = 32;
	state->Init_Ctrl[38].bit[0] = 2;
	state->Init_Ctrl[38].val[0] = 0;
	state->Init_Ctrl[38].addr[1] = 32;
	state->Init_Ctrl[38].bit[1] = 3;
	state->Init_Ctrl[38].val[1] = 0;
	state->Init_Ctrl[38].addr[2] = 32;
	state->Init_Ctrl[38].bit[2] = 4;
	state->Init_Ctrl[38].val[2] = 0;
	state->Init_Ctrl[38].addr[3] = 32;
	state->Init_Ctrl[38].bit[3] = 5;
	state->Init_Ctrl[38].val[3] = 0;
	state->Init_Ctrl[38].addr[4] = 32;
	state->Init_Ctrl[38].bit[4] = 6;
	state->Init_Ctrl[38].val[4] = 1;
	state->Init_Ctrl[38].addr[5] = 32;
	state->Init_Ctrl[38].bit[5] = 7;
	state->Init_Ctrl[38].val[5] = 0;

	state->Init_Ctrl[39].Ctrl_Num = EN_CHP_LIN_B ;
	state->Init_Ctrl[39].size = 1 ;
	state->Init_Ctrl[39].addr[0] = 25;
	state->Init_Ctrl[39].bit[0] = 3;
	state->Init_Ctrl[39].val[0] = 1;


	state->CH_Ctrl_Num = CHCTRL_NUM ;

	state->CH_Ctrl[0].Ctrl_Num = DN_POLY ;
	state->CH_Ctrl[0].size = 2 ;
	state->CH_Ctrl[0].addr[0] = 68;
	state->CH_Ctrl[0].bit[0] = 6;
	state->CH_Ctrl[0].val[0] = 1;
	state->CH_Ctrl[0].addr[1] = 68;
	state->CH_Ctrl[0].bit[1] = 7;
	state->CH_Ctrl[0].val[1] = 1;

	state->CH_Ctrl[1].Ctrl_Num = DN_RFGAIN ;
	state->CH_Ctrl[1].size = 2 ;
	state->CH_Ctrl[1].addr[0] = 70;
	state->CH_Ctrl[1].bit[0] = 6;
	state->CH_Ctrl[1].val[0] = 1;
	state->CH_Ctrl[1].addr[1] = 70;
	state->CH_Ctrl[1].bit[1] = 7;
	state->CH_Ctrl[1].val[1] = 0;

	state->CH_Ctrl[2].Ctrl_Num = DN_CAP_RFLPF ;
	state->CH_Ctrl[2].size = 9 ;
	state->CH_Ctrl[2].addr[0] = 69;
	state->CH_Ctrl[2].bit[0] = 5;
	state->CH_Ctrl[2].val[0] = 0;
	state->CH_Ctrl[2].addr[1] = 69;
	state->CH_Ctrl[2].bit[1] = 6;
	state->CH_Ctrl[2].val[1] = 0;
	state->CH_Ctrl[2].addr[2] = 69;
	state->CH_Ctrl[2].bit[2] = 7;
	state->CH_Ctrl[2].val[2] = 0;
	state->CH_Ctrl[2].addr[3] = 68;
	state->CH_Ctrl[2].bit[3] = 0;
	state->CH_Ctrl[2].val[3] = 0;
	state->CH_Ctrl[2].addr[4] = 68;
	state->CH_Ctrl[2].bit[4] = 1;
	state->CH_Ctrl[2].val[4] = 0;
	state->CH_Ctrl[2].addr[5] = 68;
	state->CH_Ctrl[2].bit[5] = 2;
	state->CH_Ctrl[2].val[5] = 0;
	state->CH_Ctrl[2].addr[6] = 68;
	state->CH_Ctrl[2].bit[6] = 3;
	state->CH_Ctrl[2].val[6] = 0;
	state->CH_Ctrl[2].addr[7] = 68;
	state->CH_Ctrl[2].bit[7] = 4;
	state->CH_Ctrl[2].val[7] = 0;
	state->CH_Ctrl[2].addr[8] = 68;
	state->CH_Ctrl[2].bit[8] = 5;
	state->CH_Ctrl[2].val[8] = 0;

	state->CH_Ctrl[3].Ctrl_Num = DN_EN_VHFUHFBAR ;
	state->CH_Ctrl[3].size = 1 ;
	state->CH_Ctrl[3].addr[0] = 70;
	state->CH_Ctrl[3].bit[0] = 5;
	state->CH_Ctrl[3].val[0] = 0;

	state->CH_Ctrl[4].Ctrl_Num = DN_GAIN_ADJUST ;
	state->CH_Ctrl[4].size = 3 ;
	state->CH_Ctrl[4].addr[0] = 73;
	state->CH_Ctrl[4].bit[0] = 4;
	state->CH_Ctrl[4].val[0] = 0;
	state->CH_Ctrl[4].addr[1] = 73;
	state->CH_Ctrl[4].bit[1] = 5;
	state->CH_Ctrl[4].val[1] = 1;
	state->CH_Ctrl[4].addr[2] = 73;
	state->CH_Ctrl[4].bit[2] = 6;
	state->CH_Ctrl[4].val[2] = 0;

	state->CH_Ctrl[5].Ctrl_Num = DN_IQTNBUF_AMP ;
	state->CH_Ctrl[5].size = 4 ;
	state->CH_Ctrl[5].addr[0] = 70;
	state->CH_Ctrl[5].bit[0] = 0;
	state->CH_Ctrl[5].val[0] = 0;
	state->CH_Ctrl[5].addr[1] = 70;
	state->CH_Ctrl[5].bit[1] = 1;
	state->CH_Ctrl[5].val[1] = 0;
	state->CH_Ctrl[5].addr[2] = 70;
	state->CH_Ctrl[5].bit[2] = 2;
	state->CH_Ctrl[5].val[2] = 0;
	state->CH_Ctrl[5].addr[3] = 70;
	state->CH_Ctrl[5].bit[3] = 3;
	state->CH_Ctrl[5].val[3] = 0;

	state->CH_Ctrl[6].Ctrl_Num = DN_IQTNGNBFBIAS_BST ;
	state->CH_Ctrl[6].size = 1 ;
	state->CH_Ctrl[6].addr[0] = 70;
	state->CH_Ctrl[6].bit[0] = 4;
	state->CH_Ctrl[6].val[0] = 1;

	state->CH_Ctrl[7].Ctrl_Num = RFSYN_EN_OUTMUX ;
	state->CH_Ctrl[7].size = 1 ;
	state->CH_Ctrl[7].addr[0] = 111;
	state->CH_Ctrl[7].bit[0] = 4;
	state->CH_Ctrl[7].val[0] = 0;

	state->CH_Ctrl[8].Ctrl_Num = RFSYN_SEL_VCO_OUT ;
	state->CH_Ctrl[8].size = 1 ;
	state->CH_Ctrl[8].addr[0] = 111;
	state->CH_Ctrl[8].bit[0] = 7;
	state->CH_Ctrl[8].val[0] = 1;

	state->CH_Ctrl[9].Ctrl_Num = RFSYN_SEL_VCO_HI ;
	state->CH_Ctrl[9].size = 1 ;
	state->CH_Ctrl[9].addr[0] = 111;
	state->CH_Ctrl[9].bit[0] = 6;
	state->CH_Ctrl[9].val[0] = 1;

	state->CH_Ctrl[10].Ctrl_Num = RFSYN_SEL_DIVM ;
	state->CH_Ctrl[10].size = 1 ;
	state->CH_Ctrl[10].addr[0] = 111;
	state->CH_Ctrl[10].bit[0] = 5;
	state->CH_Ctrl[10].val[0] = 0;

	state->CH_Ctrl[11].Ctrl_Num = RFSYN_RF_DIV_BIAS ;
	state->CH_Ctrl[11].size = 2 ;
	state->CH_Ctrl[11].addr[0] = 110;
	state->CH_Ctrl[11].bit[0] = 0;
	state->CH_Ctrl[11].val[0] = 1;
	state->CH_Ctrl[11].addr[1] = 110;
	state->CH_Ctrl[11].bit[1] = 1;
	state->CH_Ctrl[11].val[1] = 0;

	state->CH_Ctrl[12].Ctrl_Num = DN_SEL_FREQ ;
	state->CH_Ctrl[12].size = 3 ;
	state->CH_Ctrl[12].addr[0] = 69;
	state->CH_Ctrl[12].bit[0] = 2;
	state->CH_Ctrl[12].val[0] = 0;
	state->CH_Ctrl[12].addr[1] = 69;
	state->CH_Ctrl[12].bit[1] = 3;
	state->CH_Ctrl[12].val[1] = 0;
	state->CH_Ctrl[12].addr[2] = 69;
	state->CH_Ctrl[12].bit[2] = 4;
	state->CH_Ctrl[12].val[2] = 0;

	state->CH_Ctrl[13].Ctrl_Num = RFSYN_VCO_BIAS ;
	state->CH_Ctrl[13].size = 6 ;
	state->CH_Ctrl[13].addr[0] = 110;
	state->CH_Ctrl[13].bit[0] = 2;
	state->CH_Ctrl[13].val[0] = 0;
	state->CH_Ctrl[13].addr[1] = 110;
	state->CH_Ctrl[13].bit[1] = 3;
	state->CH_Ctrl[13].val[1] = 0;
	state->CH_Ctrl[13].addr[2] = 110;
	state->CH_Ctrl[13].bit[2] = 4;
	state->CH_Ctrl[13].val[2] = 0;
	state->CH_Ctrl[13].addr[3] = 110;
	state->CH_Ctrl[13].bit[3] = 5;
	state->CH_Ctrl[13].val[3] = 0;
	state->CH_Ctrl[13].addr[4] = 110;
	state->CH_Ctrl[13].bit[4] = 6;
	state->CH_Ctrl[13].val[4] = 0;
	state->CH_Ctrl[13].addr[5] = 110;
	state->CH_Ctrl[13].bit[5] = 7;
	state->CH_Ctrl[13].val[5] = 1;

	state->CH_Ctrl[14].Ctrl_Num = CHCAL_INT_MOD_RF ;
	state->CH_Ctrl[14].size = 7 ;
	state->CH_Ctrl[14].addr[0] = 14;
	state->CH_Ctrl[14].bit[0] = 0;
	state->CH_Ctrl[14].val[0] = 0;
	state->CH_Ctrl[14].addr[1] = 14;
	state->CH_Ctrl[14].bit[1] = 1;
	state->CH_Ctrl[14].val[1] = 0;
	state->CH_Ctrl[14].addr[2] = 14;
	state->CH_Ctrl[14].bit[2] = 2;
	state->CH_Ctrl[14].val[2] = 0;
	state->CH_Ctrl[14].addr[3] = 14;
	state->CH_Ctrl[14].bit[3] = 3;
	state->CH_Ctrl[14].val[3] = 0;
	state->CH_Ctrl[14].addr[4] = 14;
	state->CH_Ctrl[14].bit[4] = 4;
	state->CH_Ctrl[14].val[4] = 0;
	state->CH_Ctrl[14].addr[5] = 14;
	state->CH_Ctrl[14].bit[5] = 5;
	state->CH_Ctrl[14].val[5] = 0;
	state->CH_Ctrl[14].addr[6] = 14;
	state->CH_Ctrl[14].bit[6] = 6;
	state->CH_Ctrl[14].val[6] = 0;

	state->CH_Ctrl[15].Ctrl_Num = CHCAL_FRAC_MOD_RF ;
	state->CH_Ctrl[15].size = 18 ;
	state->CH_Ctrl[15].addr[0] = 17;
	state->CH_Ctrl[15].bit[0] = 6;
	state->CH_Ctrl[15].val[0] = 0;
	state->CH_Ctrl[15].addr[1] = 17;
	state->CH_Ctrl[15].bit[1] = 7;
	state->CH_Ctrl[15].val[1] = 0;
	state->CH_Ctrl[15].addr[2] = 16;
	state->CH_Ctrl[15].bit[2] = 0;
	state->CH_Ctrl[15].val[2] = 0;
	state->CH_Ctrl[15].addr[3] = 16;
	state->CH_Ctrl[15].bit[3] = 1;
	state->CH_Ctrl[15].val[3] = 0;
	state->CH_Ctrl[15].addr[4] = 16;
	state->CH_Ctrl[15].bit[4] = 2;
	state->CH_Ctrl[15].val[4] = 0;
	state->CH_Ctrl[15].addr[5] = 16;
	state->CH_Ctrl[15].bit[5] = 3;
	state->CH_Ctrl[15].val[5] = 0;
	state->CH_Ctrl[15].addr[6] = 16;
	state->CH_Ctrl[15].bit[6] = 4;
	state->CH_Ctrl[15].val[6] = 0;
	state->CH_Ctrl[15].addr[7] = 16;
	state->CH_Ctrl[15].bit[7] = 5;
	state->CH_Ctrl[15].val[7] = 0;
	state->CH_Ctrl[15].addr[8] = 16;
	state->CH_Ctrl[15].bit[8] = 6;
	state->CH_Ctrl[15].val[8] = 0;
	state->CH_Ctrl[15].addr[9] = 16;
	state->CH_Ctrl[15].bit[9] = 7;
	state->CH_Ctrl[15].val[9] = 0;
	state->CH_Ctrl[15].addr[10] = 15;
	state->CH_Ctrl[15].bit[10] = 0;
	state->CH_Ctrl[15].val[10] = 0;
	state->CH_Ctrl[15].addr[11] = 15;
	state->CH_Ctrl[15].bit[11] = 1;
	state->CH_Ctrl[15].val[11] = 0;
	state->CH_Ctrl[15].addr[12] = 15;
	state->CH_Ctrl[15].bit[12] = 2;
	state->CH_Ctrl[15].val[12] = 0;
	state->CH_Ctrl[15].addr[13] = 15;
	state->CH_Ctrl[15].bit[13] = 3;
	state->CH_Ctrl[15].val[13] = 0;
	state->CH_Ctrl[15].addr[14] = 15;
	state->CH_Ctrl[15].bit[14] = 4;
	state->CH_Ctrl[15].val[14] = 0;
	state->CH_Ctrl[15].addr[15] = 15;
	state->CH_Ctrl[15].bit[15] = 5;
	state->CH_Ctrl[15].val[15] = 0;
	state->CH_Ctrl[15].addr[16] = 15;
	state->CH_Ctrl[15].bit[16] = 6;
	state->CH_Ctrl[15].val[16] = 1;
	state->CH_Ctrl[15].addr[17] = 15;
	state->CH_Ctrl[15].bit[17] = 7;
	state->CH_Ctrl[15].val[17] = 1;

	state->CH_Ctrl[16].Ctrl_Num = RFSYN_LPF_R ;
	state->CH_Ctrl[16].size = 5 ;
	state->CH_Ctrl[16].addr[0] = 112;
	state->CH_Ctrl[16].bit[0] = 0;
	state->CH_Ctrl[16].val[0] = 0;
	state->CH_Ctrl[16].addr[1] = 112;
	state->CH_Ctrl[16].bit[1] = 1;
	state->CH_Ctrl[16].val[1] = 0;
	state->CH_Ctrl[16].addr[2] = 112;
	state->CH_Ctrl[16].bit[2] = 2;
	state->CH_Ctrl[16].val[2] = 0;
	state->CH_Ctrl[16].addr[3] = 112;
	state->CH_Ctrl[16].bit[3] = 3;
	state->CH_Ctrl[16].val[3] = 0;
	state->CH_Ctrl[16].addr[4] = 112;
	state->CH_Ctrl[16].bit[4] = 4;
	state->CH_Ctrl[16].val[4] = 1;

	state->CH_Ctrl[17].Ctrl_Num = CHCAL_EN_INT_RF ;
	state->CH_Ctrl[17].size = 1 ;
	state->CH_Ctrl[17].addr[0] = 14;
	state->CH_Ctrl[17].bit[0] = 7;
	state->CH_Ctrl[17].val[0] = 0;

	state->CH_Ctrl[18].Ctrl_Num = TG_LO_DIVVAL ;
	state->CH_Ctrl[18].size = 4 ;
	state->CH_Ctrl[18].addr[0] = 107;
	state->CH_Ctrl[18].bit[0] = 3;
	state->CH_Ctrl[18].val[0] = 0;
	state->CH_Ctrl[18].addr[1] = 107;
	state->CH_Ctrl[18].bit[1] = 4;
	state->CH_Ctrl[18].val[1] = 0;
	state->CH_Ctrl[18].addr[2] = 107;
	state->CH_Ctrl[18].bit[2] = 5;
	state->CH_Ctrl[18].val[2] = 0;
	state->CH_Ctrl[18].addr[3] = 107;
	state->CH_Ctrl[18].bit[3] = 6;
	state->CH_Ctrl[18].val[3] = 0;

	state->CH_Ctrl[19].Ctrl_Num = TG_LO_SELVAL ;
	state->CH_Ctrl[19].size = 3 ;
	state->CH_Ctrl[19].addr[0] = 107;
	state->CH_Ctrl[19].bit[0] = 7;
	state->CH_Ctrl[19].val[0] = 1;
	state->CH_Ctrl[19].addr[1] = 106;
	state->CH_Ctrl[19].bit[1] = 0;
	state->CH_Ctrl[19].val[1] = 1;
	state->CH_Ctrl[19].addr[2] = 106;
	state->CH_Ctrl[19].bit[2] = 1;
	state->CH_Ctrl[19].val[2] = 1;

	state->CH_Ctrl[20].Ctrl_Num = TG_DIV_VAL ;
	state->CH_Ctrl[20].size = 11 ;
	state->CH_Ctrl[20].addr[0] = 109;
	state->CH_Ctrl[20].bit[0] = 2;
	state->CH_Ctrl[20].val[0] = 0;
	state->CH_Ctrl[20].addr[1] = 109;
	state->CH_Ctrl[20].bit[1] = 3;
	state->CH_Ctrl[20].val[1] = 0;
	state->CH_Ctrl[20].addr[2] = 109;
	state->CH_Ctrl[20].bit[2] = 4;
	state->CH_Ctrl[20].val[2] = 0;
	state->CH_Ctrl[20].addr[3] = 109;
	state->CH_Ctrl[20].bit[3] = 5;
	state->CH_Ctrl[20].val[3] = 0;
	state->CH_Ctrl[20].addr[4] = 109;
	state->CH_Ctrl[20].bit[4] = 6;
	state->CH_Ctrl[20].val[4] = 0;
	state->CH_Ctrl[20].addr[5] = 109;
	state->CH_Ctrl[20].bit[5] = 7;
	state->CH_Ctrl[20].val[5] = 0;
	state->CH_Ctrl[20].addr[6] = 108;
	state->CH_Ctrl[20].bit[6] = 0;
	state->CH_Ctrl[20].val[6] = 0;
	state->CH_Ctrl[20].addr[7] = 108;
	state->CH_Ctrl[20].bit[7] = 1;
	state->CH_Ctrl[20].val[7] = 0;
	state->CH_Ctrl[20].addr[8] = 108;
	state->CH_Ctrl[20].bit[8] = 2;
	state->CH_Ctrl[20].val[8] = 1;
	state->CH_Ctrl[20].addr[9] = 108;
	state->CH_Ctrl[20].bit[9] = 3;
	state->CH_Ctrl[20].val[9] = 1;
	state->CH_Ctrl[20].addr[10] = 108;
	state->CH_Ctrl[20].bit[10] = 4;
	state->CH_Ctrl[20].val[10] = 1;

	state->CH_Ctrl[21].Ctrl_Num = TG_VCO_BIAS ;
	state->CH_Ctrl[21].size = 6 ;
	state->CH_Ctrl[21].addr[0] = 106;
	state->CH_Ctrl[21].bit[0] = 2;
	state->CH_Ctrl[21].val[0] = 0;
	state->CH_Ctrl[21].addr[1] = 106;
	state->CH_Ctrl[21].bit[1] = 3;
	state->CH_Ctrl[21].val[1] = 0;
	state->CH_Ctrl[21].addr[2] = 106;
	state->CH_Ctrl[21].bit[2] = 4;
	state->CH_Ctrl[21].val[2] = 0;
	state->CH_Ctrl[21].addr[3] = 106;
	state->CH_Ctrl[21].bit[3] = 5;
	state->CH_Ctrl[21].val[3] = 0;
	state->CH_Ctrl[21].addr[4] = 106;
	state->CH_Ctrl[21].bit[4] = 6;
	state->CH_Ctrl[21].val[4] = 0;
	state->CH_Ctrl[21].addr[5] = 106;
	state->CH_Ctrl[21].bit[5] = 7;
	state->CH_Ctrl[21].val[5] = 1;

	state->CH_Ctrl[22].Ctrl_Num = SEQ_EXTPOWERUP ;
	state->CH_Ctrl[22].size = 1 ;
	state->CH_Ctrl[22].addr[0] = 138;
	state->CH_Ctrl[22].bit[0] = 4;
	state->CH_Ctrl[22].val[0] = 1;

	state->CH_Ctrl[23].Ctrl_Num = OVERRIDE_2 ;
	state->CH_Ctrl[23].size = 1 ;
	state->CH_Ctrl[23].addr[0] = 17;
	state->CH_Ctrl[23].bit[0] = 5;
	state->CH_Ctrl[23].val[0] = 0;

	state->CH_Ctrl[24].Ctrl_Num = OVERRIDE_3 ;
	state->CH_Ctrl[24].size = 1 ;
	state->CH_Ctrl[24].addr[0] = 111;
	state->CH_Ctrl[24].bit[0] = 3;
	state->CH_Ctrl[24].val[0] = 0;

	state->CH_Ctrl[25].Ctrl_Num = OVERRIDE_4 ;
	state->CH_Ctrl[25].size = 1 ;
	state->CH_Ctrl[25].addr[0] = 112;
	state->CH_Ctrl[25].bit[0] = 7;
	state->CH_Ctrl[25].val[0] = 0;

	state->CH_Ctrl[26].Ctrl_Num = SEQ_FSM_PULSE ;
	state->CH_Ctrl[26].size = 1 ;
	state->CH_Ctrl[26].addr[0] = 136;
	state->CH_Ctrl[26].bit[0] = 7;
	state->CH_Ctrl[26].val[0] = 0;

	state->CH_Ctrl[27].Ctrl_Num = GPIO_4B ;
	state->CH_Ctrl[27].size = 1 ;
	state->CH_Ctrl[27].addr[0] = 149;
	state->CH_Ctrl[27].bit[0] = 7;
	state->CH_Ctrl[27].val[0] = 0;

	state->CH_Ctrl[28].Ctrl_Num = GPIO_3B ;
	state->CH_Ctrl[28].size = 1 ;
	state->CH_Ctrl[28].addr[0] = 149;
	state->CH_Ctrl[28].bit[0] = 6;
	state->CH_Ctrl[28].val[0] = 0;

	state->CH_Ctrl[29].Ctrl_Num = GPIO_4 ;
	state->CH_Ctrl[29].size = 1 ;
	state->CH_Ctrl[29].addr[0] = 149;
	state->CH_Ctrl[29].bit[0] = 5;
	state->CH_Ctrl[29].val[0] = 1;

	state->CH_Ctrl[30].Ctrl_Num = GPIO_3 ;
	state->CH_Ctrl[30].size = 1 ;
	state->CH_Ctrl[30].addr[0] = 149;
	state->CH_Ctrl[30].bit[0] = 4;
	state->CH_Ctrl[30].val[0] = 1;

	state->CH_Ctrl[31].Ctrl_Num = GPIO_1B ;
	state->CH_Ctrl[31].size = 1 ;
	state->CH_Ctrl[31].addr[0] = 149;
	state->CH_Ctrl[31].bit[0] = 3;
	state->CH_Ctrl[31].val[0] = 0;

	state->CH_Ctrl[32].Ctrl_Num = DAC_A_ENABLE ;
	state->CH_Ctrl[32].size = 1 ;
	state->CH_Ctrl[32].addr[0] = 93;
	state->CH_Ctrl[32].bit[0] = 1;
	state->CH_Ctrl[32].val[0] = 0;

	state->CH_Ctrl[33].Ctrl_Num = DAC_B_ENABLE ;
	state->CH_Ctrl[33].size = 1 ;
	state->CH_Ctrl[33].addr[0] = 93;
	state->CH_Ctrl[33].bit[0] = 0;
	state->CH_Ctrl[33].val[0] = 0;

	state->CH_Ctrl[34].Ctrl_Num = DAC_DIN_A ;
	state->CH_Ctrl[34].size = 6 ;
	state->CH_Ctrl[34].addr[0] = 92;
	state->CH_Ctrl[34].bit[0] = 2;
	state->CH_Ctrl[34].val[0] = 0;
	state->CH_Ctrl[34].addr[1] = 92;
	state->CH_Ctrl[34].bit[1] = 3;
	state->CH_Ctrl[34].val[1] = 0;
	state->CH_Ctrl[34].addr[2] = 92;
	state->CH_Ctrl[34].bit[2] = 4;
	state->CH_Ctrl[34].val[2] = 0;
	state->CH_Ctrl[34].addr[3] = 92;
	state->CH_Ctrl[34].bit[3] = 5;
	state->CH_Ctrl[34].val[3] = 0;
	state->CH_Ctrl[34].addr[4] = 92;
	state->CH_Ctrl[34].bit[4] = 6;
	state->CH_Ctrl[34].val[4] = 0;
	state->CH_Ctrl[34].addr[5] = 92;
	state->CH_Ctrl[34].bit[5] = 7;
	state->CH_Ctrl[34].val[5] = 0;

	state->CH_Ctrl[35].Ctrl_Num = DAC_DIN_B ;
	state->CH_Ctrl[35].size = 6 ;
	state->CH_Ctrl[35].addr[0] = 93;
	state->CH_Ctrl[35].bit[0] = 2;
	state->CH_Ctrl[35].val[0] = 0;
	state->CH_Ctrl[35].addr[1] = 93;
	state->CH_Ctrl[35].bit[1] = 3;
	state->CH_Ctrl[35].val[1] = 0;
	state->CH_Ctrl[35].addr[2] = 93;
	state->CH_Ctrl[35].bit[2] = 4;
	state->CH_Ctrl[35].val[2] = 0;
	state->CH_Ctrl[35].addr[3] = 93;
	state->CH_Ctrl[35].bit[3] = 5;
	state->CH_Ctrl[35].val[3] = 0;
	state->CH_Ctrl[35].addr[4] = 93;
	state->CH_Ctrl[35].bit[4] = 6;
	state->CH_Ctrl[35].val[4] = 0;
	state->CH_Ctrl[35].addr[5] = 93;
	state->CH_Ctrl[35].bit[5] = 7;
	state->CH_Ctrl[35].val[5] = 0;

#ifdef _MXL_PRODUCTION
	state->CH_Ctrl[36].Ctrl_Num = RFSYN_EN_DIV ;
	state->CH_Ctrl[36].size = 1 ;
	state->CH_Ctrl[36].addr[0] = 109;
	state->CH_Ctrl[36].bit[0] = 1;
	state->CH_Ctrl[36].val[0] = 1;

	state->CH_Ctrl[37].Ctrl_Num = RFSYN_DIVM ;
	state->CH_Ctrl[37].size = 2 ;
	state->CH_Ctrl[37].addr[0] = 112;
	state->CH_Ctrl[37].bit[0] = 5;
	state->CH_Ctrl[37].val[0] = 0;
	state->CH_Ctrl[37].addr[1] = 112;
	state->CH_Ctrl[37].bit[1] = 6;
	state->CH_Ctrl[37].val[1] = 0;

	state->CH_Ctrl[38].Ctrl_Num = DN_BYPASS_AGC_I2C ;
	state->CH_Ctrl[38].size = 1 ;
	state->CH_Ctrl[38].addr[0] = 65;
	state->CH_Ctrl[38].bit[0] = 1;
	state->CH_Ctrl[38].val[0] = 0;
#endif

	return 0 ;
}

static void InitTunerControls(struct dvb_frontend *fe)
{
	MXL5005_RegisterInit(fe);
	MXL5005_ControlInit(fe);
#ifdef _MXL_INTERNAL
	MXL5005_MXLControlInit(fe);
#endif
}

static u16 MXL5005_TunerConfig(struct dvb_frontend *fe,
	u8	Mode,		/* 0: Analog Mode ; 1: Digital Mode */
	u8	IF_mode,	/* for Analog Mode, 0: zero IF; 1: low IF */
	u32	Bandwidth,	/* filter  channel bandwidth (6, 7, 8) */
	u32	IF_out,		/* Desired IF Out Frequency */
	u32	Fxtal,		/* XTAL Frequency */
	u8	AGC_Mode,	/* AGC Mode - Dual AGC: 0, Single AGC: 1 */
	u16	TOP,		/* 0: Dual AGC; Value: take over point */
	u16	IF_OUT_LOAD,	/* IF Out Load Resistor (200 / 300 Ohms) */
	u8	CLOCK_OUT,	/* 0: turn off clk out; 1: turn on clock out */
	u8	DIV_OUT,	/* 0: Div-1; 1: Div-4 */
	u8	CAPSELECT,	/* 0: disable On-Chip pulling cap; 1: enable */
	u8	EN_RSSI,	/* 0: disable RSSI; 1: enable RSSI */

	/* Modulation Type; */
	/* 0 - Default;	1 - DVB-T; 2 - ATSC; 3 - QAM; 4 - Analog Cable */
	u8	Mod_Type,

	/* Tracking Filter */
	/* 0 - Default; 1 - Off; 2 - Type C; 3 - Type C-H */
	u8	TF_Type
	)
{
	struct mxl5005s_state *state = fe->tuner_priv;

	state->Mode = Mode;
	state->IF_Mode = IF_mode;
	state->Chan_Bandwidth = Bandwidth;
	state->IF_OUT = IF_out;
	state->Fxtal = Fxtal;
	state->AGC_Mode = AGC_Mode;
	state->TOP = TOP;
	state->IF_OUT_LOAD = IF_OUT_LOAD;
	state->CLOCK_OUT = CLOCK_OUT;
	state->DIV_OUT = DIV_OUT;
	state->CAPSELECT = CAPSELECT;
	state->EN_RSSI = EN_RSSI;
	state->Mod_Type = Mod_Type;
	state->TF_Type = TF_Type;

	/* Initialize all the controls and registers */
	InitTunerControls(fe);

	/* Synthesizer LO frequency calculation */
	MXL_SynthIFLO_Calc(fe);

	return 0;
}

static void MXL_SynthIFLO_Calc(struct dvb_frontend *fe)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	if (state->Mode == 1) /* Digital Mode */
		state->IF_LO = state->IF_OUT;
	else /* Analog Mode */ {
		if (state->IF_Mode == 0) /* Analog Zero IF mode */
			state->IF_LO = state->IF_OUT + 400000;
		else /* Analog Low IF mode */
			state->IF_LO = state->IF_OUT + state->Chan_Bandwidth/2;
	}
}

static void MXL_SynthRFTGLO_Calc(struct dvb_frontend *fe)
{
	struct mxl5005s_state *state = fe->tuner_priv;

	if (state->Mode == 1) /* Digital Mode */ {
			/* remove 20.48MHz setting for 2.6.10 */
			state->RF_LO = state->RF_IN;
			/* change for 2.6.6 */
			state->TG_LO = state->RF_IN - 750000;
	} else /* Analog Mode */ {
		if (state->IF_Mode == 0) /* Analog Zero IF mode */ {
			state->RF_LO = state->RF_IN - 400000;
			state->TG_LO = state->RF_IN - 1750000;
		} else /* Analog Low IF mode */ {
			state->RF_LO = state->RF_IN - state->Chan_Bandwidth/2;
			state->TG_LO = state->RF_IN -
				state->Chan_Bandwidth + 500000;
		}
	}
}

static u16 MXL_OverwriteICDefault(struct dvb_frontend *fe)
{
	u16 status = 0;

	status += MXL_ControlWrite(fe, OVERRIDE_1, 1);
	status += MXL_ControlWrite(fe, OVERRIDE_2, 1);
	status += MXL_ControlWrite(fe, OVERRIDE_3, 1);
	status += MXL_ControlWrite(fe, OVERRIDE_4, 1);

	return status;
}

static u16 MXL_BlockInit(struct dvb_frontend *fe)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	u16 status = 0;

	status += MXL_OverwriteICDefault(fe);

	/* Downconverter Control Dig Ana */
	status += MXL_ControlWrite(fe, DN_IQTN_AMP_CUT, state->Mode ? 1 : 0);

	/* Filter Control  Dig  Ana */
	status += MXL_ControlWrite(fe, BB_MODE, state->Mode ? 0 : 1);
	status += MXL_ControlWrite(fe, BB_BUF, state->Mode ? 3 : 2);
	status += MXL_ControlWrite(fe, BB_BUF_OA, state->Mode ? 1 : 0);
	status += MXL_ControlWrite(fe, BB_IQSWAP, state->Mode ? 0 : 1);
	status += MXL_ControlWrite(fe, BB_INITSTATE_DLPF_TUNE, 0);

	/* Initialize Low-Pass Filter */
	if (state->Mode) { /* Digital Mode */
		switch (state->Chan_Bandwidth) {
		case 8000000:
			status += MXL_ControlWrite(fe, BB_DLPF_BANDSEL, 0);
			break;
		case 7000000:
			status += MXL_ControlWrite(fe, BB_DLPF_BANDSEL, 2);
			break;
		case 6000000:
			status += MXL_ControlWrite(fe,
					BB_DLPF_BANDSEL, 3);
			break;
		}
	} else { /* Analog Mode */
		switch (state->Chan_Bandwidth) {
		case 8000000:	/* Low Zero */
			status += MXL_ControlWrite(fe, BB_ALPF_BANDSELECT,
					(state->IF_Mode ? 0 : 3));
			break;
		case 7000000:
			status += MXL_ControlWrite(fe, BB_ALPF_BANDSELECT,
					(state->IF_Mode ? 1 : 4));
			break;
		case 6000000:
			status += MXL_ControlWrite(fe, BB_ALPF_BANDSELECT,
					(state->IF_Mode ? 2 : 5));
			break;
		}
	}

	/* Charge Pump Control Dig  Ana */
	status += MXL_ControlWrite(fe, RFSYN_CHP_GAIN, state->Mode ? 5 : 8);
	status += MXL_ControlWrite(fe,
		RFSYN_EN_CHP_HIGAIN, state->Mode ? 1 : 1);
	status += MXL_ControlWrite(fe, EN_CHP_LIN_B, state->Mode ? 0 : 0);

	/* AGC TOP Control */
	if (state->AGC_Mode == 0) /* Dual AGC */ {
		status += MXL_ControlWrite(fe, AGC_IF, 15);
		status += MXL_ControlWrite(fe, AGC_RF, 15);
	} else /*  Single AGC Mode Dig  Ana */
		status += MXL_ControlWrite(fe, AGC_RF, state->Mode ? 15 : 12);

	if (state->TOP == 55) /* TOP == 5.5 */
		status += MXL_ControlWrite(fe, AGC_IF, 0x0);

	if (state->TOP == 72) /* TOP == 7.2 */
		status += MXL_ControlWrite(fe, AGC_IF, 0x1);

	if (state->TOP == 92) /* TOP == 9.2 */
		status += MXL_ControlWrite(fe, AGC_IF, 0x2);

	if (state->TOP == 110) /* TOP == 11.0 */
		status += MXL_ControlWrite(fe, AGC_IF, 0x3);

	if (state->TOP == 129) /* TOP == 12.9 */
		status += MXL_ControlWrite(fe, AGC_IF, 0x4);

	if (state->TOP == 147) /* TOP == 14.7 */
		status += MXL_ControlWrite(fe, AGC_IF, 0x5);

	if (state->TOP == 168) /* TOP == 16.8 */
		status += MXL_ControlWrite(fe, AGC_IF, 0x6);

	if (state->TOP == 194) /* TOP == 19.4 */
		status += MXL_ControlWrite(fe, AGC_IF, 0x7);

	if (state->TOP == 212) /* TOP == 21.2 */
		status += MXL_ControlWrite(fe, AGC_IF, 0x9);

	if (state->TOP == 232) /* TOP == 23.2 */
		status += MXL_ControlWrite(fe, AGC_IF, 0xA);

	if (state->TOP == 252) /* TOP == 25.2 */
		status += MXL_ControlWrite(fe, AGC_IF, 0xB);

	if (state->TOP == 271) /* TOP == 27.1 */
		status += MXL_ControlWrite(fe, AGC_IF, 0xC);

	if (state->TOP == 292) /* TOP == 29.2 */
		status += MXL_ControlWrite(fe, AGC_IF, 0xD);

	if (state->TOP == 317) /* TOP == 31.7 */
		status += MXL_ControlWrite(fe, AGC_IF, 0xE);

	if (state->TOP == 349) /* TOP == 34.9 */
		status += MXL_ControlWrite(fe, AGC_IF, 0xF);

	/* IF Synthesizer Control */
	status += MXL_IFSynthInit(fe);

	/* IF UpConverter Control */
	if (state->IF_OUT_LOAD == 200) {
		status += MXL_ControlWrite(fe, DRV_RES_SEL, 6);
		status += MXL_ControlWrite(fe, I_DRIVER, 2);
	}
	if (state->IF_OUT_LOAD == 300) {
		status += MXL_ControlWrite(fe, DRV_RES_SEL, 4);
		status += MXL_ControlWrite(fe, I_DRIVER, 1);
	}

	/* Anti-Alias Filtering Control
	 * initialise Anti-Aliasing Filter
	 */
	if (state->Mode) { /* Digital Mode */
		if (state->IF_OUT >= 4000000UL && state->IF_OUT <= 6280000UL) {
			status += MXL_ControlWrite(fe, EN_AAF, 1);
			status += MXL_ControlWrite(fe, EN_3P, 1);
			status += MXL_ControlWrite(fe, EN_AUX_3P, 1);
			status += MXL_ControlWrite(fe, SEL_AAF_BAND, 0);
		}
		if ((state->IF_OUT == 36125000UL) ||
			(state->IF_OUT == 36150000UL)) {
			status += MXL_ControlWrite(fe, EN_AAF, 1);
			status += MXL_ControlWrite(fe, EN_3P, 1);
			status += MXL_ControlWrite(fe, EN_AUX_3P, 1);
			status += MXL_ControlWrite(fe, SEL_AAF_BAND, 1);
		}
		if (state->IF_OUT > 36150000UL) {
			status += MXL_ControlWrite(fe, EN_AAF, 0);
			status += MXL_ControlWrite(fe, EN_3P, 1);
			status += MXL_ControlWrite(fe, EN_AUX_3P, 1);
			status += MXL_ControlWrite(fe, SEL_AAF_BAND, 1);
		}
	} else { /* Analog Mode */
		if (state->IF_OUT >= 4000000UL && state->IF_OUT <= 5000000UL) {
			status += MXL_ControlWrite(fe, EN_AAF, 1);
			status += MXL_ControlWrite(fe, EN_3P, 1);
			status += MXL_ControlWrite(fe, EN_AUX_3P, 1);
			status += MXL_ControlWrite(fe, SEL_AAF_BAND, 0);
		}
		if (state->IF_OUT > 5000000UL) {
			status += MXL_ControlWrite(fe, EN_AAF, 0);
			status += MXL_ControlWrite(fe, EN_3P, 0);
			status += MXL_ControlWrite(fe, EN_AUX_3P, 0);
			status += MXL_ControlWrite(fe, SEL_AAF_BAND, 0);
		}
	}

	/* Demod Clock Out */
	if (state->CLOCK_OUT)
		status += MXL_ControlWrite(fe, SEQ_ENCLK16_CLK_OUT, 1);
	else
		status += MXL_ControlWrite(fe, SEQ_ENCLK16_CLK_OUT, 0);

	if (state->DIV_OUT == 1)
		status += MXL_ControlWrite(fe, SEQ_SEL4_16B, 1);
	if (state->DIV_OUT == 0)
		status += MXL_ControlWrite(fe, SEQ_SEL4_16B, 0);

	/* Crystal Control */
	if (state->CAPSELECT)
		status += MXL_ControlWrite(fe, XTAL_CAPSELECT, 1);
	else
		status += MXL_ControlWrite(fe, XTAL_CAPSELECT, 0);

	if (state->Fxtal >= 12000000UL && state->Fxtal <= 16000000UL)
		status += MXL_ControlWrite(fe, IF_SEL_DBL, 1);
	if (state->Fxtal > 16000000UL && state->Fxtal <= 32000000UL)
		status += MXL_ControlWrite(fe, IF_SEL_DBL, 0);

	if (state->Fxtal >= 12000000UL && state->Fxtal <= 22000000UL)
		status += MXL_ControlWrite(fe, RFSYN_R_DIV, 3);
	if (state->Fxtal > 22000000UL && state->Fxtal <= 32000000UL)
		status += MXL_ControlWrite(fe, RFSYN_R_DIV, 0);

	/* Misc Controls */
	if (state->Mode == 0 && state->IF_Mode == 1) /* Analog LowIF mode */
		status += MXL_ControlWrite(fe, SEQ_EXTIQFSMPULSE, 0);
	else
		status += MXL_ControlWrite(fe, SEQ_EXTIQFSMPULSE, 1);

	/* status += MXL_ControlRead(fe, IF_DIVVAL, &IF_DIVVAL_Val); */

	/* Set TG_R_DIV */
	status += MXL_ControlWrite(fe, TG_R_DIV,
		MXL_Ceiling(state->Fxtal, 1000000));

	/* Apply Default value to BB_INITSTATE_DLPF_TUNE */

	/* RSSI Control */
	if (state->EN_RSSI) {
		status += MXL_ControlWrite(fe, SEQ_EXTSYNTHCALIF, 1);
		status += MXL_ControlWrite(fe, SEQ_EXTDCCAL, 1);
		status += MXL_ControlWrite(fe, AGC_EN_RSSI, 1);
		status += MXL_ControlWrite(fe, RFA_ENCLKRFAGC, 1);

		/* RSSI reference point */
		status += MXL_ControlWrite(fe, RFA_RSSI_REF, 2);
		status += MXL_ControlWrite(fe, RFA_RSSI_REFH, 3);
		status += MXL_ControlWrite(fe, RFA_RSSI_REFL, 1);

		/* TOP point */
		status += MXL_ControlWrite(fe, RFA_FLR, 0);
		status += MXL_ControlWrite(fe, RFA_CEIL, 12);
	}

	/* Modulation type bit settings
	 * Override the control values preset
	 */
	if (state->Mod_Type == MXL_DVBT) /* DVB-T Mode */ {
		state->AGC_Mode = 1; /* Single AGC Mode */

		/* Enable RSSI */
		status += MXL_ControlWrite(fe, SEQ_EXTSYNTHCALIF, 1);
		status += MXL_ControlWrite(fe, SEQ_EXTDCCAL, 1);
		status += MXL_ControlWrite(fe, AGC_EN_RSSI, 1);
		status += MXL_ControlWrite(fe, RFA_ENCLKRFAGC, 1);

		/* RSSI reference point */
		status += MXL_ControlWrite(fe, RFA_RSSI_REF, 3);
		status += MXL_ControlWrite(fe, RFA_RSSI_REFH, 5);
		status += MXL_ControlWrite(fe, RFA_RSSI_REFL, 1);

		/* TOP point */
		status += MXL_ControlWrite(fe, RFA_FLR, 2);
		status += MXL_ControlWrite(fe, RFA_CEIL, 13);
		if (state->IF_OUT <= 6280000UL)	/* Low IF */
			status += MXL_ControlWrite(fe, BB_IQSWAP, 0);
		else /* High IF */
			status += MXL_ControlWrite(fe, BB_IQSWAP, 1);

	}
	if (state->Mod_Type == MXL_ATSC) /* ATSC Mode */ {
		state->AGC_Mode = 1;	/* Single AGC Mode */

		/* Enable RSSI */
		status += MXL_ControlWrite(fe, SEQ_EXTSYNTHCALIF, 1);
		status += MXL_ControlWrite(fe, SEQ_EXTDCCAL, 1);
		status += MXL_ControlWrite(fe, AGC_EN_RSSI, 1);
		status += MXL_ControlWrite(fe, RFA_ENCLKRFAGC, 1);

		/* RSSI reference point */
		status += MXL_ControlWrite(fe, RFA_RSSI_REF, 2);
		status += MXL_ControlWrite(fe, RFA_RSSI_REFH, 4);
		status += MXL_ControlWrite(fe, RFA_RSSI_REFL, 1);

		/* TOP point */
		status += MXL_ControlWrite(fe, RFA_FLR, 2);
		status += MXL_ControlWrite(fe, RFA_CEIL, 13);
		status += MXL_ControlWrite(fe, BB_INITSTATE_DLPF_TUNE, 1);
		/* Low Zero */
		status += MXL_ControlWrite(fe, RFSYN_CHP_GAIN, 5);

		if (state->IF_OUT <= 6280000UL)	/* Low IF */
			status += MXL_ControlWrite(fe, BB_IQSWAP, 0);
		else /* High IF */
			status += MXL_ControlWrite(fe, BB_IQSWAP, 1);
	}
	if (state->Mod_Type == MXL_QAM) /* QAM Mode */ {
		state->Mode = MXL_DIGITAL_MODE;

		/* state->AGC_Mode = 1; */ /* Single AGC Mode */

		/* Disable RSSI */	/* change here for v2.6.5 */
		status += MXL_ControlWrite(fe, SEQ_EXTSYNTHCALIF, 1);
		status += MXL_ControlWrite(fe, SEQ_EXTDCCAL, 1);
		status += MXL_ControlWrite(fe, AGC_EN_RSSI, 0);
		status += MXL_ControlWrite(fe, RFA_ENCLKRFAGC, 1);

		/* RSSI reference point */
		status += MXL_ControlWrite(fe, RFA_RSSI_REFH, 5);
		status += MXL_ControlWrite(fe, RFA_RSSI_REF, 3);
		status += MXL_ControlWrite(fe, RFA_RSSI_REFL, 2);
		/* change here for v2.6.5 */
		status += MXL_ControlWrite(fe, RFSYN_CHP_GAIN, 3);

		if (state->IF_OUT <= 6280000UL)	/* Low IF */
			status += MXL_ControlWrite(fe, BB_IQSWAP, 0);
		else /* High IF */
			status += MXL_ControlWrite(fe, BB_IQSWAP, 1);
		status += MXL_ControlWrite(fe, RFSYN_CHP_GAIN, 2);

	}
	if (state->Mod_Type == MXL_ANALOG_CABLE) {
		/* Analog Cable Mode */
		/* state->Mode = MXL_DIGITAL_MODE; */

		state->AGC_Mode = 1; /* Single AGC Mode */

		/* Disable RSSI */
		status += MXL_ControlWrite(fe, SEQ_EXTSYNTHCALIF, 1);
		status += MXL_ControlWrite(fe, SEQ_EXTDCCAL, 1);
		status += MXL_ControlWrite(fe, AGC_EN_RSSI, 0);
		status += MXL_ControlWrite(fe, RFA_ENCLKRFAGC, 1);
		/* change for 2.6.3 */
		status += MXL_ControlWrite(fe, AGC_IF, 1);
		status += MXL_ControlWrite(fe, AGC_RF, 15);
		status += MXL_ControlWrite(fe, BB_IQSWAP, 1);
	}

	if (state->Mod_Type == MXL_ANALOG_OTA) {
		/* Analog OTA Terrestrial mode add for 2.6.7 */
		/* state->Mode = MXL_ANALOG_MODE; */

		/* Enable RSSI */
		status += MXL_ControlWrite(fe, SEQ_EXTSYNTHCALIF, 1);
		status += MXL_ControlWrite(fe, SEQ_EXTDCCAL, 1);
		status += MXL_ControlWrite(fe, AGC_EN_RSSI, 1);
		status += MXL_ControlWrite(fe, RFA_ENCLKRFAGC, 1);

		/* RSSI reference point */
		status += MXL_ControlWrite(fe, RFA_RSSI_REFH, 5);
		status += MXL_ControlWrite(fe, RFA_RSSI_REF, 3);
		status += MXL_ControlWrite(fe, RFA_RSSI_REFL, 2);
		status += MXL_ControlWrite(fe, RFSYN_CHP_GAIN, 3);
		status += MXL_ControlWrite(fe, BB_IQSWAP, 1);
	}

	/* RSSI disable */
	if (state->EN_RSSI == 0) {
		status += MXL_ControlWrite(fe, SEQ_EXTSYNTHCALIF, 1);
		status += MXL_ControlWrite(fe, SEQ_EXTDCCAL, 1);
		status += MXL_ControlWrite(fe, AGC_EN_RSSI, 0);
		status += MXL_ControlWrite(fe, RFA_ENCLKRFAGC, 1);
	}

	return status;
}

static u16 MXL_IFSynthInit(struct dvb_frontend *fe)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	u16 status = 0 ;
	u32	Fref = 0 ;
	u32	Kdbl, intModVal ;
	u32	fracModVal ;
	Kdbl = 2 ;

	if (state->Fxtal >= 12000000UL && state->Fxtal <= 16000000UL)
		Kdbl = 2 ;
	if (state->Fxtal > 16000000UL && state->Fxtal <= 32000000UL)
		Kdbl = 1 ;

	/* IF Synthesizer Control */
	if (state->Mode == 0 && state->IF_Mode == 1) /* Analog Low IF mode */ {
		if (state->IF_LO == 41000000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x08);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x0C);
			Fref = 328000000UL ;
		}
		if (state->IF_LO == 47000000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x08);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 376000000UL ;
		}
		if (state->IF_LO == 54000000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x10);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x0C);
			Fref = 324000000UL ;
		}
		if (state->IF_LO == 60000000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x10);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 360000000UL ;
		}
		if (state->IF_LO == 39250000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x08);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x0C);
			Fref = 314000000UL ;
		}
		if (state->IF_LO == 39650000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x08);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x0C);
			Fref = 317200000UL ;
		}
		if (state->IF_LO == 40150000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x08);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x0C);
			Fref = 321200000UL ;
		}
		if (state->IF_LO == 40650000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x08);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x0C);
			Fref = 325200000UL ;
		}
	}

	if (state->Mode || (state->Mode == 0 && state->IF_Mode == 0)) {
		if (state->IF_LO == 57000000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x10);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 342000000UL ;
		}
		if (state->IF_LO == 44000000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x08);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 352000000UL ;
		}
		if (state->IF_LO == 43750000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x08);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 350000000UL ;
		}
		if (state->IF_LO == 36650000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x04);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 366500000UL ;
		}
		if (state->IF_LO == 36150000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x04);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 361500000UL ;
		}
		if (state->IF_LO == 36000000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x04);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 360000000UL ;
		}
		if (state->IF_LO == 35250000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x04);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 352500000UL ;
		}
		if (state->IF_LO == 34750000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x04);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 347500000UL ;
		}
		if (state->IF_LO == 6280000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x07);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 376800000UL ;
		}
		if (state->IF_LO == 5000000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x09);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 360000000UL ;
		}
		if (state->IF_LO == 4500000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x06);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 360000000UL ;
		}
		if (state->IF_LO == 4570000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x06);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 365600000UL ;
		}
		if (state->IF_LO == 4000000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x05);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 360000000UL ;
		}
		if (state->IF_LO == 57400000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x10);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 344400000UL ;
		}
		if (state->IF_LO == 44400000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x08);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 355200000UL ;
		}
		if (state->IF_LO == 44150000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x08);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 353200000UL ;
		}
		if (state->IF_LO == 37050000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x04);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 370500000UL ;
		}
		if (state->IF_LO == 36550000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x04);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 365500000UL ;
		}
		if (state->IF_LO == 36125000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x04);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 361250000UL ;
		}
		if (state->IF_LO == 6000000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x07);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 360000000UL ;
		}
		if (state->IF_LO == 5400000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x07);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x0C);
			Fref = 324000000UL ;
		}
		if (state->IF_LO == 5380000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x07);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x0C);
			Fref = 322800000UL ;
		}
		if (state->IF_LO == 5200000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x09);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 374400000UL ;
		}
		if (state->IF_LO == 4900000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x09);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 352800000UL ;
		}
		if (state->IF_LO == 4400000UL) {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x06);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 352000000UL ;
		}
		if (state->IF_LO == 4063000UL)  /* add for 2.6.8 */ {
			status += MXL_ControlWrite(fe, IF_DIVVAL,   0x05);
			status += MXL_ControlWrite(fe, IF_VCO_BIAS, 0x08);
			Fref = 365670000UL ;
		}
	}
	/* CHCAL_INT_MOD_IF */
	/* CHCAL_FRAC_MOD_IF */
	intModVal = Fref / (state->Fxtal * Kdbl/2);
	status += MXL_ControlWrite(fe, CHCAL_INT_MOD_IF, intModVal);

	fracModVal = (2<<15)*(Fref/1000 - (state->Fxtal/1000 * Kdbl/2) *
		intModVal);

	fracModVal = fracModVal / ((state->Fxtal * Kdbl/2)/1000);
	status += MXL_ControlWrite(fe, CHCAL_FRAC_MOD_IF, fracModVal);

	return status ;
}

static u16 MXL_TuneRF(struct dvb_frontend *fe, u32 RF_Freq)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	u16 status = 0;
	u32 divider_val, E3, E4, E5, E5A;
	u32 Fmax, Fmin, FmaxBin, FminBin;
	u32 Kdbl_RF = 2;
	u32 tg_divval;
	u32 tg_lo;

	u32 Fref_TG;
	u32 Fvco;

	state->RF_IN = RF_Freq;

	MXL_SynthRFTGLO_Calc(fe);

	if (state->Fxtal >= 12000000UL && state->Fxtal <= 22000000UL)
		Kdbl_RF = 2;
	if (state->Fxtal > 22000000 && state->Fxtal <= 32000000)
		Kdbl_RF = 1;

	/* Downconverter Controls
	 * Look-Up Table Implementation for:
	 *	DN_POLY
	 *	DN_RFGAIN
	 *	DN_CAP_RFLPF
	 *	DN_EN_VHFUHFBAR
	 *	DN_GAIN_ADJUST
	 *  Change the boundary reference from RF_IN to RF_LO
	 */
	if (state->RF_LO < 40000000UL)
		return -1;

	if (state->RF_LO >= 40000000UL && state->RF_LO <= 75000000UL) {
		status += MXL_ControlWrite(fe, DN_POLY,              2);
		status += MXL_ControlWrite(fe, DN_RFGAIN,            3);
		status += MXL_ControlWrite(fe, DN_CAP_RFLPF,         423);
		status += MXL_ControlWrite(fe, DN_EN_VHFUHFBAR,      1);
		status += MXL_ControlWrite(fe, DN_GAIN_ADJUST,       1);
	}
	if (state->RF_LO > 75000000UL && state->RF_LO <= 100000000UL) {
		status += MXL_ControlWrite(fe, DN_POLY,              3);
		status += MXL_ControlWrite(fe, DN_RFGAIN,            3);
		status += MXL_ControlWrite(fe, DN_CAP_RFLPF,         222);
		status += MXL_ControlWrite(fe, DN_EN_VHFUHFBAR,      1);
		status += MXL_ControlWrite(fe, DN_GAIN_ADJUST,       1);
	}
	if (state->RF_LO > 100000000UL && state->RF_LO <= 150000000UL) {
		status += MXL_ControlWrite(fe, DN_POLY,              3);
		status += MXL_ControlWrite(fe, DN_RFGAIN,            3);
		status += MXL_ControlWrite(fe, DN_CAP_RFLPF,         147);
		status += MXL_ControlWrite(fe, DN_EN_VHFUHFBAR,      1);
		status += MXL_ControlWrite(fe, DN_GAIN_ADJUST,       2);
	}
	if (state->RF_LO > 150000000UL && state->RF_LO <= 200000000UL) {
		status += MXL_ControlWrite(fe, DN_POLY,              3);
		status += MXL_ControlWrite(fe, DN_RFGAIN,            3);
		status += MXL_ControlWrite(fe, DN_CAP_RFLPF,         9);
		status += MXL_ControlWrite(fe, DN_EN_VHFUHFBAR,      1);
		status += MXL_ControlWrite(fe, DN_GAIN_ADJUST,       2);
	}
	if (state->RF_LO > 200000000UL && state->RF_LO <= 300000000UL) {
		status += MXL_ControlWrite(fe, DN_POLY,              3);
		status += MXL_ControlWrite(fe, DN_RFGAIN,            3);
		status += MXL_ControlWrite(fe, DN_CAP_RFLPF,         0);
		status += MXL_ControlWrite(fe, DN_EN_VHFUHFBAR,      1);
		status += MXL_ControlWrite(fe, DN_GAIN_ADJUST,       3);
	}
	if (state->RF_LO > 300000000UL && state->RF_LO <= 650000000UL) {
		status += MXL_ControlWrite(fe, DN_POLY,              3);
		status += MXL_ControlWrite(fe, DN_RFGAIN,            1);
		status += MXL_ControlWrite(fe, DN_CAP_RFLPF,         0);
		status += MXL_ControlWrite(fe, DN_EN_VHFUHFBAR,      0);
		status += MXL_ControlWrite(fe, DN_GAIN_ADJUST,       3);
	}
	if (state->RF_LO > 650000000UL && state->RF_LO <= 900000000UL) {
		status += MXL_ControlWrite(fe, DN_POLY,              3);
		status += MXL_ControlWrite(fe, DN_RFGAIN,            2);
		status += MXL_ControlWrite(fe, DN_CAP_RFLPF,         0);
		status += MXL_ControlWrite(fe, DN_EN_VHFUHFBAR,      0);
		status += MXL_ControlWrite(fe, DN_GAIN_ADJUST,       3);
	}
	if (state->RF_LO > 900000000UL)
		return -1;

	/*	DN_IQTNBUF_AMP */
	/*	DN_IQTNGNBFBIAS_BST */
	if (state->RF_LO >= 40000000UL && state->RF_LO <= 75000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 75000000UL && state->RF_LO <= 100000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 100000000UL && state->RF_LO <= 150000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 150000000UL && state->RF_LO <= 200000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 200000000UL && state->RF_LO <= 300000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 300000000UL && state->RF_LO <= 400000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 400000000UL && state->RF_LO <= 450000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 450000000UL && state->RF_LO <= 500000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 500000000UL && state->RF_LO <= 550000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 550000000UL && state->RF_LO <= 600000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 600000000UL && state->RF_LO <= 650000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 650000000UL && state->RF_LO <= 700000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 700000000UL && state->RF_LO <= 750000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 750000000UL && state->RF_LO <= 800000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       1);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  0);
	}
	if (state->RF_LO > 800000000UL && state->RF_LO <= 850000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       10);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  1);
	}
	if (state->RF_LO > 850000000UL && state->RF_LO <= 900000000UL) {
		status += MXL_ControlWrite(fe, DN_IQTNBUF_AMP,       10);
		status += MXL_ControlWrite(fe, DN_IQTNGNBFBIAS_BST,  1);
	}

	/*
	 * Set RF Synth and LO Path Control
	 *
	 * Look-Up table implementation for:
	 *	RFSYN_EN_OUTMUX
	 *	RFSYN_SEL_VCO_OUT
	 *	RFSYN_SEL_VCO_HI
	 *  RFSYN_SEL_DIVM
	 *	RFSYN_RF_DIV_BIAS
	 *	DN_SEL_FREQ
	 *
	 * Set divider_val, Fmax, Fmix to use in Equations
	 */
	FminBin = 28000000UL ;
	FmaxBin = 42500000UL ;
	if (state->RF_LO >= 40000000UL && state->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX,     1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT,   0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI,    0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM,      0);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS,   1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ,         1);
		divider_val = 64 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 42500000UL ;
	FmaxBin = 56000000UL ;
	if (state->RF_LO > FminBin && state->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX,     1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT,   0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI,    1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM,      0);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS,   1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ,         1);
		divider_val = 64 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 56000000UL ;
	FmaxBin = 85000000UL ;
	if (state->RF_LO > FminBin && state->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX,     0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT,   1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI,    0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM,      0);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS,   1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ,         1);
		divider_val = 32 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 85000000UL ;
	FmaxBin = 112000000UL ;
	if (state->RF_LO > FminBin && state->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX,     0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT,   1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI,    1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM,      0);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS,   1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ,         1);
		divider_val = 32 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 112000000UL ;
	FmaxBin = 170000000UL ;
	if (state->RF_LO > FminBin && state->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX,     0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT,   1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI,    0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM,      0);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS,   1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ,         2);
		divider_val = 16 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 170000000UL ;
	FmaxBin = 225000000UL ;
	if (state->RF_LO > FminBin && state->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX,     0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT,   1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI,    1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM,      0);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS,   1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ,         2);
		divider_val = 16 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 225000000UL ;
	FmaxBin = 300000000UL ;
	if (state->RF_LO > FminBin && state->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX,     0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT,   1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI,    0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM,      0);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS,   1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ,         4);
		divider_val = 8 ;
		Fmax = 340000000UL ;
		Fmin = FminBin ;
	}
	FminBin = 300000000UL ;
	FmaxBin = 340000000UL ;
	if (state->RF_LO > FminBin && state->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX,     1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT,   0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI,    0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM,      0);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS,   1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ,         0);
		divider_val = 8 ;
		Fmax = FmaxBin ;
		Fmin = 225000000UL ;
	}
	FminBin = 340000000UL ;
	FmaxBin = 450000000UL ;
	if (state->RF_LO > FminBin && state->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX,     1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT,   0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI,    1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM,      0);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS,   2);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ,         0);
		divider_val = 8 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 450000000UL ;
	FmaxBin = 680000000UL ;
	if (state->RF_LO > FminBin && state->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX,     0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT,   1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI,    0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM,      1);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS,   1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ,         0);
		divider_val = 4 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 680000000UL ;
	FmaxBin = 900000000UL ;
	if (state->RF_LO > FminBin && state->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX,     0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT,   1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI,    1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM,      1);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS,   1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ,         0);
		divider_val = 4 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}

	/*	CHCAL_INT_MOD_RF
	 *	CHCAL_FRAC_MOD_RF
	 *	RFSYN_LPF_R
	 *	CHCAL_EN_INT_RF
	 */
	/* Equation E3 RFSYN_VCO_BIAS */
	E3 = (((Fmax-state->RF_LO)/1000)*32)/((Fmax-Fmin)/1000) + 8 ;
	status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, E3);

	/* Equation E4 CHCAL_INT_MOD_RF */
	E4 = (state->RF_LO*divider_val/1000)/(2*state->Fxtal*Kdbl_RF/1000);
	MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, E4);

	/* Equation E5 CHCAL_FRAC_MOD_RF CHCAL_EN_INT_RF */
	E5 = ((2<<17)*(state->RF_LO/10000*divider_val -
		(E4*(2*state->Fxtal*Kdbl_RF)/10000))) /
		(2*state->Fxtal*Kdbl_RF/10000);

	status += MXL_ControlWrite(fe, CHCAL_FRAC_MOD_RF, E5);

	/* Equation E5A RFSYN_LPF_R */
	E5A = (((Fmax - state->RF_LO)/1000)*4/((Fmax-Fmin)/1000)) + 1 ;
	status += MXL_ControlWrite(fe, RFSYN_LPF_R, E5A);

	/* Euqation E5B CHCAL_EN_INIT_RF */
	status += MXL_ControlWrite(fe, CHCAL_EN_INT_RF, ((E5 == 0) ? 1 : 0));
	/*if (E5 == 0)
	 *	status += MXL_ControlWrite(fe, CHCAL_EN_INT_RF, 1);
	 *else
	 *	status += MXL_ControlWrite(fe, CHCAL_FRAC_MOD_RF, E5);
	 */

	/*
	 * Set TG Synth
	 *
	 * Look-Up table implementation for:
	 *	TG_LO_DIVVAL
	 *	TG_LO_SELVAL
	 *
	 * Set divider_val, Fmax, Fmix to use in Equations
	 */
	if (state->TG_LO < 33000000UL)
		return -1;

	FminBin = 33000000UL ;
	FmaxBin = 50000000UL ;
	if (state->TG_LO >= FminBin && state->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, TG_LO_DIVVAL,	0x6);
		status += MXL_ControlWrite(fe, TG_LO_SELVAL,	0x0);
		divider_val = 36 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 50000000UL ;
	FmaxBin = 67000000UL ;
	if (state->TG_LO > FminBin && state->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, TG_LO_DIVVAL,	0x1);
		status += MXL_ControlWrite(fe, TG_LO_SELVAL,	0x0);
		divider_val = 24 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 67000000UL ;
	FmaxBin = 100000000UL ;
	if (state->TG_LO > FminBin && state->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, TG_LO_DIVVAL,	0xC);
		status += MXL_ControlWrite(fe, TG_LO_SELVAL,	0x2);
		divider_val = 18 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 100000000UL ;
	FmaxBin = 150000000UL ;
	if (state->TG_LO > FminBin && state->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, TG_LO_DIVVAL,	0x8);
		status += MXL_ControlWrite(fe, TG_LO_SELVAL,	0x2);
		divider_val = 12 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 150000000UL ;
	FmaxBin = 200000000UL ;
	if (state->TG_LO > FminBin && state->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, TG_LO_DIVVAL,	0x0);
		status += MXL_ControlWrite(fe, TG_LO_SELVAL,	0x2);
		divider_val = 8 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 200000000UL ;
	FmaxBin = 300000000UL ;
	if (state->TG_LO > FminBin && state->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, TG_LO_DIVVAL,	0x8);
		status += MXL_ControlWrite(fe, TG_LO_SELVAL,	0x3);
		divider_val = 6 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 300000000UL ;
	FmaxBin = 400000000UL ;
	if (state->TG_LO > FminBin && state->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, TG_LO_DIVVAL,	0x0);
		status += MXL_ControlWrite(fe, TG_LO_SELVAL,	0x3);
		divider_val = 4 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 400000000UL ;
	FmaxBin = 600000000UL ;
	if (state->TG_LO > FminBin && state->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, TG_LO_DIVVAL,	0x8);
		status += MXL_ControlWrite(fe, TG_LO_SELVAL,	0x7);
		divider_val = 3 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 600000000UL ;
	FmaxBin = 900000000UL ;
	if (state->TG_LO > FminBin && state->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(fe, TG_LO_DIVVAL,	0x0);
		status += MXL_ControlWrite(fe, TG_LO_SELVAL,	0x7);
		divider_val = 2 ;
	}

	/* TG_DIV_VAL */
	tg_divval = (state->TG_LO*divider_val/100000) *
		(MXL_Ceiling(state->Fxtal, 1000000) * 100) /
		(state->Fxtal/1000);

	status += MXL_ControlWrite(fe, TG_DIV_VAL, tg_divval);

	if (state->TG_LO > 600000000UL)
		status += MXL_ControlWrite(fe, TG_DIV_VAL, tg_divval + 1);

	Fmax = 1800000000UL ;
	Fmin = 1200000000UL ;

	/* prevent overflow of 32 bit unsigned integer, use
	 * following equation. Edit for v2.6.4
	 */
	/* Fref_TF = Fref_TG * 1000 */
	Fref_TG = (state->Fxtal/1000) / MXL_Ceiling(state->Fxtal, 1000000);

	/* Fvco = Fvco/10 */
	Fvco = (state->TG_LO/10000) * divider_val * Fref_TG;

	tg_lo = (((Fmax/10 - Fvco)/100)*32) / ((Fmax-Fmin)/1000)+8;

	/* below equation is same as above but much harder to debug.
	 *
	 * static u32 MXL_GetXtalInt(u32 Xtal_Freq)
	 * {
	 *	if ((Xtal_Freq % 1000000) == 0)
	 *		return (Xtal_Freq / 10000);
	 *	else
	 *		return (((Xtal_Freq / 1000000) + 1)*100);
	 * }
	 *
	 * u32 Xtal_Int = MXL_GetXtalInt(state->Fxtal);
	 * tg_lo = ( ((Fmax/10000 * Xtal_Int)/100) -
	 * ((state->TG_LO/10000)*divider_val *
	 * (state->Fxtal/10000)/100) )*32/((Fmax-Fmin)/10000 *
	 * Xtal_Int/100) + 8;
	 */

	status += MXL_ControlWrite(fe, TG_VCO_BIAS , tg_lo);

	/* add for 2.6.5 Special setting for QAM */
	if (state->Mod_Type == MXL_QAM) {
		if (state->config->qam_gain != 0)
			status += MXL_ControlWrite(fe, RFSYN_CHP_GAIN,
						   state->config->qam_gain);
		else if (state->RF_IN < 680000000)
			status += MXL_ControlWrite(fe, RFSYN_CHP_GAIN, 3);
		else
			status += MXL_ControlWrite(fe, RFSYN_CHP_GAIN, 2);
	}

	/* Off Chip Tracking Filter Control */
	if (state->TF_Type == MXL_TF_OFF) {
		/* Tracking Filter Off State; turn off all the banks */
		status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
		status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
		status += MXL_SetGPIO(fe, 3, 1); /* Bank1 Off */
		status += MXL_SetGPIO(fe, 1, 1); /* Bank2 Off */
		status += MXL_SetGPIO(fe, 4, 1); /* Bank3 Off */
	}

	if (state->TF_Type == MXL_TF_C) /* Tracking Filter type C */ {
		status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
		status += MXL_ControlWrite(fe, DAC_DIN_A, 0);

		if (state->RF_IN >= 43000000 && state->RF_IN < 150000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
			status += MXL_ControlWrite(fe, DAC_DIN_B, 0);
			status += MXL_SetGPIO(fe, 3, 0);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 4, 1);
		}
		if (state->RF_IN >= 150000000 && state->RF_IN < 280000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
			status += MXL_ControlWrite(fe, DAC_DIN_B, 0);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 4, 1);
		}
		if (state->RF_IN >= 280000000 && state->RF_IN < 360000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
			status += MXL_ControlWrite(fe, DAC_DIN_B, 0);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 4, 0);
		}
		if (state->RF_IN >= 360000000 && state->RF_IN < 560000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
			status += MXL_ControlWrite(fe, DAC_DIN_B, 0);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 4, 0);
		}
		if (state->RF_IN >= 560000000 && state->RF_IN < 580000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
			status += MXL_ControlWrite(fe, DAC_DIN_B, 29);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 4, 0);
		}
		if (state->RF_IN >= 580000000 && state->RF_IN < 630000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
			status += MXL_ControlWrite(fe, DAC_DIN_B, 0);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 4, 0);
		}
		if (state->RF_IN >= 630000000 && state->RF_IN < 700000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
			status += MXL_ControlWrite(fe, DAC_DIN_B, 16);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 4, 1);
		}
		if (state->RF_IN >= 700000000 && state->RF_IN < 760000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
			status += MXL_ControlWrite(fe, DAC_DIN_B, 7);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 4, 1);
		}
		if (state->RF_IN >= 760000000 && state->RF_IN <= 900000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
			status += MXL_ControlWrite(fe, DAC_DIN_B, 0);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 4, 1);
		}
	}

	if (state->TF_Type == MXL_TF_C_H) {

		/* Tracking Filter type C-H for Hauppauge only */
		status += MXL_ControlWrite(fe, DAC_DIN_A, 0);

		if (state->RF_IN >= 43000000 && state->RF_IN < 150000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 0);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 1);
		}
		if (state->RF_IN >= 150000000 && state->RF_IN < 280000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 3, 0);
			status += MXL_SetGPIO(fe, 1, 1);
		}
		if (state->RF_IN >= 280000000 && state->RF_IN < 360000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 3, 0);
			status += MXL_SetGPIO(fe, 1, 0);
		}
		if (state->RF_IN >= 360000000 && state->RF_IN < 560000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 0);
		}
		if (state->RF_IN >= 560000000 && state->RF_IN < 580000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 0);
		}
		if (state->RF_IN >= 580000000 && state->RF_IN < 630000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 0);
		}
		if (state->RF_IN >= 630000000 && state->RF_IN < 700000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 1);
		}
		if (state->RF_IN >= 700000000 && state->RF_IN < 760000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 1);
		}
		if (state->RF_IN >= 760000000 && state->RF_IN <= 900000000) {
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 1);
		}
	}

	if (state->TF_Type == MXL_TF_D) { /* Tracking Filter type D */

		status += MXL_ControlWrite(fe, DAC_DIN_B, 0);

		if (state->RF_IN >= 43000000 && state->RF_IN < 174000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 0);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 174000000 && state->RF_IN < 250000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 0);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 250000000 && state->RF_IN < 310000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 310000000 && state->RF_IN < 360000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 360000000 && state->RF_IN < 470000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 470000000 && state->RF_IN < 640000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 640000000 && state->RF_IN <= 900000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 1);
		}
	}

	if (state->TF_Type == MXL_TF_D_L) {

		/* Tracking Filter type D-L for Lumanate ONLY change 2.6.3 */
		status += MXL_ControlWrite(fe, DAC_DIN_A, 0);

		/* if UHF and terrestrial => Turn off Tracking Filter */
		if (state->RF_IN >= 471000000 &&
			(state->RF_IN - 471000000)%6000000 != 0) {
			/* Turn off all the banks */
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
			status += MXL_ControlWrite(fe, AGC_IF, 10);
		} else {
			/* if VHF or cable => Turn on Tracking Filter */
			if (state->RF_IN >= 43000000 &&
				state->RF_IN < 140000000) {

				status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
				status += MXL_SetGPIO(fe, 4, 1);
				status += MXL_SetGPIO(fe, 1, 1);
				status += MXL_SetGPIO(fe, 3, 0);
			}
			if (state->RF_IN >= 140000000 &&
				state->RF_IN < 240000000) {
				status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
				status += MXL_SetGPIO(fe, 4, 1);
				status += MXL_SetGPIO(fe, 1, 0);
				status += MXL_SetGPIO(fe, 3, 0);
			}
			if (state->RF_IN >= 240000000 &&
				state->RF_IN < 340000000) {
				status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
				status += MXL_SetGPIO(fe, 4, 0);
				status += MXL_SetGPIO(fe, 1, 1);
				status += MXL_SetGPIO(fe, 3, 0);
			}
			if (state->RF_IN >= 340000000 &&
				state->RF_IN < 430000000) {
				status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
				status += MXL_SetGPIO(fe, 4, 0);
				status += MXL_SetGPIO(fe, 1, 0);
				status += MXL_SetGPIO(fe, 3, 1);
			}
			if (state->RF_IN >= 430000000 &&
				state->RF_IN < 470000000) {
				status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
				status += MXL_SetGPIO(fe, 4, 1);
				status += MXL_SetGPIO(fe, 1, 0);
				status += MXL_SetGPIO(fe, 3, 1);
			}
			if (state->RF_IN >= 470000000 &&
				state->RF_IN < 570000000) {
				status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
				status += MXL_SetGPIO(fe, 4, 0);
				status += MXL_SetGPIO(fe, 1, 0);
				status += MXL_SetGPIO(fe, 3, 1);
			}
			if (state->RF_IN >= 570000000 &&
				state->RF_IN < 620000000) {
				status += MXL_ControlWrite(fe, DAC_A_ENABLE, 0);
				status += MXL_SetGPIO(fe, 4, 0);
				status += MXL_SetGPIO(fe, 1, 1);
				status += MXL_SetGPIO(fe, 3, 1);
			}
			if (state->RF_IN >= 620000000 &&
				state->RF_IN < 760000000) {
				status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
				status += MXL_SetGPIO(fe, 4, 0);
				status += MXL_SetGPIO(fe, 1, 1);
				status += MXL_SetGPIO(fe, 3, 1);
			}
			if (state->RF_IN >= 760000000 &&
				state->RF_IN <= 900000000) {
				status += MXL_ControlWrite(fe, DAC_A_ENABLE, 1);
				status += MXL_SetGPIO(fe, 4, 1);
				status += MXL_SetGPIO(fe, 1, 1);
				status += MXL_SetGPIO(fe, 3, 1);
			}
		}
	}

	if (state->TF_Type == MXL_TF_E) /* Tracking Filter type E */ {

		status += MXL_ControlWrite(fe, DAC_DIN_B, 0);

		if (state->RF_IN >= 43000000 && state->RF_IN < 174000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 0);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 174000000 && state->RF_IN < 250000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 0);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 250000000 && state->RF_IN < 310000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 310000000 && state->RF_IN < 360000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 360000000 && state->RF_IN < 470000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 470000000 && state->RF_IN < 640000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 640000000 && state->RF_IN <= 900000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 1);
		}
	}

	if (state->TF_Type == MXL_TF_F) {

		/* Tracking Filter type F */
		status += MXL_ControlWrite(fe, DAC_DIN_B, 0);

		if (state->RF_IN >= 43000000 && state->RF_IN < 160000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 0);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 160000000 && state->RF_IN < 210000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 0);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 210000000 && state->RF_IN < 300000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 300000000 && state->RF_IN < 390000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 390000000 && state->RF_IN < 515000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 515000000 && state->RF_IN < 650000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 650000000 && state->RF_IN <= 900000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 1);
		}
	}

	if (state->TF_Type == MXL_TF_E_2) {

		/* Tracking Filter type E_2 */
		status += MXL_ControlWrite(fe, DAC_DIN_B, 0);

		if (state->RF_IN >= 43000000 && state->RF_IN < 174000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 0);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 174000000 && state->RF_IN < 250000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 0);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 250000000 && state->RF_IN < 350000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 350000000 && state->RF_IN < 400000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 400000000 && state->RF_IN < 570000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 570000000 && state->RF_IN < 770000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 770000000 && state->RF_IN <= 900000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 1);
		}
	}

	if (state->TF_Type == MXL_TF_G) {

		/* Tracking Filter type G add for v2.6.8 */
		status += MXL_ControlWrite(fe, DAC_DIN_B, 0);

		if (state->RF_IN >= 50000000 && state->RF_IN < 190000000) {

			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 0);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 190000000 && state->RF_IN < 280000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 0);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 280000000 && state->RF_IN < 350000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 350000000 && state->RF_IN < 400000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 400000000 && state->RF_IN < 470000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 470000000 && state->RF_IN < 640000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 640000000 && state->RF_IN < 820000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 820000000 && state->RF_IN <= 900000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 1);
		}
	}

	if (state->TF_Type == MXL_TF_E_NA) {

		/* Tracking Filter type E-NA for Empia ONLY change for 2.6.8 */
		status += MXL_ControlWrite(fe, DAC_DIN_B, 0);

		/* if UHF and terrestrial=> Turn off Tracking Filter */
		if (state->RF_IN >= 471000000 &&
			(state->RF_IN - 471000000)%6000000 != 0) {

			/* Turn off all the banks */
			status += MXL_SetGPIO(fe, 3, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);

			/* 2.6.12 Turn on RSSI */
			status += MXL_ControlWrite(fe, SEQ_EXTSYNTHCALIF, 1);
			status += MXL_ControlWrite(fe, SEQ_EXTDCCAL, 1);
			status += MXL_ControlWrite(fe, AGC_EN_RSSI, 1);
			status += MXL_ControlWrite(fe, RFA_ENCLKRFAGC, 1);

			/* RSSI reference point */
			status += MXL_ControlWrite(fe, RFA_RSSI_REFH, 5);
			status += MXL_ControlWrite(fe, RFA_RSSI_REF, 3);
			status += MXL_ControlWrite(fe, RFA_RSSI_REFL, 2);

			/* following parameter is from analog OTA mode,
			 * can be change to seek better performance */
			status += MXL_ControlWrite(fe, RFSYN_CHP_GAIN, 3);
		} else {
		/* if VHF or Cable =>  Turn on Tracking Filter */

		/* 2.6.12 Turn off RSSI */
		status += MXL_ControlWrite(fe, AGC_EN_RSSI, 0);

		/* change back from above condition */
		status += MXL_ControlWrite(fe, RFSYN_CHP_GAIN, 5);


		if (state->RF_IN >= 43000000 && state->RF_IN < 174000000) {

			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 0);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 174000000 && state->RF_IN < 250000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 0);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 250000000 && state->RF_IN < 350000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		if (state->RF_IN >= 350000000 && state->RF_IN < 400000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 0);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 400000000 && state->RF_IN < 570000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 0);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 570000000 && state->RF_IN < 770000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 0);
		}
		if (state->RF_IN >= 770000000 && state->RF_IN <= 900000000) {
			status += MXL_ControlWrite(fe, DAC_B_ENABLE, 1);
			status += MXL_SetGPIO(fe, 4, 1);
			status += MXL_SetGPIO(fe, 1, 1);
			status += MXL_SetGPIO(fe, 3, 1);
		}
		}
	}
	return status ;
}

static u16 MXL_SetGPIO(struct dvb_frontend *fe, u8 GPIO_Num, u8 GPIO_Val)
{
	u16 status = 0;

	if (GPIO_Num == 1)
		status += MXL_ControlWrite(fe, GPIO_1B, GPIO_Val ? 0 : 1);

	/* GPIO2 is not available */

	if (GPIO_Num == 3) {
		if (GPIO_Val == 1) {
			status += MXL_ControlWrite(fe, GPIO_3, 0);
			status += MXL_ControlWrite(fe, GPIO_3B, 0);
		}
		if (GPIO_Val == 0) {
			status += MXL_ControlWrite(fe, GPIO_3, 1);
			status += MXL_ControlWrite(fe, GPIO_3B, 1);
		}
		if (GPIO_Val == 3) { /* tri-state */
			status += MXL_ControlWrite(fe, GPIO_3, 0);
			status += MXL_ControlWrite(fe, GPIO_3B, 1);
		}
	}
	if (GPIO_Num == 4) {
		if (GPIO_Val == 1) {
			status += MXL_ControlWrite(fe, GPIO_4, 0);
			status += MXL_ControlWrite(fe, GPIO_4B, 0);
		}
		if (GPIO_Val == 0) {
			status += MXL_ControlWrite(fe, GPIO_4, 1);
			status += MXL_ControlWrite(fe, GPIO_4B, 1);
		}
		if (GPIO_Val == 3) { /* tri-state */
			status += MXL_ControlWrite(fe, GPIO_4, 0);
			status += MXL_ControlWrite(fe, GPIO_4B, 1);
		}
	}

	return status;
}

static u16 MXL_ControlWrite(struct dvb_frontend *fe, u16 ControlNum, u32 value)
{
	u16 status = 0;

	/* Will write ALL Matching Control Name */
	/* Write Matching INIT Control */
	status += MXL_ControlWrite_Group(fe, ControlNum, value, 1);
	/* Write Matching CH Control */
	status += MXL_ControlWrite_Group(fe, ControlNum, value, 2);
#ifdef _MXL_INTERNAL
	/* Write Matching MXL Control */
	status += MXL_ControlWrite_Group(fe, ControlNum, value, 3);
#endif
	return status;
}

static u16 MXL_ControlWrite_Group(struct dvb_frontend *fe, u16 controlNum,
	u32 value, u16 controlGroup)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	u16 i, j, k;
	u32 highLimit;
	u32 ctrlVal;

	if (controlGroup == 1) /* Initial Control */ {

		for (i = 0; i < state->Init_Ctrl_Num; i++) {

			if (controlNum == state->Init_Ctrl[i].Ctrl_Num) {

				highLimit = 1 << state->Init_Ctrl[i].size;
				if (value < highLimit) {
					for (j = 0; j < state->Init_Ctrl[i].size; j++) {
						state->Init_Ctrl[i].val[j] = (u8)((value >> j) & 0x01);
						MXL_RegWriteBit(fe, (u8)(state->Init_Ctrl[i].addr[j]),
							(u8)(state->Init_Ctrl[i].bit[j]),
							(u8)((value>>j) & 0x01));
					}
					ctrlVal = 0;
					for (k = 0; k < state->Init_Ctrl[i].size; k++)
						ctrlVal += state->Init_Ctrl[i].val[k] * (1 << k);
				} else
					return -1;
			}
		}
	}
	if (controlGroup == 2) /* Chan change Control */ {

		for (i = 0; i < state->CH_Ctrl_Num; i++) {

			if (controlNum == state->CH_Ctrl[i].Ctrl_Num) {

				highLimit = 1 << state->CH_Ctrl[i].size;
				if (value < highLimit) {
					for (j = 0; j < state->CH_Ctrl[i].size; j++) {
						state->CH_Ctrl[i].val[j] = (u8)((value >> j) & 0x01);
						MXL_RegWriteBit(fe, (u8)(state->CH_Ctrl[i].addr[j]),
							(u8)(state->CH_Ctrl[i].bit[j]),
							(u8)((value>>j) & 0x01));
					}
					ctrlVal = 0;
					for (k = 0; k < state->CH_Ctrl[i].size; k++)
						ctrlVal += state->CH_Ctrl[i].val[k] * (1 << k);
				} else
					return -1;
			}
		}
	}
#ifdef _MXL_INTERNAL
	if (controlGroup == 3) /* Maxlinear Control */ {

		for (i = 0; i < state->MXL_Ctrl_Num; i++) {

			if (controlNum == state->MXL_Ctrl[i].Ctrl_Num) {

				highLimit = (1 << state->MXL_Ctrl[i].size);
				if (value < highLimit) {
					for (j = 0; j < state->MXL_Ctrl[i].size; j++) {
						state->MXL_Ctrl[i].val[j] = (u8)((value >> j) & 0x01);
						MXL_RegWriteBit(fe, (u8)(state->MXL_Ctrl[i].addr[j]),
							(u8)(state->MXL_Ctrl[i].bit[j]),
							(u8)((value>>j) & 0x01));
					}
					ctrlVal = 0;
					for (k = 0; k < state->MXL_Ctrl[i].size; k++)
						ctrlVal += state->
							MXL_Ctrl[i].val[k] *
							(1 << k);
				} else
					return -1;
			}
		}
	}
#endif
	return 0 ; /* successful return */
}

static u16 MXL_RegRead(struct dvb_frontend *fe, u8 RegNum, u8 *RegVal)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	int i ;

	for (i = 0; i < 104; i++) {
		if (RegNum == state->TunerRegs[i].Reg_Num) {
			*RegVal = (u8)(state->TunerRegs[i].Reg_Val);
			return 0;
		}
	}

	return 1;
}

static u16 MXL_ControlRead(struct dvb_frontend *fe, u16 controlNum, u32 *value)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	u32 ctrlVal ;
	u16 i, k ;

	for (i = 0; i < state->Init_Ctrl_Num ; i++) {

		if (controlNum == state->Init_Ctrl[i].Ctrl_Num) {

			ctrlVal = 0;
			for (k = 0; k < state->Init_Ctrl[i].size; k++)
				ctrlVal += state->Init_Ctrl[i].val[k] * (1<<k);
			*value = ctrlVal;
			return 0;
		}
	}

	for (i = 0; i < state->CH_Ctrl_Num ; i++) {

		if (controlNum == state->CH_Ctrl[i].Ctrl_Num) {

			ctrlVal = 0;
			for (k = 0; k < state->CH_Ctrl[i].size; k++)
				ctrlVal += state->CH_Ctrl[i].val[k] * (1 << k);
			*value = ctrlVal;
			return 0;

		}
	}

#ifdef _MXL_INTERNAL
	for (i = 0; i < state->MXL_Ctrl_Num ; i++) {

		if (controlNum == state->MXL_Ctrl[i].Ctrl_Num) {

			ctrlVal = 0;
			for (k = 0; k < state->MXL_Ctrl[i].size; k++)
				ctrlVal += state->MXL_Ctrl[i].val[k] * (1<<k);
			*value = ctrlVal;
			return 0;

		}
	}
#endif
	return 1;
}

static void MXL_RegWriteBit(struct dvb_frontend *fe, u8 address, u8 bit,
	u8 bitVal)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	int i ;

	const u8 AND_MAP[8] = {
		0xFE, 0xFD, 0xFB, 0xF7,
		0xEF, 0xDF, 0xBF, 0x7F } ;

	const u8 OR_MAP[8] = {
		0x01, 0x02, 0x04, 0x08,
		0x10, 0x20, 0x40, 0x80 } ;

	for (i = 0; i < state->TunerRegs_Num; i++) {
		if (state->TunerRegs[i].Reg_Num == address) {
			if (bitVal)
				state->TunerRegs[i].Reg_Val |= OR_MAP[bit];
			else
				state->TunerRegs[i].Reg_Val &= AND_MAP[bit];
			break ;
		}
	}
}

static u32 MXL_Ceiling(u32 value, u32 resolution)
{
	return value / resolution + (value % resolution > 0 ? 1 : 0);
}

/* Retrieve the Initialzation Registers */
static u16 MXL_GetInitRegister(struct dvb_frontend *fe, u8 *RegNum,
	u8 *RegVal, int *count)
{
	u16 status = 0;
	int i ;

	static const u8 RegAddr[] = {
		11, 12, 13, 22, 32, 43, 44, 53, 56, 59, 73,
		76, 77, 91, 134, 135, 137, 147,
		156, 166, 167, 168, 25
	};

	*count = ARRAY_SIZE(RegAddr);

	status += MXL_BlockInit(fe);

	for (i = 0 ; i < *count; i++) {
		RegNum[i] = RegAddr[i];
		status += MXL_RegRead(fe, RegNum[i], &RegVal[i]);
	}

	return status;
}

static u16 MXL_GetCHRegister(struct dvb_frontend *fe, u8 *RegNum, u8 *RegVal,
	int *count)
{
	u16 status = 0;
	int i ;

/* add 77, 166, 167, 168 register for 2.6.12 */
#ifdef _MXL_PRODUCTION
	static const u8 RegAddr[] = {
		14, 15, 16, 17, 22, 43, 65, 68, 69, 70, 73, 92, 93, 106,
		107, 108, 109, 110, 111, 112, 136, 138, 149, 77, 166, 167, 168
	};
#else
	static const u8 RegAddr[] = {
		14, 15, 16, 17, 22, 43, 68, 69, 70, 73, 92, 93, 106,
		107, 108, 109, 110, 111, 112, 136, 138, 149, 77, 166, 167, 168
	};
	/*
	u8 RegAddr[171];
	for (i = 0; i <= 170; i++)
		RegAddr[i] = i;
	*/
#endif

	*count = ARRAY_SIZE(RegAddr);

	for (i = 0 ; i < *count; i++) {
		RegNum[i] = RegAddr[i];
		status += MXL_RegRead(fe, RegNum[i], &RegVal[i]);
	}

	return status;
}

static u16 MXL_GetCHRegister_ZeroIF(struct dvb_frontend *fe, u8 *RegNum,
	u8 *RegVal, int *count)
{
	u16 status = 0;
	int i;

	u8 RegAddr[] = {43, 136};

	*count = ARRAY_SIZE(RegAddr);

	for (i = 0; i < *count; i++) {
		RegNum[i] = RegAddr[i];
		status += MXL_RegRead(fe, RegNum[i], &RegVal[i]);
	}

	return status;
}

static u16 MXL_GetMasterControl(u8 *MasterReg, int state)
{
	if (state == 1) /* Load_Start */
		*MasterReg = 0xF3;
	if (state == 2) /* Power_Down */
		*MasterReg = 0x41;
	if (state == 3) /* Synth_Reset */
		*MasterReg = 0xB1;
	if (state == 4) /* Seq_Off */
		*MasterReg = 0xF1;

	return 0;
}

#ifdef _MXL_PRODUCTION
static u16 MXL_VCORange_Test(struct dvb_frontend *fe, int VCO_Range)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	u16 status = 0 ;

	if (VCO_Range == 1) {
		status += MXL_ControlWrite(fe, RFSYN_EN_DIV, 1);
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX, 0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM, 0);
		status += MXL_ControlWrite(fe, RFSYN_DIVM, 1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT, 1);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS, 1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ, 0);
		if (state->Mode == 0 && state->IF_Mode == 1) {
			/* Analog Low IF Mode */
			status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 1);
			status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 8);
			status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 56);
			status += MXL_ControlWrite(fe,
				CHCAL_FRAC_MOD_RF, 180224);
		}
		if (state->Mode == 0 && state->IF_Mode == 0) {
			/* Analog Zero IF Mode */
			status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 1);
			status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 8);
			status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 56);
			status += MXL_ControlWrite(fe,
				CHCAL_FRAC_MOD_RF, 222822);
		}
		if (state->Mode == 1) /* Digital Mode */ {
			status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 1);
			status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 8);
			status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 56);
			status += MXL_ControlWrite(fe,
				CHCAL_FRAC_MOD_RF, 229376);
		}
	}

	if (VCO_Range == 2) {
		status += MXL_ControlWrite(fe, RFSYN_EN_DIV, 1);
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX, 0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM, 0);
		status += MXL_ControlWrite(fe, RFSYN_DIVM, 1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT, 1);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS, 1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ, 0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 1);
		status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 40);
		status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 41);
		if (state->Mode == 0 && state->IF_Mode == 1) {
			/* Analog Low IF Mode */
			status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 1);
			status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 40);
			status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 42);
			status += MXL_ControlWrite(fe,
				CHCAL_FRAC_MOD_RF, 206438);
		}
		if (state->Mode == 0 && state->IF_Mode == 0) {
			/* Analog Zero IF Mode */
			status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 1);
			status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 40);
			status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 42);
			status += MXL_ControlWrite(fe,
				CHCAL_FRAC_MOD_RF, 206438);
		}
		if (state->Mode == 1) /* Digital Mode */ {
			status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 1);
			status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 40);
			status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 41);
			status += MXL_ControlWrite(fe,
				CHCAL_FRAC_MOD_RF, 16384);
		}
	}

	if (VCO_Range == 3) {
		status += MXL_ControlWrite(fe, RFSYN_EN_DIV, 1);
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX, 0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM, 0);
		status += MXL_ControlWrite(fe, RFSYN_DIVM, 1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT, 1);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS, 1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ, 0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 0);
		status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 8);
		status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 42);
		if (state->Mode == 0 && state->IF_Mode == 1) {
			/* Analog Low IF Mode */
			status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 0);
			status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 8);
			status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 44);
			status += MXL_ControlWrite(fe,
				CHCAL_FRAC_MOD_RF, 173670);
		}
		if (state->Mode == 0 && state->IF_Mode == 0) {
			/* Analog Zero IF Mode */
			status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 0);
			status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 8);
			status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 44);
			status += MXL_ControlWrite(fe,
				CHCAL_FRAC_MOD_RF, 173670);
		}
		if (state->Mode == 1) /* Digital Mode */ {
			status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 0);
			status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 8);
			status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 42);
			status += MXL_ControlWrite(fe,
				CHCAL_FRAC_MOD_RF, 245760);
		}
	}

	if (VCO_Range == 4) {
		status += MXL_ControlWrite(fe, RFSYN_EN_DIV, 1);
		status += MXL_ControlWrite(fe, RFSYN_EN_OUTMUX, 0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_DIVM, 0);
		status += MXL_ControlWrite(fe, RFSYN_DIVM, 1);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_OUT, 1);
		status += MXL_ControlWrite(fe, RFSYN_RF_DIV_BIAS, 1);
		status += MXL_ControlWrite(fe, DN_SEL_FREQ, 0);
		status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 0);
		status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 40);
		status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 27);
		if (state->Mode == 0 && state->IF_Mode == 1) {
			/* Analog Low IF Mode */
			status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 0);
			status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 40);
			status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 27);
			status += MXL_ControlWrite(fe,
				CHCAL_FRAC_MOD_RF, 206438);
		}
		if (state->Mode == 0 && state->IF_Mode == 0) {
			/* Analog Zero IF Mode */
			status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 0);
			status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 40);
			status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 27);
			status += MXL_ControlWrite(fe,
				CHCAL_FRAC_MOD_RF, 206438);
		}
		if (state->Mode == 1) /* Digital Mode */ {
			status += MXL_ControlWrite(fe, RFSYN_SEL_VCO_HI, 0);
			status += MXL_ControlWrite(fe, RFSYN_VCO_BIAS, 40);
			status += MXL_ControlWrite(fe, CHCAL_INT_MOD_RF, 27);
			status += MXL_ControlWrite(fe,
				CHCAL_FRAC_MOD_RF, 212992);
		}
	}

	return status;
}

static u16 MXL_Hystersis_Test(struct dvb_frontend *fe, int Hystersis)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	u16 status = 0;

	if (Hystersis == 1)
		status += MXL_ControlWrite(fe, DN_BYPASS_AGC_I2C, 1);

	return status;
}
#endif
/* End: Reference driver code found in the Realtek driver that
 * is copyright MaxLinear */

/* ----------------------------------------------------------------
 * Begin: Everything after here is new code to adapt the
 * proprietary Realtek driver into a Linux API tuner.
 * Copyright (C) 2008 Steven Toth <stoth@linuxtv.org>
 */
static int mxl5005s_reset(struct dvb_frontend *fe)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	int ret = 0;

	u8 buf[2] = { 0xff, 0x00 };
	struct i2c_msg msg = { .addr = state->config->i2c_address, .flags = 0,
			       .buf = buf, .len = 2 };

	dprintk(2, "%s()\n", __func__);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	if (i2c_transfer(state->i2c, &msg, 1) != 1) {
		printk(KERN_WARNING "mxl5005s I2C reset failed\n");
		ret = -EREMOTEIO;
	}

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return ret;
}

/* Write a single byte to a single reg, latch the value if required by
 * following the transaction with the latch byte.
 */
static int mxl5005s_writereg(struct dvb_frontend *fe, u8 reg, u8 val, int latch)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	u8 buf[3] = { reg, val, MXL5005S_LATCH_BYTE };
	struct i2c_msg msg = { .addr = state->config->i2c_address, .flags = 0,
			       .buf = buf, .len = 3 };

	if (latch == 0)
		msg.len = 2;

	dprintk(2, "%s(0x%x, 0x%x, 0x%x)\n", __func__, reg, val, msg.addr);

	if (i2c_transfer(state->i2c, &msg, 1) != 1) {
		printk(KERN_WARNING "mxl5005s I2C write failed\n");
		return -EREMOTEIO;
	}
	return 0;
}

static int mxl5005s_writeregs(struct dvb_frontend *fe, u8 *addrtable,
	u8 *datatable, u8 len)
{
	int ret = 0, i;

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	for (i = 0 ; i < len-1; i++) {
		ret = mxl5005s_writereg(fe, addrtable[i], datatable[i], 0);
		if (ret < 0)
			break;
	}

	ret = mxl5005s_writereg(fe, addrtable[i], datatable[i], 1);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 0);

	return ret;
}

static int mxl5005s_init(struct dvb_frontend *fe)
{
	struct mxl5005s_state *state = fe->tuner_priv;

	dprintk(1, "%s()\n", __func__);
	state->current_mode = MXL_QAM;
	return mxl5005s_reconfigure(fe, MXL_QAM, MXL5005S_BANDWIDTH_6MHZ);
}

static int mxl5005s_reconfigure(struct dvb_frontend *fe, u32 mod_type,
	u32 bandwidth)
{
	struct mxl5005s_state *state = fe->tuner_priv;

	u8 AddrTable[MXL5005S_REG_WRITING_TABLE_LEN_MAX];
	u8 ByteTable[MXL5005S_REG_WRITING_TABLE_LEN_MAX];
	int TableLen;

	dprintk(1, "%s(type=%d, bw=%d)\n", __func__, mod_type, bandwidth);

	mxl5005s_reset(fe);

	/* Tuner initialization stage 0 */
	MXL_GetMasterControl(ByteTable, MC_SYNTH_RESET);
	AddrTable[0] = MASTER_CONTROL_ADDR;
	ByteTable[0] |= state->config->AgcMasterByte;

	mxl5005s_writeregs(fe, AddrTable, ByteTable, 1);

	mxl5005s_AssignTunerMode(fe, mod_type, bandwidth);

	/* Tuner initialization stage 1 */
	MXL_GetInitRegister(fe, AddrTable, ByteTable, &TableLen);

	mxl5005s_writeregs(fe, AddrTable, ByteTable, TableLen);

	return 0;
}

static int mxl5005s_AssignTunerMode(struct dvb_frontend *fe, u32 mod_type,
	u32 bandwidth)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	struct mxl5005s_config *c = state->config;

	InitTunerControls(fe);

	/* Set MxL5005S parameters. */
	MXL5005_TunerConfig(
		fe,
		c->mod_mode,
		c->if_mode,
		bandwidth,
		c->if_freq,
		c->xtal_freq,
		c->agc_mode,
		c->top,
		c->output_load,
		c->clock_out,
		c->div_out,
		c->cap_select,
		c->rssi_enable,
		mod_type,
		c->tracking_filter);

	return 0;
}

static int mxl5005s_set_params(struct dvb_frontend *fe)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 delsys = c->delivery_system;
	u32 bw = c->bandwidth_hz;
	u32 req_mode, req_bw = 0;
	int ret;

	dprintk(1, "%s()\n", __func__);

	switch (delsys) {
	case SYS_ATSC:
		req_mode = MXL_ATSC;
		req_bw  = MXL5005S_BANDWIDTH_6MHZ;
		break;
	case SYS_DVBC_ANNEX_B:
		req_mode = MXL_QAM;
		req_bw  = MXL5005S_BANDWIDTH_6MHZ;
		break;
	default:	/* Assume DVB-T */
		req_mode = MXL_DVBT;
		switch (bw) {
		case 6000000:
			req_bw = MXL5005S_BANDWIDTH_6MHZ;
			break;
		case 7000000:
			req_bw = MXL5005S_BANDWIDTH_7MHZ;
			break;
		case 8000000:
		case 0:
			req_bw = MXL5005S_BANDWIDTH_8MHZ;
			break;
		default:
			return -EINVAL;
		}
	}

	/* Change tuner for new modulation type if reqd */
	if (req_mode != state->current_mode ||
	    req_bw != state->Chan_Bandwidth) {
		state->current_mode = req_mode;
		ret = mxl5005s_reconfigure(fe, req_mode, req_bw);

	} else
		ret = 0;

	if (ret == 0) {
		dprintk(1, "%s() freq=%d\n", __func__, c->frequency);
		ret = mxl5005s_SetRfFreqHz(fe, c->frequency);
	}

	return ret;
}

static int mxl5005s_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	dprintk(1, "%s()\n", __func__);

	*frequency = state->RF_IN;

	return 0;
}

static int mxl5005s_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	dprintk(1, "%s()\n", __func__);

	*bandwidth = state->Chan_Bandwidth;

	return 0;
}

static int mxl5005s_get_if_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct mxl5005s_state *state = fe->tuner_priv;
	dprintk(1, "%s()\n", __func__);

	*frequency = state->IF_OUT;

	return 0;
}

static void mxl5005s_release(struct dvb_frontend *fe)
{
	dprintk(1, "%s()\n", __func__);
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
}

static const struct dvb_tuner_ops mxl5005s_tuner_ops = {
	.info = {
		.name           = "MaxLinear MXL5005S",
		.frequency_min  =  48000000,
		.frequency_max  = 860000000,
		.frequency_step =     50000,
	},

	.release       = mxl5005s_release,
	.init          = mxl5005s_init,

	.set_params    = mxl5005s_set_params,
	.get_frequency = mxl5005s_get_frequency,
	.get_bandwidth = mxl5005s_get_bandwidth,
	.get_if_frequency = mxl5005s_get_if_frequency,
};

struct dvb_frontend *mxl5005s_attach(struct dvb_frontend *fe,
				     struct i2c_adapter *i2c,
				     struct mxl5005s_config *config)
{
	struct mxl5005s_state *state = NULL;
	dprintk(1, "%s()\n", __func__);

	state = kzalloc(sizeof(struct mxl5005s_state), GFP_KERNEL);
	if (state == NULL)
		return NULL;

	state->frontend = fe;
	state->config = config;
	state->i2c = i2c;

	printk(KERN_INFO "MXL5005S: Attached at address 0x%02x\n",
		config->i2c_address);

	memcpy(&fe->ops.tuner_ops, &mxl5005s_tuner_ops,
		sizeof(struct dvb_tuner_ops));

	fe->tuner_priv = state;
	return fe;
}
EXPORT_SYMBOL(mxl5005s_attach);

MODULE_DESCRIPTION("MaxLinear MXL5005S silicon tuner driver");
MODULE_AUTHOR("Steven Toth");
MODULE_LICENSE("GPL");
