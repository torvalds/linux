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
#include <linux/spi/spi.h>
#include <mach/board.h>

#include "app_i2c_lib_icatch.h"
#include <linux/delay.h>
#include "icatch7002_common.h"

#define I2CDataWrite(reg,val) icatch_sensor_write((reg),(val))
#define I2CDataRead(reg)  icatch_sensor_read((reg))
extern struct spi_device* g_icatch_spi_dev;

/****************************************************************************
 *						C O N S T A N T S										*
 ****************************************************************************/
#define FW_HEADER_SIZE 16
#define RES_3ACALI_HEADER_SIZE	8
#define RES_LSC_HEADER_SIZE		24
#define RES_LSCDQ_HEADER_SIZE	16

/****************************************************************************
 *						M A C R O S											*
 ****************************************************************************/
#ifndef tmrUsDelay
#define tmrUsDelay(ulTime)	(((ulTime)>1000)?(msleep((ulTime)/1000)):udelay((ulTime)))
#endif
/****************************************************************************
 *						D A T A    T Y P E S									*
 ****************************************************************************/

/****************************************************************************
 *						G L O B A L    D A T A									*
 ****************************************************************************/

/****************************************************************************
 *						E X T E R N A L    R E F E R E N C E S						*
 ****************************************************************************/

/****************************************************************************
 *						F U N C T I O N    D E C L A R A T I O N S					*
 ****************************************************************************/

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_BandSelectionSet
 *  ucParam:
     0x00	Reserved
     0x01	50
     0x02	60
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_BandSelectionSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_BANDING_SELECTION, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_ColorEffectSet
 *  ucParam:
     0x00	Normal
     0x01	Aqua
     0x02	Negative
     0x03	Sepia
     0x04	Grayscale
     0x05	Vivid
     0x06	Aura
     0x07	Vintage
     0x08	Vintage2
     0x09	Lomo
     0x0A	Red
     0x0B	Blue
     0x0C	Green
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_ColorEffectSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_COLOR_EFFECT, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_EvSet
 *  ucParam:
     0x00	+2.0
     0x01	+1.7
     0x02	+1.3
     0x03	+1.0
     0x04	+0.7
     0x05	+0.3
     0x06	0
     0x07	-0.3
     0x08	-0.7
     0x09	-1.0
     0x0A	-1.3
     0x0B	-1.7
     0x0C	-2.0
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_EvSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_EV_COMPENSATION, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_FlashModeSet
 *  ucParam:
     0x00	Auto
     0x01	Off
     0x02	On
     0x03	Reserved
     0x04	Torch
     0xFF	Main Flash(100%, once)
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_FlashModeSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_FLASH_MODE, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_FocusModeSet
 *  ucParam:
     0x00	Auto
     0x01	Macro
     0x02	Infinity Fixed
     0x03	Continuous AF
     0x04	Full Search
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_FocusModeSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_FOCUS_MODE, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_PvSizeSet
 *  ucParam:
     0x00	1280x960
     0x01	3264x2448
     0x02	1920x1080
     0x03	320x240(reserved)
     0x04	1280x720
     0x05	1040x780
     0x06	2080x1560
     0x07	3648x2736
     0x08	4160x3120
     0x09	3360x1890
     0x0A	2592x1944
     0x0B	640x480
     0x0C	1408x1408
     0x0D	1920x1088
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_PvSizeSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_PV_SIZE, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_SceneModeSet
 *  ucParam:
     0x00	Normal
     0x01	Action
     0x02	Text
     0x03	Beach
     0x04	Candle light
     0x05	Firework
     0x06	Landscape
     0x07	Night
     0x08	Night Portrait
     0x09	Party
     0x0A	Portrait
     0x0B	Snow
     0x0C	Sport
     0x0D	Anti-shake
     0x0E	Sunset
     0x0F	High Sensitivity
     0x10	Landscape Portrait
     0x11	Kids
     0x12	Pet
     0x13	Flower
     0x14	Soft Flowing Water
     0x15	Food
     0x16	Backlight
     0x17	Indoor
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_SceneModeSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_SCENE_MODE, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_WhiteBalanceSet
 *  ucParam:
     0x00	Auto
     0x01	Daylight
     0x02	Cloudy
     0x03	Shade
     0x04	Fluorescent_L
     0x05	Fluorescent_H
     0x06	Tungsten
     0x07	Reserved
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_WhiteBalanceSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_WHITE_BALANCE, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_AEModeSet
 *  ucParam:
     0x00	Multi
     0x01	Center
     0x02	Spot
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_AEModeSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_AE_MODE, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CapModeSet
 *  ucParam:
     0x00	Single
     0x01	HDR
     0x02	Reserved
     0x03	Burst capture(Unlimited)(ZSL)
     0x04	Burst capture(Unlimited)(mode switch)
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CapModeSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CAP_MODE, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_ISOSet
 *  ucParam:
     0x00	Auto
     0x01	ISO 50
     0x02	ISO 100
     0x03	ISO 200
     0x04	ISO 400
     0x05	ISO 800
     0x06	ISO 1600
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_ISOSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CAP_ISO, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_AuraColorIndexSet
 *  ucParam : Color index (bit control)
     Effect bit : bit 0~5
     1 : Color enable
     0 : Color disable
     bit 0 : Red
     bit 1 : Orange
     bit 2 : Yellow
     bit 3 : Green
     bit 4 : Blue
     bit 5 : Violet
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_AuraColorIndexSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_AURA_COLOR_INDEX, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_OpticalZoomSet
 *  ucParam:
     0x00	Zoom in
     0x01	Zoom out
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_OpticalZoomSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_OPTICAL_ZOOM, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_IspFuncSet
 *  ucParam: Function control (bit control)
     Effect bit : bit 0~1
     1 : function enable
     0 : function disable
     bit 0 : DWDR
     bit 1 : Edge information
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_IspFuncSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_ISP_FUNCTION, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_PvCapModeSet
 *  ucParam:
     0x00	Preview mode
     0x01	Capture mode(mode switch)
     0x02	Capture mode(ZSL)
     0x03	Capture mode(Non-ZSL)
     0x04	Idle mode
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_PvCapModeSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_PV_CAP_MODE, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_PvStreamSet
 *  ucParam:
     0x00	Stream off
     0x01	Stream on
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_PvStreamSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_PV_STREAM, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_BurstAbortSet
 *  ucParam:
     0x00	Burst abort(mode switch)
     0x01	Burst abort(ZSL)
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_BurstAbortSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_BURST_ABORT, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CapEdgeQuantitySet
 *  ucValue:
     Max = 100
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CapEdgeQuantitySet(UINT8 ucValue)
{
	I2CDataWrite(SP7K_MSG_CAP_EDGE_QUANTITY, ucValue);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_PvFixFrameRateSet
 *  ucValue :
     0x00	Disable Fix Frame Rate function
     0x01~0xFF : Frame Rate
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_PvFixFrameRateSet(UINT8 ucValue)
{
	I2CDataWrite(SP7K_MSG_PV_FIX_FRAME_RATE, ucValue);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_PvMaxExposureTimeSet
 *  ucValue : 1/N
     N=0 : Disable Max. Exposure Time function
     N=0x01~0xFF : Max. Exposure time
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_PvMaxExposureTimeSet(UINT8 ucValue)
{
	I2CDataWrite(SP7K_MSG_PV_MAX_EXPTIME, ucValue);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_HdrPositiveEvSet
 *  ucValue: 2.4bits
     eg. +1.5EV : 0x15
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_HdrPositiveEvSet(UINT8 ucValue)
{
	I2CDataWrite(SP7K_MSG_HDR_POSITIVE_EV, ucValue);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_HdrNegativeEvSet
 *  ucValue: 2.4bits
     eg. -0.4EV : 0x04
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_HdrNegativeEvSet(UINT8 ucValue)
{
	I2CDataWrite(SP7K_MSG_HDR_NEGATIVE_EV, ucValue);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_PvMinISOSet
 *  ucParam:
     0x00	Disable
     0x01	ISO 50
     0x02	ISO 100
     0x03	ISO 200
     0x04	ISO 400
     0x05	ISO 800
     0x06	ISO 1600
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_PvMinISOSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_PV_MIN_ISO, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_GSensorInfoSet
 *  ucX: G sensor information X axis
     ucY: G sensor information Y axis
     ucZ: G sensor information Z axis
     Value: ((-32.768~32.767)*1000)>>8
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_GSensorInfoSet(SINT8 ucX, SINT8 ucY, SINT8 ucZ)
{
	I2CDataWrite(SP7K_MSG_G_SENSOR_INFO_X, ucX);
	I2CDataWrite(SP7K_MSG_G_SENSOR_INFO_Y, ucY);
	I2CDataWrite(SP7K_MSG_G_SENSOR_INFO_Z, ucZ);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_GyroInfoSet
 *  ucX: Gyro information X axis
     ucY: Gyro information Y axis
     ucZ: Gyro information Z axis
     Value: ((Radian : 0~2£k)*1000)>>8
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_GyroInfoSet(UINT8 ucX, UINT8 ucY, UINT8 ucZ)
{
	I2CDataWrite(SP7K_MSG_GYRO_INFO_X, ucX);
	I2CDataWrite(SP7K_MSG_GYRO_INFO_Y, ucY);
	I2CDataWrite(SP7K_MSG_GYRO_INFO_Z, ucZ);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_AFROISet
 *  usSize: AF ROI size
     usX: AF ROI x
     usY: AF ROI y
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_AFROISet(UINT16 usSize, UINT16 usX, UINT16 usY)
{
	I2CDataWrite(SP7K_MSG_AF_ROI_SIZE_H, (usSize>>8)&0x00FF);
	I2CDataWrite(SP7K_MSG_AF_ROI_SIZE_L, usSize&0x00FF);
	I2CDataWrite(SP7K_MSG_AF_ROI_X_H, (usX>>8)&0x00FF);
	I2CDataWrite(SP7K_MSG_AF_ROI_X_L, usX&0x00FF);
	I2CDataWrite(SP7K_MSG_AF_ROI_Y_H, (usY>>8)&0x00FF);
	I2CDataWrite(SP7K_MSG_AF_ROI_Y_L, usY&0x00FF);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_AFROITriggerSet
 *  Input : None
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_AFROITriggerSet(void)
{
	I2CDataWrite(SP7K_MSG_AF_ROI_TRIGGER, 0x01);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_AEROISet
 *  usSize: AE ROI size
     usX: AE ROI x
     usY: AE ROI y
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_AEROISet(UINT16 usSize, UINT16 usX, UINT16 usY)
{
	I2CDataWrite(SP7K_MSG_AE_ROI_SIZE_H, (usSize>>8)&0x00FF);
	I2CDataWrite(SP7K_MSG_AE_ROI_SIZE_L, usSize&0x00FF);
	I2CDataWrite(SP7K_MSG_AE_ROI_X_H, (usX>>8)&0x00FF);
	I2CDataWrite(SP7K_MSG_AE_ROI_X_L, usX&0x00FF);
	I2CDataWrite(SP7K_MSG_AE_ROI_Y_H, (usY>>8)&0x00FF);
	I2CDataWrite(SP7K_MSG_AE_ROI_Y_L, usY&0x00FF);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_AEROITriggerSet
 *  ucParam:
     0x00	TAE off
     0x01	TAE on (Use TAE ROI : 0x48~0x4D)
     0x02	TAE on (Use TAF ROI : 0x40~0x45)
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_AEROITriggerSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_AE_ROI_TRIGGER, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_AFAbortSet
 *  ucParam:
     0x00	AF abort and go to infinity
     0x01	AF abort and stop in the current position
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_AFAbortSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_AF_ABORT, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_VcmStepSet
 *  usValue : VCM step value (0~1023)
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_VcmStepSet(UINT16 usValue)
{
	I2CDataWrite(SP7K_MSG_VCM_STEP_L, usValue&0x00FF);
	I2CDataWrite(SP7K_MSG_VCM_STEP_H, (usValue>>8)&0x00FF);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CapEdgeInfoSet
 *  ucParam:
     0x00	None
     0x01	Ask for capture edge
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CapEdgeInfoSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CAP_EDGE_INFO, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_RawPathSet
 *  ucParam:
     0x00	After Front
     0x01	After WB
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_RawPathSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_RAW_PATH, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_RawFormatSet
 *  ucParam:
     0x00	RAW8
     0x01	RAW10
     0x02	RAW12
     0x03	YUV8
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_RawFormatSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_RAW_FORMAT, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationRawLinearSet
 *  ucParam:
     0x00	Off
     0x01	On
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationRawLinearSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_RAW_LINEAR, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationRawObSet
 *  ucParam:
     0x00	Off
     0x01	On
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationRawObSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_RAW_OB, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationRawBpSet
 *  ucParam:
     0x00	Off
     0x01	On
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationRawBpSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_RAW_BP, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationRawLscSet
 *  ucParam:
     0x00	Off
     0x01	On
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationRawLscSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_RAW_LSC, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationRawCaSet
 *  ucParam:
     0x00	Off
     0x01	On
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationRawCaSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_RAW_CA, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationRawArdSet
 *  ucParam:
     0x00	Off
     0x01	On
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationRawArdSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_RAW_ARD, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationRawDepeakSet
 *  ucParam:
     0x00	Off
     0x01	On
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationRawDepeakSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_RAW_DEPEAK, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationRawWbSet
 *  ucParam:
     0x00	Off
     0x01	On
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationRawWbSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_RAW_WB, ucParam);
}

/*-------------------------------------------------------------------------
 *  File Name : EXISP_I2C_CalibrationAwbCriteriaSet
 *  ucVaule: 1~100, default 10%
 *  return none
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationAwbCriteriaSet(UINT8 ucVaule)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_AWB_CRITERIA, ucVaule);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_AEAWBThreadModeSet
 *  ucParam:
     0x00	AE/AWB thread off
     0x01	AE/AWB thread on
     0x02	AWB thread off
     0x03	AWB thread on
     0x04	AE thread off
     0x05	AE thread on
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_AEAWBThreadModeSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_AEAWBTHREAD_MODE, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_DqModeSet
 *  ucParam:
     0x00	Pause on
     0x01	Pause off
     0x02	Interpolation Off
     0x03	Interpolation On
     0x04	Fix Color Temperature On
     0x05	Fix Color Temperature Off
     0x06	Fix ISO On(Reserved)
     0x07	Fix ISO Off(Reserved)
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_DqModeSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_DQ_MODE, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CdspOnOffModeSet
 *  ucParam:
     0x00	CDSP all block off
     0x01	CDSP all block recovery
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CdspOnOffModeSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CDSP_MODE, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationCmd1Set
 *  ucParam:
     bit 0 : Reserved
     bit 1 : OB calibration(Reserved)
     bit 2 : WB calibration
     bit 3 : AF calibration(Reserved)
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationCmd1Set(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_CMD1, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationCmd2Set
 *  ucParam:
     bit 0 : LSC_D50 calibration
     bit 1 : LSC_A calibration
     bit 2 : LSC_CW calibration
     bit 3 : LSC_TL84 calibration
     bit 4 : LSC_D65 calibration
     bit 5 : LSC_H calibration
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationCmd2Set(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_CMD2, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationInitCmdSet
 *  ucParam:
     bit 0, 1: Calibration data target
     0x01	HOST
     0x02	EEPROM
     0x03	SPI
     bit 2, 3: Band selection
     0x00	50Hz
     0x04	60Hz
     bit 4, 5: LSC compression method
     0x00	Differential
     0x10	Non-compression
     bit 6, 7: LSC table number
     0x00	4 table
     0x40	5 table
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationInitCmdSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_INIT_CMD, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationLscMaxGainSet
 *  ucVaule : 6.2bits
     Range : 0x00~0xFF
     0x01	0.25%
     0x02	0.50%
     0x03	0.75%
     0xFF	63.75%
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationLscMaxGainSet(UINT8 ucVaule)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_LENS_MAX_LUM_DECAY, ucVaule);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationDqIndexFixSet
 *  ucParam:
     bit 0~3: Color Temperature Index
     0x00	LSC off
     0x01	SPI flash is 1MB to load golden LSC & LSC_DQ table, auto
     0x02	SPI flash is 1MB to load golden LSC & LSC_DQ table, fix A light table
     0x03	SPI flash is 1MB to load golden LSC & LSC_DQ table, fix TL84 table
     0x04	SPI flash is 1MB to load golden LSC & LSC_DQ table, fix CW table
     0x05	SPI flash is 1MB to load golden LSC & LSC_DQ table, fix D50 table
     0x06	SPI flash is 1MB to load golden LSC & LSC_DQ table, fix D65 table
     0x07	SPI flash is 1MB to load golden LSC & LSC_DQ table, fix H table
     bit 4~7: ISO index(0~5) (Reserved)
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CalibrationDqIndexFixSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_DQ_INDEX_FIX, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CaliUtilizationOptionSet
 *  ucParam:
     bit 0: Rear Camera(3ACALI)
     0 : Use Calubration data(default)
     1 : Use Golden data
     bit 1: Rear Camera(LSC)
     0 : Use Calubration data(default)
     1 : Use Golden data
     bit 2: Front Camera(3ACALI)
     0 : Use Calubration data(default)
     1 : Use Golden data
     bit 3: Front Camera(LSC)
     0 : Use Calubration data(default)
     1 : Use Golden data
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_CaliUtilizationOptionSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_CALIBRATION_UTILIZATION_OPTION, ucParam);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_ROISwitchSet
 *  ucParam:
     0x00	ROI off
     0x01	ROI on
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_ROISwitchSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_ROI_SWITCH, ucParam);
}


/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_VendreqIdSet
 *  ucValue: vendor command ID
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_VendreqIdSet(UINT8 ucValue)
{
	I2CDataWrite(SP7K_MSG_VENDREQ_ID, ucValue);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_VendreqParamSet
 *  ucValue: vendor command parameter
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_VendreqParamSet(UINT8 ucValue)
{
	I2CDataWrite(SP7K_MSG_VENDREQ_PARAM, ucValue);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_VendreqValuesSet_L
 *  ucVaule: vendor command the low byte of value
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_VendreqValuesSet_L(UINT8 ucVaule)
{
	I2CDataWrite(SP7K_MSG_VENDREQ_VALUES_L, ucVaule);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_VendreqProcessSet
 *  Input : None
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_VendreqProcessSet(void)
{
	I2CDataWrite(SP7K_MSG_VENDREQ_PROCESS, 0x00);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_VendreqValuesSet_H
 *  ucVaule: vendor command the high byte of value
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_VendreqValuesSet_H(UINT8 ucVaule)
{
	I2CDataWrite(SP7K_MSG_VENDREQ_VALUES_H, ucVaule);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_VendreqCmdSet
 *  ucParam:
     0x00	Reserved
     0x01	Reserved
     0x02	Reserved
     0x03	AE lock
     0x04	AWB lock
     0x05	AE unlock
     0x06	AWB unlock
 *  Return : None
 *------------------------------------------------------------------------*/
void EXISP_I2C_VendreqCmdSet(UINT8 ucParam)
{
	I2CDataWrite(SP7K_MSG_VENDREQ_CMD, ucParam);
}


/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_AFStatusGet
 *  Input : None
 *  Return : AF status
     0x00	Idle
     0x01	Busy
 *------------------------------------------------------------------------*/
UINT8 EXISP_I2C_AFStatusGet(void)
{
	return I2CDataRead(SP7K_RDREG_AF_STATUS);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_AFResultGet
 *  Input : None
 *  Return : AF result
     0x00	Focus success
     0x01	Focus fail
 *------------------------------------------------------------------------*/
UINT8 EXISP_I2C_AFResultGet(void)
{
	return I2CDataRead(SP7K_RDREG_AF_RESULT);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_AFConsumeTimeGet
 *  Input : None
 *  Return : AF consuming time (ms)
 *------------------------------------------------------------------------*/
UINT16 EXISP_I2C_AFConsumeTimeGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_AF_CONSUME_TIME_H)<<8) |
			 I2CDataRead(SP7K_RDREG_AF_CONSUME_TIME_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_VcmCurrentPosGet
 *  Input : None
 *  Return : VCM current position (Range 0~1023)
 *------------------------------------------------------------------------*/
UINT16 EXISP_I2C_VcmCurrentPosGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_VCM_CURRENT_POS_H)<<8) |
			 I2CDataRead(SP7K_RDREG_VCM_CURRENT_POS_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_ExposureTimeNumeratorGet
 *  Input : None
 *  Return : exposure time numerator
 *------------------------------------------------------------------------*/
UINT8 EXISP_I2C_ExposureTimeNumeratorGet(void)
{
	return I2CDataRead(SP7K_RDREG_EXPTIME_NUMERATOR);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_ExposureTimeDenominatorGet
 *  Input : None
 *  Return : exposure time denominator
 *------------------------------------------------------------------------*/
UINT32 EXISP_I2C_ExposureTimeDenominatorGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_EXPTIME_DENOMINATOR_H)<<16) |
			(I2CDataRead(SP7K_RDREG_EXPTIME_DENOMINATOR_M)<<8) |
			 I2CDataRead(SP7K_RDREG_EXPTIME_DENOMINATOR_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_ExposureTimeCompensationGet
 *  Input : None
 *  Return : exposure time compensation
 *------------------------------------------------------------------------*/
UINT8 EXISP_I2C_ExposureTimeCompensationGet(void)
{
	return I2CDataRead(SP7K_RDREG_EXPTIME_COMPENSATION);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_LensFocalLengthGet
 *  Input : None
 *  Return : lens focal length
 *------------------------------------------------------------------------*/
UINT16 EXISP_I2C_LensFocalLengthGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_LENS_FOCAL_LENGTH_H)<<8) |
			 I2CDataRead(SP7K_RDREG_LENS_FOCAL_LENGTH_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_ISOValueGet
 *  Input : None
 *  Return : ISO value
 *------------------------------------------------------------------------*/
UINT16 EXISP_I2C_ISOValueGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_ISO_H)<<8) |
			 I2CDataRead(SP7K_RDREG_ISO_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_FlashModeGet
 *  Input : None
 *  Return : flash mode
     0x00	Flash off
     0x01	Flash on
 *------------------------------------------------------------------------*/
UINT8 EXISP_I2C_FlashModeGet(void)
{
	return I2CDataRead(SP7K_RDREG_FLASH_MODE);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CapEdgeInfoGet
 *  Input : None
 *  Return : capture edge information
 *------------------------------------------------------------------------*/
UINT32 EXISP_I2C_CapEdgeInfoGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_CAP_EDGE_INFO_B3)<<24) |
			(I2CDataRead(SP7K_RDREG_CAP_EDGE_INFO_B2)<<16) |
			(I2CDataRead(SP7K_RDREG_CAP_EDGE_INFO_B1)<<8) |
			 I2CDataRead(SP7K_RDREG_CAP_EDGE_INFO_B0));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_3AStatusGet
 *  Input : None
 *  Return : 3A status
     bit 0: AE ready bit
     0 : AE not ready
     1 : AE ready
     bit 1: AWB ready bit
     0 : AWB not ready
     1 : AWB ready
     bit 2: flash info
     0 : Do not flash fire
     1 : Do flash fire
 *------------------------------------------------------------------------*/
UINT8 EXISP_I2C_3AStatusGet(void)
{
	return I2CDataRead(SP7K_RDREG_3A_STATUS);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_EdgeInfoCountGet
 *  Input : None
 *  Return : edge information count
 *------------------------------------------------------------------------*/
UINT8 EXISP_I2C_EdgeInfoCountGet(void)
{
	return I2CDataRead(SP7K_RDREG_EDGE_INFO_COUNT);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_RearSensorIdGet
 *  Input : None
 *  Return : rear sensor id
 *------------------------------------------------------------------------*/
UINT16 EXISP_I2C_RearSensorIdGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_REAR_SENSORID_H)<<8) |
			 I2CDataRead(SP7K_RDREG_REAR_SENSORID_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_FrontSensorIdGet
 *  Input : None
 *  Return : front sensor id
 *------------------------------------------------------------------------*/
UINT16 EXISP_I2C_FrontSensorIdGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_FRONT_SENSORID_H)<<8) |
			 I2CDataRead(SP7K_RDREG_FRONT_SENSORID_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_FWVersionGet
 *  Input : None
 *  Return : FW version
 *------------------------------------------------------------------------*/
UINT32 EXISP_I2C_FWVersionGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_FW_VER_H)<<16) |
			(I2CDataRead(SP7K_RDREG_FW_VER_M)<<8) |
			 I2CDataRead(SP7K_RDREG_FW_VER_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_VendorIdGet
 *  Input : None
 *  Return : vendor ID (0x7002)
 *------------------------------------------------------------------------*/
UINT16 EXISP_I2C_VendorIdGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_VENDOR_ID_H)<<8) |
			 I2CDataRead(SP7K_RDREG_VENDOR_ID_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationLscMaxRGainGet
 *  Input : None
 *  Return : the max R gain after LSC calibration
 *------------------------------------------------------------------------*/
UINT16 EXISP_I2C_CalibrationLscMaxRGainGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_CALIBRATION_LSC_MAX_RGAIN_H)<<8) |
			 I2CDataRead(SP7K_RDREG_CALIBRATION_LSC_MAX_RGAIN_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationLscMaxGGainGet
 *  Input : None
 *  Return : the max G gain after LSC calibration
 *------------------------------------------------------------------------*/
UINT16 EXISP_I2C_CalibrationLscMaxGGainGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_CALIBRATION_LSC_MAX_GGAIN_H)<<8) |
			 I2CDataRead(SP7K_RDREG_CALIBRATION_LSC_MAX_GGAIN_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationLscMaxBGainGet
 *  Input : None
 *  Return : the max B gain after LSC calibration
 *------------------------------------------------------------------------*/
UINT16 EXISP_I2C_CalibrationLscMaxBGainGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_CALIBRATION_LSC_MAX_BGAIN_H)<<8) |
			 I2CDataRead(SP7K_RDREG_CALIBRATION_LSC_MAX_BGAIN_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationAWBRGainGet
 *  Input : None
 *  Return : the R gain after AWB calibration
 *------------------------------------------------------------------------*/
UINT16 EXISP_I2C_CalibrationAWBRGainGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_CALIBRATION_AWB_RGain_H)<<8) |
			 I2CDataRead(SP7K_RDREG_CALIBRATION_AWB_RGain_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationAWBGGainGet
 *  Input : None
 *  Return : the G gain after AWB calibration
 *------------------------------------------------------------------------*/
UINT16 EXISP_I2C_CalibrationAWBGGainGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_CALIBRATION_AWB_GGain_H)<<8) |
			 I2CDataRead(SP7K_RDREG_CALIBRATION_AWB_GGain_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationAWBBGainGet
 *  Input : None
 *  Return : the B gain after AWB calibration
 *------------------------------------------------------------------------*/
UINT16 EXISP_I2C_CalibrationAWBBGainGet(void)
{
	return ((I2CDataRead(SP7K_RDREG_CALIBRATION_AWB_BGain_H)<<8) |
			 I2CDataRead(SP7K_RDREG_CALIBRATION_AWB_BGain_L));
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationExecStatus1Get
 *  Input : None
 *  Return : OB/WB/AF calibration execution status
 *------------------------------------------------------------------------*/
UINT8 EXISP_I2C_CalibrationExecStatus1Get(void)
{
	return I2CDataRead(SP7K_RDREG_CALIBRATION_EXEC_STATUS1);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationExecStatus2Get
 *  Input : None
 *  Return : LSC_D50/LSC_A/LSC_CW/LSC_TL84/LSC_D65/LSC_H calibration execution status
 *------------------------------------------------------------------------*/
UINT8 EXISP_I2C_CalibrationExecStatus2Get(void)
{
	return I2CDataRead(SP7K_RDREG_CALIBRATION_EXEC_STATUS2);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationErrorStatus1Get
 *  Input : None
 *  Return : OB/WB/AF calibration error status
 *------------------------------------------------------------------------*/
UINT8 EXISP_I2C_CalibrationErrorStatus1Get(void)
{
	return I2CDataRead(SP7K_RDREG_CALIBRATION_ERROR_STATUS1);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationErrorStatus2Get
 *  Input : None
 *  Return : LSC_D50/LSC_A/LSC_CW/LSC_TL84/LSC_D65/LSC_H calibration error status
 *------------------------------------------------------------------------*/
UINT8 EXISP_I2C_CalibrationErrorStatus2Get(void)
{
	return I2CDataRead(SP7K_RDREG_CALIBRATION_ERROR_STATUS2);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationErrorCodeGet
 *  Input : None
 *  Return : calibration error code
 *------------------------------------------------------------------------*/
UINT8 EXISP_I2C_CalibrationErrorCodeGet(void)
{
	return I2CDataRead(SP7K_RDREG_CALIBRATION_ERROR_CODE);
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_I2C_CalibrationLoadTableStatusGet
 *  Input : None
 *  Return : load table status
 *------------------------------------------------------------------------*/
UINT8 EXISP_I2C_CalibrationLoadTableStatusGet(void)
{
	return I2CDataRead(SP7K_RDREG_CALIBRATION_LOAD_TABLE_STATUS);
}


/*-------------------------------------------------------------------------
 *  Function Name : EXISP_SuspendGpioCfgSet
 *  Description : Suspend GPIO configuration
 *  ucCtl:
     0x00	GPIO_CTL_LOW(GPIO pull low)
     0x01	GPIO_CTL_HIGH(GPIO pull high)
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_SuspendGpioCfgSet(UINT8 ucCtl)
{
	UINT8 ucRet = SUCCESS;

	if (ucCtl >= GPIO_CTL_MAX) {
		ucRet = FAIL;
		return ucRet;
	}

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_DigitalPowerCfgSet
 *  Description : Provide digital power to external ISP
     On:
     0x00	Close
     0x01	Open
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_DigitalPowerCfgSet(UINT8 On)
{
	UINT8 ucRet = SUCCESS;

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_AnalogPowerCfgSet
 *  Description : Provide analog power to external ISP
     On:
     0x00	Close
     0x01	Open
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_AnalogPowerCfgSet(UINT8 On)
{
	UINT8 ucRet = SUCCESS;

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_ClockCfgSet
 *  Description : Provide clock or not to external ISP
     ucEnable:
     0x00	Don't provide clock
     0x01	Provide clock
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_ClockCfgSet(UINT8 ucEnable)
{
	UINT8 ucRet = SUCCESS;

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_ResetGpioCfgSet
 *  Description : Reset GPIO configuration
 *  ucCtl:
     0x00	GPIO_CTL_LOW(GPIO pull low)
     0x01	GPIO_CTL_HIGH(GPIO pull high)
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_ResetGpioCfgSet(UINT8 ucCtl)
{
	UINT8 ucRet = SUCCESS;

	if (ucCtl >= GPIO_CTL_MAX) {
		ucRet = FAIL;
		return ucRet;
	}

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_PowerOn
 *  Description : Power on external ISP
 *  ucBoot:
     0x00	BOOT_FROM_SPI(Boot from SPI)
     0x01	BOOT_FROM_HOST(Boot from host)
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_PowerOn(UINT8 ucBoot)
{
	UINT8 ucRet = SUCCESS;

	/* 1. Open digital power, 1.2V */
	ucRet = EXISP_DigitalPowerCfgSet(0x01);	/* 2. Open analog power, 1.8V and 2.8V */
	ucRet = EXISP_AnalogPowerCfgSet(0x01);
	/* 3. Open clock */
	ucRet = EXISP_ClockCfgSet(0x01);
	/* 4. Suspend GPIO configuration */
	if (ucBoot == BOOT_FROM_SPI) {
		ucRet = EXISP_SuspendGpioCfgSet(GPIO_CTL_HIGH);
	}
	else if (ucBoot == BOOT_FROM_HOST) {
		ucRet = EXISP_SuspendGpioCfgSet(GPIO_CTL_LOW);
	}
	else {
		ucRet = FAIL;
		return ucRet;
	}
	/* 5. Reset GPIO configuration */
	ucRet = EXISP_ResetGpioCfgSet(GPIO_CTL_LOW);
	/* Delay 10ms */
	tmrUsDelay(10000);
	/* 6. Reset GPIO configuration */
	ucRet = EXISP_ResetGpioCfgSet(GPIO_CTL_HIGH);
	/* Delay 16ms */
	tmrUsDelay(16000);
	/* 7. Suspend GPIO configuration for boot from SPI */
	if (ucBoot == BOOT_FROM_SPI) {
		ucRet = EXISP_SuspendGpioCfgSet(GPIO_CTL_LOW);
	}

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_PowerOff
 *  Description : Power off external ISP
 *  Input : None
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_PowerOff(void)
{
	UINT8 ucRet = SUCCESS;

	/* 1. Reset GPIO configuration */
	ucRet = EXISP_ResetGpioCfgSet(GPIO_CTL_LOW);
	/* 2. Close clock */
	ucRet = EXISP_ClockCfgSet(0x00);
	/* 3. Close analog power, 2.8V and 1.8V */
	ucRet = EXISP_AnalogPowerCfgSet(0x00);
	/* 4. Close digital power, 1.2V */
	ucRet = EXISP_DigitalPowerCfgSet(0x00);

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_LoadCodeStart
 *  Description : Load code start
 *  ucBoot:
     0x00	BOOT_FROM_SPI(Boot from SPI)
     0x01	BOOT_FROM_HOST(Boot from host)
     ucFwIdx: Set which FW will be loaded
     is_calibration: calibration flag, calibration:1 normal:0
     *pIspFw: external ISP FW pointer
     *pCalibOpt: read from calibration_option.BIN
     *p3acali: read from 3ACALI.BIN or 3ACALI_F.BIN
     *pLsc: read from LSC.BIN or LSC_F.BIN
     *pLscdq: read from LSC_DQ.BIN or LSC_DQ_F.BIN
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_LoadCodeStart(
	UINT8 ucBoot,
	UINT8 ucFwIdx,
	UINT8 is_calibration,
	UINT8 *pIspFw,
	UINT8 *pCalibOpt,
	UINT8 *p3acali,
	UINT8 *pLsc,
	UINT8 *pLscdq)
{
	UINT8 ucRet = SUCCESS;

	if (ucBoot >= BOOT_FROM_MAX || ucFwIdx > 2) {
		ucRet = FAIL;		return ucRet;
	}

	DEBUG_TRACE("@@@ISP FW: Load Code Start\n");
	if (ucBoot == BOOT_FROM_SPI) {
		I2CDataWrite(0x1011, 1); /* CPU reset */
		/* Reset flash module */
		I2CDataWrite(0x001C, 0x08); /* reset FM */
		I2CDataWrite(0x001C, 0x00);
		I2CDataWrite(0x1010, 0x02);
		I2CDataWrite(0x1010, 0x00);

		I2CDataWrite(0x1306, ucFwIdx); /* Set which FW will be loaded */
		I2CDataWrite(0x1011, 0);
	}
	else if (ucBoot == BOOT_FROM_HOST) {
		ucRet = EXISP_LoadCode(ucFwIdx, is_calibration, pIspFw, pCalibOpt, p3acali, pLsc, pLscdq);
		I2CDataWrite(0x1306, ucFwIdx); /* Set which FW to be loaded */
	}
	DEBUG_TRACE("@@@ISP FW: Load Code End, sts=%d\n", ucRet);

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_LoadCode
 *  Description : Load code from host, support boot from host only
 *  ucFwIdx: Set which FW will be loaded
     is_calibration: calibration flag, calibration:1 normal:0
     *pIspFw: external ISP FW pointer
     *pCaliOpt: read from calibration_option.BIN
     *p3acali: read from 3ACALI.BIN or 3ACALI_F.BIN
     *pLsc: read from LSC.BIN or LSC_F.BIN
     *pLscdq: read from LSC_DQ.BIN or LSC_DQ_F.BIN
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_LoadCode(
	UINT8 ucFwIdx,
	UINT8 is_calibration,
	UINT8 *pIspFw,
	UINT8 *pCalibOpt,
	UINT8 *p3acali,
	UINT8 *pLsc,
	UINT8 *pLscdq)
{
	UINT8 ucRet = SUCCESS;
	UINT32 t1=0, t2=0, t3=0, tmrCnt=0, i=0;
	UINT32 checksumWrite=0, checksumRead=0;
	ispFwHeaderInfo_t *pFwInfo;
	ispLoadCodeRet_t loadCodeRet;

	if (pIspFw == NULL) {
		return LOADCODE_BOOT_FILE_ERR;
	}

	pFwInfo = (ispFwHeaderInfo_t *)pIspFw;
	for (i = 0; i < ucFwIdx; i++) {
		pIspFw += FW_HEADER_SIZE+pFwInfo->DmemFicdmemSize+pFwInfo->ImemSize;
		pFwInfo = (ispFwHeaderInfo_t *)pIspFw;
	}

	/* Modify length to 16-alignment */
	pFwInfo->DmemFicdmemSize = (pFwInfo->DmemFicdmemSize+15)&0xFFFFFFF0;
	pFwInfo->ImemSize = (pFwInfo->ImemSize+15)&0xFFFFFFF0;
	//DEBUG_TRACE(" %s, %d, DmemFicdmemSize=%d\n", __FUNCTION__, __LINE__, pFwInfo->DmemFicdmemSize);
	//DEBUG_TRACE(" %s, %d, ImemSize=%d\n", __FUNCTION__, __LINE__, pFwInfo->ImemSize);
	//DEBUG_TRACE("0.pIspFw =%x\n", pIspFw);
	DEBUG_TRACE("@@@ISP FW: Get BOOT.BIN Bin File End\n");

#if 1 //update golden or calibration resource
	DEBUG_TRACE("@@@ISP FW: Update Resource Start\n");
	if (is_calibration == 1) { //calibration
		//Nothing to do
	}
	else { //normal
		memset(&loadCodeRet, 0, sizeof(ispLoadCodeRet_t));

		/* pCalibOpt check */
		if (pCalibOpt == NULL) {
			loadCodeRet.retCalibOpt= LOADCODE_CALIB_OPT_FILE_ERR;
			goto _EXIT_;
		}
		/* p3acali check */
		if (p3acali == NULL) {
			loadCodeRet.ret3acli= LOADCODE_3ACALI_FILE_ERR;
		}
		/* pLsc check */
		if (pLsc == NULL) {
			loadCodeRet.retLsc= LOADCODE_LSC_FILE_ERR;
		}
		/* pLscdq check */
		if (pLscdq == NULL) {
			loadCodeRet.retLscdq = LOADCODE_LSC_DQ_FILE_ERR;
		}

		EXISP_UpdateCalibResStart(ucFwIdx, pIspFw, pFwInfo, &loadCodeRet, *pCalibOpt, p3acali, pLsc, pLscdq);
	}
	DEBUG_TRACE("@@@ISP FW: Update Resource End\n");
#endif

_EXIT_:
	if (is_calibration == 1) { //calibration
		DEBUG_TRACE("********** Load Golden Res **********\n");
	}
	else { //normal
		if (loadCodeRet.retCalibOpt == SUCCESS &&
			loadCodeRet.ret3acli == SUCCESS &&
			loadCodeRet.retLsc == SUCCESS &&
			loadCodeRet.retLscdq == SUCCESS) {
			DEBUG_TRACE("********** Load Calibration Res **********\n");
		}
		else {
			if (loadCodeRet.retCalibOpt != SUCCESS) {
				DEBUG_TRACE("********** Load Golden Res, retCalibOpt=%d **********\n", loadCodeRet.retCalibOpt);
			}
			else if (loadCodeRet.ret3acli != SUCCESS) {
				DEBUG_TRACE("********** Load Golden 3ACALI Res, ret3acli=%d **********\n", loadCodeRet.ret3acli);
			}
			else if (loadCodeRet.retLsc != SUCCESS || loadCodeRet.retLscdq == SUCCESS) {
				DEBUG_TRACE("********** Load Golden LSC Res, retLsc=%d, retLscdq=%d **********\n", loadCodeRet.retLsc, loadCodeRet.retLscdq);
			}
		}
	}

	/* CPU reset */
	I2CDataWrite(0x1011, 1);

	/* Set imemmode*/
	t1 = pFwInfo->ImemSize/8192;
	t2 = t1-32;
	t3 = 1;
	if ((int)t2 < 0) {
		t1 = t3 << t1;
		--t1;
	}
	else {
		t3 <<= t2;
		t1 = -1U;
	}
	--t3;
	I2CDataWrite(0x1070, t1);
	I2CDataWrite(0x1071, t1>>8);
	I2CDataWrite(0x1072, t1>>16);
	I2CDataWrite(0x1073, t1>>24);
	I2CDataWrite(0x1074, t3);

	/* @@@ Start load code to SPCA7002 */

	/* Enable checksum mechanism */
	I2CDataWrite(0x4280, 1);

	/* Wait Ready For Load Code interrupt */
	while (!(I2CDataRead(SP7K_RDREG_INT_STS_REG_0)&0x02)) {
		tmrCnt++;
		if (tmrCnt >= 10) {
			DEBUG_TRACE("@@@ISP FW: polling RFLC bit timeout\n");
			ucRet = FAIL;			return ucRet;
		}
		tmrUsDelay(10000);
	}
	I2CDataWrite(SP7K_RDREG_INT_STS_REG_0, 0x02);

	/* Load DMEM/FICDMEM bin file Start */
	DEBUG_TRACE("@@@ISP FW: Load DMEM/FICDMEM Bin File Start\n");
	pIspFw += FW_HEADER_SIZE;
	/* Reset checksum value */
	I2CDataWrite(0x4284, 0x00);
	checksumWrite = 0;
	for (i = 0; i < pFwInfo->DmemFicdmemSize; i++) {
		checksumWrite += pIspFw[i];
	}
	checksumWrite &= 0xFF;

	/* Transmit DMEM/FICDMEM data */
	if(pFwInfo->DmemFicdmemSize <= 6*1024) { /* Data size <6K, load all bin file */
		ucRet = EXISP_SPIDataWrite(0/*SPI mode0*/, pIspFw, pFwInfo->DmemFicdmemSize, 0x0800);
		I2CDataWrite(0x1011, 0); /* Set CPU to normal operation */
	}
	else {
		ucRet = EXISP_SPIDataWrite(0/*SPI mode0*/, pIspFw, 6*1024, 0x0800);
		I2CDataWrite(0x1011, 0); /* Set CPU to normal operation */
		ucRet = EXISP_SPIDataWrite(0/*SPI mode0*/, pIspFw+(6*1024), pFwInfo->DmemFicdmemSize-(6*1024), 0x0800+(6*1024));
	}

	/* Checksum value check */
	checksumRead = I2CDataRead(0x4284);
	if (checksumWrite == checksumRead) {
		DEBUG_TRACE("@@@ISP FW: Checksum DMEM/FICDMEM test: OK, %x, %x\n", checksumRead, checksumWrite);
	}
	else {
		DEBUG_TRACE("@@@ISP FW: Checksum DMEM/FICDMEM test: FAIL, %x, %x\n", checksumRead, checksumWrite);
		ucRet = FAIL;		return ucRet;
	}
	DEBUG_TRACE("@@@ISP FW: Load DMEM/FICDMEM Bin File End\n");
	/* Load DMEM/FICDMEM bin file End */

	/* Load IMEM bin file Start */
	DEBUG_TRACE("@@@ISP FW: Load IMEM Bin File Start\n");
	pIspFw += pFwInfo->DmemFicdmemSize;
	/* Reset checksum value */
	I2CDataWrite(0x4284, 0x00);
	checksumWrite = 0;
	for (i = 0; i < pFwInfo->ImemSize; i++) {
		checksumWrite += pIspFw[i];
	}
	checksumWrite &= 0xFF;

	/* Transmit IMEM data */
	ucRet = EXISP_SPIDataWrite(0/*SPI mode0*/, pIspFw, pFwInfo->ImemSize, (320*1024)-pFwInfo->ImemSize);

	/* Checksum value check */
	checksumRead = I2CDataRead(0x4284);
	if (checksumWrite == checksumRead) {
		DEBUG_TRACE("@@@ISP FW: Checksum IMEM test: OK, %x, %x\n", checksumRead, checksumWrite);
	}
	else {
		DEBUG_TRACE("@@@ISP FW: Checksum IMEM test: FAIL, %x, %x\n", checksumRead, checksumWrite);
		ucRet = FAIL;		return ucRet;
	}
	DEBUG_TRACE("@@@ISP FW: Load IMEM Bin File End\n");
	/* Load IMEM bin file End */

	/* @@@ End load code to SPCA7002 */

	/* Disable checksum mechanism */
	I2CDataWrite(0x4280, 0);

	/* Write load code end register */
	I2CDataWrite(0x1307, 0xA5);

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_UpdateCalibResStart
 *  Description : Update calibration data start
 *  ucFwIdx: Set which FW will be loaded
     *pIspFw: external ISP FW pointer
     *pFwInfo: external ISP FW header information
     *pLoadCodeRet: load code status result
     ucCaliOpt: read from calibration_option.BIN
     *p3acali: read from 3ACALI.BIN or 3ACALI_F.BIN
     *pLsc: read from LSC.BIN or LSC_F.BIN
     *pLscdq: read from LSC_DQ.BIN or LSC_DQ_F.BIN
 *  Return : none
 *------------------------------------------------------------------------*/
void EXISP_UpdateCalibResStart(
	UINT8 ucFwIdx,
	UINT8 *pIspFw,
	ispFwHeaderInfo_t *pFwInfo,
	ispLoadCodeRet_t *pLoadCodeRet,
	UINT8 ucCaliOpt,
	UINT8 *p3acali,
	UINT8 *pLsc,
	UINT8 *pLscdq)
{
	//DEBUG_TRACE("1.pIspFw =%x\n", pIspFw);

	if ((ucFwIdx == 0) && (ucCaliOpt != 0xFF)) { //rear sensor
		ucCaliOpt = ucCaliOpt & 0x03;
	}
	else if ((ucFwIdx == 1) && (ucCaliOpt != 0xFF)){ //front sensor
		ucCaliOpt = ucCaliOpt >> 2;
	}

	switch (ucCaliOpt) {
	case 1: /* load golden 3ACALI.BIN and calibrated LSC.BIN & LSC_DQ.BIN */
		if (pLsc == NULL || pLscdq == NULL) {
			pLoadCodeRet->retLsc = LOADCODE_LSC_FILE_ERR;
			pLoadCodeRet->retLscdq = LOADCODE_LSC_DQ_FILE_ERR;
			break;
		}
		pLoadCodeRet->retLsc = EXISP_UpdateCalibRes(1, pIspFw, pFwInfo, p3acali, pLsc, pLscdq);
		pLoadCodeRet->retLscdq = pLoadCodeRet->retLsc;
		break;
	case 2: /* load calibrated 3ACALI.BIN and golden LSC.BIN & LSC_DQ.BIN */
		if (p3acali == NULL) {
			pLoadCodeRet->ret3acli = LOADCODE_3ACALI_FILE_ERR;
			break;
		}
		pLoadCodeRet->ret3acli = EXISP_UpdateCalibRes(0, pIspFw, pFwInfo, p3acali, pLsc, pLscdq);
		break;
	case 3: /* load golden 3ACALI.BIN and golden LSC.BIN & LSC_DQ.BIN */
		//Nothing to do
		break;
	default: /* load calibrated 3ACALI.BIN and calibrated LSC.BIN & LSC_DQ.BIN */
		if (p3acali == NULL) {
			pLoadCodeRet->ret3acli = LOADCODE_3ACALI_FILE_ERR;
		}
		if (pLoadCodeRet->ret3acli == SUCCESS) {
				pLoadCodeRet->ret3acli = EXISP_UpdateCalibRes(0, pIspFw, pFwInfo, p3acali, pLsc, pLscdq);
		}

		if (pLoadCodeRet->ret3acli == LOADCODE_GET_RES_NUM_ERR) {
			break;
		}
		else if (pLsc == NULL || pLscdq == NULL) {
			pLoadCodeRet->retLsc = LOADCODE_LSC_FILE_ERR;
			pLoadCodeRet->retLscdq = LOADCODE_LSC_DQ_FILE_ERR;
			break;
		}
		pLoadCodeRet->retLsc = EXISP_UpdateCalibRes(1, pIspFw, pFwInfo, p3acali, pLsc, pLscdq);
		pLoadCodeRet->retLscdq = pLoadCodeRet->retLsc;
		break;
	}
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_UpdateCalibRes
 *  Description : Update calibration data from host, support boot from host only
 *  idx: Set which resource will be loaded
     *pIspFw: external ISP FW pointer
     *pFwInfo: external ISP FW header information
     *p3acali: read from 3ACALI.BIN or 3ACALI_F.BIN
     *pLsc: read from LSC.BIN or LSC_F.BIN
     *pLscdq: read from LSC_DQ.BIN or LSC_DQ_F.BIN
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_UpdateCalibRes(
	UINT8 idx,
	UINT8 *pIspFw,
	ispFwHeaderInfo_t *pFwInfo,
	UINT8 *p3acali,
	UINT8 *pLsc,
	UINT8 *pLscdq)
{
	UINT32 iqOffset = 0, resNumCnt = 0, iqSize = 0, caliSize = 0, tempSize = 0, i;
	UINT32 start3acali = 0, startLsc = 0, startLscdq = 0, size3acali = 0, sizeLsc = 0, sizeLscdq = 0;
	UINT8 ucRet = SUCCESS;

	/* resource header and checksum check */
	ucRet = EXISP_ResCheck(idx, p3acali, pLsc, pLscdq);
	if (ucRet != SUCCESS) {
		goto _EXIT_;
	}

	/* find out where IQ.BIN is */
	pIspFw += FW_HEADER_SIZE + pFwInfo->DmemSize;
	//DEBUG_TRACE("2.pIspFw =%x\n", pIspFw);
	if (*(pIspFw+0x38) == 0x43 &&
		*(pIspFw+0x39) == 0x41 &&
		*(pIspFw+0x3A) == 0x4C &&
		*(pIspFw+0x3B) == 0x49) {
		iqOffset = ((*(pIspFw+0x51)<<8)&0x0000FF00) + (*(pIspFw+0x50)&0x000000FF);
	}
	else {
		iqOffset = ((*(pIspFw+0x31)<<8)&0x0000FF00) + (*(pIspFw+0x30)&0x000000FF);
	}
	//DEBUG_TRACE("iqOffset=%x\n", iqOffset);

	/* point to IQ.BIN start position */
	pIspFw += iqOffset;
	//DEBUG_TRACE("3.pIspFw =%x\n", pIspFw);

	/* parsing out the file size to get the start position of calibration data,
	FICDMEM file size should be 16 alignment */
	ucRet = EXISP_ResNumGet(&resNumCnt, pIspFw+0x10);
	if (ucRet != SUCCESS) {
		goto _EXIT_;
	}
	//DEBUG_TRACE("resNumCnt=%d\n", resNumCnt);
	//DEBUG_TRACE("5.pIspFw =%x\n", pIspFw);

	for (i = 0; i < resNumCnt; i++) {
		tempSize = *(pIspFw+14+(1+i)*0x10);
		tempSize += ((*(pIspFw+15+((1+i)*0x10)))<<8);
		//DEBUG_TRACE("tempSize=0x%02x\n", tempSize);
		if ((tempSize%0x10) != 0) {
			tempSize = ((tempSize+0xF)>>4)<<4;
		}
		//DEBUG_TRACE("size of IQ BIN files %d: 0x%02x\n", i, tempSize);
		iqSize += tempSize;
	}
	//DEBUG_TRACE("iqSize=%d\n", iqSize);
	//DEBUG_TRACE("6.pIspFw =%x\n", pIspFw);

	/* find out 3ACALI.BIN & LSC.BIN & LSC_DQ.BIN start position */
	start3acali = iqSize+(1+resNumCnt+3)*0x10;
	for (i = 0; i < 3; i++) {
		tempSize = *(pIspFw+14+(1+resNumCnt+i)*0x10);
		tempSize += ((*(pIspFw+15+(1+resNumCnt+i)*0x10))<<8);
		if (i == 0) {
			size3acali = tempSize;
		}
		else if (i == 1){
			sizeLsc = tempSize;
		}
		else {
			sizeLscdq = tempSize;
		}

		if ((tempSize%0x10) != 0) {
			tempSize = ((tempSize+0xF)>>4)<<4;
		}
		//DEBUG_TRACE("0.size of IQ BIN files %d: 0x%02x\n", i, tempSize);
		caliSize += tempSize;
		if (i == 0) {
			startLsc = start3acali + caliSize;
		}
		else if (i == 1) {
			startLscdq = start3acali + caliSize;
		}
	}
	//DEBUG_TRACE("start3acali=%x, size3acali=%d\n", start3acali, size3acali);
	//DEBUG_TRACE("startLsc=%x, sizeLsc=%d\n", startLsc, sizeLsc);
	//DEBUG_TRACE("startLscdq=%x, size=%d\n", startLscdq, sizeLscdq);

	/* update calibration data into FW buffer */
	if (idx == 0) {
		memcpy(pIspFw+start3acali, p3acali, size3acali);
	}
	else if (idx == 1) {
		memcpy(pIspFw+startLsc, pLsc, sizeLsc);
		memcpy(pIspFw+startLscdq, pLscdq, sizeLscdq);
	}

_EXIT_:

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_ResCheck
 *  Description : check resource header and checksum
 *  idx: Set which resource will be loaded
     *p3acali: read from 3ACALI.BIN or 3ACALI_F.BIN
     *pLsc: read from LSC.BIN or LSC_F.BIN
     *pLscdq: read from LSC_DQ.BIN or LSC_DQ_F.BIN
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_ResCheck(UINT8 idx, UINT8 *p3acali, UINT8 *pLsc, UINT8 *Lscdq)
{
	UINT8 ucRet = SUCCESS;
	UINT32 header_3ACALI = 0, header_LSC = 0, header_LSCDQ = 0;
	UINT32 chksuum_3ACALI = 0, chksuum_LSC = 0, chksuum_LSCDQ = 0, checksum = 0, dataSize = 0, i;

	/* Header check */
	if (idx == 0) { //3ACALI
		header_3ACALI = *(p3acali);
		header_3ACALI += (*(p3acali+1)<<8);
		header_3ACALI += (*(p3acali+2)<<16);
		header_3ACALI += (*(p3acali+3)<<24);
		if (header_3ACALI == 0xFFFFFFFF || header_3ACALI == 0x00000000) {
			DEBUG_TRACE("header_3ACALI=0x%04x\n", header_3ACALI);
			ucRet = LOADCODE_3ACALI_HEADER_ERR;
			goto _EXIT_;
		}
	}
	else if (idx == 1) { //LSC & LSC_DQ
		header_LSC = *(pLsc);
		header_LSC += (*(pLsc+1)<<8);
		header_LSC += (*(pLsc+2)<<16);
		header_LSC += (*(pLsc+3)<<24);
		if (header_LSC == 0xFFFFFFFF || header_LSC == 0x00000000) {
			DEBUG_TRACE("header_LSC=0x%04x\n", header_LSC);
			ucRet = LOADCODE_LSC_HEADER_ERR;
			goto _EXIT_;
		}

		header_LSCDQ = *(Lscdq);
		header_LSCDQ += (*(Lscdq+1)<<8);
		header_LSCDQ += (*(Lscdq+2)<<16);
		header_LSCDQ += (*(Lscdq+3)<<24);
		if (header_LSCDQ == 0xFFFFFFFF || header_LSCDQ == 0x00000000) {
			DEBUG_TRACE("header_LSCDQ=0x%04x\n", header_LSCDQ);
			ucRet = LOADCODE_LSC_DQ_HEADER_ERR;
			goto _EXIT_;
		}
	}

	/* Checksum check */
	if (idx == 0) { //3ACALI
		dataSize = *(p3acali+6);
		dataSize += (*(p3acali+7)<<8);
		checksum = *(p3acali+RES_3ACALI_HEADER_SIZE);
		checksum += (*(p3acali+RES_3ACALI_HEADER_SIZE+1)<<8);
		checksum += (*(p3acali+RES_3ACALI_HEADER_SIZE+2)<<16);
		checksum += (*(p3acali+RES_3ACALI_HEADER_SIZE+3)<<24);
		
		for (i = 0; i < dataSize-sizeof(UINT32); i++) {
			chksuum_3ACALI = chksuum_3ACALI + (*(p3acali+RES_3ACALI_HEADER_SIZE+sizeof(UINT32)+i));
		}
		if (chksuum_3ACALI != checksum) {
			DEBUG_TRACE("dataSize=%d, checksum=0x%04x, chksuum_3ACALI=0x%04x\n", dataSize, checksum, chksuum_3ACALI);
			ucRet = LOADCODE_3ACALI_CHKSUM_ERR;
			goto _EXIT_;
		}
	}
	else if (idx == 1) { //LSC & LSC_DQ
		dataSize = *(pLsc+6);
		dataSize += (*(pLsc+7)<<8);
		checksum = *(pLsc+RES_LSC_HEADER_SIZE-4);
		checksum += (*(pLsc+RES_LSC_HEADER_SIZE-3)<<8);
		checksum += (*(pLsc+RES_LSC_HEADER_SIZE-2)<<16);
		checksum += (*(pLsc+RES_LSC_HEADER_SIZE-1)<<24);
		for (i = 0; i < dataSize; i++) {
			chksuum_LSC = chksuum_LSC + (*(pLsc+RES_LSC_HEADER_SIZE+i));
		}
		if (chksuum_LSC != checksum) {
			DEBUG_TRACE("dataSize=%d, checksum=0x%04x, chksuum_LSC=0x%04x\n", dataSize, checksum, chksuum_LSC);
			ucRet = LOADCODE_LSC_CHKSUM_ERR;
			goto _EXIT_;
		}

		dataSize = *(Lscdq+6);
		dataSize += (*(Lscdq+7)<<8);
		checksum = *(Lscdq+RES_LSCDQ_HEADER_SIZE-4);
		checksum += (*(Lscdq+RES_LSCDQ_HEADER_SIZE-3)<<8);
		checksum += (*(Lscdq+RES_LSCDQ_HEADER_SIZE-2)<<16);
		checksum += (*(Lscdq+RES_LSCDQ_HEADER_SIZE-1)<<24);
		for (i = 0; i < dataSize; i++) {
			chksuum_LSCDQ = chksuum_LSCDQ + (*(Lscdq+RES_LSCDQ_HEADER_SIZE+i));
		}
		if (chksuum_LSCDQ != checksum) {
			DEBUG_TRACE("dataSize=%d, checksum=0x%04x, chksuum_LSCDQ=0x%04x\n", dataSize, checksum, chksuum_LSCDQ);
			ucRet = LOADCODE_LSC_DQ_CHKSUN_ERR;
			goto _EXIT_;
		}
	}

_EXIT_:

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_ResNumGet
 *  Description : get the resource number in the IQ.BIN
 *  *resNum: resource number
     *pIspFw: external ISP FW pointer
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_ResNumGet(UINT32 *resNum, UINT8 *pIspFw)
{
	UINT8 i = 0, ucRet = SUCCESS;

	//DEBUG_TRACE("4.pIspFw =%x\n", pIspFw);
	while (1) {
		//DEBUG_TRACE("[%s] %d 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x 0x%2x\n", __FUNCTION__, __LINE__,
		//	*(pIspFw),*(pIspFw+1),*(pIspFw+2), *(pIspFw+3),*(pIspFw+4),*(pIspFw+5));
		if ((*(pIspFw) == 0x33) && (*(pIspFw+1) == 0x41) && (*(pIspFw+2) == 0x43) &&
			(*(pIspFw+3)==0x41) && (*(pIspFw+4)==0x4C) && (*(pIspFw+5)==0x49)) {
			break;
		}
		i++;
		pIspFw += 0x10;
		if (i > 30) {
			ucRet = LOADCODE_GET_RES_NUM_ERR;
			goto _EXIT_;
		}
	}

_EXIT_:
	*resNum = i;

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_SPIDataWrite
 *  Description : write data to external ISP through SPI interface
 *  ucSPIMode: SPI mode
     *ucStartAddr: buffer to be wrote
     ulTransByteCnt: buffer size to be wrote
     ulDmaAddr: SPCA7002 DMA start address
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_SPIDataWrite(UINT8 ucSPIMode, UINT8 *ucStartAddr, UINT32 ulTransByteCnt, UINT32 ulDmaAddr)
{
	UINT8 ucRet = SUCCESS;
	UINT16 tmrCnt = 0;

	EXISP_7002SPICfg(ucSPIMode, ulDmaAddr, ulTransByteCnt);

	/*Trigger the 7002 SPI DMA0*/
	I2CDataWrite(0x4160, 0x01);
		/* Enable data transaction */
	if(g_icatch_spi_dev)
		spi_write(g_icatch_spi_dev, (void*)ucStartAddr, ulTransByteCnt);
	else{
		DEBUG_TRACE("%s:g_icatch_spi_dev is null\n",__func__);
		return FAIL;
	}
	/* Wait SPCA7002 DAM done */
	while ((I2CDataRead(0x4003)&0x02) != 0x02) {
		tmrUsDelay(5000);
		tmrCnt++;
		if (1000 < tmrCnt) {
			DEBUG_TRACE("Wait 7002 DMA0 INT Timeout.\n");
			return FAIL;
		}
	}

	/* Restore SPCA7002 DMA setting */
	I2CDataWrite(0x1084, 0);
	I2CDataWrite(0x1085, 0);
	I2CDataWrite(0x1086, 0);
	I2CDataWrite(0x1087, 0);
	I2CDataWrite(0x1088, 0);
	I2CDataWrite(0x108c, 0);

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_7002SPICfg
 *  Description : external SPI configuration
 *  ucSPIMode: SPI mode
     ulDmaAddr: SPCA7002 DMA start address
     ulTransByteCnt: buffer size to be wrote
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_7002SPICfg(UINT8 ucSPIMode, UINT32 ulDmaAddr, UINT32 ulTransByteCnt)
{
	UINT8 ucRet = SUCCESS;
	UINT8 startBank, endBank, realEndBank, readData, i;
	UINT32 bankEndValue=0;
	const UINT16 bankSize = 8192;
	const UINT32 maxTransByteCnt = 0x50000;

	static UINT8 regdata1[][3] = {
		{0x40, 0x10, 0x10}, /*SPI reset*/
		{0x40, 0xe1, 0x00}, /*SPI mode*/
		{0x40, 0x51, 0x01}, /*SPI enable*/
		{0x40, 0x11, 0x10}, /*DMA0 reset*/
		{0x41, 0x64, 0x01}, /*Read data from host*/
		{0x41, 0x70, 0x0f}, /*Byte Cnt Low*/
		{0x41, 0x71, 0x0f}, /*Byte Cnt Mid*/
		{0x41, 0x72, 0x0f}, /*Byte Cnt High*/
		{0x10, 0x8c, 0x00}, /*DMA master select FM*/
		{0x10, 0x80, 0x00}, /*DMA start addr Low*/
		{0x10, 0x81, 0x00}, /*DMA start addr Mid*/
		{0x10, 0x82, 0x00}, /*DMA start addr High*/
		{0x10, 0x84, 0x00}, /*DMA bank enable*/
		{0x10, 0x85, 0x00},
		{0x10, 0x86, 0x00},
		{0x10, 0x87, 0x00},
		{0x10, 0x88, 0x00},
		{0x00, 0x26, 0x00},
		{0x40, 0x03, 0x02}, /*Clear DMA0 INT status*/
	};

	regdata1[1][2] = (((UINT8)ucSPIMode) & 0xf);
	regdata1[4][2] = (1&0x1);

	if (maxTransByteCnt < ulTransByteCnt)
		ulTransByteCnt = (maxTransByteCnt-1);
	else
		ulTransByteCnt--;

	regdata1[5][2] = (ulTransByteCnt & 0xff);
	regdata1[6][2] = ((ulTransByteCnt >> 8) & 0xff);
	regdata1[7][2] = ((ulTransByteCnt >> 16) & 0xff);
	regdata1[8][2] = 0<<4;
	regdata1[9][2] = (ulDmaAddr & 0xff);
	regdata1[10][2] = ((ulDmaAddr >> 8) & 0xff);
	regdata1[11][2] = ((ulDmaAddr >> 16) & 0xff);

	startBank = (ulDmaAddr&0xffffff)/bankSize;
	endBank = ((ulDmaAddr&0xffffff)+ulTransByteCnt)/bankSize;
	realEndBank = endBank;

	if (endBank > 31) {

		for (i = 32; i <= endBank; i++)
			bankEndValue |= (1 << (i-32));

		regdata1[16][2] = (bankEndValue & 0xff);
		realEndBank = 32;
		bankEndValue = 0;
	}

	for (i = startBank; i <= realEndBank; i++)
		bankEndValue |= (1 << i);

	regdata1[12][2] = (bankEndValue & 0xff);
	regdata1[13][2] = ((bankEndValue >> 8) & 0xff);
	regdata1[14][2] = ((bankEndValue >>16) & 0xff);
	regdata1[15][2] = ((bankEndValue >>24) & 0xff);

	readData = I2CDataRead(0x0026); /*Config the SPI pin GPIO/Function mode.*/
	readData &= (~0xf);
	regdata1[17][2] = readData;

	for (i = 0; i < sizeof(regdata1)/sizeof(regdata1[0]); i++)
		I2CDataWrite(((regdata1[i][0]<<8)&0xFF00)+(regdata1[i][1]&0x00FF), regdata1[i][2]);

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_PvSizeSet
 *  Description : Set preview size
 *  ucResolutionIdx: resolution index
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_PvSizeSet(UINT8 ucResolutionIdx)
{
	UINT8 ucRet = SUCCESS;

	EXISP_I2C_PvSizeSet(ucResolutionIdx);
	EXISP_I2C_PvCapModeSet(0x00);

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_ImageCapSet
 *  Description : Image capture
 *  ucImageCapIdx: image capture index
     0x00	IMAGE_CAP_SINGLE
     0x01	IMAGE_CAP_HDR
     0x02	IMAGE_CAP_ZSL_SINGLE_FLASH
     0x03	IMAGE_CAP_ZSL_BURST_FLASH
     0x04	IMAGE_CAP_NONZSL_SINGLE
     0x05	IMAGE_CAP_NONZSL_BURST
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_ImageCapSet(UINT8 ucImageCapIdx)
{
	UINT8 ucRet = SUCCESS;

	if (ucImageCapIdx >= IMAGE_CAP_MAX) {
		ucRet = FAIL;
		return ucRet;
	}

	switch (ucImageCapIdx) {
	case IMAGE_CAP_SINGLE:
		EXISP_I2C_CapModeSet(CAP_MODE_SINGLE);
		EXISP_I2C_PvCapModeSet(PVCAP_MODE_CAP);
		break;
	case IMAGE_CAP_HDR:
		EXISP_I2C_CapModeSet(CAP_MODE_HDR);
		EXISP_I2C_PvCapModeSet(PVCAP_MODE_CAP);
		break;
	case IMAGE_CAP_ZSL_SINGLE_FLASH:
		EXISP_I2C_CapModeSet(CAP_MODE_SINGLE);
		EXISP_I2C_PvCapModeSet(PVCAP_MODE_CAP_ZSL);
		break;
	case IMAGE_CAP_ZSL_BURST_FLASH:
		EXISP_I2C_CapModeSet(CAP_MODE_BURST);
		EXISP_I2C_PvCapModeSet(PVCAP_MODE_CAP_ZSL);
		break;
	case IMAGE_CAP_NONZSL_SINGLE:
		EXISP_I2C_CapModeSet(CAP_MODE_SINGLE);
		EXISP_I2C_PvCapModeSet(PVCAP_MODE_CAP_NONZSL);
		break;
	case IMAGE_CAP_NONZSL_BURST:
		EXISP_I2C_CapModeSet(CAP_MODE_BURST);
		EXISP_I2C_PvCapModeSet(PVCAP_MODE_CAP_NONZSL);
		break;
	}

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_TAFTAEROISet
 *  Description : Set TAF or TAE ROI and trigger
 *  ucTrg:
     0x00	TAFTAE_TAF_ONLY
     0x01	TAFTAE_TAE_OFF
     0x02	TAFTAE_TAE_USE_TAE_ROI
     0x03	TAFTAE_TAE_USE_TAF_ROI
     0x04	TAFTAE_TAE_ONLY
     usTAFSize: AF ROI size
     usTAFX: AF ROI x
     usTAFY: AF ROI y
     usTAESize: AE ROI size
     usTAEX: AE ROI x
     usTAEY: AE ROI y
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_TAFTAEROISet(
	UINT8 ucTrg,
	UINT16 usTAFSize,
	UINT16 usTAFX,
	UINT16 usTAFY,
	UINT16 usTAESize,
	UINT16 usTAEX,
	UINT16 usTAEY)
{
	UINT8 ucRet = SUCCESS;

	if (ucTrg >= TAFTAE_MAX) {
		ucRet = FAIL;
		return ucRet;
	}

	switch (ucTrg) {
	case TAFTAE_TAF_ONLY:
		EXISP_I2C_ROISwitchSet(0x01);
		EXISP_I2C_AFROISet(usTAFSize, usTAFX, usTAFY);
		EXISP_I2C_AFROITriggerSet();
		break;
	case TAFTAE_TAE_OFF:
		EXISP_I2C_AEROITriggerSet(0x00);
		break;

	case TAFTAE_TAE_USE_TAE_ROI:
		EXISP_I2C_ROISwitchSet(0x01);
		EXISP_I2C_AEROISet(usTAESize, usTAEX, usTAEY);
		EXISP_I2C_AEROITriggerSet(0x01);
		EXISP_I2C_AFROISet(usTAFSize, usTAFX, usTAFY);
		EXISP_I2C_AFROITriggerSet();
		break;
	case TAFTAE_TAE_USE_TAF_ROI:
		EXISP_I2C_ROISwitchSet(0x01);
		EXISP_I2C_AFROISet(usTAFSize, usTAFX, usTAFY);
		EXISP_I2C_AEROITriggerSet(0x02);
		EXISP_I2C_AFROITriggerSet();
		break;
	case TAFTAE_TAE_ONLY:
		EXISP_I2C_ROISwitchSet(0x01);
		EXISP_I2C_AEROISet(usTAESize, usTAEX, usTAEY);
		EXISP_I2C_AEROITriggerSet(0x01);
	}

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_SwitchRAWFormatSet
 *  Description : Switch to RAW format from YUV format
 *  Input : None
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_SwitchRAWFormatSet(void)
{
	UINT8 ucRet = SUCCESS;

	EXISP_I2C_RawPathSet(0x00);
	EXISP_I2C_RawFormatSet(0x01);

	return ucRet;
}

/*-------------------------------------------------------------------------
 *  Function Name : EXISP_ExifInfoGet
 *  Description : Get EXIF information from external ISP
 *  Input : None
 *  Return : status result
 *------------------------------------------------------------------------*/
UINT8 EXISP_ExifInfoGet(
	UINT8 *ucExpTimeNumerator,
	UINT32 *ulExpTimeDenominator,
	UINT8 *ucExpTimeCompensation,
	UINT16 *usLensFocalLength,
	UINT16 *usIsoInfo,
	UINT8 *ucFlashInfo)
{
	UINT8 ucRet = SUCCESS;

	/* Get exposure time numerator */
	*ucExpTimeNumerator = EXISP_I2C_ExposureTimeNumeratorGet();
	/* Get exposure time denominator */
	*ulExpTimeDenominator = EXISP_I2C_ExposureTimeDenominatorGet();
	/* Get exposure time compensation */
	*ucExpTimeCompensation = EXISP_I2C_ExposureTimeCompensationGet();
	/* Get lens focal length */
	*usLensFocalLength = EXISP_I2C_LensFocalLengthGet();
	/* Get ISO information */
	*usIsoInfo = EXISP_I2C_ISOValueGet();
	/* Get flash mode information */
	*ucFlashInfo = EXISP_I2C_FlashModeGet();

	return ucRet;
}

