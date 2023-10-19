/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef ARCTURUS_PP_SMC_H
#define ARCTURUS_PP_SMC_H

#pragma pack(push, 1)

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
#define PPSMC_MSG_ArmD3                          0x1A

//DPM
#define PPSMC_MSG_SetSoftMinByFreq               0x1B
#define PPSMC_MSG_SetSoftMaxByFreq               0x1C
#define PPSMC_MSG_SetHardMinByFreq               0x1D
#define PPSMC_MSG_SetHardMaxByFreq               0x1E
#define PPSMC_MSG_GetMinDpmFreq                  0x1F
#define PPSMC_MSG_GetMaxDpmFreq                  0x20
#define PPSMC_MSG_GetDpmFreqByIndex              0x21

#define PPSMC_MSG_SetWorkloadMask                0x22
#define PPSMC_MSG_SetDfSwitchType                0x23
#define PPSMC_MSG_GetVoltageByDpm                0x24
#define PPSMC_MSG_GetVoltageByDpmOverdrive       0x25

#define PPSMC_MSG_SetPptLimit                    0x26
#define PPSMC_MSG_GetPptLimit                    0x27

//Power Gating
#define PPSMC_MSG_PowerUpVcn0                    0x28
#define PPSMC_MSG_PowerDownVcn0                  0x29
#define PPSMC_MSG_PowerUpVcn1                    0x2A
#define PPSMC_MSG_PowerDownVcn1                  0x2B

//Resets and reload
#define PPSMC_MSG_PrepareMp1ForUnload            0x2C
#define PPSMC_MSG_PrepareMp1ForReset             0x2D
#define PPSMC_MSG_PrepareMp1ForShutdown          0x2E
#define PPSMC_MSG_SoftReset                      0x2F

//BTC
#define PPSMC_MSG_RunAfllBtc                     0x30
#define PPSMC_MSG_RunDcBtc                       0x31

//Debug
#define PPSMC_MSG_DramLogSetDramAddrHigh         0x33
#define PPSMC_MSG_DramLogSetDramAddrLow          0x34
#define PPSMC_MSG_DramLogSetDramSize             0x35
#define PPSMC_MSG_GetDebugData                   0x36

//WAFL and XGMI
#define PPSMC_MSG_WaflTest                       0x37
#define PPSMC_MSG_SetXgmiMode                    0x38

//Others
#define PPSMC_MSG_SetMemoryChannelEnable         0x39

//OOB
#define PPSMC_MSG_SetNumBadHbmPagesRetired	 0x3A

#define PPSMC_MSG_DFCstateControl		 0x3B
#define PPSMC_MSG_GmiPwrDnControl                0x3D
#define PPSMC_Message_Count                      0x3E

#define PPSMC_MSG_ReadSerialNumTop32		 0x40
#define PPSMC_MSG_ReadSerialNumBottom32		 0x41

/* parameter for MSG_LightSBR
 * 1 -- Enable light secondary bus reset, only do nbio respond without further handling,
 *      leave driver to handle the real reset
 * 0 -- Disable LightSBR, default behavior, SMU will pass the reset to PSP
 */
#define PPSMC_MSG_LightSBR			 0x42

typedef uint32_t PPSMC_Result;
typedef uint32_t PPSMC_Msg;
#pragma pack(pop)

#endif
