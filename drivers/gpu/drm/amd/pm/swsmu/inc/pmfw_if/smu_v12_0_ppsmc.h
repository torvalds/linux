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

#ifndef SMU_12_0_PPSMC_H
#define SMU_12_0_PPSMC_H

// SMU Response Codes:
#define PPSMC_Result_OK                    0x1
#define PPSMC_Result_Failed                0xFF
#define PPSMC_Result_UnknownCmd            0xFE
#define PPSMC_Result_CmdRejectedPrereq     0xFD
#define PPSMC_Result_CmdRejectedBusy       0xFC


// Message Definitions:
#define PPSMC_MSG_TestMessage                   0x1
#define PPSMC_MSG_GetSmuVersion                 0x2
#define PPSMC_MSG_GetDriverIfVersion            0x3
#define PPSMC_MSG_PowerUpGfx                    0x6
#define PPSMC_MSG_EnableGfxOff                  0x7
#define PPSMC_MSG_DisableGfxOff                 0x8
#define PPSMC_MSG_PowerDownIspByTile            0x9 // ISP is power gated by default
#define PPSMC_MSG_PowerUpIspByTile              0xA
#define PPSMC_MSG_PowerDownVcn                  0xB // VCN is power gated by default
#define PPSMC_MSG_PowerUpVcn                    0xC
#define PPSMC_MSG_PowerDownSdma                 0xD // SDMA is power gated by default
#define PPSMC_MSG_PowerUpSdma                   0xE
#define PPSMC_MSG_SetHardMinIspclkByFreq        0xF
#define PPSMC_MSG_SetHardMinVcn                 0x10 // For wireless display
#define PPSMC_MSG_spare1                        0x11
#define PPSMC_MSG_spare2                        0x12
#define PPSMC_MSG_SetAllowFclkSwitch            0x13
#define PPSMC_MSG_SetMinVideoGfxclkFreq         0x14
#define PPSMC_MSG_ActiveProcessNotify           0x15
#define PPSMC_MSG_SetCustomPolicy               0x16
#define PPSMC_MSG_SetVideoFps                   0x17
#define PPSMC_MSG_SetDisplayCount               0x18 // Moved to VBIOS
#define PPSMC_MSG_QueryPowerLimit               0x19 //Driver to look up sustainable clocks for VQ
#define PPSMC_MSG_SetDriverDramAddrHigh         0x1A
#define PPSMC_MSG_SetDriverDramAddrLow          0x1B
#define PPSMC_MSG_TransferTableSmu2Dram         0x1C
#define PPSMC_MSG_TransferTableDram2Smu         0x1D
#define PPSMC_MSG_GfxDeviceDriverReset          0x1E
#define PPSMC_MSG_SetGfxclkOverdriveByFreqVid   0x1F
#define PPSMC_MSG_SetHardMinDcfclkByFreq        0x20 // Moved to VBIOS
#define PPSMC_MSG_SetHardMinSocclkByFreq        0x21
#define PPSMC_MSG_ControlIgpuATS                0x22
#define PPSMC_MSG_SetMinVideoFclkFreq           0x23
#define PPSMC_MSG_SetMinDeepSleepDcfclk         0x24 // Moved to VBIOS
#define PPSMC_MSG_ForcePowerDownGfx             0x25
#define PPSMC_MSG_SetPhyclkVoltageByFreq        0x26 // Moved to VBIOS
#define PPSMC_MSG_SetDppclkVoltageByFreq        0x27 // Moved to VBIOS and is SetDppclkFreq
#define PPSMC_MSG_SetSoftMinVcn                 0x28
#define PPSMC_MSG_EnablePostCode                0x29
#define PPSMC_MSG_GetGfxclkFrequency            0x2A
#define PPSMC_MSG_GetFclkFrequency              0x2B
#define PPSMC_MSG_GetMinGfxclkFrequency         0x2C
#define PPSMC_MSG_GetMaxGfxclkFrequency         0x2D
#define PPSMC_MSG_SoftReset                     0x2E // Not supported
#define PPSMC_MSG_SetGfxCGPG                    0x2F
#define PPSMC_MSG_SetSoftMaxGfxClk              0x30
#define PPSMC_MSG_SetHardMinGfxClk              0x31
#define PPSMC_MSG_SetSoftMaxSocclkByFreq        0x32
#define PPSMC_MSG_SetSoftMaxFclkByFreq          0x33
#define PPSMC_MSG_SetSoftMaxVcn                 0x34
#define PPSMC_MSG_PowerGateMmHub                0x35
#define PPSMC_MSG_UpdatePmeRestore              0x36 // Moved to VBIOS
#define PPSMC_MSG_GpuChangeState                0x37
#define PPSMC_MSG_SetPowerLimitPercentage       0x38
#define PPSMC_MSG_ForceGfxContentSave           0x39
#define PPSMC_MSG_EnableTmdp48MHzRefclkPwrDown  0x3A // Moved to VBIOS
#define PPSMC_MSG_PowerDownJpeg                 0x3B
#define PPSMC_MSG_PowerUpJpeg                   0x3C
#define PPSMC_MSG_PowerGateAtHub                0x3D
#define PPSMC_MSG_SetSoftMinJpeg                0x3E
#define PPSMC_MSG_SetHardMinFclkByFreq          0x3F
#define PPSMC_Message_Count                     0x40


//Argument for  PPSMC_MSG_GpuChangeState
enum {
  eGpuChangeState_D0Entry = 1,
  eGpuChangeState_D3Entry,
};

#endif
