/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

#ifndef __SMU_V13_0_5_PPSMC_H__
#define __SMU_V13_0_5_PPSMC_H__

// SMU Response Codes:
#define PPSMC_Result_OK                    0x1
#define PPSMC_Result_Failed                0xFF
#define PPSMC_Result_UnknownCmd            0xFE
#define PPSMC_Result_CmdRejectedPrereq     0xFD
#define PPSMC_Result_CmdRejectedBusy       0xFC


// Message Definitions:
#define PPSMC_MSG_TestMessage               1
#define PPSMC_MSG_GetSmuVersion             2
#define PPSMC_MSG_EnableGfxOff              3  ///< Enable GFXOFF
#define PPSMC_MSG_DisableGfxOff             4  ///< Disable GFXOFF
#define PPSMC_MSG_PowerDownVcn              5  ///< Power down VCN
#define PPSMC_MSG_PowerUpVcn                6  ///< Power up VCN; VCN is power gated by default
#define PPSMC_MSG_SetHardMinVcn             7  ///< For wireless display
#define PPSMC_MSG_SetSoftMinGfxclk          8  ///< Set SoftMin for GFXCLK, argument is frequency in MHz
#define PPSMC_MSG_Spare0                    9  ///< Spare
#define PPSMC_MSG_GfxDeviceDriverReset      10 ///< Request GFX mode 2 reset
#define PPSMC_MSG_SetDriverDramAddrHigh     11 ///< Set high 32 bits of DRAM address for Driver table transfer
#define PPSMC_MSG_SetDriverDramAddrLow      12 ///< Set low 32 bits of DRAM address for Driver table transfer
#define PPSMC_MSG_TransferTableSmu2Dram     13 ///< Transfer driver interface table from PMFW SRAM to DRAM
#define PPSMC_MSG_TransferTableDram2Smu     14 ///< Transfer driver interface table from DRAM to PMFW SRAM
#define PPSMC_MSG_GetGfxclkFrequency        15 ///< Get GFX clock frequency
#define PPSMC_MSG_GetEnabledSmuFeatures     16 ///< Get enabled features in PMFW
#define PPSMC_MSG_SetSoftMaxVcn             17 ///< Set soft max for VCN clocks (VCLK and DCLK)
#define PPSMC_MSG_PowerDownJpeg             18 ///< Power down Jpeg
#define PPSMC_MSG_PowerUpJpeg               19 ///< Power up Jpeg; VCN is power gated by default
#define PPSMC_MSG_SetSoftMaxGfxClk          20
#define PPSMC_MSG_SetHardMinGfxClk          21 ///< Set hard min for GFX CLK
#define PPSMC_MSG_AllowGfxOff               22 ///< Inform PMFW of allowing GFXOFF entry
#define PPSMC_MSG_DisallowGfxOff            23 ///< Inform PMFW of disallowing GFXOFF entry
#define PPSMC_MSG_SetSoftMinVcn             24 ///< Set soft min for VCN clocks (VCLK and DCLK)
#define PPSMC_MSG_GetDriverIfVersion        25 ///< Get PMFW_DRIVER_IF version
#define PPSMC_MSG_PrepareMp1ForUnload        26 ///< Prepare PMFW for GFX driver unload
#define PPSMC_Message_Count                 27

/** @enum Mode_Reset_e
* Mode reset type, argument for PPSMC_MSG_GfxDeviceDriverReset
*/
typedef enum {
  MODE1_RESET = 1,  ///< Mode reset type 1
  MODE2_RESET = 2   ///< Mode reset type 2
} Mode_Reset_e;
/** @}*/

#endif

