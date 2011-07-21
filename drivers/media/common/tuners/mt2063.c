#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include "mt2063.h"

static unsigned int verbose;
module_param(verbose, int, 0644);

/* Internal structures and types */

/* FIXME: we probably don't need these new FE get/set property types for tuner */
#define DVBFE_TUNER_OPEN			99
#define DVBFE_TUNER_SOFTWARE_SHUTDOWN		100
#define DVBFE_TUNER_CLEAR_POWER_MASKBITS	101

/* FIXME: Those two error codes need conversion*/
/*  Error:  Upconverter PLL is not locked  */
#define MT2063_UPC_UNLOCK                   (0x80000002)
/*  Error:  Downconverter PLL is not locked  */
#define MT2063_DNC_UNLOCK                   (0x80000004)

/*  Info: Unavoidable LO-related spur may be present in the output  */
#define MT2063_SPUR_PRESENT_ERR             (0x00800000)

/*  Info: Mask of bits used for # of LO-related spurs that were avoided during tuning  */
#define MT2063_SPUR_CNT_MASK                (0x001f0000)
#define MT2063_SPUR_SHIFT                   (16)

/*  Info: Upconverter frequency is out of range (may be reason for MT_UPC_UNLOCK) */
#define MT2063_UPC_RANGE                    (0x04000000)

/*  Info: Downconverter frequency is out of range (may be reason for MT_DPC_UNLOCK) */
#define MT2063_DNC_RANGE                    (0x08000000)

/*
 *  Data Types
 */

/*
 *  Constant defining the version of the following structure
 *  and therefore the API for this code.
 *
 *  When compiling the tuner driver, the preprocessor will
 *  check against this version number to make sure that
 *  it matches the version that the tuner driver knows about.
 */

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

struct MT2063_ExclZone_t {
	u32 min_;
	u32 max_;
	struct MT2063_ExclZone_t *next_;
};

/*
 *  Structure of data needed for Spur Avoidance
 */
struct MT2063_AvoidSpursData_t {
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


struct mt2063_state {
	struct i2c_adapter *i2c;

	const struct mt2063_config *config;
	struct dvb_tuner_ops ops;
	struct dvb_frontend *frontend;
	struct tuner_state status;
	bool MT2063_init;

	enum MTTune_atv_standard tv_type;
	u32 frequency;
	u32 srate;
	u32 bandwidth;
	u32 reference;

	u32 tuner_id;
	struct MT2063_AvoidSpursData_t AS_Data;
	u32 f_IF1_actual;
	u32 rcvr_mode;
	u32 ctfilt_sw;
	u32 CTFiltMax[31];
	u32 num_regs;
	u8 reg[MT2063_REG_END_REGS];
};

/* Prototypes */
static void MT2063_AddExclZone(struct MT2063_AvoidSpursData_t *pAS_Info,
                        u32 f_min, u32 f_max);
static u32 MT2063_ReInit(struct mt2063_state *state);
static u32 MT2063_GetReg(struct mt2063_state *state, u8 reg, u8 * val);
static u32 MT2063_GetParam(struct mt2063_state *state, enum MT2063_Param param, u32 * pValue);
static u32 MT2063_SetReg(struct mt2063_state *state, u8 reg, u8 val);
static u32 MT2063_SetParam(struct mt2063_state *state, enum MT2063_Param param,
			   enum MT2063_DNC_Output_Enable nValue);

/*****************/
/* From drivers/media/common/tuners/mt2063_cfg.h */

unsigned int mt2063_setTune(struct dvb_frontend *fe, u32 f_in,
			    u32 bw_in,
			    enum MTTune_atv_standard tv_type)
{
	struct dvb_frontend_ops *frontend_ops = NULL;
	struct dvb_tuner_ops *tuner_ops = NULL;
	struct tuner_state t_state;
	struct mt2063_state *state = fe->tuner_priv;
	int err = 0;

	t_state.frequency = f_in;
	t_state.bandwidth = bw_in;
	state->tv_type = tv_type;
	if (&fe->ops)
		frontend_ops = &fe->ops;
	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;
	if (tuner_ops->set_state) {
		if ((err =
		     tuner_ops->set_state(fe, DVBFE_TUNER_FREQUENCY,
					  &t_state)) < 0) {
			printk("%s: Invalid parameter\n", __func__);
			return err;
		}
	}

	return err;
}

unsigned int mt2063_lockStatus(struct dvb_frontend *fe)
{
	struct dvb_frontend_ops *frontend_ops = &fe->ops;
	struct dvb_tuner_ops *tuner_ops = &frontend_ops->tuner_ops;
	struct tuner_state t_state;
	int err = 0;

	if (&fe->ops)
		frontend_ops = &fe->ops;
	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;
	if (tuner_ops->get_state) {
		if ((err =
		     tuner_ops->get_state(fe, DVBFE_TUNER_REFCLOCK,
					  &t_state)) < 0) {
			printk("%s: Invalid parameter\n", __func__);
			return err;
		}
	}
	return err;
}

unsigned int tuner_MT2063_Open(struct dvb_frontend *fe)
{
	struct dvb_frontend_ops *frontend_ops = &fe->ops;
	struct dvb_tuner_ops *tuner_ops = &frontend_ops->tuner_ops;
	struct tuner_state t_state;
	int err = 0;

	if (&fe->ops)
		frontend_ops = &fe->ops;
	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;
	if (tuner_ops->set_state) {
		if ((err =
		     tuner_ops->set_state(fe, DVBFE_TUNER_OPEN,
					  &t_state)) < 0) {
			printk("%s: Invalid parameter\n", __func__);
			return err;
		}
	}

	return err;
}

unsigned int tuner_MT2063_SoftwareShutdown(struct dvb_frontend *fe)
{
	struct dvb_frontend_ops *frontend_ops = &fe->ops;
	struct dvb_tuner_ops *tuner_ops = &frontend_ops->tuner_ops;
	struct tuner_state t_state;
	int err = 0;

	if (&fe->ops)
		frontend_ops = &fe->ops;
	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;
	if (tuner_ops->set_state) {
		if ((err =
		     tuner_ops->set_state(fe, DVBFE_TUNER_SOFTWARE_SHUTDOWN,
					  &t_state)) < 0) {
			printk("%s: Invalid parameter\n", __func__);
			return err;
		}
	}

	return err;
}

unsigned int tuner_MT2063_ClearPowerMaskBits(struct dvb_frontend *fe)
{
	struct dvb_frontend_ops *frontend_ops = &fe->ops;
	struct dvb_tuner_ops *tuner_ops = &frontend_ops->tuner_ops;
	struct tuner_state t_state;
	int err = 0;

	if (&fe->ops)
		frontend_ops = &fe->ops;
	if (&frontend_ops->tuner_ops)
		tuner_ops = &frontend_ops->tuner_ops;
	if (tuner_ops->set_state) {
		if ((err =
		     tuner_ops->set_state(fe, DVBFE_TUNER_CLEAR_POWER_MASKBITS,
					  &t_state)) < 0) {
			printk("%s: Invalid parameter\n", __func__);
			return err;
		}
	}

	return err;
}

/*
 * mt2063_write - Write data into the I2C bus
 */
static u32 mt2063_write(struct mt2063_state *state,
			   u8 reg, u8 *data, u32 len)
{
	struct dvb_frontend *fe = state->frontend;
	int ret;
	u8 buf[60];
	struct i2c_msg msg = {
		.addr = state->config->tuner_address,
		.flags = 0,
		.buf = buf,
		.len = len + 1
	};

	msg.buf[0] = reg;
	memcpy(msg.buf + 1, data, len);

	fe->ops.i2c_gate_ctrl(fe, 1);
	ret = i2c_transfer(state->i2c, &msg, 1);
	fe->ops.i2c_gate_ctrl(fe, 0);

	if (ret < 0)
		printk("mt2063_writeregs error ret=%d\n", ret);

	return ret;
}

/*
 * mt2063_read - Read data from the I2C bus
 */
static u32 mt2063_read(struct mt2063_state *state,
			   u8 subAddress, u8 *pData, u32 cnt)
{
	u32 status = 0;	/* Status to be returned        */
	struct dvb_frontend *fe = state->frontend;
	u32 i = 0;

	fe->ops.i2c_gate_ctrl(fe, 1);

	for (i = 0; i < cnt; i++) {
		int ret;
		u8 b0[] = { subAddress + i };
		struct i2c_msg msg[] = {
			{
				.addr = state->config->tuner_address,
				.flags = I2C_M_RD,
				.buf = b0,
				.len = 1
			}, {
				.addr = state->config->tuner_address,
				.flags = I2C_M_RD,
				.buf = pData + 1,
				.len = 1
			}
		};

		ret = i2c_transfer(state->i2c, msg, 2);
		if (ret < 0)
			break;
	}
	fe->ops.i2c_gate_ctrl(fe, 0);
	return (status);
}

/*
 * FIXME: Is this really needed?
 */
static int MT2063_Sleep(struct dvb_frontend *fe)
{
	/*
	 **  ToDo:  Add code here to implement a OS blocking
	 **         for a period of "nMinDelayTime" milliseconds.
	 */
	msleep(10);

	return 0;
}

/*
 * Microtune spur avoidance
 */

/*  Implement ceiling, floor functions.  */
#define ceil(n, d) (((n) < 0) ? (-((-(n))/(d))) : (n)/(d) + ((n)%(d) != 0))
#define floor(n, d) (((n) < 0) ? (-((-(n))/(d))) - ((n)%(d) != 0) : (n)/(d))

struct MT2063_FIFZone_t {
	s32 min_;
	s32 max_;
};

/*
**  Reset all exclusion zones.
**  Add zones to protect the PLL FracN regions near zero
**
**   N/A I 06-17-2008    RSK    Ver 1.19: Refactoring avoidance of DECT
**                              frequencies into MT_ResetExclZones().
*/
static void MT2063_ResetExclZones(struct MT2063_AvoidSpursData_t *pAS_Info)
{
	u32 center;

	pAS_Info->nZones = 0;	/*  this clears the used list  */
	pAS_Info->usedZones = NULL;	/*  reset ptr                  */
	pAS_Info->freeZones = NULL;	/*  reset ptr                  */

	center =
	    pAS_Info->f_ref *
	    ((pAS_Info->f_if1_Center - pAS_Info->f_if1_bw / 2 +
	      pAS_Info->f_in) / pAS_Info->f_ref) - pAS_Info->f_in;
	while (center <
	       pAS_Info->f_if1_Center + pAS_Info->f_if1_bw / 2 +
	       pAS_Info->f_LO1_FracN_Avoid) {
		/*  Exclude LO1 FracN  */
		MT2063_AddExclZone(pAS_Info,
				   center - pAS_Info->f_LO1_FracN_Avoid,
				   center - 1);
		MT2063_AddExclZone(pAS_Info, center + 1,
				   center + pAS_Info->f_LO1_FracN_Avoid);
		center += pAS_Info->f_ref;
	}

	center =
	    pAS_Info->f_ref *
	    ((pAS_Info->f_if1_Center - pAS_Info->f_if1_bw / 2 -
	      pAS_Info->f_out) / pAS_Info->f_ref) + pAS_Info->f_out;
	while (center <
	       pAS_Info->f_if1_Center + pAS_Info->f_if1_bw / 2 +
	       pAS_Info->f_LO2_FracN_Avoid) {
		/*  Exclude LO2 FracN  */
		MT2063_AddExclZone(pAS_Info,
				   center - pAS_Info->f_LO2_FracN_Avoid,
				   center - 1);
		MT2063_AddExclZone(pAS_Info, center + 1,
				   center + pAS_Info->f_LO2_FracN_Avoid);
		center += pAS_Info->f_ref;
	}

	if (MT2063_EXCLUDE_US_DECT_FREQUENCIES(pAS_Info->avoidDECT)) {
		/*  Exclude LO1 values that conflict with DECT channels */
		MT2063_AddExclZone(pAS_Info, 1920836000 - pAS_Info->f_in, 1922236000 - pAS_Info->f_in);	/* Ctr = 1921.536 */
		MT2063_AddExclZone(pAS_Info, 1922564000 - pAS_Info->f_in, 1923964000 - pAS_Info->f_in);	/* Ctr = 1923.264 */
		MT2063_AddExclZone(pAS_Info, 1924292000 - pAS_Info->f_in, 1925692000 - pAS_Info->f_in);	/* Ctr = 1924.992 */
		MT2063_AddExclZone(pAS_Info, 1926020000 - pAS_Info->f_in, 1927420000 - pAS_Info->f_in);	/* Ctr = 1926.720 */
		MT2063_AddExclZone(pAS_Info, 1927748000 - pAS_Info->f_in, 1929148000 - pAS_Info->f_in);	/* Ctr = 1928.448 */
	}

	if (MT2063_EXCLUDE_EURO_DECT_FREQUENCIES(pAS_Info->avoidDECT)) {
		MT2063_AddExclZone(pAS_Info, 1896644000 - pAS_Info->f_in, 1898044000 - pAS_Info->f_in);	/* Ctr = 1897.344 */
		MT2063_AddExclZone(pAS_Info, 1894916000 - pAS_Info->f_in, 1896316000 - pAS_Info->f_in);	/* Ctr = 1895.616 */
		MT2063_AddExclZone(pAS_Info, 1893188000 - pAS_Info->f_in, 1894588000 - pAS_Info->f_in);	/* Ctr = 1893.888 */
		MT2063_AddExclZone(pAS_Info, 1891460000 - pAS_Info->f_in, 1892860000 - pAS_Info->f_in);	/* Ctr = 1892.16  */
		MT2063_AddExclZone(pAS_Info, 1889732000 - pAS_Info->f_in, 1891132000 - pAS_Info->f_in);	/* Ctr = 1890.432 */
		MT2063_AddExclZone(pAS_Info, 1888004000 - pAS_Info->f_in, 1889404000 - pAS_Info->f_in);	/* Ctr = 1888.704 */
		MT2063_AddExclZone(pAS_Info, 1886276000 - pAS_Info->f_in, 1887676000 - pAS_Info->f_in);	/* Ctr = 1886.976 */
		MT2063_AddExclZone(pAS_Info, 1884548000 - pAS_Info->f_in, 1885948000 - pAS_Info->f_in);	/* Ctr = 1885.248 */
		MT2063_AddExclZone(pAS_Info, 1882820000 - pAS_Info->f_in, 1884220000 - pAS_Info->f_in);	/* Ctr = 1883.52  */
		MT2063_AddExclZone(pAS_Info, 1881092000 - pAS_Info->f_in, 1882492000 - pAS_Info->f_in);	/* Ctr = 1881.792 */
	}
}

static struct MT2063_ExclZone_t *InsertNode(struct MT2063_AvoidSpursData_t
					    *pAS_Info,
					    struct MT2063_ExclZone_t *pPrevNode)
{
	struct MT2063_ExclZone_t *pNode;
	/*  Check for a node in the free list  */
	if (pAS_Info->freeZones != NULL) {
		/*  Use one from the free list  */
		pNode = pAS_Info->freeZones;
		pAS_Info->freeZones = pNode->next_;
	} else {
		/*  Grab a node from the array  */
		pNode = &pAS_Info->MT2063_ExclZones[pAS_Info->nZones];
	}

	if (pPrevNode != NULL) {
		pNode->next_ = pPrevNode->next_;
		pPrevNode->next_ = pNode;
	} else {		/*  insert at the beginning of the list  */

		pNode->next_ = pAS_Info->usedZones;
		pAS_Info->usedZones = pNode;
	}

	pAS_Info->nZones++;
	return pNode;
}

static struct MT2063_ExclZone_t *RemoveNode(struct MT2063_AvoidSpursData_t
					    *pAS_Info,
					    struct MT2063_ExclZone_t *pPrevNode,
					    struct MT2063_ExclZone_t
					    *pNodeToRemove)
{
	struct MT2063_ExclZone_t *pNext = pNodeToRemove->next_;

	/*  Make previous node point to the subsequent node  */
	if (pPrevNode != NULL)
		pPrevNode->next_ = pNext;

	/*  Add pNodeToRemove to the beginning of the freeZones  */
	pNodeToRemove->next_ = pAS_Info->freeZones;
	pAS_Info->freeZones = pNodeToRemove;

	/*  Decrement node count  */
	pAS_Info->nZones--;

	return pNext;
}

/*****************************************************************************
**
**  Name: MT_AddExclZone
**
**  Description:    Add (and merge) an exclusion zone into the list.
**                  If the range (f_min, f_max) is totally outside the
**                  1st IF BW, ignore the entry.
**                  If the range (f_min, f_max) is negative, ignore the entry.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   103   01-31-2005    DAD    Ver 1.14: In MT_AddExclZone(), if the range
**                              (f_min, f_max) < 0, ignore the entry.
**
*****************************************************************************/
static void MT2063_AddExclZone(struct MT2063_AvoidSpursData_t *pAS_Info,
			u32 f_min, u32 f_max)
{
	struct MT2063_ExclZone_t *pNode = pAS_Info->usedZones;
	struct MT2063_ExclZone_t *pPrev = NULL;
	struct MT2063_ExclZone_t *pNext = NULL;

	/*  Check to see if this overlaps the 1st IF filter  */
	if ((f_max > (pAS_Info->f_if1_Center - (pAS_Info->f_if1_bw / 2)))
	    && (f_min < (pAS_Info->f_if1_Center + (pAS_Info->f_if1_bw / 2)))
	    && (f_min < f_max)) {
		/*
		 **                1           2          3        4         5          6
		 **
		 **   New entry:  |---|    |--|        |--|       |-|      |---|         |--|
		 **                     or          or        or       or        or
		 **   Existing:  |--|          |--|      |--|    |---|      |-|      |--|
		 */

		/*  Check for our place in the list  */
		while ((pNode != NULL) && (pNode->max_ < f_min)) {
			pPrev = pNode;
			pNode = pNode->next_;
		}

		if ((pNode != NULL) && (pNode->min_ < f_max)) {
			/*  Combine me with pNode  */
			if (f_min < pNode->min_)
				pNode->min_ = f_min;
			if (f_max > pNode->max_)
				pNode->max_ = f_max;
		} else {
			pNode = InsertNode(pAS_Info, pPrev);
			pNode->min_ = f_min;
			pNode->max_ = f_max;
		}

		/*  Look for merging possibilities  */
		pNext = pNode->next_;
		while ((pNext != NULL) && (pNext->min_ < pNode->max_)) {
			if (pNext->max_ > pNode->max_)
				pNode->max_ = pNext->max_;
			pNext = RemoveNode(pAS_Info, pNode, pNext);	/*  Remove pNext, return ptr to pNext->next  */
		}
	}
}

/*****************************************************************************
**
**  Name: MT_ChooseFirstIF
**
**  Description:    Choose the best available 1st IF
**                  If f_Desired is not excluded, choose that first.
**                  Otherwise, return the value closest to f_Center that is
**                  not excluded
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   117   03-29-2007    RSK    Ver 1.15: Re-wrote to match search order from
**                              tuner DLL.
**   147   07-27-2007    RSK    Ver 1.17: Corrected calculation (-) to (+)
**                              Added logic to force f_Center within 1/2 f_Step.
**
*****************************************************************************/
static u32 MT2063_ChooseFirstIF(struct MT2063_AvoidSpursData_t *pAS_Info)
{
	/*
	 ** Update "f_Desired" to be the nearest "combinational-multiple" of "f_LO1_Step".
	 ** The resulting number, F_LO1 must be a multiple of f_LO1_Step.  And F_LO1 is the arithmetic sum
	 ** of f_in + f_Center.  Neither f_in, nor f_Center must be a multiple of f_LO1_Step.
	 ** However, the sum must be.
	 */
	const u32 f_Desired =
	    pAS_Info->f_LO1_Step *
	    ((pAS_Info->f_if1_Request + pAS_Info->f_in +
	      pAS_Info->f_LO1_Step / 2) / pAS_Info->f_LO1_Step) -
	    pAS_Info->f_in;
	const u32 f_Step =
	    (pAS_Info->f_LO1_Step >
	     pAS_Info->f_LO2_Step) ? pAS_Info->f_LO1_Step : pAS_Info->
	    f_LO2_Step;
	u32 f_Center;

	s32 i;
	s32 j = 0;
	u32 bDesiredExcluded = 0;
	u32 bZeroExcluded = 0;
	s32 tmpMin, tmpMax;
	s32 bestDiff;
	struct MT2063_ExclZone_t *pNode = pAS_Info->usedZones;
	struct MT2063_FIFZone_t zones[MT2063_MAX_ZONES];

	if (pAS_Info->nZones == 0)
		return f_Desired;

	/*  f_Center needs to be an integer multiple of f_Step away from f_Desired */
	if (pAS_Info->f_if1_Center > f_Desired)
		f_Center =
		    f_Desired +
		    f_Step *
		    ((pAS_Info->f_if1_Center - f_Desired +
		      f_Step / 2) / f_Step);
	else
		f_Center =
		    f_Desired -
		    f_Step *
		    ((f_Desired - pAS_Info->f_if1_Center +
		      f_Step / 2) / f_Step);

	//assert;
	//if (!abs((s32) f_Center - (s32) pAS_Info->f_if1_Center) <= (s32) (f_Step/2))
	//          return 0;

	/*  Take MT_ExclZones, center around f_Center and change the resolution to f_Step  */
	while (pNode != NULL) {
		/*  floor function  */
		tmpMin =
		    floor((s32) (pNode->min_ - f_Center), (s32) f_Step);

		/*  ceil function  */
		tmpMax =
		    ceil((s32) (pNode->max_ - f_Center), (s32) f_Step);

		if ((pNode->min_ < f_Desired) && (pNode->max_ > f_Desired))
			bDesiredExcluded = 1;

		if ((tmpMin < 0) && (tmpMax > 0))
			bZeroExcluded = 1;

		/*  See if this zone overlaps the previous  */
		if ((j > 0) && (tmpMin < zones[j - 1].max_))
			zones[j - 1].max_ = tmpMax;
		else {
			/*  Add new zone  */
			//assert(j<MT2063_MAX_ZONES);
			//if (j>=MT2063_MAX_ZONES)
			//break;

			zones[j].min_ = tmpMin;
			zones[j].max_ = tmpMax;
			j++;
		}
		pNode = pNode->next_;
	}

	/*
	 **  If the desired is okay, return with it
	 */
	if (bDesiredExcluded == 0)
		return f_Desired;

	/*
	 **  If the desired is excluded and the center is okay, return with it
	 */
	if (bZeroExcluded == 0)
		return f_Center;

	/*  Find the value closest to 0 (f_Center)  */
	bestDiff = zones[0].min_;
	for (i = 0; i < j; i++) {
		if (abs(zones[i].min_) < abs(bestDiff))
			bestDiff = zones[i].min_;
		if (abs(zones[i].max_) < abs(bestDiff))
			bestDiff = zones[i].max_;
	}

	if (bestDiff < 0)
		return f_Center - ((u32) (-bestDiff) * f_Step);

	return f_Center + (bestDiff * f_Step);
}

/****************************************************************************
**
**  Name: gcd
**
**  Description:    Uses Euclid's algorithm
**
**  Parameters:     u, v     - unsigned values whose GCD is desired.
**
**  Global:         None
**
**  Returns:        greatest common divisor of u and v, if either value
**                  is 0, the other value is returned as the result.
**
**  Dependencies:   None.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   06-01-2004    JWS    Original
**   N/A   08-03-2004    DAD    Changed to Euclid's since it can handle
**                              unsigned numbers.
**
****************************************************************************/
static u32 MT2063_gcd(u32 u, u32 v)
{
	u32 r;

	while (v != 0) {
		r = u % v;
		u = v;
		v = r;
	}

	return u;
}

/****************************************************************************
**
**  Name: umax
**
**  Description:    Implements a simple maximum function for unsigned numbers.
**                  Implemented as a function rather than a macro to avoid
**                  multiple evaluation of the calling parameters.
**
**  Parameters:     a, b     - Values to be compared
**
**  Global:         None
**
**  Returns:        larger of the input values.
**
**  Dependencies:   None.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   06-02-2004    JWS    Original
**
****************************************************************************/
static u32 MT2063_umax(u32 a, u32 b)
{
	return (a >= b) ? a : b;
}

/****************************************************************************
**
**  Name: IsSpurInBand
**
**  Description:    Checks to see if a spur will be present within the IF's
**                  bandwidth. (fIFOut +/- fIFBW, -fIFOut +/- fIFBW)
**
**                    ma   mb                                     mc   md
**                  <--+-+-+-------------------+-------------------+-+-+-->
**                     |   ^                   0                   ^   |
**                     ^   b=-fIFOut+fIFBW/2      -b=+fIFOut-fIFBW/2   ^
**                     a=-fIFOut-fIFBW/2              -a=+fIFOut+fIFBW/2
**
**                  Note that some equations are doubled to prevent round-off
**                  problems when calculating fIFBW/2
**
**  Parameters:     pAS_Info - Avoid Spurs information block
**                  fm       - If spur, amount f_IF1 has to move negative
**                  fp       - If spur, amount f_IF1 has to move positive
**
**  Global:         None
**
**  Returns:        1 if an LO spur would be present, otherwise 0.
**
**  Dependencies:   None.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   11-28-2002    DAD    Implemented algorithm from applied patent
**
****************************************************************************/
static u32 IsSpurInBand(struct MT2063_AvoidSpursData_t *pAS_Info,
			    u32 * fm, u32 * fp)
{
	/*
	 **  Calculate LO frequency settings.
	 */
	u32 n, n0;
	const u32 f_LO1 = pAS_Info->f_LO1;
	const u32 f_LO2 = pAS_Info->f_LO2;
	const u32 d = pAS_Info->f_out + pAS_Info->f_out_bw / 2;
	const u32 c = d - pAS_Info->f_out_bw;
	const u32 f = pAS_Info->f_zif_bw / 2;
	const u32 f_Scale = (f_LO1 / (UINT_MAX / 2 / pAS_Info->maxH1)) + 1;
	s32 f_nsLO1, f_nsLO2;
	s32 f_Spur;
	u32 ma, mb, mc, md, me, mf;
	u32 lo_gcd, gd_Scale, gc_Scale, gf_Scale, hgds, hgfs, hgcs;
	*fm = 0;

	/*
	 ** For each edge (d, c & f), calculate a scale, based on the gcd
	 ** of f_LO1, f_LO2 and the edge value.  Use the larger of this
	 ** gcd-based scale factor or f_Scale.
	 */
	lo_gcd = MT2063_gcd(f_LO1, f_LO2);
	gd_Scale = MT2063_umax((u32) MT2063_gcd(lo_gcd, d), f_Scale);
	hgds = gd_Scale / 2;
	gc_Scale = MT2063_umax((u32) MT2063_gcd(lo_gcd, c), f_Scale);
	hgcs = gc_Scale / 2;
	gf_Scale = MT2063_umax((u32) MT2063_gcd(lo_gcd, f), f_Scale);
	hgfs = gf_Scale / 2;

	n0 = DIV_ROUND_UP(f_LO2 - d, f_LO1 - f_LO2);

	/*  Check out all multiples of LO1 from n0 to m_maxLOSpurHarmonic  */
	for (n = n0; n <= pAS_Info->maxH1; ++n) {
		md = (n * ((f_LO1 + hgds) / gd_Scale) -
		      ((d + hgds) / gd_Scale)) / ((f_LO2 + hgds) / gd_Scale);

		/*  If # fLO2 harmonics > m_maxLOSpurHarmonic, then no spurs present  */
		if (md >= pAS_Info->maxH1)
			break;

		ma = (n * ((f_LO1 + hgds) / gd_Scale) +
		      ((d + hgds) / gd_Scale)) / ((f_LO2 + hgds) / gd_Scale);

		/*  If no spurs between +/- (f_out + f_IFBW/2), then try next harmonic  */
		if (md == ma)
			continue;

		mc = (n * ((f_LO1 + hgcs) / gc_Scale) -
		      ((c + hgcs) / gc_Scale)) / ((f_LO2 + hgcs) / gc_Scale);
		if (mc != md) {
			f_nsLO1 = (s32) (n * (f_LO1 / gc_Scale));
			f_nsLO2 = (s32) (mc * (f_LO2 / gc_Scale));
			f_Spur =
			    (gc_Scale * (f_nsLO1 - f_nsLO2)) +
			    n * (f_LO1 % gc_Scale) - mc * (f_LO2 % gc_Scale);

			*fp = ((f_Spur - (s32) c) / (mc - n)) + 1;
			*fm = (((s32) d - f_Spur) / (mc - n)) + 1;
			return 1;
		}

		/*  Location of Zero-IF-spur to be checked  */
		me = (n * ((f_LO1 + hgfs) / gf_Scale) +
		      ((f + hgfs) / gf_Scale)) / ((f_LO2 + hgfs) / gf_Scale);
		mf = (n * ((f_LO1 + hgfs) / gf_Scale) -
		      ((f + hgfs) / gf_Scale)) / ((f_LO2 + hgfs) / gf_Scale);
		if (me != mf) {
			f_nsLO1 = n * (f_LO1 / gf_Scale);
			f_nsLO2 = me * (f_LO2 / gf_Scale);
			f_Spur =
			    (gf_Scale * (f_nsLO1 - f_nsLO2)) +
			    n * (f_LO1 % gf_Scale) - me * (f_LO2 % gf_Scale);

			*fp = ((f_Spur + (s32) f) / (me - n)) + 1;
			*fm = (((s32) f - f_Spur) / (me - n)) + 1;
			return 1;
		}

		mb = (n * ((f_LO1 + hgcs) / gc_Scale) +
		      ((c + hgcs) / gc_Scale)) / ((f_LO2 + hgcs) / gc_Scale);
		if (ma != mb) {
			f_nsLO1 = n * (f_LO1 / gc_Scale);
			f_nsLO2 = ma * (f_LO2 / gc_Scale);
			f_Spur =
			    (gc_Scale * (f_nsLO1 - f_nsLO2)) +
			    n * (f_LO1 % gc_Scale) - ma * (f_LO2 % gc_Scale);

			*fp = (((s32) d + f_Spur) / (ma - n)) + 1;
			*fm = (-(f_Spur + (s32) c) / (ma - n)) + 1;
			return 1;
		}
	}

	/*  No spurs found  */
	return 0;
}

/*****************************************************************************
**
**  Name: MT_AvoidSpurs
**
**  Description:    Main entry point to avoid spurs.
**                  Checks for existing spurs in present LO1, LO2 freqs
**                  and if present, chooses spur-free LO1, LO2 combination
**                  that tunes the same input/output frequencies.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   096   04-06-2005    DAD    Ver 1.11: Fix divide by 0 error if maxH==0.
**
*****************************************************************************/
static u32 MT2063_AvoidSpurs(void *h, struct MT2063_AvoidSpursData_t * pAS_Info)
{
	u32 status = 0;
	u32 fm, fp;		/*  restricted range on LO's        */
	pAS_Info->bSpurAvoided = 0;
	pAS_Info->nSpursFound = 0;

	if (pAS_Info->maxH1 == 0)
		return 0;

	/*
	 **  Avoid LO Generated Spurs
	 **
	 **  Make sure that have no LO-related spurs within the IF output
	 **  bandwidth.
	 **
	 **  If there is an LO spur in this band, start at the current IF1 frequency
	 **  and work out until we find a spur-free frequency or run up against the
	 **  1st IF SAW band edge.  Use temporary copies of fLO1 and fLO2 so that they
	 **  will be unchanged if a spur-free setting is not found.
	 */
	pAS_Info->bSpurPresent = IsSpurInBand(pAS_Info, &fm, &fp);
	if (pAS_Info->bSpurPresent) {
		u32 zfIF1 = pAS_Info->f_LO1 - pAS_Info->f_in;	/*  current attempt at a 1st IF  */
		u32 zfLO1 = pAS_Info->f_LO1;	/*  current attempt at an LO1 freq  */
		u32 zfLO2 = pAS_Info->f_LO2;	/*  current attempt at an LO2 freq  */
		u32 delta_IF1;
		u32 new_IF1;

		/*
		 **  Spur was found, attempt to find a spur-free 1st IF
		 */
		do {
			pAS_Info->nSpursFound++;

			/*  Raise f_IF1_upper, if needed  */
			MT2063_AddExclZone(pAS_Info, zfIF1 - fm, zfIF1 + fp);

			/*  Choose next IF1 that is closest to f_IF1_CENTER              */
			new_IF1 = MT2063_ChooseFirstIF(pAS_Info);

			if (new_IF1 > zfIF1) {
				pAS_Info->f_LO1 += (new_IF1 - zfIF1);
				pAS_Info->f_LO2 += (new_IF1 - zfIF1);
			} else {
				pAS_Info->f_LO1 -= (zfIF1 - new_IF1);
				pAS_Info->f_LO2 -= (zfIF1 - new_IF1);
			}
			zfIF1 = new_IF1;

			if (zfIF1 > pAS_Info->f_if1_Center)
				delta_IF1 = zfIF1 - pAS_Info->f_if1_Center;
			else
				delta_IF1 = pAS_Info->f_if1_Center - zfIF1;
		}
		/*
		 **  Continue while the new 1st IF is still within the 1st IF bandwidth
		 **  and there is a spur in the band (again)
		 */
		while ((2 * delta_IF1 + pAS_Info->f_out_bw <=
			pAS_Info->f_if1_bw)
		       && (pAS_Info->bSpurPresent =
			   IsSpurInBand(pAS_Info, &fm, &fp)));

		/*
		 ** Use the LO-spur free values found.  If the search went all the way to
		 ** the 1st IF band edge and always found spurs, just leave the original
		 ** choice.  It's as "good" as any other.
		 */
		if (pAS_Info->bSpurPresent == 1) {
			status |= MT2063_SPUR_PRESENT_ERR;
			pAS_Info->f_LO1 = zfLO1;
			pAS_Info->f_LO2 = zfLO2;
		} else
			pAS_Info->bSpurAvoided = 1;
	}

	status |=
	    ((pAS_Info->
	      nSpursFound << MT2063_SPUR_SHIFT) & MT2063_SPUR_CNT_MASK);

	return (status);
}

//end of mt2063_spuravoid.c
//=================================================================
//#################################################################
//=================================================================

/*
**  The expected version of MT_AvoidSpursData_t
**  If the version is different, an updated file is needed from Microtune
*/
/* Expecting version 1.21 of the Spur Avoidance API */

typedef enum {
	MT2063_SET_ATTEN,
	MT2063_INCR_ATTEN,
	MT2063_DECR_ATTEN
} MT2063_ATTEN_CNTL_MODE;

//#define TUNER_MT2063_OPTIMIZATION
/*
** Constants used by the tuning algorithm
*/
#define MT2063_REF_FREQ          (16000000UL)	/* Reference oscillator Frequency (in Hz) */
#define MT2063_IF1_BW            (22000000UL)	/* The IF1 filter bandwidth (in Hz) */
#define MT2063_TUNE_STEP_SIZE       (50000UL)	/* Tune in steps of 50 kHz */
#define MT2063_SPUR_STEP_HZ        (250000UL)	/* Step size (in Hz) to move IF1 when avoiding spurs */
#define MT2063_ZIF_BW             (2000000UL)	/* Zero-IF spur-free bandwidth (in Hz) */
#define MT2063_MAX_HARMONICS_1         (15UL)	/* Highest intra-tuner LO Spur Harmonic to be avoided */
#define MT2063_MAX_HARMONICS_2          (5UL)	/* Highest inter-tuner LO Spur Harmonic to be avoided */
#define MT2063_MIN_LO_SEP         (1000000UL)	/* Minimum inter-tuner LO frequency separation */
#define MT2063_LO1_FRACN_AVOID          (0UL)	/* LO1 FracN numerator avoid region (in Hz) */
#define MT2063_LO2_FRACN_AVOID     (199999UL)	/* LO2 FracN numerator avoid region (in Hz) */
#define MT2063_MIN_FIN_FREQ      (44000000UL)	/* Minimum input frequency (in Hz) */
#define MT2063_MAX_FIN_FREQ    (1100000000UL)	/* Maximum input frequency (in Hz) */
#define MT2063_MIN_FOUT_FREQ     (36000000UL)	/* Minimum output frequency (in Hz) */
#define MT2063_MAX_FOUT_FREQ     (57000000UL)	/* Maximum output frequency (in Hz) */
#define MT2063_MIN_DNC_FREQ    (1293000000UL)	/* Minimum LO2 frequency (in Hz) */
#define MT2063_MAX_DNC_FREQ    (1614000000UL)	/* Maximum LO2 frequency (in Hz) */
#define MT2063_MIN_UPC_FREQ    (1396000000UL)	/* Minimum LO1 frequency (in Hz) */
#define MT2063_MAX_UPC_FREQ    (2750000000UL)	/* Maximum LO1 frequency (in Hz) */

/*
**  Define the supported Part/Rev codes for the MT2063
*/
#define MT2063_B0       (0x9B)
#define MT2063_B1       (0x9C)
#define MT2063_B2       (0x9D)
#define MT2063_B3       (0x9E)

/*
**  The number of Tuner Registers
*/
static const u32 MT2063_Num_Registers = MT2063_REG_END_REGS;

#define USE_GLOBAL_TUNER			0

static u32 nMT2063MaxTuners = 1;
static u32 nMT2063OpenTuners = 0;

/*
**  Constants for setting receiver modes.
**  (6 modes defined at this time, enumerated by MT2063_RCVR_MODES)
**  (DNC1GC & DNC2GC are the values, which are used, when the specific
**   DNC Output is selected, the other is always off)
**
**   If PAL-L or L' is received, set:
**       MT2063_SetParam(hMT2063,MT2063_TAGC,1);
**
**                --------------+----------------------------------------------
**                 Mode 0 :     | MT2063_CABLE_QAM
**                 Mode 1 :     | MT2063_CABLE_ANALOG
**                 Mode 2 :     | MT2063_OFFAIR_COFDM
**                 Mode 3 :     | MT2063_OFFAIR_COFDM_SAWLESS
**                 Mode 4 :     | MT2063_OFFAIR_ANALOG
**                 Mode 5 :     | MT2063_OFFAIR_8VSB
**                --------------+----+----+----+----+-----+-----+--------------
**                 Mode         |  0 |  1 |  2 |  3 |  4  |  5  |
**                --------------+----+----+----+----+-----+-----+
**
**
*/
static const u8 RFAGCEN[] = { 0, 0, 0, 0, 0, 0 };
static const u8 LNARIN[] = { 0, 0, 3, 3, 3, 3 };
static const u8 FIFFQEN[] = { 1, 1, 1, 1, 1, 1 };
static const u8 FIFFQ[] = { 0, 0, 0, 0, 0, 0 };
static const u8 DNC1GC[] = { 0, 0, 0, 0, 0, 0 };
static const u8 DNC2GC[] = { 0, 0, 0, 0, 0, 0 };
static const u8 ACLNAMAX[] = { 31, 31, 31, 31, 31, 31 };
static const u8 LNATGT[] = { 44, 43, 43, 43, 43, 43 };
static const u8 RFOVDIS[] = { 0, 0, 0, 0, 0, 0 };
static const u8 ACRFMAX[] = { 31, 31, 31, 31, 31, 31 };
static const u8 PD1TGT[] = { 36, 36, 38, 38, 36, 38 };
static const u8 FIFOVDIS[] = { 0, 0, 0, 0, 0, 0 };
static const u8 ACFIFMAX[] = { 29, 29, 29, 29, 29, 29 };
static const u8 PD2TGT[] = { 40, 33, 38, 42, 30, 38 };

/*
**  Local Function Prototypes - not available for external access.
*/

/*  Forward declaration(s):  */
static u32 MT2063_CalcLO1Mult(u32 * Div, u32 * FracN, u32 f_LO,
				  u32 f_LO_Step, u32 f_Ref);
static u32 MT2063_CalcLO2Mult(u32 * Div, u32 * FracN, u32 f_LO,
				  u32 f_LO_Step, u32 f_Ref);
static u32 MT2063_fLO_FractionalTerm(u32 f_ref, u32 num,
					 u32 denom);

/******************************************************************************
**
**  Name: MT2063_Open
**
**  Description:    Initialize the tuner's register values.
**
**  Parameters:     MT2063_Addr  - Serial bus address of the tuner.
**                  hMT2063      - Tuner handle passed back.
**                  hUserData    - User-defined data, if needed for the
**                                 MT_ReadSub() & MT_WriteSub functions.
**
**  Returns:        status:
**                      MT_OK             - No errors
**                      MT_TUNER_ID_ERR   - Tuner Part/Rev code mismatch
**                      MT_TUNER_INIT_ERR - Tuner initialization failed
**                      MT_COMM_ERR       - Serial bus communications error
**                      MT_ARG_NULL       - Null pointer argument passed
**                      MT_TUNER_CNT_ERR  - Too many tuners open
**
**  Dependencies:   MT_ReadSub  - Read byte(s) of data from the two-wire bus
**                  MT_WriteSub - Write byte(s) of data to the two-wire bus
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
******************************************************************************/
static u32 MT2063_Open(struct dvb_frontend *fe)
{
	u32 status;	/*  Status to be returned.  */
	struct mt2063_state *state = fe->tuner_priv;

	state->rcvr_mode = MT2063_CABLE_QAM;
	if (state->MT2063_init != false) {
		status = MT2063_ReInit(state);
		if (status < 0)
			return status;
	}

	state->MT2063_init = true;
	return 0;
}

/****************************************************************************
**
**  Name: MT2063_GetLocked
**
**  Description:    Checks to see if LO1 and LO2 are locked.
**
**  Parameters:     h            - Open handle to the tuner (from MT2063_Open).
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_UPC_UNLOCK    - Upconverter PLL unlocked
**                      MT_DNC_UNLOCK    - Downconverter PLL unlocked
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**
**  Dependencies:   MT_ReadSub    - Read byte(s) of data from the serial bus
**                  MT_Sleep      - Delay execution for x milliseconds
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
static u32 MT2063_GetLocked(struct mt2063_state *state)
{
	const u32 nMaxWait = 100;	/*  wait a maximum of 100 msec   */
	const u32 nPollRate = 2;	/*  poll status bits every 2 ms */
	const u32 nMaxLoops = nMaxWait / nPollRate;
	const u8 LO1LK = 0x80;
	u8 LO2LK = 0x08;
	u32 status = 0;	/* Status to be returned        */
	u32 nDelays = 0;

	/*  LO2 Lock bit was in a different place for B0 version  */
	if (state->tuner_id == MT2063_B0)
		LO2LK = 0x40;

	do {
		status |=
		    mt2063_read(state,
				   MT2063_REG_LO_STATUS,
				   &state->reg[MT2063_REG_LO_STATUS], 1);

		if (status < 0)
			return (status);

		if ((state->reg[MT2063_REG_LO_STATUS] & (LO1LK | LO2LK)) ==
		    (LO1LK | LO2LK)) {
			return (status);
		}
		msleep(nPollRate);	/*  Wait between retries  */
	}
	while (++nDelays < nMaxLoops);

	if ((state->reg[MT2063_REG_LO_STATUS] & LO1LK) == 0x00)
		status |= MT2063_UPC_UNLOCK;
	if ((state->reg[MT2063_REG_LO_STATUS] & LO2LK) == 0x00)
		status |= MT2063_DNC_UNLOCK;

	return (status);
}

/****************************************************************************
**
**  Name: MT2063_GetParam
**
**  Description:    Gets a tuning algorithm parameter.
**
**                  This function provides access to the internals of the
**                  tuning algorithm - mostly for testing purposes.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  param       - Tuning algorithm parameter
**                                (see enum MT2063_Param)
**                  pValue      - ptr to returned value
**
**                  param                     Description
**                  ----------------------    --------------------------------
**                  MT2063_IC_ADDR            Serial Bus address of this tuner
**                  MT2063_MAX_OPEN           Max # of MT2063's allowed open
**                  MT2063_NUM_OPEN           # of MT2063's open
**                  MT2063_SRO_FREQ           crystal frequency
**                  MT2063_STEPSIZE           minimum tuning step size
**                  MT2063_INPUT_FREQ         input center frequency
**                  MT2063_LO1_FREQ           LO1 Frequency
**                  MT2063_LO1_STEPSIZE       LO1 minimum step size
**                  MT2063_LO1_FRACN_AVOID    LO1 FracN keep-out region
**                  MT2063_IF1_ACTUAL         Current 1st IF in use
**                  MT2063_IF1_REQUEST        Requested 1st IF
**                  MT2063_IF1_CENTER         Center of 1st IF SAW filter
**                  MT2063_IF1_BW             Bandwidth of 1st IF SAW filter
**                  MT2063_ZIF_BW             zero-IF bandwidth
**                  MT2063_LO2_FREQ           LO2 Frequency
**                  MT2063_LO2_STEPSIZE       LO2 minimum step size
**                  MT2063_LO2_FRACN_AVOID    LO2 FracN keep-out region
**                  MT2063_OUTPUT_FREQ        output center frequency
**                  MT2063_OUTPUT_BW          output bandwidth
**                  MT2063_LO_SEPARATION      min inter-tuner LO separation
**                  MT2063_AS_ALG             ID of avoid-spurs algorithm in use
**                  MT2063_MAX_HARM1          max # of intra-tuner harmonics
**                  MT2063_MAX_HARM2          max # of inter-tuner harmonics
**                  MT2063_EXCL_ZONES         # of 1st IF exclusion zones
**                  MT2063_NUM_SPURS          # of spurs found/avoided
**                  MT2063_SPUR_AVOIDED       >0 spurs avoided
**                  MT2063_SPUR_PRESENT       >0 spurs in output (mathematically)
**                  MT2063_RCVR_MODE          Predefined modes.
**                  MT2063_ACLNA              LNA attenuator gain code
**                  MT2063_ACRF               RF attenuator gain code
**                  MT2063_ACFIF              FIF attenuator gain code
**                  MT2063_ACLNA_MAX          LNA attenuator limit
**                  MT2063_ACRF_MAX           RF attenuator limit
**                  MT2063_ACFIF_MAX          FIF attenuator limit
**                  MT2063_PD1                Actual value of PD1
**                  MT2063_PD2                Actual value of PD2
**                  MT2063_DNC_OUTPUT_ENABLE  DNC output selection
**                  MT2063_VGAGC              VGA gain code
**                  MT2063_VGAOI              VGA output current
**                  MT2063_TAGC               TAGC setting
**                  MT2063_AMPGC              AMP gain code
**                  MT2063_AVOID_DECT         Avoid DECT Frequencies
**                  MT2063_CTFILT_SW          Cleartune filter selection
**
**  Usage:          status |= MT2063_GetParam(hMT2063,
**                                            MT2063_IF1_ACTUAL,
**                                            &f_IF1_Actual);
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Null pointer argument passed
**                      MT_ARG_RANGE     - Invalid parameter requested
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**  See Also:       MT2063_SetParam, MT2063_Open
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**   154   09-13-2007    RSK    Ver 1.05: Get/SetParam changes for LOx_FREQ
**         10-31-2007    PINZ   Ver 1.08: Get/SetParam add VGAGC, VGAOI, AMPGC, TAGC
**   173 M 01-23-2008    RSK    Ver 1.12: Read LO1C and LO2C registers from HW
**                                        in GetParam.
**         04-18-2008    PINZ   Ver 1.15: Add SetParam LNARIN & PDxTGT
**                                        Split SetParam up to ACLNA / ACLNA_MAX
**                                        removed ACLNA_INRC/DECR (+RF & FIF)
**                                        removed GCUAUTO / BYPATNDN/UP
**   175 I 16-06-2008    PINZ   Ver 1.16: Add control to avoid US DECT freqs.
**   175 I 06-19-2008    RSK    Ver 1.17: Refactor DECT control to SpurAvoid.
**         06-24-2008    PINZ   Ver 1.18: Add Get/SetParam CTFILT_SW
**
****************************************************************************/
static u32 MT2063_GetParam(struct mt2063_state *state, enum MT2063_Param param, u32 *pValue)
{
	u32 status = 0;	/* Status to be returned        */
	u32 Div;
	u32 Num;

	if (pValue == NULL)
		return -EINVAL;

	switch (param) {
		/*  Serial Bus address of this tuner      */
	case MT2063_IC_ADDR:
		*pValue = state->config->tuner_address;
		break;

		/*  Max # of MT2063's allowed to be open  */
	case MT2063_MAX_OPEN:
		*pValue = nMT2063MaxTuners;
		break;

		/*  # of MT2063's open                    */
	case MT2063_NUM_OPEN:
		*pValue = nMT2063OpenTuners;
		break;

		/*  crystal frequency                     */
	case MT2063_SRO_FREQ:
		*pValue = state->AS_Data.f_ref;
		break;

		/*  minimum tuning step size              */
	case MT2063_STEPSIZE:
		*pValue = state->AS_Data.f_LO2_Step;
		break;

		/*  input center frequency                */
	case MT2063_INPUT_FREQ:
		*pValue = state->AS_Data.f_in;
		break;

		/*  LO1 Frequency                         */
	case MT2063_LO1_FREQ:
		{
			/* read the actual tuner register values for LO1C_1 and LO1C_2 */
			status |=
			    mt2063_read(state,
					   MT2063_REG_LO1C_1,
					   &state->
					   reg[MT2063_REG_LO1C_1], 2);
			Div = state->reg[MT2063_REG_LO1C_1];
			Num = state->reg[MT2063_REG_LO1C_2] & 0x3F;
			state->AS_Data.f_LO1 =
			    (state->AS_Data.f_ref * Div) +
			    MT2063_fLO_FractionalTerm(state->AS_Data.
						      f_ref, Num, 64);
		}
		*pValue = state->AS_Data.f_LO1;
		break;

		/*  LO1 minimum step size                 */
	case MT2063_LO1_STEPSIZE:
		*pValue = state->AS_Data.f_LO1_Step;
		break;

		/*  LO1 FracN keep-out region             */
	case MT2063_LO1_FRACN_AVOID_PARAM:
		*pValue = state->AS_Data.f_LO1_FracN_Avoid;
		break;

		/*  Current 1st IF in use                 */
	case MT2063_IF1_ACTUAL:
		*pValue = state->f_IF1_actual;
		break;

		/*  Requested 1st IF                      */
	case MT2063_IF1_REQUEST:
		*pValue = state->AS_Data.f_if1_Request;
		break;

		/*  Center of 1st IF SAW filter           */
	case MT2063_IF1_CENTER:
		*pValue = state->AS_Data.f_if1_Center;
		break;

		/*  Bandwidth of 1st IF SAW filter        */
	case MT2063_IF1_BW:
		*pValue = state->AS_Data.f_if1_bw;
		break;

		/*  zero-IF bandwidth                     */
	case MT2063_ZIF_BW:
		*pValue = state->AS_Data.f_zif_bw;
		break;

		/*  LO2 Frequency                         */
	case MT2063_LO2_FREQ:
		{
			/* Read the actual tuner register values for LO2C_1, LO2C_2 and LO2C_3 */
			status |=
			    mt2063_read(state,
					   MT2063_REG_LO2C_1,
					   &state->
					   reg[MT2063_REG_LO2C_1], 3);
			Div =
			    (state->reg[MT2063_REG_LO2C_1] & 0xFE) >> 1;
			Num =
			    ((state->
			      reg[MT2063_REG_LO2C_1] & 0x01) << 12) |
			    (state->
			     reg[MT2063_REG_LO2C_2] << 4) | (state->
							     reg
							     [MT2063_REG_LO2C_3]
							     & 0x00F);
			state->AS_Data.f_LO2 =
			    (state->AS_Data.f_ref * Div) +
			    MT2063_fLO_FractionalTerm(state->AS_Data.
						      f_ref, Num, 8191);
		}
		*pValue = state->AS_Data.f_LO2;
		break;

		/*  LO2 minimum step size                 */
	case MT2063_LO2_STEPSIZE:
		*pValue = state->AS_Data.f_LO2_Step;
		break;

		/*  LO2 FracN keep-out region             */
	case MT2063_LO2_FRACN_AVOID:
		*pValue = state->AS_Data.f_LO2_FracN_Avoid;
		break;

		/*  output center frequency               */
	case MT2063_OUTPUT_FREQ:
		*pValue = state->AS_Data.f_out;
		break;

		/*  output bandwidth                      */
	case MT2063_OUTPUT_BW:
		*pValue = state->AS_Data.f_out_bw - 750000;
		break;

		/*  min inter-tuner LO separation         */
	case MT2063_LO_SEPARATION:
		*pValue = state->AS_Data.f_min_LO_Separation;
		break;

		/*  max # of intra-tuner harmonics        */
	case MT2063_MAX_HARM1:
		*pValue = state->AS_Data.maxH1;
		break;

		/*  max # of inter-tuner harmonics        */
	case MT2063_MAX_HARM2:
		*pValue = state->AS_Data.maxH2;
		break;

		/*  # of 1st IF exclusion zones           */
	case MT2063_EXCL_ZONES:
		*pValue = state->AS_Data.nZones;
		break;

		/*  # of spurs found/avoided              */
	case MT2063_NUM_SPURS:
		*pValue = state->AS_Data.nSpursFound;
		break;

		/*  >0 spurs avoided                      */
	case MT2063_SPUR_AVOIDED:
		*pValue = state->AS_Data.bSpurAvoided;
		break;

		/*  >0 spurs in output (mathematically)   */
	case MT2063_SPUR_PRESENT:
		*pValue = state->AS_Data.bSpurPresent;
		break;

		/*  Predefined receiver setup combination */
	case MT2063_RCVR_MODE:
		*pValue = state->rcvr_mode;
		break;

	case MT2063_PD1:
	case MT2063_PD2: {
		u8 mask = (param == MT2063_PD1 ? 0x01 : 0x03);	/* PD1 vs PD2 */
		u8 orig = (state->reg[MT2063_REG_BYP_CTRL]);
		u8 reg = (orig & 0xF1) | mask;	/* Only set 3 bits (not 5) */
		int i;

		*pValue = 0;

		/* Initiate ADC output to reg 0x0A */
		if (reg != orig)
			status |=
			    mt2063_write(state,
					    MT2063_REG_BYP_CTRL,
					    &reg, 1);

		if (status < 0)
			return (status);

		for (i = 0; i < 8; i++) {
			status |=
			    mt2063_read(state,
					   MT2063_REG_ADC_OUT,
					   &state->
					   reg
					   [MT2063_REG_ADC_OUT],
					   1);

			if (status >= 0)
				*pValue +=
				    state->
				    reg[MT2063_REG_ADC_OUT];
			else {
				if (i)
					*pValue /= i;
				return (status);
			}
		}
		*pValue /= 8;	/*  divide by number of reads  */
		*pValue >>= 2;	/*  only want 6 MSB's out of 8  */

		/* Restore value of Register BYP_CTRL */
		if (reg != orig)
			status |=
			    mt2063_write(state,
					    MT2063_REG_BYP_CTRL,
						&orig, 1);
		}
		break;

		/*  Get LNA attenuator code                */
	case MT2063_ACLNA:
	{
		u8 val;
		status |=
		    MT2063_GetReg(state, MT2063_REG_XO_STATUS,
				  &val);
		*pValue = val & 0x1f;
	}
	break;

	/*  Get RF attenuator code                */
	case MT2063_ACRF:
	{
		u8 val;
		status |=
		    MT2063_GetReg(state, MT2063_REG_RF_STATUS,
				  &val);
		*pValue = val & 0x1f;
	}
	break;

	/*  Get FIF attenuator code               */
	case MT2063_ACFIF:
	{
		u8 val;
		status |=
		    MT2063_GetReg(state, MT2063_REG_FIF_STATUS,
				  &val);
		*pValue = val & 0x1f;
	}
	break;

	/*  Get LNA attenuator limit              */
	case MT2063_ACLNA_MAX:
	{
		u8 val;
		status |=
		    MT2063_GetReg(state, MT2063_REG_LNA_OV,
				  &val);
		*pValue = val & 0x1f;
	}
	break;

	/*  Get RF attenuator limit               */
	case MT2063_ACRF_MAX:
	{
		u8 val;
		status |=
		    MT2063_GetReg(state, MT2063_REG_RF_OV,
				  &val);
		*pValue = val & 0x1f;
	}
	break;

	/*  Get FIF attenuator limit               */
	case MT2063_ACFIF_MAX:
	{
		u8 val;
		status |=
		    MT2063_GetReg(state, MT2063_REG_FIF_OV,
				  &val);
		*pValue = val & 0x1f;
	}
	break;

	/*  Get current used DNC output */
	case MT2063_DNC_OUTPUT_ENABLE:
	{
		if ((state->reg[MT2063_REG_DNC_GAIN] & 0x03) == 0x03) {	/* if DNC1 is off */
			if ((state->reg[MT2063_REG_VGA_GAIN] & 0x03) == 0x03)	/* if DNC2 is off */
				*pValue =
				    (u32) MT2063_DNC_NONE;
			else
				*pValue =
				    (u32) MT2063_DNC_2;
		} else {	/* DNC1 is on */

			if ((state->reg[MT2063_REG_VGA_GAIN] & 0x03) == 0x03)	/* if DNC2 is off */
				*pValue =
				    (u32) MT2063_DNC_1;
			else
				*pValue =
				    (u32) MT2063_DNC_BOTH;
		}
	}
	break;

	/*  Get VGA Gain Code */
		case MT2063_VGAGC:
		*pValue = ((state->reg[MT2063_REG_VGA_GAIN] & 0x0C) >> 2);
		break;

		/*  Get VGA bias current */
	case MT2063_VGAOI:
		*pValue = (state->reg[MT2063_REG_RSVD_31] & 0x07);
		break;

		/*  Get TAGC setting */
	case MT2063_TAGC:
		*pValue = (state->reg[MT2063_REG_RSVD_1E] & 0x03);
		break;

		/*  Get AMP Gain Code */
	case MT2063_AMPGC:
		*pValue = (state->reg[MT2063_REG_TEMP_SEL] & 0x03);
		break;

		/*  Avoid DECT Frequencies  */
	case MT2063_AVOID_DECT:
		*pValue = state->AS_Data.avoidDECT;
		break;

		/*  Cleartune filter selection: 0 - by IC (default), 1 - by software  */
	case MT2063_CTFILT_SW:
		*pValue = state->ctfilt_sw;
		break;

	case MT2063_EOP:
	default:
		status |= -ERANGE;
	}
	return (status);
}

/****************************************************************************
**
**  Name: MT2063_GetReg
**
**  Description:    Gets an MT2063 register.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  reg         - MT2063 register/subaddress location
**                  *val        - MT2063 register/subaddress value
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Null pointer argument passed
**                      MT_ARG_RANGE     - Argument out of range
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**                  Use this function if you need to read a register from
**                  the MT2063.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
static u32 MT2063_GetReg(struct mt2063_state *state, u8 reg, u8 * val)
{
	u32 status = 0;	/* Status to be returned        */

	if (val == NULL)
		return -EINVAL;

	if (reg >= MT2063_REG_END_REGS)
		return -ERANGE;

	status = mt2063_read(state, reg, &state->reg[reg], 1);

	return (status);
}

/******************************************************************************
**
**  Name: MT2063_SetReceiverMode
**
**  Description:    Set the MT2063 receiver mode
**
**   --------------+----------------------------------------------
**    Mode 0 :     | MT2063_CABLE_QAM
**    Mode 1 :     | MT2063_CABLE_ANALOG
**    Mode 2 :     | MT2063_OFFAIR_COFDM
**    Mode 3 :     | MT2063_OFFAIR_COFDM_SAWLESS
**    Mode 4 :     | MT2063_OFFAIR_ANALOG
**    Mode 5 :     | MT2063_OFFAIR_8VSB
**   --------------+----+----+----+----+-----+--------------------
**  (DNC1GC & DNC2GC are the values, which are used, when the specific
**   DNC Output is selected, the other is always off)
**
**                |<----------   Mode  -------------->|
**    Reg Field   |  0  |  1  |  2  |  3  |  4  |  5  |
**    ------------+-----+-----+-----+-----+-----+-----+
**    RFAGCen     | OFF | OFF | OFF | OFF | OFF | OFF
**    LNARin      |   0 |   0 |   3 |   3 |  3  |  3
**    FIFFQen     |   1 |   1 |   1 |   1 |  1  |  1
**    FIFFq       |   0 |   0 |   0 |   0 |  0  |  0
**    DNC1gc      |   0 |   0 |   0 |   0 |  0  |  0
**    DNC2gc      |   0 |   0 |   0 |   0 |  0  |  0
**    GCU Auto    |   1 |   1 |   1 |   1 |  1  |  1
**    LNA max Atn |  31 |  31 |  31 |  31 | 31  | 31
**    LNA Target  |  44 |  43 |  43 |  43 | 43  | 43
**    ign  RF Ovl |   0 |   0 |   0 |   0 |  0  |  0
**    RF  max Atn |  31 |  31 |  31 |  31 | 31  | 31
**    PD1 Target  |  36 |  36 |  38 |  38 | 36  | 38
**    ign FIF Ovl |   0 |   0 |   0 |   0 |  0  |  0
**    FIF max Atn |   5 |   5 |   5 |   5 |  5  |  5
**    PD2 Target  |  40 |  33 |  42 |  42 | 33  | 42
**
**
**  Parameters:     state       - ptr to mt2063_state structure
**                  Mode        - desired reciever mode
**
**  Usage:          status = MT2063_SetReceiverMode(hMT2063, Mode);
**
**  Returns:        status:
**                      MT_OK             - No errors
**                      MT_COMM_ERR       - Serial bus communications error
**
**  Dependencies:   MT2063_SetReg - Write a byte of data to a HW register.
**                  Assumes that the tuner cache is valid.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**   N/A   01-10-2007    PINZ   Added additional GCU Settings, FIFF Calib will be triggered
**   155   10-01-2007    DAD    Ver 1.06: Add receiver mode for SECAM positive
**                                        modulation
**                                        (MT2063_ANALOG_TV_POS_NO_RFAGC_MODE)
**   N/A   10-22-2007    PINZ   Ver 1.07: Changed some Registers at init to have
**                                        the same settings as with MT Launcher
**   N/A   10-30-2007    PINZ             Add SetParam VGAGC & VGAOI
**                                        Add SetParam DNC_OUTPUT_ENABLE
**                                        Removed VGAGC from receiver mode,
**                                        default now 1
**   N/A   10-31-2007    PINZ   Ver 1.08: Add SetParam TAGC, removed from rcvr-mode
**                                        Add SetParam AMPGC, removed from rcvr-mode
**                                        Corrected names of GCU values
**                                        reorganized receiver modes, removed,
**                                        (MT2063_ANALOG_TV_POS_NO_RFAGC_MODE)
**                                        Actualized Receiver-Mode values
**   N/A   11-12-2007    PINZ   Ver 1.09: Actualized Receiver-Mode values
**   N/A   11-27-2007    PINZ             Improved buffered writing
**         01-03-2008    PINZ   Ver 1.10: Added a trigger of BYPATNUP for
**                                        correct wakeup of the LNA after shutdown
**                                        Set AFCsd = 1 as default
**                                        Changed CAP1sel default
**         01-14-2008    PINZ   Ver 1.11: Updated gain settings
**         04-18-2008    PINZ   Ver 1.15: Add SetParam LNARIN & PDxTGT
**                                        Split SetParam up to ACLNA / ACLNA_MAX
**                                        removed ACLNA_INRC/DECR (+RF & FIF)
**                                        removed GCUAUTO / BYPATNDN/UP
**
******************************************************************************/
static u32 MT2063_SetReceiverMode(struct mt2063_state *state,
				      enum MT2063_RCVR_MODES Mode)
{
	u32 status = 0;	/* Status to be returned        */
	u8 val;
	u32 longval;

	if (Mode >= MT2063_NUM_RCVR_MODES)
		status = -ERANGE;

	/* RFAGCen */
	if (status >= 0) {
		val =
		    (state->
		     reg[MT2063_REG_PD1_TGT] & (u8) ~ 0x40) | (RFAGCEN[Mode]
								   ? 0x40 :
								   0x00);
		if (state->reg[MT2063_REG_PD1_TGT] != val) {
			status |= MT2063_SetReg(state, MT2063_REG_PD1_TGT, val);
		}
	}

	/* LNARin */
	if (status >= 0) {
		status |= MT2063_SetParam(state, MT2063_LNA_RIN, LNARIN[Mode]);
	}

	/* FIFFQEN and FIFFQ */
	if (status >= 0) {
		val =
		    (state->
		     reg[MT2063_REG_FIFF_CTRL2] & (u8) ~ 0xF0) |
		    (FIFFQEN[Mode] << 7) | (FIFFQ[Mode] << 4);
		if (state->reg[MT2063_REG_FIFF_CTRL2] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_FIFF_CTRL2, val);
			/* trigger FIFF calibration, needed after changing FIFFQ */
			val =
			    (state->reg[MT2063_REG_FIFF_CTRL] | (u8) 0x01);
			status |=
			    MT2063_SetReg(state, MT2063_REG_FIFF_CTRL, val);
			val =
			    (state->
			     reg[MT2063_REG_FIFF_CTRL] & (u8) ~ 0x01);
			status |=
			    MT2063_SetReg(state, MT2063_REG_FIFF_CTRL, val);
		}
	}

	/* DNC1GC & DNC2GC */
	status |= MT2063_GetParam(state, MT2063_DNC_OUTPUT_ENABLE, &longval);
	status |= MT2063_SetParam(state, MT2063_DNC_OUTPUT_ENABLE, longval);

	/* acLNAmax */
	if (status >= 0) {
		status |=
		    MT2063_SetParam(state, MT2063_ACLNA_MAX, ACLNAMAX[Mode]);
	}

	/* LNATGT */
	if (status >= 0) {
		status |= MT2063_SetParam(state, MT2063_LNA_TGT, LNATGT[Mode]);
	}

	/* ACRF */
	if (status >= 0) {
		status |=
		    MT2063_SetParam(state, MT2063_ACRF_MAX, ACRFMAX[Mode]);
	}

	/* PD1TGT */
	if (status >= 0) {
		status |= MT2063_SetParam(state, MT2063_PD1_TGT, PD1TGT[Mode]);
	}

	/* FIFATN */
	if (status >= 0) {
		status |=
		    MT2063_SetParam(state, MT2063_ACFIF_MAX, ACFIFMAX[Mode]);
	}

	/* PD2TGT */
	if (status >= 0) {
		status |= MT2063_SetParam(state, MT2063_PD2_TGT, PD2TGT[Mode]);
	}

	/* Ignore ATN Overload */
	if (status >= 0) {
		val =
		    (state->
		     reg[MT2063_REG_LNA_TGT] & (u8) ~ 0x80) | (RFOVDIS[Mode]
								   ? 0x80 :
								   0x00);
		if (state->reg[MT2063_REG_LNA_TGT] != val) {
			status |= MT2063_SetReg(state, MT2063_REG_LNA_TGT, val);
		}
	}

	/* Ignore FIF Overload */
	if (status >= 0) {
		val =
		    (state->
		     reg[MT2063_REG_PD1_TGT] & (u8) ~ 0x80) |
		    (FIFOVDIS[Mode] ? 0x80 : 0x00);
		if (state->reg[MT2063_REG_PD1_TGT] != val) {
			status |= MT2063_SetReg(state, MT2063_REG_PD1_TGT, val);
		}
	}

	if (status >= 0)
		state->rcvr_mode = Mode;

	return (status);
}

/******************************************************************************
**
**  Name: MT2063_ReInit
**
**  Description:    Initialize the tuner's register values.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_TUNER_ID_ERR   - Tuner Part/Rev code mismatch
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_COMM_ERR      - Serial bus communications error
**
**  Dependencies:   MT_ReadSub  - Read byte(s) of data from the two-wire bus
**                  MT_WriteSub - Write byte(s) of data to the two-wire bus
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**   148   09-04-2007    RSK    Ver 1.02: Corrected logic of Reg 3B Reference
**   153   09-07-2007    RSK    Ver 1.03: Lock Time improvements
**   N/A   10-31-2007    PINZ   Ver 1.08: Changed values suitable to rcvr-mode 0
**   N/A   11-12-2007    PINZ   Ver 1.09: Changed values suitable to rcvr-mode 0
**   N/A   01-03-2007    PINZ   Ver 1.10: Added AFCsd = 1 into defaults
**   N/A   01-04-2007    PINZ   Ver 1.10: Changed CAP1sel default
**         01-14-2008    PINZ   Ver 1.11: Updated gain settings
**         03-18-2008    PINZ   Ver 1.13: Added Support for B3
**   175 I 06-19-2008    RSK    Ver 1.17: Refactor DECT control to SpurAvoid.
**         06-24-2008    PINZ   Ver 1.18: Add Get/SetParam CTFILT_SW
**
******************************************************************************/
static u32 MT2063_ReInit(struct mt2063_state *state)
{
	u8 all_resets = 0xF0;	/* reset/load bits */
	u32 status = 0;	/* Status to be returned */
	u8 *def = NULL;
	u32 FCRUN;
	s32 maxReads;
	u32 fcu_osc;
	u32 i;
	u8 MT2063B0_defaults[] = {	/* Reg,  Value */
		0x19, 0x05,
		0x1B, 0x1D,
		0x1C, 0x1F,
		0x1D, 0x0F,
		0x1E, 0x3F,
		0x1F, 0x0F,
		0x20, 0x3F,
		0x22, 0x21,
		0x23, 0x3F,
		0x24, 0x20,
		0x25, 0x3F,
		0x27, 0xEE,
		0x2C, 0x27,	/*  bit at 0x20 is cleared below  */
		0x30, 0x03,
		0x2C, 0x07,	/*  bit at 0x20 is cleared here   */
		0x2D, 0x87,
		0x2E, 0xAA,
		0x28, 0xE1,	/*  Set the FIFCrst bit here      */
		0x28, 0xE0,	/*  Clear the FIFCrst bit here    */
		0x00
	};
	/* writing 0x05 0xf0 sw-resets all registers, so we write only needed changes */
	u8 MT2063B1_defaults[] = {	/* Reg,  Value */
		0x05, 0xF0,
		0x11, 0x10,	/* New Enable AFCsd */
		0x19, 0x05,
		0x1A, 0x6C,
		0x1B, 0x24,
		0x1C, 0x28,
		0x1D, 0x8F,
		0x1E, 0x14,
		0x1F, 0x8F,
		0x20, 0x57,
		0x22, 0x21,	/* New - ver 1.03 */
		0x23, 0x3C,	/* New - ver 1.10 */
		0x24, 0x20,	/* New - ver 1.03 */
		0x2C, 0x24,	/*  bit at 0x20 is cleared below  */
		0x2D, 0x87,	/*  FIFFQ=0  */
		0x2F, 0xF3,
		0x30, 0x0C,	/* New - ver 1.11 */
		0x31, 0x1B,	/* New - ver 1.11 */
		0x2C, 0x04,	/*  bit at 0x20 is cleared here  */
		0x28, 0xE1,	/*  Set the FIFCrst bit here      */
		0x28, 0xE0,	/*  Clear the FIFCrst bit here    */
		0x00
	};
	/* writing 0x05 0xf0 sw-resets all registers, so we write only needed changes */
	u8 MT2063B3_defaults[] = {	/* Reg,  Value */
		0x05, 0xF0,
		0x19, 0x3D,
		0x2C, 0x24,	/*  bit at 0x20 is cleared below  */
		0x2C, 0x04,	/*  bit at 0x20 is cleared here  */
		0x28, 0xE1,	/*  Set the FIFCrst bit here      */
		0x28, 0xE0,	/*  Clear the FIFCrst bit here    */
		0x00
	};

	/*  Read the Part/Rev code from the tuner */
	status = mt2063_read(state, MT2063_REG_PART_REV, state->reg, 1);
	if (status < 0)
		return status;

	/* Check the part/rev code */
	if (((state->reg[MT2063_REG_PART_REV] != MT2063_B0)	/*  MT2063 B0  */
	    &&(state->reg[MT2063_REG_PART_REV] != MT2063_B1)	/*  MT2063 B1  */
	    &&(state->reg[MT2063_REG_PART_REV] != MT2063_B3)))	/*  MT2063 B3  */
		return -ENODEV;	/*  Wrong tuner Part/Rev code */

	/*  Check the 2nd byte of the Part/Rev code from the tuner */
	status = mt2063_read(state,
			        MT2063_REG_RSVD_3B,
			        &state->reg[MT2063_REG_RSVD_3B], 1);

	if (status >= 0
	    &&((state->reg[MT2063_REG_RSVD_3B] & 0x80) != 0x00))	/* b7 != 0 ==> NOT MT2063 */
		return -ENODEV;	/*  Wrong tuner Part/Rev code */

	/*  Reset the tuner  */
	status = mt2063_write(state, MT2063_REG_LO2CQ_3, &all_resets, 1);
	if (status < 0)
		return status;

	/* change all of the default values that vary from the HW reset values */
	/*  def = (state->reg[PART_REV] == MT2063_B0) ? MT2063B0_defaults : MT2063B1_defaults; */
	switch (state->reg[MT2063_REG_PART_REV]) {
	case MT2063_B3:
		def = MT2063B3_defaults;
		break;

	case MT2063_B1:
		def = MT2063B1_defaults;
		break;

	case MT2063_B0:
		def = MT2063B0_defaults;
		break;

	default:
		return -ENODEV;
		break;
	}

	while (status >= 0 && *def) {
		u8 reg = *def++;
		u8 val = *def++;
		status = mt2063_write(state, reg, &val, 1);
	}
	if (status < 0)
		return status;

	/*  Wait for FIFF location to complete.  */
	FCRUN = 1;
	maxReads = 10;
	while (status >= 0 && (FCRUN != 0) && (maxReads-- > 0)) {
		msleep(2);
		status = mt2063_read(state,
					 MT2063_REG_XO_STATUS,
					 &state->
					 reg[MT2063_REG_XO_STATUS], 1);
		FCRUN = (state->reg[MT2063_REG_XO_STATUS] & 0x40) >> 6;
	}

	if (FCRUN != 0)
		return -ENODEV;

	status = mt2063_read(state,
			   MT2063_REG_FIFFC,
			   &state->reg[MT2063_REG_FIFFC], 1);
	if (status < 0)
		return status;

	/* Read back all the registers from the tuner */
	status = mt2063_read(state,
				MT2063_REG_PART_REV,
				state->reg, MT2063_REG_END_REGS);
	if (status < 0)
		return status;

	/*  Initialize the tuner state.  */
	state->tuner_id = state->reg[MT2063_REG_PART_REV];
	state->AS_Data.f_ref = MT2063_REF_FREQ;
	state->AS_Data.f_if1_Center = (state->AS_Data.f_ref / 8) *
				      ((u32) state->reg[MT2063_REG_FIFFC] + 640);
	state->AS_Data.f_if1_bw = MT2063_IF1_BW;
	state->AS_Data.f_out = 43750000UL;
	state->AS_Data.f_out_bw = 6750000UL;
	state->AS_Data.f_zif_bw = MT2063_ZIF_BW;
	state->AS_Data.f_LO1_Step = state->AS_Data.f_ref / 64;
	state->AS_Data.f_LO2_Step = MT2063_TUNE_STEP_SIZE;
	state->AS_Data.maxH1 = MT2063_MAX_HARMONICS_1;
	state->AS_Data.maxH2 = MT2063_MAX_HARMONICS_2;
	state->AS_Data.f_min_LO_Separation = MT2063_MIN_LO_SEP;
	state->AS_Data.f_if1_Request = state->AS_Data.f_if1_Center;
	state->AS_Data.f_LO1 = 2181000000UL;
	state->AS_Data.f_LO2 = 1486249786UL;
	state->f_IF1_actual = state->AS_Data.f_if1_Center;
	state->AS_Data.f_in = state->AS_Data.f_LO1 - state->f_IF1_actual;
	state->AS_Data.f_LO1_FracN_Avoid = MT2063_LO1_FRACN_AVOID;
	state->AS_Data.f_LO2_FracN_Avoid = MT2063_LO2_FRACN_AVOID;
	state->num_regs = MT2063_REG_END_REGS;
	state->AS_Data.avoidDECT = MT2063_AVOID_BOTH;
	state->ctfilt_sw = 0;

	state->CTFiltMax[0] = 69230000;
	state->CTFiltMax[1] = 105770000;
	state->CTFiltMax[2] = 140350000;
	state->CTFiltMax[3] = 177110000;
	state->CTFiltMax[4] = 212860000;
	state->CTFiltMax[5] = 241130000;
	state->CTFiltMax[6] = 274370000;
	state->CTFiltMax[7] = 309820000;
	state->CTFiltMax[8] = 342450000;
	state->CTFiltMax[9] = 378870000;
	state->CTFiltMax[10] = 416210000;
	state->CTFiltMax[11] = 456500000;
	state->CTFiltMax[12] = 495790000;
	state->CTFiltMax[13] = 534530000;
	state->CTFiltMax[14] = 572610000;
	state->CTFiltMax[15] = 598970000;
	state->CTFiltMax[16] = 635910000;
	state->CTFiltMax[17] = 672130000;
	state->CTFiltMax[18] = 714840000;
	state->CTFiltMax[19] = 739660000;
	state->CTFiltMax[20] = 770410000;
	state->CTFiltMax[21] = 814660000;
	state->CTFiltMax[22] = 846950000;
	state->CTFiltMax[23] = 867820000;
	state->CTFiltMax[24] = 915980000;
	state->CTFiltMax[25] = 947450000;
	state->CTFiltMax[26] = 983110000;
	state->CTFiltMax[27] = 1021630000;
	state->CTFiltMax[28] = 1061870000;
	state->CTFiltMax[29] = 1098330000;
	state->CTFiltMax[30] = 1138990000;

	/*
	 **   Fetch the FCU osc value and use it and the fRef value to
	 **   scale all of the Band Max values
	 */

	state->reg[MT2063_REG_CTUNE_CTRL] = 0x0A;
	status = mt2063_write(state,
				 MT2063_REG_CTUNE_CTRL,
				 &state->reg[MT2063_REG_CTUNE_CTRL], 1);
	if (status < 0)
		return status;
	/*  Read the ClearTune filter calibration value  */
	status = mt2063_read(state,
			        MT2063_REG_FIFFC,
			        &state->reg[MT2063_REG_FIFFC], 1);
	if (status < 0)
		return status;

	fcu_osc = state->reg[MT2063_REG_FIFFC];

	state->reg[MT2063_REG_CTUNE_CTRL] = 0x00;
	status = mt2063_write(state,
				 MT2063_REG_CTUNE_CTRL,
				 &state->reg[MT2063_REG_CTUNE_CTRL], 1);
	if (status < 0)
		return status;

	/*  Adjust each of the values in the ClearTune filter cross-over table  */
	for (i = 0; i < 31; i++)
		state->CTFiltMax[i] =(state->CTFiltMax[i] / 768) * (fcu_osc + 640);

	return (status);
}

/****************************************************************************
**
**  Name: MT2063_SetParam
**
**  Description:    Sets a tuning algorithm parameter.
**
**                  This function provides access to the internals of the
**                  tuning algorithm.  You can override many of the tuning
**                  algorithm defaults using this function.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  param       - Tuning algorithm parameter
**                                (see enum MT2063_Param)
**                  nValue      - value to be set
**
**                  param                     Description
**                  ----------------------    --------------------------------
**                  MT2063_SRO_FREQ           crystal frequency
**                  MT2063_STEPSIZE           minimum tuning step size
**                  MT2063_LO1_FREQ           LO1 frequency
**                  MT2063_LO1_STEPSIZE       LO1 minimum step size
**                  MT2063_LO1_FRACN_AVOID    LO1 FracN keep-out region
**                  MT2063_IF1_REQUEST        Requested 1st IF
**                  MT2063_ZIF_BW             zero-IF bandwidth
**                  MT2063_LO2_FREQ           LO2 frequency
**                  MT2063_LO2_STEPSIZE       LO2 minimum step size
**                  MT2063_LO2_FRACN_AVOID    LO2 FracN keep-out region
**                  MT2063_OUTPUT_FREQ        output center frequency
**                  MT2063_OUTPUT_BW          output bandwidth
**                  MT2063_LO_SEPARATION      min inter-tuner LO separation
**                  MT2063_MAX_HARM1          max # of intra-tuner harmonics
**                  MT2063_MAX_HARM2          max # of inter-tuner harmonics
**                  MT2063_RCVR_MODE          Predefined modes
**                  MT2063_LNA_RIN            Set LNA Rin (*)
**                  MT2063_LNA_TGT            Set target power level at LNA (*)
**                  MT2063_PD1_TGT            Set target power level at PD1 (*)
**                  MT2063_PD2_TGT            Set target power level at PD2 (*)
**                  MT2063_ACLNA_MAX          LNA attenuator limit (*)
**                  MT2063_ACRF_MAX           RF attenuator limit (*)
**                  MT2063_ACFIF_MAX          FIF attenuator limit (*)
**                  MT2063_DNC_OUTPUT_ENABLE  DNC output selection
**                  MT2063_VGAGC              VGA gain code
**                  MT2063_VGAOI              VGA output current
**                  MT2063_TAGC               TAGC setting
**                  MT2063_AMPGC              AMP gain code
**                  MT2063_AVOID_DECT         Avoid DECT Frequencies
**                  MT2063_CTFILT_SW          Cleartune filter selection
**
**                  (*) This parameter is set by MT2063_RCVR_MODE, do not call
**                      additionally.
**
**  Usage:          status |= MT2063_SetParam(hMT2063,
**                                            MT2063_STEPSIZE,
**                                            50000);
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Null pointer argument passed
**                      MT_ARG_RANGE     - Invalid parameter requested
**                                         or set value out of range
**                                         or non-writable parameter
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**  See Also:       MT2063_GetParam, MT2063_Open
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**   154   09-13-2007    RSK    Ver 1.05: Get/SetParam changes for LOx_FREQ
**         10-31-2007    PINZ   Ver 1.08: Get/SetParam add VGAGC, VGAOI, AMPGC, TAGC
**         04-18-2008    PINZ   Ver 1.15: Add SetParam LNARIN & PDxTGT
**                                        Split SetParam up to ACLNA / ACLNA_MAX
**                                        removed ACLNA_INRC/DECR (+RF & FIF)
**                                        removed GCUAUTO / BYPATNDN/UP
**   175 I 06-06-2008    PINZ   Ver 1.16: Add control to avoid US DECT freqs.
**   175 I 06-19-2008    RSK    Ver 1.17: Refactor DECT control to SpurAvoid.
**         06-24-2008    PINZ   Ver 1.18: Add Get/SetParam CTFILT_SW
**
****************************************************************************/
static u32 MT2063_SetParam(struct mt2063_state *state,
		           enum MT2063_Param param,
			   enum MT2063_DNC_Output_Enable nValue)
{
	u32 status = 0;	/* Status to be returned        */
	u8 val = 0;

	switch (param) {
		/*  crystal frequency                     */
	case MT2063_SRO_FREQ:
		state->AS_Data.f_ref = nValue;
		state->AS_Data.f_LO1_FracN_Avoid = 0;
		state->AS_Data.f_LO2_FracN_Avoid = nValue / 80 - 1;
		state->AS_Data.f_LO1_Step = nValue / 64;
		state->AS_Data.f_if1_Center =
		    (state->AS_Data.f_ref / 8) *
		    (state->reg[MT2063_REG_FIFFC] + 640);
		break;

		/*  minimum tuning step size              */
	case MT2063_STEPSIZE:
		state->AS_Data.f_LO2_Step = nValue;
		break;

		/*  LO1 frequency                         */
	case MT2063_LO1_FREQ:
		{
			/* Note: LO1 and LO2 are BOTH written at toggle of LDLOos  */
			/* Capture the Divider and Numerator portions of other LO  */
			u8 tempLO2CQ[3];
			u8 tempLO2C[3];
			u8 tmpOneShot;
			u32 Div, FracN;
			u8 restore = 0;

			/* Buffer the queue for restoration later and get actual LO2 values. */
			status |=
			    mt2063_read(state,
					   MT2063_REG_LO2CQ_1,
					   &(tempLO2CQ[0]), 3);
			status |=
			    mt2063_read(state,
					   MT2063_REG_LO2C_1,
					   &(tempLO2C[0]), 3);

			/* clear the one-shot bits */
			tempLO2CQ[2] = tempLO2CQ[2] & 0x0F;
			tempLO2C[2] = tempLO2C[2] & 0x0F;

			/* only write the queue values if they are different from the actual. */
			if ((tempLO2CQ[0] != tempLO2C[0]) ||
			    (tempLO2CQ[1] != tempLO2C[1]) ||
			    (tempLO2CQ[2] != tempLO2C[2])) {
				/* put actual LO2 value into queue (with 0 in one-shot bits) */
				status |=
				    mt2063_write(state,
						    MT2063_REG_LO2CQ_1,
						    &(tempLO2C[0]), 3);

				if (status == 0) {
					/* cache the bytes just written. */
					state->reg[MT2063_REG_LO2CQ_1] =
					    tempLO2C[0];
					state->reg[MT2063_REG_LO2CQ_2] =
					    tempLO2C[1];
					state->reg[MT2063_REG_LO2CQ_3] =
					    tempLO2C[2];
				}
				restore = 1;
			}

			/* Calculate the Divider and Numberator components of LO1 */
			status =
			    MT2063_CalcLO1Mult(&Div, &FracN, nValue,
					       state->AS_Data.f_ref /
					       64,
					       state->AS_Data.f_ref);
			state->reg[MT2063_REG_LO1CQ_1] =
			    (u8) (Div & 0x00FF);
			state->reg[MT2063_REG_LO1CQ_2] =
			    (u8) (FracN);
			status |=
			    mt2063_write(state,
					    MT2063_REG_LO1CQ_1,
					    &state->
					    reg[MT2063_REG_LO1CQ_1], 2);

			/* set the one-shot bit to load the pair of LO values */
			tmpOneShot = tempLO2CQ[2] | 0xE0;
			status |=
			    mt2063_write(state,
					    MT2063_REG_LO2CQ_3,
					    &tmpOneShot, 1);

			/* only restore the queue values if they were different from the actual. */
			if (restore) {
				/* put actual LO2 value into queue (0 in one-shot bits) */
				status |=
				    mt2063_write(state,
						    MT2063_REG_LO2CQ_1,
						    &(tempLO2CQ[0]), 3);

				/* cache the bytes just written. */
				state->reg[MT2063_REG_LO2CQ_1] =
				    tempLO2CQ[0];
				state->reg[MT2063_REG_LO2CQ_2] =
				    tempLO2CQ[1];
				state->reg[MT2063_REG_LO2CQ_3] =
				    tempLO2CQ[2];
			}

			MT2063_GetParam(state,
					MT2063_LO1_FREQ,
					&state->AS_Data.f_LO1);
		}
		break;

		/*  LO1 minimum step size                 */
	case MT2063_LO1_STEPSIZE:
		state->AS_Data.f_LO1_Step = nValue;
		break;

		/*  LO1 FracN keep-out region             */
	case MT2063_LO1_FRACN_AVOID_PARAM:
		state->AS_Data.f_LO1_FracN_Avoid = nValue;
		break;

		/*  Requested 1st IF                      */
	case MT2063_IF1_REQUEST:
		state->AS_Data.f_if1_Request = nValue;
		break;

		/*  zero-IF bandwidth                     */
	case MT2063_ZIF_BW:
		state->AS_Data.f_zif_bw = nValue;
		break;

		/*  LO2 frequency                         */
	case MT2063_LO2_FREQ:
		{
			/* Note: LO1 and LO2 are BOTH written at toggle of LDLOos  */
			/* Capture the Divider and Numerator portions of other LO  */
			u8 tempLO1CQ[2];
			u8 tempLO1C[2];
			u32 Div2;
			u32 FracN2;
			u8 tmpOneShot;
			u8 restore = 0;

			/* Buffer the queue for restoration later and get actual LO2 values. */
			status |=
			    mt2063_read(state,
					   MT2063_REG_LO1CQ_1,
					   &(tempLO1CQ[0]), 2);
			status |=
			    mt2063_read(state,
					   MT2063_REG_LO1C_1,
					   &(tempLO1C[0]), 2);

			/* only write the queue values if they are different from the actual. */
			if ((tempLO1CQ[0] != tempLO1C[0])
			    || (tempLO1CQ[1] != tempLO1C[1])) {
				/* put actual LO1 value into queue */
				status |=
				    mt2063_write(state,
						    MT2063_REG_LO1CQ_1,
						    &(tempLO1C[0]), 2);

				/* cache the bytes just written. */
				state->reg[MT2063_REG_LO1CQ_1] =
				    tempLO1C[0];
				state->reg[MT2063_REG_LO1CQ_2] =
				    tempLO1C[1];
				restore = 1;
			}

			/* Calculate the Divider and Numberator components of LO2 */
			status =
			    MT2063_CalcLO2Mult(&Div2, &FracN2, nValue,
					       state->AS_Data.f_ref /
					       8191,
					       state->AS_Data.f_ref);
			state->reg[MT2063_REG_LO2CQ_1] =
			    (u8) ((Div2 << 1) |
				      ((FracN2 >> 12) & 0x01)) & 0xFF;
			state->reg[MT2063_REG_LO2CQ_2] =
			    (u8) ((FracN2 >> 4) & 0xFF);
			state->reg[MT2063_REG_LO2CQ_3] =
			    (u8) ((FracN2 & 0x0F));
			status |=
			    mt2063_write(state,
					    MT2063_REG_LO1CQ_1,
					    &state->
					    reg[MT2063_REG_LO1CQ_1], 3);

			/* set the one-shot bit to load the LO values */
			tmpOneShot =
			    state->reg[MT2063_REG_LO2CQ_3] | 0xE0;
			status |=
			    mt2063_write(state,
					    MT2063_REG_LO2CQ_3,
					    &tmpOneShot, 1);

			/* only restore LO1 queue value if they were different from the actual. */
			if (restore) {
				/* put previous LO1 queue value back into queue */
				status |=
				    mt2063_write(state,
						    MT2063_REG_LO1CQ_1,
						    &(tempLO1CQ[0]), 2);

				/* cache the bytes just written. */
				state->reg[MT2063_REG_LO1CQ_1] =
				    tempLO1CQ[0];
				state->reg[MT2063_REG_LO1CQ_2] =
				    tempLO1CQ[1];
			}

			MT2063_GetParam(state,
					MT2063_LO2_FREQ,
					&state->AS_Data.f_LO2);
		}
		break;

		/*  LO2 minimum step size                 */
	case MT2063_LO2_STEPSIZE:
		state->AS_Data.f_LO2_Step = nValue;
		break;

		/*  LO2 FracN keep-out region             */
	case MT2063_LO2_FRACN_AVOID:
		state->AS_Data.f_LO2_FracN_Avoid = nValue;
		break;

		/*  output center frequency               */
	case MT2063_OUTPUT_FREQ:
		state->AS_Data.f_out = nValue;
		break;

		/*  output bandwidth                      */
	case MT2063_OUTPUT_BW:
		state->AS_Data.f_out_bw = nValue + 750000;
		break;

		/*  min inter-tuner LO separation         */
	case MT2063_LO_SEPARATION:
		state->AS_Data.f_min_LO_Separation = nValue;
		break;

		/*  max # of intra-tuner harmonics        */
	case MT2063_MAX_HARM1:
		state->AS_Data.maxH1 = nValue;
		break;

		/*  max # of inter-tuner harmonics        */
	case MT2063_MAX_HARM2:
		state->AS_Data.maxH2 = nValue;
		break;

	case MT2063_RCVR_MODE:
		status |=
		    MT2063_SetReceiverMode(state,
					   (enum MT2063_RCVR_MODES)
					   nValue);
		break;

		/* Set LNA Rin -- nValue is desired value */
	case MT2063_LNA_RIN:
		val =
		    (state->
		     reg[MT2063_REG_CTRL_2C] & (u8) ~ 0x03) |
		    (nValue & 0x03);
		if (state->reg[MT2063_REG_CTRL_2C] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_CTRL_2C,
					  val);
		}
		break;

		/* Set target power level at LNA -- nValue is desired value */
	case MT2063_LNA_TGT:
		val =
		    (state->
		     reg[MT2063_REG_LNA_TGT] & (u8) ~ 0x3F) |
		    (nValue & 0x3F);
		if (state->reg[MT2063_REG_LNA_TGT] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_LNA_TGT,
					  val);
		}
		break;

		/* Set target power level at PD1 -- nValue is desired value */
	case MT2063_PD1_TGT:
		val =
		    (state->
		     reg[MT2063_REG_PD1_TGT] & (u8) ~ 0x3F) |
		    (nValue & 0x3F);
		if (state->reg[MT2063_REG_PD1_TGT] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_PD1_TGT,
					  val);
		}
		break;

		/* Set target power level at PD2 -- nValue is desired value */
	case MT2063_PD2_TGT:
		val =
		    (state->
		     reg[MT2063_REG_PD2_TGT] & (u8) ~ 0x3F) |
		    (nValue & 0x3F);
		if (state->reg[MT2063_REG_PD2_TGT] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_PD2_TGT,
					  val);
		}
		break;

		/* Set LNA atten limit -- nValue is desired value */
	case MT2063_ACLNA_MAX:
		val =
		    (state->
		     reg[MT2063_REG_LNA_OV] & (u8) ~ 0x1F) | (nValue
								  &
								  0x1F);
		if (state->reg[MT2063_REG_LNA_OV] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_LNA_OV,
					  val);
		}
		break;

		/* Set RF atten limit -- nValue is desired value */
	case MT2063_ACRF_MAX:
		val =
		    (state->
		     reg[MT2063_REG_RF_OV] & (u8) ~ 0x1F) | (nValue
								 &
								 0x1F);
		if (state->reg[MT2063_REG_RF_OV] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_RF_OV, val);
		}
		break;

		/* Set FIF atten limit -- nValue is desired value, max. 5 if no B3 */
	case MT2063_ACFIF_MAX:
		if (state->reg[MT2063_REG_PART_REV] != MT2063_B3
		    && nValue > 5)
			nValue = 5;
		val =
		    (state->
		     reg[MT2063_REG_FIF_OV] & (u8) ~ 0x1F) | (nValue
								  &
								  0x1F);
		if (state->reg[MT2063_REG_FIF_OV] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_FIF_OV,
					  val);
		}
		break;

	case MT2063_DNC_OUTPUT_ENABLE:
		/* selects, which DNC output is used */
		switch (nValue) {
		case MT2063_DNC_NONE:
			{
				val = (state->reg[MT2063_REG_DNC_GAIN] & 0xFC) | 0x03;	/* Set DNC1GC=3 */
				if (state->reg[MT2063_REG_DNC_GAIN] !=
				    val)
					status |=
					    MT2063_SetReg(state,
							  MT2063_REG_DNC_GAIN,
							  val);

				val = (state->reg[MT2063_REG_VGA_GAIN] & 0xFC) | 0x03;	/* Set DNC2GC=3 */
				if (state->reg[MT2063_REG_VGA_GAIN] !=
				    val)
					status |=
					    MT2063_SetReg(state,
							  MT2063_REG_VGA_GAIN,
							  val);

				val = (state->reg[MT2063_REG_RSVD_20] & ~0x40);	/* Set PD2MUX=0 */
				if (state->reg[MT2063_REG_RSVD_20] !=
				    val)
					status |=
					    MT2063_SetReg(state,
							  MT2063_REG_RSVD_20,
							  val);

				break;
			}
		case MT2063_DNC_1:
			{
				val = (state->reg[MT2063_REG_DNC_GAIN] & 0xFC) | (DNC1GC[state->rcvr_mode] & 0x03);	/* Set DNC1GC=x */
				if (state->reg[MT2063_REG_DNC_GAIN] !=
				    val)
					status |=
					    MT2063_SetReg(state,
							  MT2063_REG_DNC_GAIN,
							  val);

				val = (state->reg[MT2063_REG_VGA_GAIN] & 0xFC) | 0x03;	/* Set DNC2GC=3 */
				if (state->reg[MT2063_REG_VGA_GAIN] !=
				    val)
					status |=
					    MT2063_SetReg(state,
							  MT2063_REG_VGA_GAIN,
							  val);

				val = (state->reg[MT2063_REG_RSVD_20] & ~0x40);	/* Set PD2MUX=0 */
				if (state->reg[MT2063_REG_RSVD_20] !=
				    val)
					status |=
					    MT2063_SetReg(state,
							  MT2063_REG_RSVD_20,
							  val);

				break;
			}
		case MT2063_DNC_2:
			{
				val = (state->reg[MT2063_REG_DNC_GAIN] & 0xFC) | 0x03;	/* Set DNC1GC=3 */
				if (state->reg[MT2063_REG_DNC_GAIN] !=
				    val)
					status |=
					    MT2063_SetReg(state,
							  MT2063_REG_DNC_GAIN,
							  val);

				val = (state->reg[MT2063_REG_VGA_GAIN] & 0xFC) | (DNC2GC[state->rcvr_mode] & 0x03);	/* Set DNC2GC=x */
				if (state->reg[MT2063_REG_VGA_GAIN] !=
				    val)
					status |=
					    MT2063_SetReg(state,
							  MT2063_REG_VGA_GAIN,
							  val);

				val = (state->reg[MT2063_REG_RSVD_20] | 0x40);	/* Set PD2MUX=1 */
				if (state->reg[MT2063_REG_RSVD_20] !=
				    val)
					status |=
					    MT2063_SetReg(state,
							  MT2063_REG_RSVD_20,
							  val);

				break;
			}
		case MT2063_DNC_BOTH:
			{
				val = (state->reg[MT2063_REG_DNC_GAIN] & 0xFC) | (DNC1GC[state->rcvr_mode] & 0x03);	/* Set DNC1GC=x */
				if (state->reg[MT2063_REG_DNC_GAIN] !=
				    val)
					status |=
					    MT2063_SetReg(state,
							  MT2063_REG_DNC_GAIN,
							  val);

				val = (state->reg[MT2063_REG_VGA_GAIN] & 0xFC) | (DNC2GC[state->rcvr_mode] & 0x03);	/* Set DNC2GC=x */
				if (state->reg[MT2063_REG_VGA_GAIN] !=
				    val)
					status |=
					    MT2063_SetReg(state,
							  MT2063_REG_VGA_GAIN,
							  val);

				val = (state->reg[MT2063_REG_RSVD_20] | 0x40);	/* Set PD2MUX=1 */
				if (state->reg[MT2063_REG_RSVD_20] !=
				    val)
					status |=
					    MT2063_SetReg(state,
							  MT2063_REG_RSVD_20,
							  val);

				break;
			}
		default:
			break;
		}
		break;

	case MT2063_VGAGC:
		/* Set VGA gain code */
		val =
		    (state->
		     reg[MT2063_REG_VGA_GAIN] & (u8) ~ 0x0C) |
		    ((nValue & 0x03) << 2);
		if (state->reg[MT2063_REG_VGA_GAIN] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_VGA_GAIN,
					  val);
		}
		break;

	case MT2063_VGAOI:
		/* Set VGA bias current */
		val =
		    (state->
		     reg[MT2063_REG_RSVD_31] & (u8) ~ 0x07) |
		    (nValue & 0x07);
		if (state->reg[MT2063_REG_RSVD_31] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_RSVD_31,
					  val);
		}
		break;

	case MT2063_TAGC:
		/* Set TAGC */
		val =
		    (state->
		     reg[MT2063_REG_RSVD_1E] & (u8) ~ 0x03) |
		    (nValue & 0x03);
		if (state->reg[MT2063_REG_RSVD_1E] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_RSVD_1E,
					  val);
		}
		break;

	case MT2063_AMPGC:
		/* Set Amp gain code */
		val =
		    (state->
		     reg[MT2063_REG_TEMP_SEL] & (u8) ~ 0x03) |
		    (nValue & 0x03);
		if (state->reg[MT2063_REG_TEMP_SEL] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_TEMP_SEL,
					  val);
		}
		break;

		/*  Avoid DECT Frequencies                */
	case MT2063_AVOID_DECT:
		{
			enum MT2063_DECT_Avoid_Type newAvoidSetting =
			    (enum MT2063_DECT_Avoid_Type)nValue;
			if ((newAvoidSetting >=
			     MT2063_NO_DECT_AVOIDANCE)
			    && (newAvoidSetting <= MT2063_AVOID_BOTH)) {
				state->AS_Data.avoidDECT =
				    newAvoidSetting;
			}
		}
		break;

		/*  Cleartune filter selection: 0 - by IC (default), 1 - by software  */
	case MT2063_CTFILT_SW:
		state->ctfilt_sw = (nValue & 0x01);
		break;

		/*  These parameters are read-only  */
	case MT2063_IC_ADDR:
	case MT2063_MAX_OPEN:
	case MT2063_NUM_OPEN:
	case MT2063_INPUT_FREQ:
	case MT2063_IF1_ACTUAL:
	case MT2063_IF1_CENTER:
	case MT2063_IF1_BW:
	case MT2063_AS_ALG:
	case MT2063_EXCL_ZONES:
	case MT2063_SPUR_AVOIDED:
	case MT2063_NUM_SPURS:
	case MT2063_SPUR_PRESENT:
	case MT2063_ACLNA:
	case MT2063_ACRF:
	case MT2063_ACFIF:
	case MT2063_EOP:
	default:
		status |= -ERANGE;
	}
	return (status);
}

/****************************************************************************
**
**  Name: MT2063_ClearPowerMaskBits
**
**  Description:    Clears the power-down mask bits for various sections of
**                  the MT2063
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  Bits        - Mask bits to be cleared.
**
**                  See definition of MT2063_Mask_Bits type for description
**                  of each of the power bits.
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_COMM_ERR      - Serial bus communications error
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
static u32 MT2063_ClearPowerMaskBits(struct mt2063_state *state, enum MT2063_Mask_Bits Bits)
{
	u32 status = 0;	/* Status to be returned        */

	Bits = (enum MT2063_Mask_Bits)(Bits & MT2063_ALL_SD);	/* Only valid bits for this tuner */
	if ((Bits & 0xFF00) != 0) {
		state->reg[MT2063_REG_PWR_2] &= ~(u8) (Bits >> 8);
		status |=
		    mt2063_write(state,
				    MT2063_REG_PWR_2,
				    &state->reg[MT2063_REG_PWR_2], 1);
	}
	if ((Bits & 0xFF) != 0) {
		state->reg[MT2063_REG_PWR_1] &= ~(u8) (Bits & 0xFF);
		status |=
		    mt2063_write(state,
				    MT2063_REG_PWR_1,
				    &state->reg[MT2063_REG_PWR_1], 1);
	}

	return (status);
}

/****************************************************************************
**
**  Name: MT2063_SoftwareShutdown
**
**  Description:    Enables or disables software shutdown function.  When
**                  Shutdown==1, any section whose power mask is set will be
**                  shutdown.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  Shutdown    - 1 = shutdown the masked sections, otherwise
**                                power all sections on
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_COMM_ERR      - Serial bus communications error
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**         01-03-2008    PINZ   Ver 1.xx: Added a trigger of BYPATNUP for
**                              correct wakeup of the LNA
**
****************************************************************************/
static u32 MT2063_SoftwareShutdown(struct mt2063_state *state, u8 Shutdown)
{
	u32 status = 0;	/* Status to be returned        */

	if (Shutdown == 1)
		state->reg[MT2063_REG_PWR_1] |= 0x04;	/* Turn the bit on */
	else
		state->reg[MT2063_REG_PWR_1] &= ~0x04;	/* Turn off the bit */

	status |=
	    mt2063_write(state,
			    MT2063_REG_PWR_1,
			    &state->reg[MT2063_REG_PWR_1], 1);

	if (Shutdown != 1) {
		state->reg[MT2063_REG_BYP_CTRL] =
		    (state->reg[MT2063_REG_BYP_CTRL] & 0x9F) | 0x40;
		status |=
		    mt2063_write(state,
				    MT2063_REG_BYP_CTRL,
				    &state->reg[MT2063_REG_BYP_CTRL],
				    1);
		state->reg[MT2063_REG_BYP_CTRL] =
		    (state->reg[MT2063_REG_BYP_CTRL] & 0x9F);
		status |=
		    mt2063_write(state,
				    MT2063_REG_BYP_CTRL,
				    &state->reg[MT2063_REG_BYP_CTRL],
				    1);
	}

	return (status);
}

/****************************************************************************
**
**  Name: MT2063_SetReg
**
**  Description:    Sets an MT2063 register.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  reg         - MT2063 register/subaddress location
**                  val         - MT2063 register/subaddress value
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_RANGE     - Argument out of range
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**                  Use this function if you need to override a default
**                  register value
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
static u32 MT2063_SetReg(struct mt2063_state *state, u8 reg, u8 val)
{
	u32 status = 0;	/* Status to be returned        */

	if (reg >= MT2063_REG_END_REGS)
		status |= -ERANGE;

	status = mt2063_write(state, reg, &val,
			         1);
	if (status >= 0)
		state->reg[reg] = val;

	return (status);
}

static u32 MT2063_Round_fLO(u32 f_LO, u32 f_LO_Step, u32 f_ref)
{
	return f_ref * (f_LO / f_ref)
	    + f_LO_Step * (((f_LO % f_ref) + (f_LO_Step / 2)) / f_LO_Step);
}

/****************************************************************************
**
**  Name: fLO_FractionalTerm
**
**  Description:    Calculates the portion contributed by FracN / denom.
**
**                  This function preserves maximum precision without
**                  risk of overflow.  It accurately calculates
**                  f_ref * num / denom to within 1 HZ with fixed math.
**
**  Parameters:     num       - Fractional portion of the multiplier
**                  denom     - denominator portion of the ratio
**                              This routine successfully handles denom values
**                              up to and including 2^18.
**                  f_Ref     - SRO frequency.  This calculation handles
**                              f_ref as two separate 14-bit fields.
**                              Therefore, a maximum value of 2^28-1
**                              may safely be used for f_ref.  This is
**                              the genesis of the magic number "14" and the
**                              magic mask value of 0x03FFF.
**
**  Returns:        f_ref * num / denom
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
static u32 MT2063_fLO_FractionalTerm(u32 f_ref,
					 u32 num, u32 denom)
{
	u32 t1 = (f_ref >> 14) * num;
	u32 term1 = t1 / denom;
	u32 loss = t1 % denom;
	u32 term2 =
	    (((f_ref & 0x00003FFF) * num + (loss << 14)) + (denom / 2)) / denom;
	return ((term1 << 14) + term2);
}

/****************************************************************************
**
**  Name: CalcLO1Mult
**
**  Description:    Calculates Integer divider value and the numerator
**                  value for a FracN PLL.
**
**                  This function assumes that the f_LO and f_Ref are
**                  evenly divisible by f_LO_Step.
**
**  Parameters:     Div       - OUTPUT: Whole number portion of the multiplier
**                  FracN     - OUTPUT: Fractional portion of the multiplier
**                  f_LO      - desired LO frequency.
**                  f_LO_Step - Minimum step size for the LO (in Hz).
**                  f_Ref     - SRO frequency.
**                  f_Avoid   - Range of PLL frequencies to avoid near
**                              integer multiples of f_Ref (in Hz).
**
**  Returns:        Recalculated LO frequency.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
static u32 MT2063_CalcLO1Mult(u32 * Div,
				  u32 * FracN,
				  u32 f_LO,
				  u32 f_LO_Step, u32 f_Ref)
{
	/*  Calculate the whole number portion of the divider */
	*Div = f_LO / f_Ref;

	/*  Calculate the numerator value (round to nearest f_LO_Step) */
	*FracN =
	    (64 * (((f_LO % f_Ref) + (f_LO_Step / 2)) / f_LO_Step) +
	     (f_Ref / f_LO_Step / 2)) / (f_Ref / f_LO_Step);

	return (f_Ref * (*Div)) + MT2063_fLO_FractionalTerm(f_Ref, *FracN, 64);
}

/****************************************************************************
**
**  Name: CalcLO2Mult
**
**  Description:    Calculates Integer divider value and the numerator
**                  value for a FracN PLL.
**
**                  This function assumes that the f_LO and f_Ref are
**                  evenly divisible by f_LO_Step.
**
**  Parameters:     Div       - OUTPUT: Whole number portion of the multiplier
**                  FracN     - OUTPUT: Fractional portion of the multiplier
**                  f_LO      - desired LO frequency.
**                  f_LO_Step - Minimum step size for the LO (in Hz).
**                  f_Ref     - SRO frequency.
**                  f_Avoid   - Range of PLL frequencies to avoid near
**                              integer multiples of f_Ref (in Hz).
**
**  Returns:        Recalculated LO frequency.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
static u32 MT2063_CalcLO2Mult(u32 * Div,
				  u32 * FracN,
				  u32 f_LO,
				  u32 f_LO_Step, u32 f_Ref)
{
	/*  Calculate the whole number portion of the divider */
	*Div = f_LO / f_Ref;

	/*  Calculate the numerator value (round to nearest f_LO_Step) */
	*FracN =
	    (8191 * (((f_LO % f_Ref) + (f_LO_Step / 2)) / f_LO_Step) +
	     (f_Ref / f_LO_Step / 2)) / (f_Ref / f_LO_Step);

	return (f_Ref * (*Div)) + MT2063_fLO_FractionalTerm(f_Ref, *FracN,
							    8191);
}

/****************************************************************************
**
**  Name: FindClearTuneFilter
**
**  Description:    Calculate the corrrect ClearTune filter to be used for
**                  a given input frequency.
**
**  Parameters:     state       - ptr to tuner data structure
**                  f_in        - RF input center frequency (in Hz).
**
**  Returns:        ClearTune filter number (0-31)
**
**  Dependencies:   MUST CALL MT2064_Open BEFORE FindClearTuneFilter!
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**         04-10-2008   PINZ    Ver 1.14: Use software-controlled ClearTune
**                                        cross-over frequency values.
**
****************************************************************************/
static u32 FindClearTuneFilter(struct mt2063_state *state, u32 f_in)
{
	u32 RFBand;
	u32 idx;		/*  index loop                      */

	/*
	 **  Find RF Band setting
	 */
	RFBand = 31;		/*  def when f_in > all    */
	for (idx = 0; idx < 31; ++idx) {
		if (state->CTFiltMax[idx] >= f_in) {
			RFBand = idx;
			break;
		}
	}
	return (RFBand);
}

/****************************************************************************
**
**  Name: MT2063_Tune
**
**  Description:    Change the tuner's tuned frequency to RFin.
**
**  Parameters:     h           - Open handle to the tuner (from MT2063_Open).
**                  f_in        - RF input center frequency (in Hz).
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_UPC_UNLOCK    - Upconverter PLL unlocked
**                      MT_DNC_UNLOCK    - Downconverter PLL unlocked
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_SPUR_CNT_MASK - Count of avoided LO spurs
**                      MT_SPUR_PRESENT  - LO spur possible in output
**                      MT_FIN_RANGE     - Input freq out of range
**                      MT_FOUT_RANGE    - Output freq out of range
**                      MT_UPC_RANGE     - Upconverter freq out of range
**                      MT_DNC_RANGE     - Downconverter freq out of range
**
**  Dependencies:   MUST CALL MT2063_Open BEFORE MT2063_Tune!
**
**                  MT_ReadSub       - Read data from the two-wire serial bus
**                  MT_WriteSub      - Write data to the two-wire serial bus
**                  MT_Sleep         - Delay execution for x milliseconds
**                  MT2063_GetLocked - Checks to see if LO1 and LO2 are locked
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**         04-10-2008   PINZ    Ver 1.05: Use software-controlled ClearTune
**                                        cross-over frequency values.
**   175 I 16-06-2008   PINZ    Ver 1.16: Add control to avoid US DECT freqs.
**   175 I 06-19-2008    RSK    Ver 1.17: Refactor DECT control to SpurAvoid.
**         06-24-2008    PINZ   Ver 1.18: Add Get/SetParam CTFILT_SW
**
****************************************************************************/
static u32 MT2063_Tune(struct mt2063_state *state, u32 f_in)
{				/* RF input center frequency   */

	u32 status = 0;	/*  status of operation             */
	u32 LO1;		/*  1st LO register value           */
	u32 Num1;		/*  Numerator for LO1 reg. value    */
	u32 f_IF1;		/*  1st IF requested                */
	u32 LO2;		/*  2nd LO register value           */
	u32 Num2;		/*  Numerator for LO2 reg. value    */
	u32 ofLO1, ofLO2;	/*  last time's LO frequencies      */
	u32 ofin, ofout;	/*  last time's I/O frequencies     */
	u8 fiffc = 0x80;	/*  FIFF center freq from tuner     */
	u32 fiffof;		/*  Offset from FIFF center freq    */
	const u8 LO1LK = 0x80;	/*  Mask for LO1 Lock bit           */
	u8 LO2LK = 0x08;	/*  Mask for LO2 Lock bit           */
	u8 val;
	u32 RFBand;

	/*  Check the input and output frequency ranges                   */
	if ((f_in < MT2063_MIN_FIN_FREQ) || (f_in > MT2063_MAX_FIN_FREQ))
		return -EINVAL;

	if ((state->AS_Data.f_out < MT2063_MIN_FOUT_FREQ)
	    || (state->AS_Data.f_out > MT2063_MAX_FOUT_FREQ))
		return -EINVAL;

	/*
	 **  Save original LO1 and LO2 register values
	 */
	ofLO1 = state->AS_Data.f_LO1;
	ofLO2 = state->AS_Data.f_LO2;
	ofin = state->AS_Data.f_in;
	ofout = state->AS_Data.f_out;

	/*
	 **  Find and set RF Band setting
	 */
	if (state->ctfilt_sw == 1) {
		val = (state->reg[MT2063_REG_CTUNE_CTRL] | 0x08);
		if (state->reg[MT2063_REG_CTUNE_CTRL] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_CTUNE_CTRL, val);
		}
		val = state->reg[MT2063_REG_CTUNE_OV];
		RFBand = FindClearTuneFilter(state, f_in);
		state->reg[MT2063_REG_CTUNE_OV] =
		    (u8) ((state->reg[MT2063_REG_CTUNE_OV] & ~0x1F)
			      | RFBand);
		if (state->reg[MT2063_REG_CTUNE_OV] != val) {
			status |=
			    MT2063_SetReg(state, MT2063_REG_CTUNE_OV, val);
		}
	}

	/*
	 **  Read the FIFF Center Frequency from the tuner
	 */
	if (status >= 0) {
		status |=
		    mt2063_read(state,
				   MT2063_REG_FIFFC,
				   &state->reg[MT2063_REG_FIFFC], 1);
		fiffc = state->reg[MT2063_REG_FIFFC];
	}
	/*
	 **  Assign in the requested values
	 */
	state->AS_Data.f_in = f_in;
	/*  Request a 1st IF such that LO1 is on a step size */
	state->AS_Data.f_if1_Request =
	    MT2063_Round_fLO(state->AS_Data.f_if1_Request + f_in,
			     state->AS_Data.f_LO1_Step,
			     state->AS_Data.f_ref) - f_in;

	/*
	 **  Calculate frequency settings.  f_IF1_FREQ + f_in is the
	 **  desired LO1 frequency
	 */
	MT2063_ResetExclZones(&state->AS_Data);

	f_IF1 = MT2063_ChooseFirstIF(&state->AS_Data);

	state->AS_Data.f_LO1 =
	    MT2063_Round_fLO(f_IF1 + f_in, state->AS_Data.f_LO1_Step,
			     state->AS_Data.f_ref);

	state->AS_Data.f_LO2 =
	    MT2063_Round_fLO(state->AS_Data.f_LO1 - state->AS_Data.f_out - f_in,
			     state->AS_Data.f_LO2_Step, state->AS_Data.f_ref);

	/*
	 ** Check for any LO spurs in the output bandwidth and adjust
	 ** the LO settings to avoid them if needed
	 */
	status |= MT2063_AvoidSpurs(state, &state->AS_Data);
	/*
	 ** MT_AvoidSpurs spurs may have changed the LO1 & LO2 values.
	 ** Recalculate the LO frequencies and the values to be placed
	 ** in the tuning registers.
	 */
	state->AS_Data.f_LO1 =
	    MT2063_CalcLO1Mult(&LO1, &Num1, state->AS_Data.f_LO1,
			       state->AS_Data.f_LO1_Step, state->AS_Data.f_ref);
	state->AS_Data.f_LO2 =
	    MT2063_Round_fLO(state->AS_Data.f_LO1 - state->AS_Data.f_out - f_in,
			     state->AS_Data.f_LO2_Step, state->AS_Data.f_ref);
	state->AS_Data.f_LO2 =
	    MT2063_CalcLO2Mult(&LO2, &Num2, state->AS_Data.f_LO2,
			       state->AS_Data.f_LO2_Step, state->AS_Data.f_ref);

	/*
	 **  Check the upconverter and downconverter frequency ranges
	 */
	if ((state->AS_Data.f_LO1 < MT2063_MIN_UPC_FREQ)
	    || (state->AS_Data.f_LO1 > MT2063_MAX_UPC_FREQ))
		status |= MT2063_UPC_RANGE;
	if ((state->AS_Data.f_LO2 < MT2063_MIN_DNC_FREQ)
	    || (state->AS_Data.f_LO2 > MT2063_MAX_DNC_FREQ))
		status |= MT2063_DNC_RANGE;
	/*  LO2 Lock bit was in a different place for B0 version  */
	if (state->tuner_id == MT2063_B0)
		LO2LK = 0x40;

	/*
	 **  If we have the same LO frequencies and we're already locked,
	 **  then skip re-programming the LO registers.
	 */
	if ((ofLO1 != state->AS_Data.f_LO1)
	    || (ofLO2 != state->AS_Data.f_LO2)
	    || ((state->reg[MT2063_REG_LO_STATUS] & (LO1LK | LO2LK)) !=
		(LO1LK | LO2LK))) {
		/*
		 **  Calculate the FIFFOF register value
		 **
		 **            IF1_Actual
		 **  FIFFOF = ------------ - 8 * FIFFC - 4992
		 **             f_ref/64
		 */
		fiffof =
		    (state->AS_Data.f_LO1 -
		     f_in) / (state->AS_Data.f_ref / 64) - 8 * (u32) fiffc -
		    4992;
		if (fiffof > 0xFF)
			fiffof = 0xFF;

		/*
		 **  Place all of the calculated values into the local tuner
		 **  register fields.
		 */
		if (status >= 0) {
			state->reg[MT2063_REG_LO1CQ_1] = (u8) (LO1 & 0xFF);	/* DIV1q */
			state->reg[MT2063_REG_LO1CQ_2] = (u8) (Num1 & 0x3F);	/* NUM1q */
			state->reg[MT2063_REG_LO2CQ_1] = (u8) (((LO2 & 0x7F) << 1)	/* DIV2q */
								   |(Num2 >> 12));	/* NUM2q (hi) */
			state->reg[MT2063_REG_LO2CQ_2] = (u8) ((Num2 & 0x0FF0) >> 4);	/* NUM2q (mid) */
			state->reg[MT2063_REG_LO2CQ_3] = (u8) (0xE0 | (Num2 & 0x000F));	/* NUM2q (lo) */

			/*
			 ** Now write out the computed register values
			 ** IMPORTANT: There is a required order for writing
			 **            (0x05 must follow all the others).
			 */
			status |= mt2063_write(state, MT2063_REG_LO1CQ_1, &state->reg[MT2063_REG_LO1CQ_1], 5);	/* 0x01 - 0x05 */
			if (state->tuner_id == MT2063_B0) {
				/* Re-write the one-shot bits to trigger the tune operation */
				status |= mt2063_write(state, MT2063_REG_LO2CQ_3, &state->reg[MT2063_REG_LO2CQ_3], 1);	/* 0x05 */
			}
			/* Write out the FIFF offset only if it's changing */
			if (state->reg[MT2063_REG_FIFF_OFFSET] !=
			    (u8) fiffof) {
				state->reg[MT2063_REG_FIFF_OFFSET] =
				    (u8) fiffof;
				status |=
				    mt2063_write(state,
						    MT2063_REG_FIFF_OFFSET,
						    &state->
						    reg[MT2063_REG_FIFF_OFFSET],
						    1);
			}
		}

		/*
		 **  Check for LO's locking
		 */

		if (status >= 0) {
			status |= MT2063_GetLocked(state);
		}
		/*
		 **  If we locked OK, assign calculated data to mt2063_state structure
		 */
		if (status >= 0) {
			state->f_IF1_actual = state->AS_Data.f_LO1 - f_in;
		}
	}

	return (status);
}

static u32 MT_Tune_atv(void *h, u32 f_in, u32 bw_in,
		    enum MTTune_atv_standard tv_type)
{

	u32 status = 0;

	s32 pict_car = 0;
	s32 pict2chanb_vsb = 0;
	s32 pict2chanb_snd = 0;
	s32 pict2snd1 = 0;
	s32 pict2snd2 = 0;
	s32 ch_bw = 0;

	s32 if_mid = 0;
	s32 rcvr_mode = 0;
	u32 mode_get = 0;

	switch (tv_type) {
	case MTTUNEA_PAL_B:{
			pict_car = 38900000;
			ch_bw = 8000000;
			pict2chanb_vsb = -1250000;
			pict2snd1 = 5500000;
			pict2snd2 = 5742000;
			rcvr_mode = 1;
			break;
		}
	case MTTUNEA_PAL_G:{
			pict_car = 38900000;
			ch_bw = 7000000;
			pict2chanb_vsb = -1250000;
			pict2snd1 = 5500000;
			pict2snd2 = 0;
			rcvr_mode = 1;
			break;
		}
	case MTTUNEA_PAL_I:{
			pict_car = 38900000;
			ch_bw = 8000000;
			pict2chanb_vsb = -1250000;
			pict2snd1 = 6000000;
			pict2snd2 = 0;
			rcvr_mode = 1;
			break;
		}
	case MTTUNEA_PAL_L:{
			pict_car = 38900000;
			ch_bw = 8000000;
			pict2chanb_vsb = -1250000;
			pict2snd1 = 6500000;
			pict2snd2 = 0;
			rcvr_mode = 1;
			break;
		}
	case MTTUNEA_PAL_MN:{
			pict_car = 38900000;
			ch_bw = 6000000;
			pict2chanb_vsb = -1250000;
			pict2snd1 = 4500000;
			pict2snd2 = 0;
			rcvr_mode = 1;
			break;
		}
	case MTTUNEA_PAL_DK:{
			pict_car = 38900000;
			ch_bw = 8000000;
			pict2chanb_vsb = -1250000;
			pict2snd1 = 6500000;
			pict2snd2 = 0;
			rcvr_mode = 1;
			break;
		}
	case MTTUNEA_DIGITAL:{
			pict_car = 36125000;
			ch_bw = 8000000;
			pict2chanb_vsb = -(ch_bw / 2);
			pict2snd1 = 0;
			pict2snd2 = 0;
			rcvr_mode = 2;
			break;
		}
	case MTTUNEA_FMRADIO:{
			pict_car = 38900000;
			ch_bw = 8000000;
			pict2chanb_vsb = -(ch_bw / 2);
			pict2snd1 = 0;
			pict2snd2 = 0;
			rcvr_mode = 4;
			//f_in -= 2900000;
			break;
		}
	case MTTUNEA_DVBC:{
			pict_car = 36125000;
			ch_bw = 8000000;
			pict2chanb_vsb = -(ch_bw / 2);
			pict2snd1 = 0;
			pict2snd2 = 0;
			rcvr_mode = MT2063_CABLE_QAM;
			break;
		}
	case MTTUNEA_DVBT:{
			pict_car = 36125000;
			ch_bw = bw_in;	//8000000
			pict2chanb_vsb = -(ch_bw / 2);
			pict2snd1 = 0;
			pict2snd2 = 0;
			rcvr_mode = MT2063_OFFAIR_COFDM;
			break;
		}
	case MTTUNEA_UNKNOWN:
		break;
	default:
		break;
	}

	pict2chanb_snd = pict2chanb_vsb - ch_bw;
	if_mid = pict_car - (pict2chanb_vsb + (ch_bw / 2));

	status |= MT2063_SetParam(h, MT2063_STEPSIZE, 125000);
	status |= MT2063_SetParam(h, MT2063_OUTPUT_FREQ, if_mid);
	status |= MT2063_SetParam(h, MT2063_OUTPUT_BW, ch_bw);
	status |= MT2063_GetParam(h, MT2063_RCVR_MODE, &mode_get);

	status |= MT2063_SetParam(h, MT2063_RCVR_MODE, rcvr_mode);
	status |= MT2063_Tune(h, (f_in + (pict2chanb_vsb + (ch_bw / 2))));
	status |= MT2063_GetParam(h, MT2063_RCVR_MODE, &mode_get);

	return (u32) status;
}

static int mt2063_init(struct dvb_frontend *fe)
{
	u32 status = -EINVAL;
	struct mt2063_state *state = fe->tuner_priv;

	status = MT2063_Open(fe);
	status |= MT2063_SoftwareShutdown(state, 1);
	status |= MT2063_ClearPowerMaskBits(state, MT2063_ALL_SD);

	if (0 != status) {
		printk("%s %d error status = 0x%x!!\n", __func__, __LINE__,
		       status);
		return -1;
	}

	return 0;
}

static int mt2063_get_status(struct dvb_frontend *fe, u32 * status)
{
	int rc = 0;

	//get tuner lock status

	return rc;
}

static int mt2063_get_state(struct dvb_frontend *fe,
			    enum tuner_param param, struct tuner_state *tunstate)
{
	struct mt2063_state *state = fe->tuner_priv;

	switch (param) {
	case DVBFE_TUNER_FREQUENCY:
		//get frequency
		break;
	case DVBFE_TUNER_TUNERSTEP:
		break;
	case DVBFE_TUNER_IFFREQ:
		break;
	case DVBFE_TUNER_BANDWIDTH:
		//get bandwidth
		break;
	case DVBFE_TUNER_REFCLOCK:
		tunstate->refclock = (u32) MT2063_GetLocked(state);
		break;
	default:
		break;
	}

	return (int)tunstate->refclock;
}

static int mt2063_set_state(struct dvb_frontend *fe,
			    enum tuner_param param, struct tuner_state *tunstate)
{
	struct mt2063_state *state = fe->tuner_priv;
	u32 status = 0;

	switch (param) {
	case DVBFE_TUNER_FREQUENCY:
		//set frequency

		status =
		    MT_Tune_atv(state,
				tunstate->frequency, tunstate->bandwidth,
				state->tv_type);

		state->frequency = tunstate->frequency;
		break;
	case DVBFE_TUNER_TUNERSTEP:
		break;
	case DVBFE_TUNER_IFFREQ:
		break;
	case DVBFE_TUNER_BANDWIDTH:
		//set bandwidth
		state->bandwidth = tunstate->bandwidth;
		break;
	case DVBFE_TUNER_REFCLOCK:

		break;
	case DVBFE_TUNER_OPEN:
		status = MT2063_Open(fe);
		break;
	case DVBFE_TUNER_SOFTWARE_SHUTDOWN:
		status = MT2063_SoftwareShutdown(state, 1);
		break;
	case DVBFE_TUNER_CLEAR_POWER_MASKBITS:
		status =
		    MT2063_ClearPowerMaskBits(state,
					      MT2063_ALL_SD);
		break;
	default:
		break;
	}

	return (int)status;
}

static int mt2063_release(struct dvb_frontend *fe)
{
	struct mt2063_state *state = fe->tuner_priv;

	fe->tuner_priv = NULL;
	kfree(state);

	return 0;
}

static struct dvb_tuner_ops mt2063_ops = {
	.info = {
		 .name = "MT2063 Silicon Tuner",
		 .frequency_min = 45000000,
		 .frequency_max = 850000000,
		 .frequency_step = 0,
		 },

	.init = mt2063_init,
	.sleep = MT2063_Sleep,
	.get_status = mt2063_get_status,
	.get_state = mt2063_get_state,
	.set_state = mt2063_set_state,
	.release = mt2063_release
};

struct dvb_frontend *mt2063_attach(struct dvb_frontend *fe,
				   struct mt2063_config *config,
				   struct i2c_adapter *i2c)
{
	struct mt2063_state *state = NULL;

	state = kzalloc(sizeof(struct mt2063_state), GFP_KERNEL);
	if (state == NULL)
		goto error;

	state->config = config;
	state->i2c = i2c;
	state->frontend = fe;
	state->reference = config->refclock / 1000;	/* kHz */
	state->MT2063_init = false;
	fe->tuner_priv = state;
	fe->ops.tuner_ops = mt2063_ops;

	printk("%s: Attaching MT2063 \n", __func__);
	return fe;

error:
	kfree(state);
	return NULL;
}

EXPORT_SYMBOL(mt2063_attach);
MODULE_PARM_DESC(verbose, "Set Verbosity level");

MODULE_AUTHOR("Henry");
MODULE_DESCRIPTION("MT2063 Silicon tuner");
MODULE_LICENSE("GPL");
