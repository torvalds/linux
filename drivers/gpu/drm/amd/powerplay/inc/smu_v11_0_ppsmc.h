/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef SMU_V11_0_PPSMC_H
#define SMU_V11_0_PPSMC_H

// SMU Response Codes:
#define PPSMC_Result_OK                    0x1
#define PPSMC_Result_Failed                0xFF
#define PPSMC_Result_UnknownCmd            0xFE
#define PPSMC_Result_CmdRejectedPrereq     0xFD
#define PPSMC_Result_CmdRejectedBusy       0xFC

// Message Definitions:
// BASIC
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
#define PPSMC_MSG_SetDriverDramAddrHigh          0xE
#define PPSMC_MSG_SetDriverDramAddrLow           0xF
#define PPSMC_MSG_SetToolsDramAddrHigh           0x10
#define PPSMC_MSG_SetToolsDramAddrLow            0x11
#define PPSMC_MSG_TransferTableSmu2Dram          0x12
#define PPSMC_MSG_TransferTableDram2Smu          0x13
#define PPSMC_MSG_UseDefaultPPTable              0x14
#define PPSMC_MSG_UseBackupPPTable               0x15
#define PPSMC_MSG_SetSystemVirtualDramAddrHigh   0x16
#define PPSMC_MSG_SetSystemVirtualDramAddrLow    0x17

//BACO/BAMACO/BOMACO
#define PPSMC_MSG_EnterBaco                      0x18
#define PPSMC_MSG_ExitBaco                       0x19

//DPM
#define PPSMC_MSG_SetSoftMinByFreq               0x1A
#define PPSMC_MSG_SetSoftMaxByFreq               0x1B
#define PPSMC_MSG_SetHardMinByFreq               0x1C
#define PPSMC_MSG_SetHardMaxByFreq               0x1D 
#define PPSMC_MSG_GetMinDpmFreq                  0x1E
#define PPSMC_MSG_GetMaxDpmFreq                  0x1F
#define PPSMC_MSG_GetDpmFreqByIndex              0x20
#define PPSMC_MSG_OverridePcieParameters         0x21
#define PPSMC_MSG_SetMinDeepSleepDcefclk         0x22
#define PPSMC_MSG_SetWorkloadMask                0x23 
#define PPSMC_MSG_SetUclkFastSwitch              0x24
#define PPSMC_MSG_GetAvfsVoltageByDpm            0x25
#define PPSMC_MSG_SetVideoFps                    0x26
#define PPSMC_MSG_GetDcModeMaxDpmFreq            0x27

//Power Gating
#define PPSMC_MSG_AllowGfxOff                    0x28
#define PPSMC_MSG_DisallowGfxOff                 0x29
#define PPSMC_MSG_PowerUpVcn					 0x2A
#define PPSMC_MSG_PowerDownVcn					 0x2B	
#define PPSMC_MSG_PowerUpJpeg                    0x2C
#define PPSMC_MSG_PowerDownJpeg					 0x2D
//reserve 0x2A to 0x2F for PG harvesting TBD

//I2C Interface
#define PPSMC_RequestI2cTransaction              0x30

//Resets
#define PPSMC_MSG_SoftReset                      0x31  //FIXME Need confirmation from driver
#define PPSMC_MSG_PrepareMp1ForUnload            0x32
#define PPSMC_MSG_PrepareMp1ForReset             0x33
#define PPSMC_MSG_PrepareMp1ForShutdown          0x34

//ACDC Power Source
#define PPSMC_MSG_SetPptLimit                    0x35
#define PPSMC_MSG_GetPptLimit                    0x36
#define PPSMC_MSG_ReenableAcDcInterrupt          0x37
#define PPSMC_MSG_NotifyPowerSource              0x38
//#define PPSMC_MSG_GfxDeviceDriverReset           0x39 //FIXME mode1 and 2 resets will go directly go PSP

//BTC
#define PPSMC_MSG_RunBtc                         0x3A

//Debug
#define PPSMC_MSG_DramLogSetDramAddrHigh         0x3B
#define PPSMC_MSG_DramLogSetDramAddrLow          0x3C
#define PPSMC_MSG_DramLogSetDramSize             0x3D
#define PPSMC_MSG_GetDebugData                   0x3E

//Others
#define PPSMC_MSG_ConfigureGfxDidt               0x3F
#define PPSMC_MSG_NumOfDisplays                  0x40

#define PPSMC_MSG_SetMemoryChannelConfig         0x41 
#define PPSMC_MSG_SetGeminiMode                  0x42
#define PPSMC_MSG_SetGeminiApertureHigh          0x43
#define PPSMC_MSG_SetGeminiApertureLow           0x44

#define PPSMC_Message_Count                      0x45

typedef uint32_t PPSMC_Result;
typedef uint32_t PPSMC_Msg;

#endif
