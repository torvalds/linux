#include "drxk_map.h"

#define DRXK_VERSION_MAJOR 0
#define DRXK_VERSION_MINOR 9
#define DRXK_VERSION_PATCH 4300

#define HI_I2C_DELAY        42
#define HI_I2C_BRIDGE_DELAY 350
#define DRXK_MAX_RETRIES    100

#define DRIVER_4400 1

#define DRXX_JTAGID   0x039210D9
#define DRXX_J_JTAGID 0x239310D9
#define DRXX_K_JTAGID 0x039210D9

#define DRX_UNKNOWN     254
#define DRX_AUTO        255

#define DRX_SCU_READY   0
#define DRXK_MAX_WAITTIME (200)
#define SCU_RESULT_OK      0
#define SCU_RESULT_SIZE   -4
#define SCU_RESULT_INVPAR -3
#define SCU_RESULT_UNKSTD -2
#define SCU_RESULT_UNKCMD -1

#ifndef DRXK_OFDM_TR_SHUTDOWN_TIMEOUT
#define DRXK_OFDM_TR_SHUTDOWN_TIMEOUT (200)
#endif

#define DRXK_8VSB_MPEG_BIT_RATE     19392658UL  /*bps*/
#define DRXK_DVBT_MPEG_BIT_RATE     32000000UL  /*bps*/
#define DRXK_QAM16_MPEG_BIT_RATE    27000000UL  /*bps*/
#define DRXK_QAM32_MPEG_BIT_RATE    33000000UL  /*bps*/
#define DRXK_QAM64_MPEG_BIT_RATE    40000000UL  /*bps*/
#define DRXK_QAM128_MPEG_BIT_RATE   46000000UL  /*bps*/
#define DRXK_QAM256_MPEG_BIT_RATE   52000000UL  /*bps*/
#define DRXK_MAX_MPEG_BIT_RATE      52000000UL  /*bps*/

#define   IQM_CF_OUT_ENA_OFDM__M                                            0x4
#define     IQM_FS_ADJ_SEL_B_QAM                                            0x1
#define     IQM_FS_ADJ_SEL_B_OFF                                            0x0
#define     IQM_FS_ADJ_SEL_B_VSB                                            0x2
#define     IQM_RC_ADJ_SEL_B_OFF                                            0x0
#define     IQM_RC_ADJ_SEL_B_QAM                                            0x1
#define     IQM_RC_ADJ_SEL_B_VSB                                            0x2

enum OperationMode {
	OM_NONE,
	OM_QAM_ITU_A,
	OM_QAM_ITU_B,
	OM_QAM_ITU_C,
	OM_DVBT
};

enum DRXPowerMode {
	DRX_POWER_UP = 0,
	DRX_POWER_MODE_1,
	DRX_POWER_MODE_2,
	DRX_POWER_MODE_3,
	DRX_POWER_MODE_4,
	DRX_POWER_MODE_5,
	DRX_POWER_MODE_6,
	DRX_POWER_MODE_7,
	DRX_POWER_MODE_8,

	DRX_POWER_MODE_9,
	DRX_POWER_MODE_10,
	DRX_POWER_MODE_11,
	DRX_POWER_MODE_12,
	DRX_POWER_MODE_13,
	DRX_POWER_MODE_14,
	DRX_POWER_MODE_15,
	DRX_POWER_MODE_16,
	DRX_POWER_DOWN = 255
};


/** /brief Intermediate power mode for DRXK, power down OFDM clock domain */
#ifndef DRXK_POWER_DOWN_OFDM
#define DRXK_POWER_DOWN_OFDM        DRX_POWER_MODE_1
#endif

/** /brief Intermediate power mode for DRXK, power down core (sysclk) */
#ifndef DRXK_POWER_DOWN_CORE
#define DRXK_POWER_DOWN_CORE        DRX_POWER_MODE_9
#endif

/** /brief Intermediate power mode for DRXK, power down pll (only osc runs) */
#ifndef DRXK_POWER_DOWN_PLL
#define DRXK_POWER_DOWN_PLL         DRX_POWER_MODE_10
#endif


enum AGC_CTRL_MODE { DRXK_AGC_CTRL_AUTO = 0, DRXK_AGC_CTRL_USER, DRXK_AGC_CTRL_OFF };
enum EDrxkState { DRXK_UNINITIALIZED = 0, DRXK_STOPPED, DRXK_DTV_STARTED, DRXK_ATV_STARTED, DRXK_POWERED_DOWN };
enum EDrxkCoefArrayIndex {
	DRXK_COEF_IDX_MN = 0,
	DRXK_COEF_IDX_FM    ,
	DRXK_COEF_IDX_L     ,
	DRXK_COEF_IDX_LP    ,
	DRXK_COEF_IDX_BG    ,
	DRXK_COEF_IDX_DK    ,
	DRXK_COEF_IDX_I     ,
	DRXK_COEF_IDX_MAX
};
enum EDrxkSifAttenuation {
	DRXK_SIF_ATTENUATION_0DB,
	DRXK_SIF_ATTENUATION_3DB,
	DRXK_SIF_ATTENUATION_6DB,
	DRXK_SIF_ATTENUATION_9DB
};
enum EDrxkConstellation {
	DRX_CONSTELLATION_BPSK = 0,
	DRX_CONSTELLATION_QPSK,
	DRX_CONSTELLATION_PSK8,
	DRX_CONSTELLATION_QAM16,
	DRX_CONSTELLATION_QAM32,
	DRX_CONSTELLATION_QAM64,
	DRX_CONSTELLATION_QAM128,
	DRX_CONSTELLATION_QAM256,
	DRX_CONSTELLATION_QAM512,
	DRX_CONSTELLATION_QAM1024,
	DRX_CONSTELLATION_UNKNOWN = DRX_UNKNOWN,
	DRX_CONSTELLATION_AUTO    = DRX_AUTO
};
enum EDrxkInterleaveMode {
	DRXK_QAM_I12_J17    = 16,
	DRXK_QAM_I_UNKNOWN  = DRX_UNKNOWN
};
enum {
	DRXK_SPIN_A1 = 0,
	DRXK_SPIN_A2,
	DRXK_SPIN_A3,
	DRXK_SPIN_UNKNOWN
};

enum DRXKCfgDvbtSqiSpeed {
	DRXK_DVBT_SQI_SPEED_FAST = 0,
	DRXK_DVBT_SQI_SPEED_MEDIUM,
	DRXK_DVBT_SQI_SPEED_SLOW,
	DRXK_DVBT_SQI_SPEED_UNKNOWN = DRX_UNKNOWN
} ;

enum DRXFftmode_t {
	DRX_FFTMODE_2K = 0,
	DRX_FFTMODE_4K,
	DRX_FFTMODE_8K,
	DRX_FFTMODE_UNKNOWN = DRX_UNKNOWN,
	DRX_FFTMODE_AUTO    = DRX_AUTO
};

enum DRXMPEGStrWidth_t {
	DRX_MPEG_STR_WIDTH_1,
	DRX_MPEG_STR_WIDTH_8
};

enum DRXQamLockRange_t {
	DRX_QAM_LOCKRANGE_NORMAL,
	DRX_QAM_LOCKRANGE_EXTENDED
};

struct DRXKCfgDvbtEchoThres_t {
	u16             threshold;
	enum DRXFftmode_t      fftMode;
} ;

struct SCfgAgc {
	enum AGC_CTRL_MODE     ctrlMode;        /* off, user, auto */
	u16            outputLevel;     /* range dependent on AGC */
	u16            minOutputLevel;  /* range dependent on AGC */
	u16            maxOutputLevel;  /* range dependent on AGC */
	u16            speed;           /* range dependent on AGC */
	u16            top;             /* rf-agc take over point */
	u16            cutOffCurrent;   /* rf-agc is accelerated if output current
					   is below cut-off current */
	u16            IngainTgtMax;
	u16            FastClipCtrlDelay;
};

struct SCfgPreSaw {
	u16        reference; /* pre SAW reference value, range 0 .. 31 */
	bool          usePreSaw; /* TRUE algorithms must use pre SAW sense */
};

struct DRXKOfdmScCmd_t {
	u16 cmd;        /**< Command number */
	u16 subcmd;     /**< Sub-command parameter*/
	u16 param0;     /**< General purpous param */
	u16 param1;     /**< General purpous param */
	u16 param2;     /**< General purpous param */
	u16 param3;     /**< General purpous param */
	u16 param4;     /**< General purpous param */
};

struct drxk_state {
	struct dvb_frontend frontend;
	struct dtv_frontend_properties props;
	struct device *dev;

	struct i2c_adapter *i2c;
	u8     demod_address;
	void  *priv;

	struct mutex mutex;

	u32    m_Instance;           /**< Channel 1,2,3 or 4 */

	int    m_ChunkSize;
	u8 Chunk[256];

	bool   m_hasLNA;
	bool   m_hasDVBT;
	bool   m_hasDVBC;
	bool   m_hasAudio;
	bool   m_hasATV;
	bool   m_hasOOB;
	bool   m_hasSAWSW;         /**< TRUE if mat_tx is available */
	bool   m_hasGPIO1;         /**< TRUE if mat_rx is available */
	bool   m_hasGPIO2;         /**< TRUE if GPIO is available */
	bool   m_hasIRQN;          /**< TRUE if IRQN is available */
	u16    m_oscClockFreq;
	u16    m_HICfgTimingDiv;
	u16    m_HICfgBridgeDelay;
	u16    m_HICfgWakeUpKey;
	u16    m_HICfgTimeout;
	u16    m_HICfgCtrl;
	s32    m_sysClockFreq;      /**< system clock frequency in kHz */

	enum EDrxkState    m_DrxkState;      /**< State of Drxk (init,stopped,started) */
	enum OperationMode m_OperationMode;  /**< digital standards */
	struct SCfgAgc     m_vsbRfAgcCfg;    /**< settings for VSB RF-AGC */
	struct SCfgAgc     m_vsbIfAgcCfg;    /**< settings for VSB IF-AGC */
	u16                m_vsbPgaCfg;      /**< settings for VSB PGA */
	struct SCfgPreSaw  m_vsbPreSawCfg;   /**< settings for pre SAW sense */
	s32    m_Quality83percent;  /**< MER level (*0.1 dB) for 83% quality indication */
	s32    m_Quality93percent;  /**< MER level (*0.1 dB) for 93% quality indication */
	bool   m_smartAntInverted;
	bool   m_bDebugEnableBridge;
	bool   m_bPDownOpenBridge;  /**< only open DRXK bridge before power-down once it has been accessed */
	bool   m_bPowerDown;        /**< Power down when not used */

	u32    m_IqmFsRateOfs;      /**< frequency shift as written to DRXK register (28bit fixpoint) */

	bool   m_enableMPEGOutput;  /**< If TRUE, enable MPEG output */
	bool   m_insertRSByte;      /**< If TRUE, insert RS byte */
	bool   m_enableParallel;    /**< If TRUE, parallel out otherwise serial */
	bool   m_invertDATA;        /**< If TRUE, invert DATA signals */
	bool   m_invertERR;         /**< If TRUE, invert ERR signal */
	bool   m_invertSTR;         /**< If TRUE, invert STR signals */
	bool   m_invertVAL;         /**< If TRUE, invert VAL signals */
	bool   m_invertCLK;         /**< If TRUE, invert CLK signals */
	bool   m_DVBCStaticCLK;
	bool   m_DVBTStaticCLK;     /**< If TRUE, static MPEG clockrate will
					 be used, otherwise clockrate will
					 adapt to the bitrate of the TS */
	u32    m_DVBTBitrate;
	u32    m_DVBCBitrate;

	u8     m_TSDataStrength;
	u8     m_TSClockkStrength;

	bool   m_itut_annex_c;      /* If true, uses ITU-T DVB-C Annex C, instead of Annex A */

	enum DRXMPEGStrWidth_t  m_widthSTR;    /**< MPEG start width */
	u32    m_mpegTsStaticBitrate;          /**< Maximum bitrate in b/s in case
						    static clockrate is selected */

	/* LARGE_INTEGER   m_StartTime; */     /**< Contains the time of the last demod start */
	s32    m_MpegLockTimeOut;      /**< WaitForLockStatus Timeout (counts from start time) */
	s32    m_DemodLockTimeOut;     /**< WaitForLockStatus Timeout (counts from start time) */

	bool   m_disableTEIhandling;

	bool   m_RfAgcPol;
	bool   m_IfAgcPol;

	struct SCfgAgc    m_atvRfAgcCfg;  /**< settings for ATV RF-AGC */
	struct SCfgAgc    m_atvIfAgcCfg;  /**< settings for ATV IF-AGC */
	struct SCfgPreSaw m_atvPreSawCfg; /**< settings for ATV pre SAW sense */
	bool              m_phaseCorrectionBypass;
	s16               m_atvTopVidPeak;
	u16               m_atvTopNoiseTh;
	enum EDrxkSifAttenuation m_sifAttenuation;
	bool              m_enableCVBSOutput;
	bool              m_enableSIFOutput;
	bool              m_bMirrorFreqSpect;
	enum EDrxkConstellation  m_Constellation; /**< Constellation type of the channel */
	u32               m_CurrSymbolRate;       /**< Current QAM symbol rate */
	struct SCfgAgc    m_qamRfAgcCfg;          /**< settings for QAM RF-AGC */
	struct SCfgAgc    m_qamIfAgcCfg;          /**< settings for QAM IF-AGC */
	u16               m_qamPgaCfg;            /**< settings for QAM PGA */
	struct SCfgPreSaw m_qamPreSawCfg;         /**< settings for QAM pre SAW sense */
	enum EDrxkInterleaveMode m_qamInterleaveMode; /**< QAM Interleave mode */
	u16               m_fecRsPlen;
	u16               m_fecRsPrescale;

	enum DRXKCfgDvbtSqiSpeed m_sqiSpeed;

	u16               m_GPIO;
	u16               m_GPIOCfg;

	struct SCfgAgc    m_dvbtRfAgcCfg;     /**< settings for QAM RF-AGC */
	struct SCfgAgc    m_dvbtIfAgcCfg;     /**< settings for QAM IF-AGC */
	struct SCfgPreSaw m_dvbtPreSawCfg;    /**< settings for QAM pre SAW sense */

	u16               m_agcFastClipCtrlDelay;
	bool              m_adcCompPassed;
	u16               m_adcCompCoef[64];
	u16               m_adcState;

	u8               *m_microcode;
	int               m_microcode_length;
	bool              m_DRXK_A1_PATCH_CODE;
	bool              m_DRXK_A1_ROM_CODE;
	bool              m_DRXK_A2_ROM_CODE;
	bool              m_DRXK_A3_ROM_CODE;
	bool              m_DRXK_A2_PATCH_CODE;
	bool              m_DRXK_A3_PATCH_CODE;

	bool              m_rfmirror;
	u8                m_deviceSpin;
	u32               m_iqmRcRate;

	enum DRXPowerMode m_currentPowerMode;

	/*
	 * Configurable parameters at the driver. They stores the values found
	 * at struct drxk_config.
	 */

	u16	UIO_mask;	/* Bits used by UIO */

	bool	enable_merr_cfg;
	bool	single_master;
	bool	no_i2c_bridge;
	bool	antenna_dvbt;
	u16	antenna_gpio;

	const char *microcode_name;
};

#define NEVER_LOCK 0
#define NOT_LOCKED 1
#define DEMOD_LOCK 2
#define FEC_LOCK   3
#define MPEG_LOCK  4

