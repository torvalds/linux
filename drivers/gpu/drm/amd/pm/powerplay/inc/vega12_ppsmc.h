/*
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#ifndef VEGA12_PP_SMC_H
#define VEGA12_PP_SMC_H

#pragma pack(push, 1)

#define SMU_UCODE_VERSION                  0x00270a00

/* SMU Response Codes: */
#define PPSMC_Result_OK                    0x1
#define PPSMC_Result_Failed                0xFF
#define PPSMC_Result_UnknownCmd            0xFE
#define PPSMC_Result_CmdRejectedPrereq     0xFD
#define PPSMC_Result_CmdRejectedBusy       0xFC

#define PPSMC_MSG_TestMessage                    0x1
#define PPSMC_MSG_GetSmuVersion                  0x2
#define PPSMC_MSG_GetDriverIfVersion             0x3
#define PPSMC_MSG_SetAllowedFeaturesMaskLow      0x4
#define PPSMC_MSG_SetAllowedFeaturesMaskHigh     0x5
#define PPSMC_MSG_EnableAllSmuFeatures           0x6
#define PPSMC_MSG_DisableAllSmuFeatures          0x7
#define PPSMC_MSG_EnableSmuFeaturesLow           0x8
#define PPSMC_MSG_EnableSmuFeaturesHigh          0x9
#define PPSMC_MSG_DisableSmuFeaturesLow          0xA
#define PPSMC_MSG_DisableSmuFeaturesHigh         0xB
#define PPSMC_MSG_GetEnabledSmuFeaturesLow       0xC
#define PPSMC_MSG_GetEnabledSmuFeaturesHigh      0xD
#define PPSMC_MSG_SetWorkloadMask                0xE
#define PPSMC_MSG_SetPptLimit                    0xF
#define PPSMC_MSG_SetDriverDramAddrHigh          0x10
#define PPSMC_MSG_SetDriverDramAddrLow           0x11
#define PPSMC_MSG_SetToolsDramAddrHigh           0x12
#define PPSMC_MSG_SetToolsDramAddrLow            0x13
#define PPSMC_MSG_TransferTableSmu2Dram          0x14
#define PPSMC_MSG_TransferTableDram2Smu          0x15
#define PPSMC_MSG_UseDefaultPPTable              0x16
#define PPSMC_MSG_UseBackupPPTable               0x17
#define PPSMC_MSG_RunBtc                         0x18
#define PPSMC_MSG_RequestI2CBus                  0x19
#define PPSMC_MSG_ReleaseI2CBus                  0x1A
#define PPSMC_MSG_SetFloorSocVoltage             0x21
#define PPSMC_MSG_SoftReset                      0x22
#define PPSMC_MSG_StartBacoMonitor               0x23
#define PPSMC_MSG_CancelBacoMonitor              0x24
#define PPSMC_MSG_EnterBaco                      0x25
#define PPSMC_MSG_SetSoftMinByFreq               0x26
#define PPSMC_MSG_SetSoftMaxByFreq               0x27
#define PPSMC_MSG_SetHardMinByFreq               0x28
#define PPSMC_MSG_SetHardMaxByFreq               0x29
#define PPSMC_MSG_GetMinDpmFreq                  0x2A
#define PPSMC_MSG_GetMaxDpmFreq                  0x2B
#define PPSMC_MSG_GetDpmFreqByIndex              0x2C
#define PPSMC_MSG_GetDpmClockFreq                0x2D
#define PPSMC_MSG_GetSsVoltageByDpm              0x2E
#define PPSMC_MSG_SetMemoryChannelConfig         0x2F
#define PPSMC_MSG_SetGeminiMode                  0x30
#define PPSMC_MSG_SetGeminiApertureHigh          0x31
#define PPSMC_MSG_SetGeminiApertureLow           0x32
#define PPSMC_MSG_SetMinLinkDpmByIndex           0x33
#define PPSMC_MSG_OverridePcieParameters         0x34
#define PPSMC_MSG_OverDriveSetPercentage         0x35
#define PPSMC_MSG_SetMinDeepSleepDcefclk         0x36
#define PPSMC_MSG_ReenableAcDcInterrupt          0x37
#define PPSMC_MSG_NotifyPowerSource              0x38
#define PPSMC_MSG_SetUclkFastSwitch              0x39
#define PPSMC_MSG_SetUclkDownHyst                0x3A
#define PPSMC_MSG_GfxDeviceDriverReset           0x3B
#define PPSMC_MSG_GetCurrentRpm                  0x3C
#define PPSMC_MSG_SetVideoFps                    0x3D
#define PPSMC_MSG_SetTjMax                       0x3E
#define PPSMC_MSG_SetFanTemperatureTarget        0x3F
#define PPSMC_MSG_PrepareMp1ForUnload            0x40
#define PPSMC_MSG_DramLogSetDramAddrHigh         0x41
#define PPSMC_MSG_DramLogSetDramAddrLow          0x42
#define PPSMC_MSG_DramLogSetDramSize             0x43
#define PPSMC_MSG_SetFanMaxRpm                   0x44
#define PPSMC_MSG_SetFanMinPwm                   0x45
#define PPSMC_MSG_ConfigureGfxDidt               0x46
#define PPSMC_MSG_NumOfDisplays                  0x47
#define PPSMC_MSG_RemoveMargins                  0x48
#define PPSMC_MSG_ReadSerialNumTop32             0x49
#define PPSMC_MSG_ReadSerialNumBottom32          0x4A
#define PPSMC_MSG_SetSystemVirtualDramAddrHigh   0x4B
#define PPSMC_MSG_SetSystemVirtualDramAddrLow    0x4C
#define PPSMC_MSG_RunAcgBtc                      0x4D
#define PPSMC_MSG_InitializeAcg                  0x4E
#define PPSMC_MSG_EnableAcgBtcTestMode           0x4F
#define PPSMC_MSG_EnableAcgSpreadSpectrum        0x50
#define PPSMC_MSG_AllowGfxOff                    0x51
#define PPSMC_MSG_DisallowGfxOff                 0x52
#define PPSMC_MSG_GetPptLimit                    0x53
#define PPSMC_MSG_GetDcModeMaxDpmFreq            0x54
#define PPSMC_Message_Count                      0x56

typedef uint16_t PPSMC_Result;
typedef int PPSMC_Msg;

#pragma pack(pop)

#endif
