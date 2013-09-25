/****************************************************************************
 *
 *        Copyright (c) 2006-2008 by Sunplus mMedia Inc., Ltd.
 *
 *  This software is copyrighted by and is the property of Sunplus
 *  mMedia Inc., Ltd. All rights are reserved by Sunplus mMedia
 *  Inc., Ltd. This software may only be used in accordance with the
 *  corresponding license agreement. Any unauthorized use, duplication,
 *  distribution, or disclosure of this software is expressly forbidden.
 *
 *  This Copyright notice MUST not be removed or modified without prior
 *  written consent of Sunplus mMedia Inc., Ltd.
 *
 *  Sunplus mMedia Inc., Ltd. reserves the right to modify this
 *  software without notice.
 *
 *  Sunplus mMedia Inc., Ltd.
 *  19-1, Innovation First Road, Science-Based Industrial Park,
 *  Hsin-Chu, Taiwan, R.O.C.
 *
 ****************************************************************************/
#ifndef APP_I2C_LIB_ICATCH_H
#define APP_I2C_LIB_ICATCH_H


/****************************************************************************
 *						C O N S T A N T S 										*
 ****************************************************************************/
#ifndef NULL
#define NULL  ((void *)0)
#endif
#ifndef SUCCESS
#define SUCCESS	0
#endif
#ifndef FAIL
#define FAIL	1
#endif
#ifndef TRUE
#define TRUE	1
#endif
#ifndef FALSE
#define FALSE	0
#endif

#define UINT64	unsigned long long
#define UINT32	unsigned int
#define UINT16	unsigned short
#define UINT8	unsigned char
#define SINT64	signed long long
#define SINT32	signed int
#define SINT16	signed short
#define SINT8	signed char
#define BOOL	unsigned char

#define SP7K_MSG_BASE	0x7100
#define SP7K_REG_BASE	0x7280

/****************************************************************************
 *						D A T A    T Y P E S									*
 ****************************************************************************/
typedef enum {
	SP7K_MSG_BANDING_SELECTION				= (SP7K_MSG_BASE | 0x01),
	SP7K_MSG_COLOR_EFFECT					= (SP7K_MSG_BASE | 0x02),
	SP7K_MSG_EV_COMPENSATION				= (SP7K_MSG_BASE | 0x03),
	SP7K_MSG_FLASH_MODE						= (SP7K_MSG_BASE | 0x04),
	SP7K_MSG_FOCUS_MODE						= (SP7K_MSG_BASE | 0x05),
	SP7K_MSG_PV_SIZE						= (SP7K_MSG_BASE | 0x06),
	SP7K_MSG_SCENE_MODE						= (SP7K_MSG_BASE | 0x09),
	SP7K_MSG_WHITE_BALANCE					= (SP7K_MSG_BASE | 0x0A),
	SP7K_MSG_AE_MODE						= (SP7K_MSG_BASE | 0x0E),
	SP7K_MSG_CAP_MODE						= (SP7K_MSG_BASE | 0x0F),
	SP7K_MSG_CAP_ISO						= (SP7K_MSG_BASE | 0x10),
	SP7K_MSG_AURA_COLOR_INDEX				= (SP7K_MSG_BASE | 0x19),
	SP7K_MSG_OPTICAL_ZOOM					= (SP7K_MSG_BASE | 0x1A),
	SP7K_MSG_ISP_FUNCTION					= (SP7K_MSG_BASE | 0x1B),
	SP7K_MSG_PV_CAP_MODE					= (SP7K_MSG_BASE | 0x20),
	SP7K_MSG_PV_STREAM						= (SP7K_MSG_BASE | 0x21),
	SP7K_MSG_BURST_ABORT					= (SP7K_MSG_BASE | 0x22),
	SP7K_MSG_CAP_EDGE_QUANTITY				= (SP7K_MSG_BASE | 0x23),
	SP7K_MSG_PV_FIX_FRAME_RATE				= (SP7K_MSG_BASE | 0x24),
	SP7K_MSG_PV_MAX_EXPTIME					= (SP7K_MSG_BASE | 0x25),
	SP7K_MSG_HDR_POSITIVE_EV				= (SP7K_MSG_BASE | 0x27),
	SP7K_MSG_HDR_NEGATIVE_EV				= (SP7K_MSG_BASE | 0x28),
	SP7K_MSG_PV_MIN_ISO						= (SP7K_MSG_BASE | 0x29),
	SP7K_MSG_G_SENSOR_INFO_X				= (SP7K_MSG_BASE | 0x2A),
	SP7K_MSG_G_SENSOR_INFO_Y				= (SP7K_MSG_BASE | 0x2B),
	SP7K_MSG_G_SENSOR_INFO_Z				= (SP7K_MSG_BASE | 0x2C),
	SP7K_MSG_GYRO_INFO_X					= (SP7K_MSG_BASE | 0x2D),
	SP7K_MSG_GYRO_INFO_Y					= (SP7K_MSG_BASE | 0x2E),
	SP7K_MSG_GYRO_INFO_Z					= (SP7K_MSG_BASE | 0x2F),
	SP7K_MSG_AF_ROI_SIZE_H					= (SP7K_MSG_BASE | 0x40),
	SP7K_MSG_AF_ROI_SIZE_L					= (SP7K_MSG_BASE | 0x41),
	SP7K_MSG_AF_ROI_X_H						= (SP7K_MSG_BASE | 0x42),
	SP7K_MSG_AF_ROI_X_L						= (SP7K_MSG_BASE | 0x43),
	SP7K_MSG_AF_ROI_Y_H						= (SP7K_MSG_BASE | 0x44),
	SP7K_MSG_AF_ROI_Y_L						= (SP7K_MSG_BASE | 0x45),
	SP7K_MSG_AF_ROI_TRIGGER					= (SP7K_MSG_BASE | 0x46),
	SP7K_MSG_AE_ROI_SIZE_H					= (SP7K_MSG_BASE | 0x48),
	SP7K_MSG_AE_ROI_SIZE_L					= (SP7K_MSG_BASE | 0x49),
	SP7K_MSG_AE_ROI_X_H						= (SP7K_MSG_BASE | 0x4A),
	SP7K_MSG_AE_ROI_X_L						= (SP7K_MSG_BASE | 0x4B),
	SP7K_MSG_AE_ROI_Y_H						= (SP7K_MSG_BASE | 0x4C),
	SP7K_MSG_AE_ROI_Y_L						= (SP7K_MSG_BASE | 0x4D),
	SP7K_MSG_AE_ROI_TRIGGER					= (SP7K_MSG_BASE | 0x4E),
	SP7K_MSG_AF_ABORT						= (SP7K_MSG_BASE | 0x4F),
	SP7K_MSG_VCM_STEP_L						= (SP7K_MSG_BASE | 0x5C),
	SP7K_MSG_VCM_STEP_H						= (SP7K_MSG_BASE | 0x5D),
	SP7K_MSG_CAP_EDGE_INFO					= (SP7K_MSG_BASE | 0x5F),
	SP7K_MSG_RAW_PATH						= (SP7K_MSG_BASE | 0x60),
	SP7K_MSG_RAW_FORMAT						= (SP7K_MSG_BASE | 0x61),
	SP7K_MSG_CALIBRATION_RAW_LINEAR			= (SP7K_MSG_BASE | 0x62),
	SP7K_MSG_CALIBRATION_RAW_OB				= (SP7K_MSG_BASE | 0x63),
	SP7K_MSG_CALIBRATION_RAW_BP				= (SP7K_MSG_BASE | 0x64),
	SP7K_MSG_CALIBRATION_RAW_LSC			= (SP7K_MSG_BASE | 0x65),
	SP7K_MSG_CALIBRATION_RAW_CA				= (SP7K_MSG_BASE | 0x66),
	SP7K_MSG_CALIBRATION_RAW_ARD			= (SP7K_MSG_BASE | 0x67),
	SP7K_MSG_CALIBRATION_RAW_DEPEAK			= (SP7K_MSG_BASE | 0x68),
	SP7K_MSG_CALIBRATION_RAW_WB				= (SP7K_MSG_BASE | 0x69),
	SP7K_MSG_CALIBRATION_AWB_CRITERIA		= (SP7K_MSG_BASE | 0x6D),
	SP7K_MSG_AEAWBTHREAD_MODE				= (SP7K_MSG_BASE | 0x70),
	SP7K_MSG_DQ_MODE						= (SP7K_MSG_BASE | 0x71),
	SP7K_MSG_CDSP_MODE						= (SP7K_MSG_BASE | 0x72),
	SP7K_MSG_CALIBRATION_CMD1				= (SP7K_MSG_BASE | 0x74),
	SP7K_MSG_CALIBRATION_CMD2				= (SP7K_MSG_BASE | 0x75),
	SP7K_MSG_CALIBRATION_INIT_CMD			= (SP7K_MSG_BASE | 0x76),
	SP7K_MSG_CALIBRATION_LENS_MAX_LUM_DECAY	= (SP7K_MSG_BASE | 0x77),
	SP7K_MSG_CALIBRATION_DQ_INDEX_FIX		= (SP7K_MSG_BASE | 0x78),
	SP7K_MSG_CALIBRATION_UTILIZATION_OPTION	= (SP7K_MSG_BASE | 0x7F),
	SP7K_MSG_ROI_SWITCH						= (SP7K_MSG_BASE | 0x88),

	SP7K_MSG_VENDREQ_ID						= (SP7K_MSG_BASE | 0xE4),
	SP7K_MSG_VENDREQ_PARAM					= (SP7K_MSG_BASE | 0xE5),
	SP7K_MSG_VENDREQ_VALUES_L				= (SP7K_MSG_BASE | 0xE6),
	SP7K_MSG_VENDREQ_PROCESS				= (SP7K_MSG_BASE | 0xE8),
	SP7K_MSG_VENDREQ_VALUES_H				= (SP7K_MSG_BASE | 0xEA),
	SP7K_MSG_VENDREQ_CMD					= (SP7K_MSG_BASE | 0xEB),
} sp7kHostMsgList_e;

typedef enum {
	SP7K_RDREG_AF_STATUS					= (SP7K_REG_BASE | 0xA0),
	SP7K_RDREG_AF_RESULT					= (SP7K_REG_BASE | 0xA1),
	SP7K_RDREG_AF_CONSUME_TIME_L			= (SP7K_REG_BASE | 0xA2),
	SP7K_RDREG_AF_CONSUME_TIME_H			= (SP7K_REG_BASE | 0xA3),
	SP7K_RDREG_VCM_CURRENT_POS_L			= (SP7K_REG_BASE | 0xA4),
	SP7K_RDREG_VCM_CURRENT_POS_H			= (SP7K_REG_BASE | 0xA5),
	SP7K_RDREG_EXPTIME_NUMERATOR			= (SP7K_REG_BASE | 0xB0),
	SP7K_RDREG_EXPTIME_DENOMINATOR_L		= (SP7K_REG_BASE | 0xB1),
	SP7K_RDREG_EXPTIME_DENOMINATOR_M		= (SP7K_REG_BASE | 0xB2),
	SP7K_RDREG_EXPTIME_DENOMINATOR_H		= (SP7K_REG_BASE | 0xB3),
	SP7K_RDREG_EXPTIME_COMPENSATION			= (SP7K_REG_BASE | 0xB4),
	SP7K_RDREG_LENS_FOCAL_LENGTH_L			= (SP7K_REG_BASE | 0xB5),
	SP7K_RDREG_LENS_FOCAL_LENGTH_H			= (SP7K_REG_BASE | 0xB6),
	SP7K_RDREG_ISO_L						= (SP7K_REG_BASE | 0xB7),
	SP7K_RDREG_ISO_H						= (SP7K_REG_BASE | 0xB8),
	SP7K_RDREG_FLASH_MODE					= (SP7K_REG_BASE | 0xB9),
	SP7K_RDREG_CAP_EDGE_INFO_B0				= (SP7K_REG_BASE | 0xBA),
	SP7K_RDREG_CAP_EDGE_INFO_B1				= (SP7K_REG_BASE | 0xBB),
	SP7K_RDREG_CAP_EDGE_INFO_B2				= (SP7K_REG_BASE | 0xBC),
	SP7K_RDREG_CAP_EDGE_INFO_B3				= (SP7K_REG_BASE | 0xBD),
	SP7K_RDREG_3A_STATUS					= (SP7K_REG_BASE | 0xC3),
	SP7K_RDREG_EDGE_INFO_COUNT				= (SP7K_REG_BASE | 0xC4),
	SP7K_RDREG_REAR_SENSORID_L				= (SP7K_REG_BASE | 0xC6),
	SP7K_RDREG_REAR_SENSORID_H				= (SP7K_REG_BASE | 0xC7),
	SP7K_RDREG_FRONT_SENSORID_L				= (SP7K_REG_BASE | 0xC8),
	SP7K_RDREG_FRONT_SENSORID_H				= (SP7K_REG_BASE | 0xC9),
	SP7K_RDREG_FW_VER_L						= (SP7K_REG_BASE | 0xCA),
	SP7K_RDREG_FW_VER_M						= (SP7K_REG_BASE | 0xCB),
	SP7K_RDREG_FW_VER_H						= (SP7K_REG_BASE | 0xCC),
	SP7K_RDREG_VENDOR_ID_L					= (SP7K_REG_BASE | 0xCE),
	SP7K_RDREG_VENDOR_ID_H					= (SP7K_REG_BASE | 0xCF),
	SP7K_RDREG_CALIBRATION_LSC_MAX_RGAIN_L	= (SP7K_REG_BASE | 0xD8),
	SP7K_RDREG_CALIBRATION_LSC_MAX_RGAIN_H	= (SP7K_REG_BASE | 0xD9),
	SP7K_RDREG_CALIBRATION_LSC_MAX_GGAIN_L	= (SP7K_REG_BASE | 0xDA),
	SP7K_RDREG_CALIBRATION_LSC_MAX_GGAIN_H	= (SP7K_REG_BASE | 0xDB),
	SP7K_RDREG_CALIBRATION_LSC_MAX_BGAIN_L	= (SP7K_REG_BASE | 0xDC),
	SP7K_RDREG_CALIBRATION_LSC_MAX_BGAIN_H	= (SP7K_REG_BASE | 0xDD),
	SP7K_RDREG_CALIBRATION_AWB_RGain_L		= (SP7K_REG_BASE | 0xE1),
	SP7K_RDREG_CALIBRATION_AWB_RGain_H		= (SP7K_REG_BASE | 0xE2),
	SP7K_RDREG_CALIBRATION_AWB_GGain_L		= (SP7K_REG_BASE | 0xE3),
	SP7K_RDREG_CALIBRATION_AWB_GGain_H		= (SP7K_REG_BASE | 0xE4),
	SP7K_RDREG_CALIBRATION_AWB_BGain_L		= (SP7K_REG_BASE | 0xE5),
	SP7K_RDREG_CALIBRATION_AWB_BGain_H		= (SP7K_REG_BASE | 0xE6),
	SP7K_RDREG_CALIBRATION_EXEC_STATUS1		= (SP7K_REG_BASE | 0xF0),
	SP7K_RDREG_CALIBRATION_EXEC_STATUS2		= (SP7K_REG_BASE | 0xF1),
	SP7K_RDREG_CALIBRATION_ERROR_STATUS1	= (SP7K_REG_BASE | 0xF2),
	SP7K_RDREG_CALIBRATION_ERROR_STATUS2	= (SP7K_REG_BASE | 0xF3),
	SP7K_RDREG_CALIBRATION_ERROR_CODE		= (SP7K_REG_BASE | 0xF4),
	SP7K_RDREG_CALIBRATION_LOAD_TABLE_STATUS= (SP7K_REG_BASE | 0xF5),
	SP7K_RDREG_INT_STS_REG_0				= (SP7K_REG_BASE | 0xF8),
	SP7K_RDREG_INT_MASK_REG_0				= (SP7K_REG_BASE | 0xFC),
} sp7kReadRegList_e;

typedef enum {
	GPIO_CTL_LOW,
	GPIO_CTL_HIGH,
	GPIO_CTL_MAX,
} sp7kGpioCtlList_e;

typedef enum {
	BOOT_FROM_SPI,
	BOOT_FROM_HOST,
	BOOT_FROM_MAX,
} sp7kPowerOnList_e;

typedef enum {
	SENSOR_ID_REAR,
	SENSOR_ID_FRONT,
	SENSOR_ID_REAR_CALIBRATION,
	SENSOR_ID_MAX,
} sp7kSensorIdList_e;

typedef enum {
	IMAGE_CAP_SINGLE,
	IMAGE_CAP_HDR,
	IMAGE_CAP_ZSL_SINGLE_FLASH,
	IMAGE_CAP_ZSL_BURST_FLASH,
	IMAGE_CAP_NONZSL_SINGLE,
	IMAGE_CAP_NONZSL_BURST,
	IMAGE_CAP_MAX,
} sp7kImageCapList_e;

typedef enum {
	CAP_MODE_SINGLE,
	CAP_MODE_HDR,
	CAP_MODE_RSV,
	CAP_MODE_BURST,
	CAP_MODE_MAX,
} sp7kCapModeList_e;

typedef enum {
	PVCAP_MODE_PV,
	PVCAP_MODE_CAP,
	PVCAP_MODE_CAP_ZSL,
	PVCAP_MODE_CAP_NONZSL,
	PVCAP_MODE_MAX,
} sp7kPvCapModeList_e;

typedef enum {
	TAFTAE_TAF_ONLY,
	TAFTAE_TAE_OFF,
	TAFTAE_TAE_USE_TAE_ROI,
	TAFTAE_TAE_USE_TAF_ROI,
	TAFTAE_TAE_ONLY,
	TAFTAE_MAX,
} sp7kTAFTAEList_e;

typedef enum {
	NONE,
	LOADCODE_BOOT_FILE_ERR,

	LOADCODE_CALIB_OPT_FILE_ERR,
	LOADCODE_CALIB_OPT_MAX_ERR,

	LOADCODE_3ACALI_FILE_ERR,
	LOADCODE_3ACALI_HEADER_ERR,
	LOADCODE_3ACALI_CHKSUM_ERR,

	LOADCODE_LSC_FILE_ERR,
	LOADCODE_LSC_HEADER_ERR,
	LOADCODE_LSC_CHKSUM_ERR,

	LOADCODE_LSC_DQ_FILE_ERR,
	LOADCODE_LSC_DQ_HEADER_ERR,
	LOADCODE_LSC_DQ_CHKSUN_ERR,

	LOADCODE_GET_RES_NUM_ERR,
} LoadCodeErrList_e;

typedef struct {
	UINT32 DmemFicdmemSize; /*DMEM+FICDMEM size*/
	UINT32 ImemSize; /*IMEM size*/
	UINT16 FmClk; /*FM clock*/
	UINT16 Checksum; /*checksum*/
	UINT32 DmemSize; /*DMEM size*/
} ispFwHeaderInfo_t;

typedef struct {
	UINT32 retCalibOpt;
	UINT32 ret3acli;
	UINT32 retLsc;
	UINT32 retLscdq;
} ispLoadCodeRet_t;

/****************************************************************************
 *						 M A C R O S											*
 ****************************************************************************/

/****************************************************************************
 *						 E X T E R N V A R I A B L E S    D E C L A R A T I O N S		*
 ****************************************************************************/

/****************************************************************************
 *						F U N C T I O N    D E C L A R A T I O N S					*
 ****************************************************************************/
void EXISP_I2C_BandSelectionSet(UINT8 ucParam);
void EXISP_I2C_ColorEffectSet(UINT8 ucParam);
void EXISP_I2C_EvSet(UINT8 ucParam);
void EXISP_I2C_FlashModeSet(UINT8 ucParam);
void EXISP_I2C_FocusModeSet(UINT8 ucParam);
void EXISP_I2C_PvSizeSet(UINT8 ucParam);
void EXISP_I2C_SceneModeSet(UINT8 ucParam);
void EXISP_I2C_WhiteBalanceSet(UINT8 ucParam);
void EXISP_I2C_AEModeSet(UINT8 ucParam);
void EXISP_I2C_CapModeSet(UINT8 ucParam);
void EXISP_I2C_ISOSet(UINT8 ucParam);
void EXISP_I2C_AuraColorIndexSet(UINT8 ucParam);
void EXISP_I2C_OpticalZoomSet(UINT8 ucParam);
void EXISP_I2C_IspFuncSet(UINT8 ucParam);
void EXISP_I2C_PvCapModeSet(UINT8 ucParam);
void EXISP_I2C_PvStreamSet(UINT8 ucParam);
void EXISP_I2C_BurstAbortSet(UINT8 ucParam);
void EXISP_I2C_CapEdgeQuantitySet(UINT8 ucValue);
void EXISP_I2C_PvFixFrameRateSet(UINT8 ucValue);
void EXISP_I2C_PvMaxExposureTimeSet(UINT8 ucValue);
void EXISP_I2C_HdrPositiveEvSet(UINT8 ucValue);
void EXISP_I2C_HdrNegativeEvSet(UINT8 ucValue);
void EXISP_I2C_PvMinISOSet(UINT8 ucParam);
void EXISP_I2C_GSensorInfoSet(SINT8 ucX, SINT8 ucY, SINT8 ucZ);
void EXISP_I2C_GyroInfoSet(UINT8 ucX, UINT8 ucY, UINT8 ucZ);
void EXISP_I2C_AFROISet(UINT16 usSize, UINT16 usX, UINT16 usY);
void EXISP_I2C_AFROITriggerSet(void);
void EXISP_I2C_AEROISet(UINT16 usSize, UINT16 usX, UINT16 usY);
void EXISP_I2C_AEROITriggerSet(UINT8 ucParam);
void EXISP_I2C_AFAbortSet(UINT8 ucParam);
void EXISP_I2C_VcmStepSet(UINT16 usValue);
void EXISP_I2C_CapEdgeInfoSet(UINT8 ucParam);
void EXISP_I2C_RawPathSet(UINT8 ucParam);
void EXISP_I2C_RawFormatSet(UINT8 ucParam);
void EXISP_I2C_CalibrationRawLinearSet(UINT8 ucParam);
void EXISP_I2C_CalibrationRawObSet(UINT8 ucParam);
void EXISP_I2C_CalibrationRawBpSet(UINT8 ucParam);
void EXISP_I2C_CalibrationRawLscSet(UINT8 ucParam);
void EXISP_I2C_CalibrationRawCaSet(UINT8 ucParam);
void EXISP_I2C_CalibrationRawArdSet(UINT8 ucParam);
void EXISP_I2C_CalibrationRawDepeakSet(UINT8 ucParam);
void EXISP_I2C_CalibrationRawWbSet(UINT8 ucParam);
void EXISP_I2C_CalibrationAwbCriteriaSet(UINT8 ucVaule);
void EXISP_I2C_AEAWBThreadModeSet(UINT8 ucParam);
void EXISP_I2C_DqModeSet(UINT8 ucParam);
void EXISP_I2C_CdspOnOffModeSet(UINT8 ucParam);
void EXISP_I2C_CalibrationCmd1Set(UINT8 ucParam);
void EXISP_I2C_CalibrationCmd2Set(UINT8 ucParam);
void EXISP_I2C_CalibrationInitCmdSet(UINT8 ucParam);
void EXISP_I2C_CalibrationLscMaxGainSet(UINT8 ucVaule);
void EXISP_I2C_CalibrationDqIndexFixSet(UINT8 ucParam);
void EXISP_I2C_CaliUtilizationOptionSet(UINT8 ucParam);
void EXISP_I2C_ROISwitchSet(UINT8 ucParam);
void EXISP_I2C_VendreqIdSet(UINT8 ucValue);
void EXISP_I2C_VendreqParamSet(UINT8 ucValue);
void EXISP_I2C_VendreqValuesSet_L(UINT8 ucVaule);
void EXISP_I2C_VendreqProcessSet(void);
void EXISP_I2C_VendreqValuesSet_H(UINT8 ucVaule);
void EXISP_I2C_VendreqCmdSet(UINT8 ucParam);


UINT8	EXISP_I2C_AFStatusGet(void);
UINT8	EXISP_I2C_AFResultGet(void);
UINT16	EXISP_I2C_AFConsumeTimeGet(void);
UINT16	EXISP_I2C_VcmCurrentPosGet(void);
UINT8	EXISP_I2C_ExposureTimeNumeratorGet(void);
UINT32	EXISP_I2C_ExposureTimeDenominatorGet(void);
UINT8	EXISP_I2C_ExposureTimeCompensationGet(void);
UINT16	EXISP_I2C_LensFocalLengthGet(void);
UINT16	EXISP_I2C_ISOValueGet(void);
UINT8	EXISP_I2C_FlashModeGet(void);
UINT32	EXISP_I2C_CapEdgeInfoGet(void);
UINT8	EXISP_I2C_3AStatusGet(void);
UINT8	EXISP_I2C_EdgeInfoCountGet(void);
UINT16	EXISP_I2C_RearSensorIdGet(void);
UINT16	EXISP_I2C_FrontSensorIdGet(void);
UINT32	EXISP_I2C_FWVersionGet(void);
UINT16	EXISP_I2C_VendorIdGet(void);
UINT16	EXISP_I2C_CalibrationLscMaxRGainGet(void);
UINT16	EXISP_I2C_CalibrationLscMaxGGainGet(void);
UINT16	EXISP_I2C_CalibrationLscMaxBGainGet(void);
UINT16	EXISP_I2C_CalibrationAWBRGainGet(void);
UINT16	EXISP_I2C_CalibrationAWBGGainGet(void);
UINT16	EXISP_I2C_CalibrationAWBBGainGet(void);
UINT8	EXISP_I2C_CalibrationExecStatus1Get(void);
UINT8	EXISP_I2C_CalibrationExecStatus2Get(void);
UINT8	EXISP_I2C_CalibrationErrorStatus1Get(void);
UINT8	EXISP_I2C_CalibrationErrorStatus2Get(void);
UINT8	EXISP_I2C_CalibrationErrorCodeGet(void);
UINT8	EXISP_I2C_CalibrationLoadTableStatusGet(void);


UINT8 EXISP_SuspendGpioCfgSet(UINT8 ucCtl);
UINT8 EXISP_DigitalPowerCfgSet(UINT8 On);
UINT8 EXISP_AnalogPowerCfgSet(UINT8 On);
UINT8 EXISP_ClockCfgSet(UINT8 ucEnable);
UINT8 EXISP_ResetGpioCfgSet(UINT8 ucCtl);
UINT8 EXISP_PowerOn(UINT8 ucBoot);
UINT8 EXISP_PowerOff(void);
UINT8 EXISP_LoadCodeStart(
	UINT8 ucBoot,
	UINT8 ucFwIdx,
	UINT8 is_calibration,
	UINT8 *pIspFw,
	UINT8 *pCalibOpt,
	UINT8 *p3acali,
	UINT8 *pLsc,
	UINT8 *pLscdq);
UINT8 EXISP_LoadCode(
	UINT8 ucFwIdx,
	UINT8 is_calibration,
	UINT8 *pIspFw,
	UINT8 *pCalibOpt,
	UINT8 *p3acali,
	UINT8 *pLsc,
	UINT8 *pLscdq);
void EXISP_UpdateCalibResStart(
	UINT8 ucFwIdx,
	UINT8 *pIspFw,
	ispFwHeaderInfo_t *pFwInfo,
	ispLoadCodeRet_t *pLoadCodeRet,
	UINT8 ucCaliOpt,
	UINT8 *p3acali,
	UINT8 *pLsc,
	UINT8 *pLscdq);
UINT8 EXISP_UpdateCalibRes(
	UINT8 idx,
	UINT8 *pIspFw,
	ispFwHeaderInfo_t *pFwInfo,
	UINT8 *p3acali,
	UINT8 *pLsc,
	UINT8 *pLscdq);
UINT8 EXISP_ResCheck(UINT8 idx, UINT8 *p3acali, UINT8 *pLsc, UINT8 *Lscdq);
UINT8 EXISP_ResNumGet(UINT32 *resNum, UINT8 *pIspFw);
UINT8 EXISP_SPIDataWrite(UINT8 ucSPIMode, UINT8 *ucStartAddr, UINT32 ulTransByteCnt, UINT32 ulDmaAddr);
UINT8 EXISP_7002SPICfg(UINT8 ucSPIMode, UINT32 ulDmaAddr, UINT32 ulTransByteCnt);
UINT8 EXISP_PvSizeSet(UINT8 ucResolutionIdx);
UINT8 EXISP_ImageCapSet(UINT8 ucImageCapIdx);
UINT8 EXISP_TAFTAEROISet(
	UINT8 ucTrg,
	UINT16 usTAFSize,
	UINT16 usTAFX,
	UINT16 usTAFY,
	UINT16 usTAESize,
	UINT16 usTAEX,
	UINT16 usTAEY);
UINT8 EXISP_SwitchRAWFormatSet(void);
UINT8 EXISP_ExifInfoGet(
	UINT8 *ucExpTimeNumerator,
	UINT32 *ulExpTimeDenominator,
	UINT8 *ucExpTimeCompensation,
	UINT16 *usLensFocalLength,
	UINT16 *usIsoInfo,
	UINT8 *ucFlashInfo);

#endif  /* APP_I2C_LIB_ICATCH_H */

