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

#ifndef ALDEBARAN_PP_SMC_H
#define ALDEBARAN_PP_SMC_H

#pragma pack(push, 1)

// SMU Response Codes:
#define PPSMC_Result_OK                    0x1
#define PPSMC_Result_Failed                0xFF
#define PPSMC_Result_UnknownCmd            0xFE
#define PPSMC_Result_CmdRejectedPrereq     0xFD
#define PPSMC_Result_CmdRejectedBusy       0xFC

// Message Definitions:
#define PPSMC_MSG_TestMessage                    0x1
#define PPSMC_MSG_GetSmuVersion                  0x2
#define PPSMC_MSG_GfxDriverReset                 0x3
#define PPSMC_MSG_GetDriverIfVersion             0x4
#define PPSMC_MSG_spare1                         0x5
#define PPSMC_MSG_spare2                         0x6
#define PPSMC_MSG_EnableAllSmuFeatures           0x7
#define PPSMC_MSG_DisableAllSmuFeatures          0x8
#define PPSMC_MSG_spare3                         0x9
#define PPSMC_MSG_spare4                         0xA
#define PPSMC_MSG_spare5                         0xB
#define PPSMC_MSG_spare6                         0xC
#define PPSMC_MSG_GetEnabledSmuFeaturesLow       0xD
#define PPSMC_MSG_GetEnabledSmuFeaturesHigh      0xE
#define PPSMC_MSG_SetDriverDramAddrHigh          0xF
#define PPSMC_MSG_SetDriverDramAddrLow           0x10
#define PPSMC_MSG_SetToolsDramAddrHigh           0x11
#define PPSMC_MSG_SetToolsDramAddrLow            0x12
#define PPSMC_MSG_TransferTableSmu2Dram          0x13
#define PPSMC_MSG_TransferTableDram2Smu          0x14
#define PPSMC_MSG_UseDefaultPPTable              0x15
#define PPSMC_MSG_SetSystemVirtualDramAddrHigh   0x16
#define PPSMC_MSG_SetSystemVirtualDramAddrLow    0x17
#define PPSMC_MSG_SetSoftMinByFreq               0x18
#define PPSMC_MSG_SetSoftMaxByFreq               0x19
#define PPSMC_MSG_SetHardMinByFreq               0x1A
#define PPSMC_MSG_SetHardMaxByFreq               0x1B
#define PPSMC_MSG_GetMinDpmFreq                  0x1C
#define PPSMC_MSG_GetMaxDpmFreq                  0x1D
#define PPSMC_MSG_GetDpmFreqByIndex              0x1E
#define PPSMC_MSG_SetWorkloadMask                0x1F
#define PPSMC_MSG_GetVoltageByDpm                0x20
#define PPSMC_MSG_GetVoltageByDpmOverdrive       0x21
#define PPSMC_MSG_SetPptLimit                    0x22
#define PPSMC_MSG_GetPptLimit                    0x23
#define PPSMC_MSG_PrepareMp1ForUnload            0x24
#define PPSMC_MSG_PrepareMp1ForReset             0x25 //retired in 68.07
#define PPSMC_MSG_SoftReset                      0x26 //retired in 68.07
#define PPSMC_MSG_RunDcBtc                       0x27
#define PPSMC_MSG_DramLogSetDramAddrHigh         0x28
#define PPSMC_MSG_DramLogSetDramAddrLow          0x29
#define PPSMC_MSG_DramLogSetDramSize             0x2A
#define PPSMC_MSG_GetDebugData                   0x2B
#define PPSMC_MSG_WaflTest                       0x2C
#define PPSMC_MSG_spare7                         0x2D
#define PPSMC_MSG_SetMemoryChannelEnable         0x2E
#define PPSMC_MSG_SetNumBadHbmPagesRetired       0x2F
#define PPSMC_MSG_DFCstateControl                0x32
#define PPSMC_MSG_GetGmiPwrDnHyst                0x33
#define PPSMC_MSG_SetGmiPwrDnHyst                0x34
#define PPSMC_MSG_GmiPwrDnControl                0x35
#define PPSMC_MSG_EnterGfxoff                    0x36
#define PPSMC_MSG_ExitGfxoff                     0x37
#define PPSMC_MSG_SetExecuteDMATest              0x38
#define PPSMC_MSG_EnableDeterminism              0x39
#define PPSMC_MSG_DisableDeterminism             0x3A
#define PPSMC_MSG_SetUclkDpmMode                 0x3B

//STB to dram log
#define PPSMC_MSG_DumpSTBtoDram                     0x3C
#define PPSMC_MSG_STBtoDramLogSetDramAddrHigh       0x3D
#define PPSMC_MSG_STBtoDramLogSetDramAddrLow        0x3E
#define PPSMC_MSG_STBtoDramLogSetDramSize           0x3F
#define PPSMC_MSG_SetSystemVirtualSTBtoDramAddrHigh 0x40
#define PPSMC_MSG_SetSystemVirtualSTBtoDramAddrLow  0x41

#define PPSMC_MSG_GfxDriverResetRecovery	0x42
#define PPSMC_MSG_BoardPowerCalibration 	0x43
#define PPSMC_Message_Count			0x44

//PPSMC Reset Types
#define PPSMC_RESET_TYPE_WARM_RESET              0x00
#define PPSMC_RESET_TYPE_DRIVER_MODE_1_RESET     0x01 //driver msg argument should be 1 for mode-1
#define PPSMC_RESET_TYPE_DRIVER_MODE_2_RESET     0x02 //and 2 for mode-2
#define PPSMC_RESET_TYPE_PCIE_LINK_RESET         0x03
#define PPSMC_RESET_TYPE_BIF_LINK_RESET          0x04
#define PPSMC_RESET_TYPE_PF0_FLR_RESET           0x05


typedef enum {
  GFXOFF_ERROR_NO_ERROR,
  GFXOFF_ERROR_DISALLOWED,
  GFXOFF_ERROR_GFX_BUSY,
  GFXOFF_ERROR_GFX_OFF,
  GFXOFF_ERROR_GFX_ON,
} GFXOFF_ERROR_e;

typedef uint32_t PPSMC_Result;
typedef uint32_t PPSMC_Msg;
#pragma pack(pop)

#endif
