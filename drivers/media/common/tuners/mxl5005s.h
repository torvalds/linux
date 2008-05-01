/*
 * For the Realtek RTL chip RTL2831U
 * Realtek Release Date: 2008-03-14, ver 080314
 * Realtek version RTL2831 Linux driver version 080314
 * ver 080314
 *
 * for linux kernel version 2.6.21.4 - 2.6.22-14
 * support MXL5005s and MT2060 tuners (support tuner auto-detecting)
 * support two IR types -- RC5 and NEC
 *
 * Known boards with Realtek RTL chip RTL2821U
 *    Freecom USB stick 14aa:0160 (version 4)
 *    Conceptronic CTVDIGRCU
 *
 * Copyright (c) 2008 Realtek
 * Copyright (c) 2008 Jan Hoogenraad, Barnaby Shearer, Andy Hasper
 * This code is placed under the terms of the GNU General Public License
 *
 * Released by Realtek under GPLv2.
 * Thanks to Realtek for a lot of support we received !
 *
 *  Revision: 080314 - original version
 */


#ifndef __MXL5005S_H
#define __MXL5005S_H

/*
 * The following context is source code provided by MaxLinear.
 * MaxLinear source code - Common.h
 */

typedef void *HANDLE; /* Pointer to memory location */

#define TUNER_REGS_NUM		104
#define INITCTRL_NUM		40

#ifdef _MXL_PRODUCTION
#define CHCTRL_NUM		39
#else
#define CHCTRL_NUM		36
#endif

#define MXLCTRL_NUM		189
#define MASTER_CONTROL_ADDR	9

/* Enumeration of AGC Mode */
typedef enum
{
	MXL_DUAL_AGC = 0,
	MXL_SINGLE_AGC
} AGC_Mode;

/* Enumeration of Master Control Register State */
typedef enum
{
	MC_LOAD_START = 1,
	MC_POWER_DOWN,
	MC_SYNTH_RESET,
	MC_SEQ_OFF
} Master_Control_State;

/* Enumeration of MXL5005 Tuner Mode */
typedef enum
{
	MXL_ANALOG_MODE = 0,
	MXL_DIGITAL_MODE
} Tuner_Mode;

/* Enumeration of MXL5005 Tuner IF Mode */
typedef enum
{
	MXL_ZERO_IF = 0,
	MXL_LOW_IF
} Tuner_IF_Mode;

/* Enumeration of MXL5005 Tuner Clock Out Mode */
typedef enum
{
	MXL_CLOCK_OUT_DISABLE = 0,
	MXL_CLOCK_OUT_ENABLE
} Tuner_Clock_Out;

/* Enumeration of MXL5005 Tuner Div Out Mode */
typedef enum
{
	MXL_DIV_OUT_1 = 0,
	MXL_DIV_OUT_4

} Tuner_Div_Out;

/* Enumeration of MXL5005 Tuner Pull-up Cap Select Mode */
typedef enum
{
	MXL_CAP_SEL_DISABLE = 0,
	MXL_CAP_SEL_ENABLE

} Tuner_Cap_Select;

/* Enumeration of MXL5005 Tuner RSSI Mode */
typedef enum
{
	MXL_RSSI_DISABLE = 0,
	MXL_RSSI_ENABLE

} Tuner_RSSI;

/* Enumeration of MXL5005 Tuner Modulation Type */
typedef enum
{
	MXL_DEFAULT_MODULATION = 0,
	MXL_DVBT,
	MXL_ATSC,
	MXL_QAM,
	MXL_ANALOG_CABLE,
	MXL_ANALOG_OTA
} Tuner_Modu_Type;

/* Enumeration of MXL5005 Tuner Tracking Filter Type */
typedef enum
{
	MXL_TF_DEFAULT = 0,
	MXL_TF_OFF,
	MXL_TF_C,
	MXL_TF_C_H,
	MXL_TF_D,
	MXL_TF_D_L,
	MXL_TF_E,
	MXL_TF_F,
	MXL_TF_E_2,
	MXL_TF_E_NA,
	MXL_TF_G
} Tuner_TF_Type;

/* MXL5005 Tuner Register Struct */
typedef struct _TunerReg_struct
{
	u16 Reg_Num;	/* Tuner Register Address */
	u16 Reg_Val;	/* Current sofware programmed value waiting to be writen */
} TunerReg_struct;

typedef enum
{
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
} MXL5005_ControlName;

/* End of common.h */

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
enum
{
	MXL5005S_STANDARD_DVBT,
	MXL5005S_STANDARD_ATSC,
};
#define MXL5005S_STANDARD_MODE_NUM		2

/* Bandwidth modes */
enum
{
	MXL5005S_BANDWIDTH_6MHZ = 6000000,
	MXL5005S_BANDWIDTH_7MHZ = 7000000,
	MXL5005S_BANDWIDTH_8MHZ = 8000000,
};
#define MXL5005S_BANDWIDTH_MODE_NUM		3

/* Top modes */
enum
{
	MXL5005S_TOP_5P5  =  55,
	MXL5005S_TOP_7P2  =  72,
	MXL5005S_TOP_9P2  =  92,
	MXL5005S_TOP_11P0 = 110,
	MXL5005S_TOP_12P9 = 129,
	MXL5005S_TOP_14P7 = 147,
	MXL5005S_TOP_16P8 = 168,
	MXL5005S_TOP_19P4 = 194,
	MXL5005S_TOP_21P2 = 212,
	MXL5005S_TOP_23P2 = 232,
	MXL5005S_TOP_25P2 = 252,
	MXL5005S_TOP_27P1 = 271,
	MXL5005S_TOP_29P2 = 292,
	MXL5005S_TOP_31P7 = 317,
	MXL5005S_TOP_34P9 = 349,
};

/* IF output load */
enum
{
	MXL5005S_IF_OUTPUT_LOAD_200_OHM = 200,
	MXL5005S_IF_OUTPUT_LOAD_300_OHM = 300,
};

/* End of common_mxl.h (?) */

#endif /* __MXL5005S_H */

