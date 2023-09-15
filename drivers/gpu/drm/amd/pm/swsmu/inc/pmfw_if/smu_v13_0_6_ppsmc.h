/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#ifndef SMU_13_0_6_PPSMC_H
#define SMU_13_0_6_PPSMC_H

// SMU Response Codes:
#define PPSMC_Result_OK                             0x1
#define PPSMC_Result_Failed                         0xFF
#define PPSMC_Result_UnknownCmd                     0xFE
#define PPSMC_Result_CmdRejectedPrereq              0xFD
#define PPSMC_Result_CmdRejectedBusy                0xFC

// Message Definitions:
#define PPSMC_MSG_TestMessage                       0x1
#define PPSMC_MSG_GetSmuVersion                     0x2
#define PPSMC_MSG_GfxDriverReset                    0x3
#define PPSMC_MSG_GetDriverIfVersion                0x4
#define PPSMC_MSG_EnableAllSmuFeatures              0x5
#define PPSMC_MSG_DisableAllSmuFeatures             0x6
#define PPSMC_MSG_RequestI2cTransaction             0x7
#define PPSMC_MSG_GetMetricsVersion                 0x8
#define PPSMC_MSG_GetMetricsTable                   0x9
#define PPSMC_MSG_GetEccInfoTable                   0xA
#define PPSMC_MSG_GetEnabledSmuFeaturesLow          0xB
#define PPSMC_MSG_GetEnabledSmuFeaturesHigh         0xC
#define PPSMC_MSG_SetDriverDramAddrHigh             0xD
#define PPSMC_MSG_SetDriverDramAddrLow              0xE
#define PPSMC_MSG_SetToolsDramAddrHigh              0xF
#define PPSMC_MSG_SetToolsDramAddrLow               0x10
#define PPSMC_MSG_SetSystemVirtualDramAddrHigh      0x11
#define PPSMC_MSG_SetSystemVirtualDramAddrLow       0x12
#define PPSMC_MSG_SetSoftMinByFreq                  0x13
#define PPSMC_MSG_SetSoftMaxByFreq                  0x14
#define PPSMC_MSG_GetMinDpmFreq                     0x15
#define PPSMC_MSG_GetMaxDpmFreq                     0x16
#define PPSMC_MSG_GetDpmFreqByIndex                 0x17
#define PPSMC_MSG_SetPptLimit                       0x18
#define PPSMC_MSG_GetPptLimit                       0x19
#define PPSMC_MSG_DramLogSetDramAddrHigh            0x1A
#define PPSMC_MSG_DramLogSetDramAddrLow             0x1B
#define PPSMC_MSG_DramLogSetDramSize                0x1C
#define PPSMC_MSG_GetDebugData                      0x1D
#define PPSMC_MSG_HeavySBR                          0x1E
#define PPSMC_MSG_SetNumBadHbmPagesRetired          0x1F
#define PPSMC_MSG_DFCstateControl                   0x20
#define PPSMC_MSG_GetGmiPwrDnHyst                   0x21
#define PPSMC_MSG_SetGmiPwrDnHyst                   0x22
#define PPSMC_MSG_GmiPwrDnControl                   0x23
#define PPSMC_MSG_EnterGfxoff                       0x24
#define PPSMC_MSG_ExitGfxoff                        0x25
#define PPSMC_MSG_EnableDeterminism                 0x26
#define PPSMC_MSG_DisableDeterminism                0x27
#define PPSMC_MSG_DumpSTBtoDram                     0x28
#define PPSMC_MSG_STBtoDramLogSetDramAddrHigh       0x29
#define PPSMC_MSG_STBtoDramLogSetDramAddrLow        0x2A
#define PPSMC_MSG_STBtoDramLogSetDramSize           0x2B
#define PPSMC_MSG_SetSystemVirtualSTBtoDramAddrHigh 0x2C
#define PPSMC_MSG_SetSystemVirtualSTBtoDramAddrLow  0x2D
#define PPSMC_MSG_GfxDriverResetRecovery            0x2E
#define PPSMC_MSG_TriggerVFFLR                      0x2F
#define PPSMC_MSG_SetSoftMinGfxClk                  0x30
#define PPSMC_MSG_SetSoftMaxGfxClk                  0x31
#define PPSMC_MSG_GetMinGfxDpmFreq                  0x32
#define PPSMC_MSG_GetMaxGfxDpmFreq                  0x33
#define PPSMC_MSG_PrepareForDriverUnload            0x34
#define PPSMC_MSG_ReadThrottlerLimit                0x35
#define PPSMC_MSG_QueryValidMcaCount                0x36
#define PPSMC_MSG_McaBankDumpDW                     0x37
#define PPSMC_MSG_GetCTFLimit                       0x38
#define PPSMC_MSG_ClearMcaOnRead                    0x39
#define PPSMC_MSG_QueryValidMcaCeCount              0x3A
#define PPSMC_MSG_McaBankCeDumpDW                   0x3B
#define PPSMC_MSG_SelectPLPDMode                    0x40
#define PPSMC_Message_Count                         0x41

//PPSMC Reset Types for driver msg argument
#define PPSMC_RESET_TYPE_DRIVER_MODE_1_RESET        0x1
#define PPSMC_RESET_TYPE_DRIVER_MODE_2_RESET	      0x2
#define PPSMC_RESET_TYPE_DRIVER_MODE_3_RESET        0x3

//PPSMC Reset Types for driver msg argument
#define PPSMC_THROTTLING_LIMIT_TYPE_SOCKET          0x1
#define PPSMC_THROTTLING_LIMIT_TYPE_HBM             0x2

//CTF/Throttle Limit types
#define PPSMC_AID_THM_TYPE                          0x1
#define PPSMC_CCD_THM_TYPE                          0x2
#define PPSMC_XCD_THM_TYPE                          0x3
#define PPSMC_HBM_THM_TYPE                          0x4

//PLPD modes
#define PPSMC_PLPD_MODE_DEFAULT                     0x1
#define PPSMC_PLPD_MODE_OPTIMIZED                   0x2

typedef uint32_t PPSMC_Result;
typedef uint32_t PPSMC_MSG;

#endif
