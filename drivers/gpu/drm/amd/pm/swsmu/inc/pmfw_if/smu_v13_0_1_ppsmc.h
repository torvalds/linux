/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef SMU_13_0_1_PPSMC_H
#define SMU_13_0_1_PPSMC_H

/** @def PPS_PMFW_IF_VER 
* PPS (PPLib) to PMFW IF version 1.0
*/
#define PPS_PMFW_IF_VER "1.0" ///< Major.Minor 

/** @defgroup ResponseCodes PMFW Response Codes: 
*  @{
*/
#define PPSMC_Result_OK                    0x1  ///< Message Response OK 
#define PPSMC_Result_Failed                0xFF ///< Message Response Failed 
#define PPSMC_Result_UnknownCmd            0xFE ///< Message Response Unknown Command 
#define PPSMC_Result_CmdRejectedPrereq     0xFD ///< Message Response Command Failed Prerequisite
#define PPSMC_Result_CmdRejectedBusy       0xFC ///< Message Response Command Rejected due to PMFW is busy. Sender should retry sending this message
/** @}*/

/** @defgroup definitions Message definitions
*  @{
*/
#define PPSMC_MSG_TestMessage                   0x01 ///< To check if PMFW is alive and responding. Requirement specified by PMFW team 
#define PPSMC_MSG_GetSmuVersion                 0x02 ///< Get PMFW version
#define PPSMC_MSG_GetDriverIfVersion            0x03 ///< Get PMFW_DRIVER_IF version
#define PPSMC_MSG_EnableGfxOff                  0x04 ///< Enable GFXOFF
#define PPSMC_MSG_DisableGfxOff                 0x05 ///< Disable GFXOFF
#define PPSMC_MSG_PowerDownVcn                  0x06 ///< Power down VCN
#define PPSMC_MSG_PowerUpVcn                    0x07 ///< Power up VCN; VCN is power gated by default
#define PPSMC_MSG_SetHardMinVcn                 0x08 ///< For wireless display
#define PPSMC_MSG_SetSoftMinGfxclk              0x09 ///< Set SoftMin for GFXCLK, argument is frequency in MHz
#define PPSMC_MSG_ActiveProcessNotify           0x0A ///< Deprecated (Not to be used)
#define PPSMC_MSG_ForcePowerDownGfx             0x0B ///< Force power down GFX, i.e. enter GFXOFF
#define PPSMC_MSG_PrepareMp1ForUnload           0x0C ///< Prepare PMFW for GFX driver unload
#define PPSMC_MSG_SetDriverDramAddrHigh         0x0D ///< Set high 32 bits of DRAM address for Driver table transfer
#define PPSMC_MSG_SetDriverDramAddrLow          0x0E ///< Set low 32 bits of DRAM address for Driver table transfer 
#define PPSMC_MSG_TransferTableSmu2Dram         0x0F ///< Transfer driver interface table from PMFW SRAM to DRAM
#define PPSMC_MSG_TransferTableDram2Smu         0x10 ///< Transfer driver interface table from DRAM to PMFW SRAM
#define PPSMC_MSG_GfxDeviceDriverReset          0x11 ///< Request GFX mode 2 reset
#define PPSMC_MSG_GetEnabledSmuFeatures         0x12 ///< Get enabled features in PMFW
#define PPSMC_MSG_SetHardMinSocclkByFreq        0x13 ///< Set hard min for SOC CLK
#define PPSMC_MSG_SetSoftMinFclk                0x14 ///< Set hard min for FCLK
#define PPSMC_MSG_SetSoftMinVcn                 0x15 ///< Set soft min for VCN clocks (VCLK and DCLK)
#define PPSMC_MSG_SPARE                         0x16 ///< Spare
#define PPSMC_MSG_GetGfxclkFrequency            0x17 ///< Get GFX clock frequency
#define PPSMC_MSG_GetFclkFrequency              0x18 ///< Get FCLK frequency
#define PPSMC_MSG_AllowGfxOff                   0x19 ///< Inform PMFW of allowing GFXOFF entry
#define PPSMC_MSG_DisallowGfxOff                0x1A ///< Inform PMFW of disallowing GFXOFF entry
#define PPSMC_MSG_SetSoftMaxGfxClk              0x1B ///< Set soft max for GFX CLK
#define PPSMC_MSG_SetHardMinGfxClk              0x1C ///< Set hard min for GFX CLK
#define PPSMC_MSG_SetSoftMaxSocclkByFreq        0x1D ///< Set soft max for SOC CLK
#define PPSMC_MSG_SetSoftMaxFclkByFreq          0x1E ///< Set soft max for FCLK
#define PPSMC_MSG_SetSoftMaxVcn                 0x1F ///< Set soft max for VCN clocks (VCLK and DCLK)
#define PPSMC_MSG_SetPowerLimitPercentage       0x20 ///< Set power limit percentage
#define PPSMC_MSG_PowerDownJpeg                 0x21 ///< Power down Jpeg
#define PPSMC_MSG_PowerUpJpeg                   0x22 ///< Power up Jpeg; VCN is power gated by default
#define PPSMC_MSG_SetHardMinFclkByFreq          0x23 ///< Set hard min for FCLK
#define PPSMC_MSG_SetSoftMinSocclkByFreq        0x24 ///< Set soft min for SOC CLK
#define PPSMC_MSG_AllowZstates                  0x25 ///< Inform PMFM of allowing Zstate entry, i.e. no Miracast activity
#define PPSMC_MSG_DisallowZstates               0x26 ///< Inform PMFW of disallowing Zstate entry, i.e. there is Miracast activity
#define PPSMC_MSG_RequestActiveWgp              0x27 ///< Request GFX active WGP number
#define PPSMC_MSG_QueryActiveWgp                0x28 ///< Query the anumber of active WGP number
#define PPSMC_Message_Count                     0x29 ///< Total number of PPS messages
/** @}*/
 
/** @enum Mode_Reset_e 
* Mode reset type, argument for PPSMC_MSG_GfxDeviceDriverReset 
*/ 
typedef enum {
  MODE1_RESET = 1,  ///< Mode reset type 1
  MODE2_RESET = 2   ///< Mode reset type 2
} Mode_Reset_e;    
/** @}*/

#endif
