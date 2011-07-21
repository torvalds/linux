#ifndef __MT2063_H__
#define __MT2063_H__

#include "dvb_frontend.h"

#define DVBFE_TUNER_OPEN			99
#define DVBFE_TUNER_SOFTWARE_SHUTDOWN		100
#define DVBFE_TUNER_CLEAR_POWER_MASKBITS	101

#define MT2063_ERROR (1 << 31)
#define MT2063_USER_ERROR (1 << 30)

/*  Macro to be used to check for errors  */
#define MT2063_IS_ERROR(s) (((s) >> 30) != 0)
#define MT2063_NO_ERROR(s) (((s) >> 30) == 0)

#define MT2063_OK                           (0x00000000)

/*  Unknown error  */
#define MT2063_UNKNOWN                      (0x80000001)

/*  Error:  Upconverter PLL is not locked  */
#define MT2063_UPC_UNLOCK                   (0x80000002)

/*  Error:  Downconverter PLL is not locked  */
#define MT2063_DNC_UNLOCK                   (0x80000004)

/*  Error:  Two-wire serial bus communications error  */
#define MT2063_COMM_ERR                     (0x80000008)

/*  Error:  Tuner handle passed to function was invalid  */
#define MT2063_INV_HANDLE                   (0x80000010)

/*  Error:  Function argument is invalid (out of range)  */
#define MT2063_ARG_RANGE                    (0x80000020)

/*  Error:  Function argument (ptr to return value) was NULL  */
#define MT2063_ARG_NULL                     (0x80000040)

/*  Error: Attempt to open more than MT_TUNER_CNT tuners  */
#define MT2063_TUNER_CNT_ERR                (0x80000080)

/*  Error: Tuner Part Code / Rev Code mismatches expected value  */
#define MT2063_TUNER_ID_ERR                 (0x80000100)

/*  Error: Tuner Initialization failure  */
#define MT2063_TUNER_INIT_ERR               (0x80000200)

#define MT2063_TUNER_OPEN_ERR               (0x80000400)

/*  User-definable fields (see mt_userdef.h)  */
#define MT2063_USER_DEFINED1                (0x00001000)
#define MT2063_USER_DEFINED2                (0x00002000)
#define MT2063_USER_DEFINED3                (0x00004000)
#define MT2063_USER_DEFINED4                (0x00008000)
#define MT2063_USER_MASK                    (0x4000f000)
#define MT2063_USER_SHIFT                   (12)

/*  Info: Mask of bits used for # of LO-related spurs that were avoided during tuning  */
#define MT2063_SPUR_CNT_MASK                (0x001f0000)
#define MT2063_SPUR_SHIFT                   (16)

/*  Info: Tuner timeout waiting for condition  */
#define MT2063_TUNER_TIMEOUT                (0x00400000)

/*  Info: Unavoidable LO-related spur may be present in the output  */
#define MT2063_SPUR_PRESENT_ERR                 (0x00800000)

/*  Info: Tuner input frequency is out of range */
#define MT2063_FIN_RANGE                    (0x01000000)

/*  Info: Tuner output frequency is out of range */
#define MT2063_FOUT_RANGE                   (0x02000000)

/*  Info: Upconverter frequency is out of range (may be reason for MT_UPC_UNLOCK) */
#define MT2063_UPC_RANGE                    (0x04000000)

/*  Info: Downconverter frequency is out of range (may be reason for MT_DPC_UNLOCK) */
#define MT2063_DNC_RANGE                    (0x08000000)

/*
 *  Data Types
 */

#define MAX_UDATA         (4294967295)	/*  max value storable in u32   */

/*
 * Define an MTxxxx_CNT macro for each type of tuner that will be built
 * into your application (e.g., MT2121, MT2060). MT_TUNER_CNT
 * must be set to the SUM of all of the MTxxxx_CNT macros.
 *
 * #define MT2050_CNT  (1)
 * #define MT2060_CNT  (1)
 * #define MT2111_CNT  (1)
 * #define MT2121_CNT  (3)
 */


#define MT2063_TUNER_CNT               (1)	/*  total num of MicroTuner tuners  */
#define MT2063_I2C (0xC0)

/*
 *  Constant defining the version of the following structure
 *  and therefore the API for this code.
 *
 *  When compiling the tuner driver, the preprocessor will
 *  check against this version number to make sure that
 *  it matches the version that the tuner driver knows about.
 */
/* Version 010201 => 1.21 */
#define MT2063_AVOID_SPURS_INFO_VERSION 010201

/* DECT Frequency Avoidance */
#define MT2063_DECT_AVOID_US_FREQS      0x00000001

#define MT2063_DECT_AVOID_EURO_FREQS    0x00000002

#define MT2063_EXCLUDE_US_DECT_FREQUENCIES(s) (((s) & MT2063_DECT_AVOID_US_FREQS) != 0)

#define MT2063_EXCLUDE_EURO_DECT_FREQUENCIES(s) (((s) & MT2063_DECT_AVOID_EURO_FREQS) != 0)

enum MT2063_DECT_Avoid_Type {
	MT2063_NO_DECT_AVOIDANCE = 0,				/* Do not create DECT exclusion zones.     */
	MT2063_AVOID_US_DECT = MT2063_DECT_AVOID_US_FREQS,	/* Avoid US DECT frequencies.              */
	MT2063_AVOID_EURO_DECT = MT2063_DECT_AVOID_EURO_FREQS,	/* Avoid European DECT frequencies.        */
	MT2063_AVOID_BOTH					/* Avoid both regions. Not typically used. */
};

#define MT2063_MAX_ZONES 48

struct MT2063_ExclZone_t;

struct MT2063_ExclZone_t {
	u32 min_;
	u32 max_;
	struct MT2063_ExclZone_t *next_;
};

/*
 *  Structure of data needed for Spur Avoidance
 */
struct MT2063_AvoidSpursData_t {
	u32 nAS_Algorithm;
	u32 f_ref;
	u32 f_in;
	u32 f_LO1;
	u32 f_if1_Center;
	u32 f_if1_Request;
	u32 f_if1_bw;
	u32 f_LO2;
	u32 f_out;
	u32 f_out_bw;
	u32 f_LO1_Step;
	u32 f_LO2_Step;
	u32 f_LO1_FracN_Avoid;
	u32 f_LO2_FracN_Avoid;
	u32 f_zif_bw;
	u32 f_min_LO_Separation;
	u32 maxH1;
	u32 maxH2;
	enum MT2063_DECT_Avoid_Type avoidDECT;
	u32 bSpurPresent;
	u32 bSpurAvoided;
	u32 nSpursFound;
	u32 nZones;
	struct MT2063_ExclZone_t *freeZones;
	struct MT2063_ExclZone_t *usedZones;
	struct MT2063_ExclZone_t MT2063_ExclZones[MT2063_MAX_ZONES];
};

/*
 *  Values returned by the MT2063's on-chip temperature sensor
 *  to be read/written.
 */
enum MT2063_Temperature {
	MT2063_T_0C = 0,	/*  Temperature approx 0C           */
	MT2063_T_10C,		/*  Temperature approx 10C          */
	MT2063_T_20C,		/*  Temperature approx 20C          */
	MT2063_T_30C,		/*  Temperature approx 30C          */
	MT2063_T_40C,		/*  Temperature approx 40C          */
	MT2063_T_50C,		/*  Temperature approx 50C          */
	MT2063_T_60C,		/*  Temperature approx 60C          */
	MT2063_T_70C,		/*  Temperature approx 70C          */
	MT2063_T_80C,		/*  Temperature approx 80C          */
	MT2063_T_90C,		/*  Temperature approx 90C          */
	MT2063_T_100C,		/*  Temperature approx 100C         */
	MT2063_T_110C,		/*  Temperature approx 110C         */
	MT2063_T_120C,		/*  Temperature approx 120C         */
	MT2063_T_130C,		/*  Temperature approx 130C         */
	MT2063_T_140C,		/*  Temperature approx 140C         */
	MT2063_T_150C,		/*  Temperature approx 150C         */
};

/*
 * Parameters for selecting GPIO bits
 */
enum MT2063_GPIO_Attr {
	MT2063_GPIO_IN,
	MT2063_GPIO_DIR,
	MT2063_GPIO_OUT,
};

enum MT2063_GPIO_ID {
	MT2063_GPIO0,
	MT2063_GPIO1,
	MT2063_GPIO2,
};

/*
 *  Parameter for function MT2063_SetExtSRO that specifies the external
 *  SRO drive frequency.
 *
 *  MT2063_EXT_SRO_OFF is the power-up default value.
 */
enum MT2063_Ext_SRO {
	MT2063_EXT_SRO_OFF,	/*  External SRO drive off          */
	MT2063_EXT_SRO_BY_4,	/*  External SRO drive divide by 4  */
	MT2063_EXT_SRO_BY_2,	/*  External SRO drive divide by 2  */
	MT2063_EXT_SRO_BY_1	/*  External SRO drive divide by 1  */
};

/*
 *  Parameter for function MT2063_SetPowerMask that specifies the power down
 *  of various sections of the MT2063.
 */
enum MT2063_Mask_Bits {
	MT2063_REG_SD = 0x0040,		/* Shutdown regulator                 */
	MT2063_SRO_SD = 0x0020,		/* Shutdown SRO                       */
	MT2063_AFC_SD = 0x0010,		/* Shutdown AFC A/D                   */
	MT2063_PD_SD = 0x0002,		/* Enable power detector shutdown     */
	MT2063_PDADC_SD = 0x0001,	/* Enable power detector A/D shutdown */
	MT2063_VCO_SD = 0x8000,		/* Enable VCO shutdown                */
	MT2063_LTX_SD = 0x4000,		/* Enable LTX shutdown                */
	MT2063_LT1_SD = 0x2000,		/* Enable LT1 shutdown                */
	MT2063_LNA_SD = 0x1000,		/* Enable LNA shutdown                */
	MT2063_UPC_SD = 0x0800,		/* Enable upconverter shutdown        */
	MT2063_DNC_SD = 0x0400,		/* Enable downconverter shutdown      */
	MT2063_VGA_SD = 0x0200,		/* Enable VGA shutdown                */
	MT2063_AMP_SD = 0x0100,		/* Enable AMP shutdown                */
	MT2063_ALL_SD = 0xFF73,		/* All shutdown bits for this tuner   */
	MT2063_NONE_SD = 0x0000		/* No shutdown bits                   */
};

/*
 *  Parameter for function MT2063_GetParam & MT2063_SetParam that
 *  specifies the tuning algorithm parameter to be read/written.
 */
enum MT2063_Param {
	/*  tuner address                                  set by MT2063_Open() */
	MT2063_IC_ADDR,

	/*  max number of MT2063 tuners     set by MT_TUNER_CNT in mt_userdef.h */
	MT2063_MAX_OPEN,

	/*  current number of open MT2063 tuners           set by MT2063_Open() */
	MT2063_NUM_OPEN,

	/*  crystal frequency                            (default: 16000000 Hz) */
	MT2063_SRO_FREQ,

	/*  min tuning step size                            (default: 50000 Hz) */
	MT2063_STEPSIZE,

	/*  input center frequency                         set by MT2063_Tune() */
	MT2063_INPUT_FREQ,

	/*  LO1 Frequency                                  set by MT2063_Tune() */
	MT2063_LO1_FREQ,

	/*  LO1 minimum step size                          (default: 250000 Hz) */
	MT2063_LO1_STEPSIZE,

	/*  LO1 FracN keep-out region                      (default: 999999 Hz) */
	MT2063_LO1_FRACN_AVOID_PARAM,

	/*  Current 1st IF in use                          set by MT2063_Tune() */
	MT2063_IF1_ACTUAL,

	/*  Requested 1st IF                               set by MT2063_Tune() */
	MT2063_IF1_REQUEST,

	/*  Center of 1st IF SAW filter                (default: 1218000000 Hz) */
	MT2063_IF1_CENTER,

	/*  Bandwidth of 1st IF SAW filter               (default: 20000000 Hz) */
	MT2063_IF1_BW,

	/*  zero-IF bandwidth                             (default: 2000000 Hz) */
	MT2063_ZIF_BW,

	/*  LO2 Frequency                                  set by MT2063_Tune() */
	MT2063_LO2_FREQ,

	/*  LO2 minimum step size                           (default: 50000 Hz) */
	MT2063_LO2_STEPSIZE,

	/*  LO2 FracN keep-out region                      (default: 374999 Hz) */
	MT2063_LO2_FRACN_AVOID,

	/*  output center frequency                        set by MT2063_Tune() */
	MT2063_OUTPUT_FREQ,

	/*  output bandwidth                               set by MT2063_Tune() */
	MT2063_OUTPUT_BW,

	/*  min inter-tuner LO separation                 (default: 1000000 Hz) */
	MT2063_LO_SEPARATION,

	/*  ID of avoid-spurs algorithm in use            compile-time constant */
	MT2063_AS_ALG,

	/*  max # of intra-tuner harmonics                       (default: 15)  */
	MT2063_MAX_HARM1,

	/*  max # of inter-tuner harmonics                        (default: 7)  */
	MT2063_MAX_HARM2,

	/*  # of 1st IF exclusion zones used               set by MT2063_Tune() */
	MT2063_EXCL_ZONES,

	/*  # of spurs found/avoided                       set by MT2063_Tune() */
	MT2063_NUM_SPURS,

	/*  >0 spurs avoided                               set by MT2063_Tune() */
	MT2063_SPUR_AVOIDED,

	/*  >0 spurs in output (mathematically)            set by MT2063_Tune() */
	MT2063_SPUR_PRESENT,

	/* Receiver Mode for some parameters. 1 is DVB-T                        */
	MT2063_RCVR_MODE,

	/* directly set LNA attenuation, parameter is value to set              */
	MT2063_ACLNA,

	/* maximum LNA attenuation, parameter is value to set                   */
	MT2063_ACLNA_MAX,

	/* directly set ATN attenuation.  Paremeter is value to set.            */
	MT2063_ACRF,

	/* maxium ATN attenuation.  Paremeter is value to set.                  */
	MT2063_ACRF_MAX,

	/* directly set FIF attenuation.  Paremeter is value to set.            */
	MT2063_ACFIF,

	/* maxium FIF attenuation.  Paremeter is value to set.                  */
	MT2063_ACFIF_MAX,

	/*  LNA Rin                                                             */
	MT2063_LNA_RIN,

	/*  Power Detector LNA level target                                     */
	MT2063_LNA_TGT,

	/*  Power Detector 1 level                                              */
	MT2063_PD1,

	/*  Power Detector 1 level target                                       */
	MT2063_PD1_TGT,

	/*  Power Detector 2 level                                              */
	MT2063_PD2,

	/*  Power Detector 2 level target                                       */
	MT2063_PD2_TGT,

	/*  Selects, which DNC is activ                                         */
	MT2063_DNC_OUTPUT_ENABLE,

	/*  VGA gain code                                                       */
	MT2063_VGAGC,

	/*  VGA bias current                                                    */
	MT2063_VGAOI,

	/*  TAGC, determins the speed of the AGC                                */
	MT2063_TAGC,

	/*  AMP gain code                                                       */
	MT2063_AMPGC,

	/* Control setting to avoid DECT freqs         (default: MT_AVOID_BOTH) */
	MT2063_AVOID_DECT,

	/* Cleartune filter selection: 0 - by IC (default), 1 - by software     */
	MT2063_CTFILT_SW,

	MT2063_EOP		/*  last entry in enumerated list         */
};

/*
 *  Parameter for selecting tuner mode
 */
enum MT2063_RCVR_MODES {
	MT2063_CABLE_QAM = 0,	/* Digital cable              */
	MT2063_CABLE_ANALOG,	/* Analog cable               */
	MT2063_OFFAIR_COFDM,	/* Digital offair             */
	MT2063_OFFAIR_COFDM_SAWLESS,	/* Digital offair without SAW */
	MT2063_OFFAIR_ANALOG,	/* Analog offair              */
	MT2063_OFFAIR_8VSB,	/* Analog offair              */
	MT2063_NUM_RCVR_MODES
};

/*
 *  Possible values for MT2063_DNC_OUTPUT
 */
enum MT2063_DNC_Output_Enable {
	MT2063_DNC_NONE = 0,
	MT2063_DNC_1,
	MT2063_DNC_2,
	MT2063_DNC_BOTH
};

/*
**  Two-wire serial bus subaddresses of the tuner registers.
**  Also known as the tuner's register addresses.
*/
enum MT2063_Register_Offsets {
	MT2063_REG_PART_REV = 0,	/*  0x00: Part/Rev Code         */
	MT2063_REG_LO1CQ_1,		/*  0x01: LO1C Queued Byte 1    */
	MT2063_REG_LO1CQ_2,		/*  0x02: LO1C Queued Byte 2    */
	MT2063_REG_LO2CQ_1,		/*  0x03: LO2C Queued Byte 1    */
	MT2063_REG_LO2CQ_2,		/*  0x04: LO2C Queued Byte 2    */
	MT2063_REG_LO2CQ_3,		/*  0x05: LO2C Queued Byte 3    */
	MT2063_REG_RSVD_06,		/*  0x06: Reserved              */
	MT2063_REG_LO_STATUS,		/*  0x07: LO Status             */
	MT2063_REG_FIFFC,		/*  0x08: FIFF Center           */
	MT2063_REG_CLEARTUNE,		/*  0x09: ClearTune Filter      */
	MT2063_REG_ADC_OUT,		/*  0x0A: ADC_OUT               */
	MT2063_REG_LO1C_1,		/*  0x0B: LO1C Byte 1           */
	MT2063_REG_LO1C_2,		/*  0x0C: LO1C Byte 2           */
	MT2063_REG_LO2C_1,		/*  0x0D: LO2C Byte 1           */
	MT2063_REG_LO2C_2,		/*  0x0E: LO2C Byte 2           */
	MT2063_REG_LO2C_3,		/*  0x0F: LO2C Byte 3           */
	MT2063_REG_RSVD_10,		/*  0x10: Reserved              */
	MT2063_REG_PWR_1,		/*  0x11: PWR Byte 1            */
	MT2063_REG_PWR_2,		/*  0x12: PWR Byte 2            */
	MT2063_REG_TEMP_STATUS,		/*  0x13: Temp Status           */
	MT2063_REG_XO_STATUS,		/*  0x14: Crystal Status        */
	MT2063_REG_RF_STATUS,		/*  0x15: RF Attn Status        */
	MT2063_REG_FIF_STATUS,		/*  0x16: FIF Attn Status       */
	MT2063_REG_LNA_OV,		/*  0x17: LNA Attn Override     */
	MT2063_REG_RF_OV,		/*  0x18: RF Attn Override      */
	MT2063_REG_FIF_OV,		/*  0x19: FIF Attn Override     */
	MT2063_REG_LNA_TGT,		/*  0x1A: Reserved              */
	MT2063_REG_PD1_TGT,		/*  0x1B: Pwr Det 1 Target      */
	MT2063_REG_PD2_TGT,		/*  0x1C: Pwr Det 2 Target      */
	MT2063_REG_RSVD_1D,		/*  0x1D: Reserved              */
	MT2063_REG_RSVD_1E,		/*  0x1E: Reserved              */
	MT2063_REG_RSVD_1F,		/*  0x1F: Reserved              */
	MT2063_REG_RSVD_20,		/*  0x20: Reserved              */
	MT2063_REG_BYP_CTRL,		/*  0x21: Bypass Control        */
	MT2063_REG_RSVD_22,		/*  0x22: Reserved              */
	MT2063_REG_RSVD_23,		/*  0x23: Reserved              */
	MT2063_REG_RSVD_24,		/*  0x24: Reserved              */
	MT2063_REG_RSVD_25,		/*  0x25: Reserved              */
	MT2063_REG_RSVD_26,		/*  0x26: Reserved              */
	MT2063_REG_RSVD_27,		/*  0x27: Reserved              */
	MT2063_REG_FIFF_CTRL,		/*  0x28: FIFF Control          */
	MT2063_REG_FIFF_OFFSET,		/*  0x29: FIFF Offset           */
	MT2063_REG_CTUNE_CTRL,		/*  0x2A: Reserved              */
	MT2063_REG_CTUNE_OV,		/*  0x2B: Reserved              */
	MT2063_REG_CTRL_2C,		/*  0x2C: Reserved              */
	MT2063_REG_FIFF_CTRL2,		/*  0x2D: Fiff Control          */
	MT2063_REG_RSVD_2E,		/*  0x2E: Reserved              */
	MT2063_REG_DNC_GAIN,		/*  0x2F: DNC Control           */
	MT2063_REG_VGA_GAIN,		/*  0x30: VGA Gain Ctrl         */
	MT2063_REG_RSVD_31,		/*  0x31: Reserved              */
	MT2063_REG_TEMP_SEL,		/*  0x32: Temperature Selection */
	MT2063_REG_RSVD_33,		/*  0x33: Reserved              */
	MT2063_REG_RSVD_34,		/*  0x34: Reserved              */
	MT2063_REG_RSVD_35,		/*  0x35: Reserved              */
	MT2063_REG_RSVD_36,		/*  0x36: Reserved              */
	MT2063_REG_RSVD_37,		/*  0x37: Reserved              */
	MT2063_REG_RSVD_38,		/*  0x38: Reserved              */
	MT2063_REG_RSVD_39,		/*  0x39: Reserved              */
	MT2063_REG_RSVD_3A,		/*  0x3A: Reserved              */
	MT2063_REG_RSVD_3B,		/*  0x3B: Reserved              */
	MT2063_REG_RSVD_3C,		/*  0x3C: Reserved              */
	MT2063_REG_END_REGS
};

struct MT2063_Info_t {
	void *handle;
	void *hUserData;
	u32 address;
	u32 version;
	u32 tuner_id;
	struct MT2063_AvoidSpursData_t AS_Data;
	u32 f_IF1_actual;
	u32 rcvr_mode;
	u32 ctfilt_sw;
	u32 CTFiltMax[31];
	u32 num_regs;
	u8 reg[MT2063_REG_END_REGS];
};
typedef struct MT2063_Info_t *pMT2063_Info_t;

enum MTTune_atv_standard {
	MTTUNEA_UNKNOWN = 0,
	MTTUNEA_PAL_B,
	MTTUNEA_PAL_G,
	MTTUNEA_PAL_I,
	MTTUNEA_PAL_L,
	MTTUNEA_PAL_MN,
	MTTUNEA_PAL_DK,
	MTTUNEA_DIGITAL,
	MTTUNEA_FMRADIO,
	MTTUNEA_DVBC,
	MTTUNEA_DVBT
};

struct mt2063_config {
	u8 tuner_address;
	u32 refclock;
};

struct mt2063_state {
	struct i2c_adapter *i2c;

	const struct mt2063_config *config;
	struct dvb_tuner_ops ops;
	struct dvb_frontend *frontend;
	struct tuner_state status;
	const struct MT2063_Info_t *MT2063_ht;
	bool MT2063_init;

	enum MTTune_atv_standard tv_type;
	u32 frequency;
	u32 srate;
	u32 bandwidth;
	u32 reference;
};

#if defined(CONFIG_MEDIA_TUNER_MT2063) || (defined(CONFIG_MEDIA_TUNER_MT2063_MODULE) && defined(MODULE))
struct dvb_frontend *mt2063_attach(struct dvb_frontend *fe,
				   struct mt2063_config *config,
				   struct i2c_adapter *i2c);

#else

static inline struct dvb_frontend *mt2063_attach(struct dvb_frontend *fe,
				   struct mt2063_config *config,
				   struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: Driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif /* CONFIG_DVB_MT2063 */

#endif /* __MT2063_H__ */
