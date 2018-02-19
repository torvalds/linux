/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#ifndef PP_SMC_H
#define PP_SMC_H

#pragma pack(push, 1)

#define SMU_UCODE_VERSION                  0x001c0800

/* SMU Response Codes: */
#define PPSMC_Result_OK                    0x1
#define PPSMC_Result_Failed                0xFF
#define PPSMC_Result_UnknownCmd            0xFE
#define PPSMC_Result_CmdRejectedPrereq     0xFD
#define PPSMC_Result_CmdRejectedBusy       0xFC

typedef uint16_t PPSMC_Result;

/* Message Definitions */
#define PPSMC_MSG_TestMessage                    0x1
#define PPSMC_MSG_GetSmuVersion                  0x2
#define PPSMC_MSG_GetDriverIfVersion             0x3
#define PPSMC_MSG_EnableSmuFeatures              0x4
#define PPSMC_MSG_DisableSmuFeatures             0x5
#define PPSMC_MSG_GetEnabledSmuFeatures          0x6
#define PPSMC_MSG_SetWorkloadMask                0x7
#define PPSMC_MSG_SetPptLimit                    0x8
#define PPSMC_MSG_SetDriverDramAddrHigh          0x9
#define PPSMC_MSG_SetDriverDramAddrLow           0xA
#define PPSMC_MSG_SetToolsDramAddrHigh           0xB
#define PPSMC_MSG_SetToolsDramAddrLow            0xC
#define PPSMC_MSG_TransferTableSmu2Dram          0xD
#define PPSMC_MSG_TransferTableDram2Smu          0xE
#define PPSMC_MSG_UseDefaultPPTable              0xF
#define PPSMC_MSG_UseBackupPPTable               0x10
#define PPSMC_MSG_RunBtc                         0x11
#define PPSMC_MSG_RequestI2CBus                  0x12
#define PPSMC_MSG_ReleaseI2CBus                  0x13
#define PPSMC_MSG_ConfigureTelemetry             0x14
#define PPSMC_MSG_SetUlvIpMask                   0x15
#define PPSMC_MSG_SetSocVidOffset                0x16
#define PPSMC_MSG_SetMemVidOffset                0x17
#define PPSMC_MSG_GetSocVidOffset                0x18
#define PPSMC_MSG_GetMemVidOffset                0x19
#define PPSMC_MSG_SetFloorSocVoltage             0x1A
#define PPSMC_MSG_SoftReset                      0x1B
#define PPSMC_MSG_StartBacoMonitor               0x1C
#define PPSMC_MSG_CancelBacoMonitor              0x1D
#define PPSMC_MSG_EnterBaco                      0x1E
#define PPSMC_MSG_AllowLowGfxclkInterrupt        0x1F
#define PPSMC_MSG_SetLowGfxclkInterruptThreshold 0x20
#define PPSMC_MSG_SetSoftMinGfxclkByIndex        0x21
#define PPSMC_MSG_SetSoftMaxGfxclkByIndex        0x22
#define PPSMC_MSG_GetCurrentGfxclkIndex          0x23
#define PPSMC_MSG_SetSoftMinUclkByIndex          0x24
#define PPSMC_MSG_SetSoftMaxUclkByIndex          0x25
#define PPSMC_MSG_GetCurrentUclkIndex            0x26
#define PPSMC_MSG_SetSoftMinUvdByIndex           0x27
#define PPSMC_MSG_SetSoftMaxUvdByIndex           0x28
#define PPSMC_MSG_GetCurrentUvdIndex             0x29
#define PPSMC_MSG_SetSoftMinVceByIndex           0x2A
#define PPSMC_MSG_SetSoftMaxVceByIndex           0x2B
#define PPSMC_MSG_SetHardMinVceByIndex           0x2C
#define PPSMC_MSG_GetCurrentVceIndex             0x2D
#define PPSMC_MSG_SetSoftMinSocclkByIndex        0x2E
#define PPSMC_MSG_SetHardMinSocclkByIndex        0x2F
#define PPSMC_MSG_SetSoftMaxSocclkByIndex        0x30
#define PPSMC_MSG_GetCurrentSocclkIndex          0x31
#define PPSMC_MSG_SetMinLinkDpmByIndex           0x32
#define PPSMC_MSG_GetCurrentLinkIndex            0x33
#define PPSMC_MSG_GetAverageGfxclkFrequency      0x34
#define PPSMC_MSG_GetAverageSocclkFrequency      0x35
#define PPSMC_MSG_GetAverageUclkFrequency        0x36
#define PPSMC_MSG_GetAverageGfxActivity          0x37
#define PPSMC_MSG_GetTemperatureEdge             0x38
#define PPSMC_MSG_GetTemperatureHotspot          0x39
#define PPSMC_MSG_GetTemperatureHBM              0x3A
#define PPSMC_MSG_GetTemperatureVrSoc            0x3B
#define PPSMC_MSG_GetTemperatureVrMem            0x3C
#define PPSMC_MSG_GetTemperatureLiquid           0x3D
#define PPSMC_MSG_GetTemperaturePlx              0x3E
#define PPSMC_MSG_OverDriveSetPercentage         0x3F
#define PPSMC_MSG_SetMinDeepSleepDcefclk         0x40
#define PPSMC_MSG_SwitchToAC                     0x41
#define PPSMC_MSG_SetUclkFastSwitch              0x42
#define PPSMC_MSG_SetUclkDownHyst                0x43
#define PPSMC_MSG_RemoveDCClamp                  0x44
#define PPSMC_MSG_GfxDeviceDriverReset           0x45
#define PPSMC_MSG_GetCurrentRpm                  0x46
#define PPSMC_MSG_SetVideoFps                    0x47
#define PPSMC_MSG_SetCustomGfxDpmParameters      0x48
#define PPSMC_MSG_SetTjMax                       0x49
#define PPSMC_MSG_SetFanTemperatureTarget        0x4A
#define PPSMC_MSG_PrepareMp1ForUnload            0x4B
#define PPSMC_MSG_RequestDisplayClockByFreq      0x4C
#define PPSMC_MSG_GetClockFreqMHz                0x4D
#define PPSMC_MSG_DramLogSetDramAddrHigh         0x4E
#define PPSMC_MSG_DramLogSetDramAddrLow          0x4F
#define PPSMC_MSG_DramLogSetDramSize             0x50
#define PPSMC_MSG_SetFanMaxRpm                   0x51
#define PPSMC_MSG_SetFanMinPwm                   0x52
#define PPSMC_MSG_ConfigureGfxDidt               0x55
#define PPSMC_MSG_NumOfDisplays                  0x56
#define PPSMC_MSG_ReadSerialNumTop32             0x58
#define PPSMC_MSG_ReadSerialNumBottom32          0x59
#define PPSMC_MSG_SetSystemVirtualDramAddrHigh   0x5A
#define PPSMC_MSG_SetSystemVirtualDramAddrLow    0x5B
#define PPSMC_MSG_RunAcgBtc                      0x5C
#define PPSMC_MSG_RunAcgInClosedLoop             0x5D
#define PPSMC_MSG_RunAcgInOpenLoop               0x5E
#define PPSMC_MSG_InitializeAcg                  0x5F
#define PPSMC_MSG_GetCurrPkgPwr                  0x61
#define PPSMC_MSG_UpdatePkgPwrPidAlpha           0x68
#define PPSMC_Message_Count                      0x69


typedef int PPSMC_Msg;

#pragma pack(pop)

#endif
