/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __SMU_V15_0_0_PPSMC_H__
#define __SMU_V15_0_0_PPSMC_H__


/**
* @file ppsmc.h
*
* @brief pplib (driver/host) to PMFW Interface
*
* Clients:  Driver/Host via pplib.
* Protocols:
*
* @date 2016 - 2024
*/

/*! @mainpage PMFW-PPS (PPLib) Message Interface
  This documentation contains the subsections:\n\n
  @ref ResponseCodes\n
  @ref definitions\n
  @ref enums\n
*/

/** @def PPS_PMFW_IF_VER
* PPS (PPLib) to PMFW IF version 1.0
*/
#define PPS_PMFW_IF_VER "1.0" ///< Major.Minor

/** @defgroup ResponseCodes PMFW Response Codes
*  @{
*/
// SMU Response Codes:
#define PPSMC_Result_OK                    0x1  ///< Message Response OK
#define PPSMC_Result_Failed                0xFF ///< Message Response Failed
#define PPSMC_Result_UnknownCmd            0xFE ///< Message Response Unknown Command
#define PPSMC_Result_CmdRejectedPrereq     0xFD ///< Message Response Command Failed Prerequisite
#define PPSMC_Result_CmdRejectedBusy       0xFC ///< Message Response Command Rejected due to PMFW is busy. Sender should retry sending this message
/** @}*/

/** @defgroup definitions Message definitions
*  @{
*/
// Message Definitions:
#define PPSMC_MSG_TestMessage                   0x01 ///< To check if PMFW is alive and responding. Requirement specified by PMFW team
#define PPSMC_MSG_GetPmfwVersion                0x02 ///< Get PMFW version
#define PPSMC_MSG_GetDriverIfVersion            0x03 ///< Get PMFW_DRIVER_IF version
#define PPSMC_MSG_PowerDownVcn                  0x04 ///< Power down VCN
#define PPSMC_MSG_PowerUpVcn                    0x05 ///< Power up VCN; VCN is power gated by default
#define PPSMC_MSG_SetSoftMinGfxclk              0x06 ///< Set SoftMin for GFXCLK, argument is frequency in MHz
#define PPSMC_MSG_PrepareMp1ForUnload           0x07 ///< Prepare PMFW for GFX driver unload
#define PPSMC_MSG_TransferTableSmu2Dram         0x08 ///< Transfer driver interface table from PMFW SRAM to DRAM
#define PPSMC_MSG_TransferTableDram2Smu         0x09 ///< Transfer driver interface table from DRAM to PMFW SRAM
#define PPSMC_MSG_GfxDeviceDriverReset          0x0A ///< Request GFX mode 2 reset
#define PPSMC_MSG_GetEnabledSmuFeatures         0x0B ///< Get enabled features in PMFW
#define PPSMC_MSG_SetSoftMinFclk                0x0C ///< Set hard min for FCLK
#define PPSMC_MSG_SetSoftMinVcn                 0x0D ///< Set soft min for VCN clocks (VCLK and DCLK)

#define PPSMC_MSG_EnableGfxImu                  0x0E ///< Enable GFX IMU

#define PPSMC_MSG_AllowGfxOff                   0x0F ///< Inform PMFW of allowing GFXOFF entry
#define PPSMC_MSG_DisallowGfxOff                0x10 ///< Inform PMFW of disallowing GFXOFF entry
#define PPSMC_MSG_SetSoftMaxGfxClk              0x11 ///< Set soft max for GFX CLK

#define PPSMC_MSG_SetSoftMaxSocclkByFreq        0x12 ///< Set soft max for SOC CLK
#define PPSMC_MSG_SetSoftMaxFclkByFreq          0x13 ///< Set soft max for FCLK
#define PPSMC_MSG_SetSoftMaxVcn                 0x14 ///< Set soft max for VCN clocks (VCLK and DCLK)
#define PPSMC_MSG_PowerDownJpeg                 0x15 ///< Power down Jpeg
#define PPSMC_MSG_PowerUpJpeg                   0x16 ///< Power up Jpeg; VCN is power gated by default

#define PPSMC_MSG_SetSoftMinSocclkByFreq        0x17 ///< Set soft min for SOC CLK
#define PPSMC_MSG_AllowZstates                  0x18 ///< Inform PMFM of allowing Zstate entry, i.e. no Miracast activity
#define PPSMC_MSG_GetSmartShiftStatus           0x19 ///< Returns SmartShift enable vs disable
#define PPSMC_MSG_PowerDownUmsch                0x1A ///< Power down VCN.UMSCH (aka VSCH) scheduler
#define PPSMC_MSG_PowerUpUmsch                  0x1B ///< Power up VCN.UMSCH (aka VSCH) scheduler
#define PPSMC_MSG_PowerUpVpe                    0x1C ///< Power up VPE
#define PPSMC_MSG_PowerDownVpe                  0x1D ///< Power down VPE
#define PPSMC_MSG_EnableLSdma                   0x1E ///< Enable LSDMA
#define PPSMC_MSG_DisableLSdma                  0x1F ///< Disable LSDMA
#define PPSMC_MSG_SetSoftMaxVpe                 0x20 ///<
#define PPSMC_MSG_SetSoftMinVpe                 0x21 ///<
#define PPSMC_Message_Count                     0x22 ///< Total number of PPSMC messages
/** @}*/

/**
* @defgroup enums Enum Definitions
*  @{
*/

/** @enum Mode_Reset_e
* Mode reset type, argument for PPSMC_MSG_GfxDeviceDriverReset
*/
//argument for PPSMC_MSG_GfxDeviceDriverReset
typedef enum {
  MODE1_RESET = 1,  ///< Mode reset type 1
  MODE2_RESET = 2   ///< Mode reset type 2
} Mode_Reset_e;

/** @}*/

/** @enum ZStates_e
* Zstate types, argument for PPSMC_MSG_AllowZstates
*/
//Argument for PPSMC_MSG_AllowZstates
typedef enum  {
  DISALLOW_ZSTATES = 0, ///< Disallow Zstates
  ALLOW_ZSTATES_Z8 = 8, ///< Allows Z8 only
  ALLOW_ZSTATES_Z9 = 9, ///< Allows Z9 and Z8
} ZStates_e;

/** @}*/
#endif
