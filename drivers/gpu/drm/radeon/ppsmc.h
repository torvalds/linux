/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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

#define PPSMC_SWSTATE_FLAG_DC                           0x01
#define PPSMC_SWSTATE_FLAG_UVD                          0x02
#define PPSMC_SWSTATE_FLAG_VCE                          0x04
#define PPSMC_SWSTATE_FLAG_PCIE_X1                      0x08

#define PPSMC_THERMAL_PROTECT_TYPE_INTERNAL             0x00
#define PPSMC_THERMAL_PROTECT_TYPE_EXTERNAL             0x01
#define PPSMC_THERMAL_PROTECT_TYPE_NONE                 0xff

#define PPSMC_SYSTEMFLAG_GPIO_DC                        0x01
#define PPSMC_SYSTEMFLAG_STEPVDDC                       0x02
#define PPSMC_SYSTEMFLAG_GDDR5                          0x04
#define PPSMC_SYSTEMFLAG_DISABLE_BABYSTEP               0x08
#define PPSMC_SYSTEMFLAG_REGULATOR_HOT                  0x10
#define PPSMC_SYSTEMFLAG_REGULATOR_HOT_ANALOG           0x20
#define PPSMC_SYSTEMFLAG_REGULATOR_HOT_PROG_GPIO        0x40

#define PPSMC_EXTRAFLAGS_AC2DC_ACTION_MASK              0x07
#define PPSMC_EXTRAFLAGS_AC2DC_DONT_WAIT_FOR_VBLANK     0x08
#define PPSMC_EXTRAFLAGS_AC2DC_ACTION_GOTODPMLOWSTATE   0x00
#define PPSMC_EXTRAFLAGS_AC2DC_ACTION_GOTOINITIALSTATE  0x01
#define PPSMC_EXTRAFLAGS_AC2DC_GPIO5_POLARITY_HIGH      0x02

#define PPSMC_DISPLAY_WATERMARK_LOW                     0
#define PPSMC_DISPLAY_WATERMARK_HIGH                    1

#define PPSMC_STATEFLAG_AUTO_PULSE_SKIP    0x01
#define PPSMC_STATEFLAG_POWERBOOST         0x02
#define PPSMC_STATEFLAG_DEEPSLEEP_THROTTLE 0x20
#define PPSMC_STATEFLAG_DEEPSLEEP_BYPASS   0x40

#define PPSMC_Result_OK             ((uint8_t)0x01)
#define PPSMC_Result_Failed         ((uint8_t)0xFF)

typedef uint8_t PPSMC_Result;

#define PPSMC_MSG_Halt                      ((uint8_t)0x10)
#define PPSMC_MSG_Resume                    ((uint8_t)0x11)
#define PPSMC_MSG_ZeroLevelsDisabled        ((uint8_t)0x13)
#define PPSMC_MSG_OneLevelsDisabled         ((uint8_t)0x14)
#define PPSMC_MSG_TwoLevelsDisabled         ((uint8_t)0x15)
#define PPSMC_MSG_EnableThermalInterrupt    ((uint8_t)0x16)
#define PPSMC_MSG_RunningOnAC               ((uint8_t)0x17)
#define PPSMC_MSG_SwitchToSwState           ((uint8_t)0x20)
#define PPSMC_MSG_SwitchToInitialState      ((uint8_t)0x40)
#define PPSMC_MSG_NoForcedLevel             ((uint8_t)0x41)
#define PPSMC_MSG_ForceHigh                 ((uint8_t)0x42)
#define PPSMC_MSG_ForceMediumOrHigh         ((uint8_t)0x43)
#define PPSMC_MSG_SwitchToMinimumPower      ((uint8_t)0x51)
#define PPSMC_MSG_ResumeFromMinimumPower    ((uint8_t)0x52)
#define PPSMC_MSG_EnableCac                 ((uint8_t)0x53)
#define PPSMC_MSG_DisableCac                ((uint8_t)0x54)
#define PPSMC_TDPClampingActive             ((uint8_t)0x59)
#define PPSMC_TDPClampingInactive           ((uint8_t)0x5A)
#define PPSMC_MSG_NoDisplay                 ((uint8_t)0x5D)
#define PPSMC_MSG_HasDisplay                ((uint8_t)0x5E)
#define PPSMC_MSG_UVDPowerOFF               ((uint8_t)0x60)
#define PPSMC_MSG_UVDPowerON                ((uint8_t)0x61)
#define PPSMC_MSG_EnableULV                 ((uint8_t)0x62)
#define PPSMC_MSG_DisableULV                ((uint8_t)0x63)
#define PPSMC_MSG_EnterULV                  ((uint8_t)0x64)
#define PPSMC_MSG_ExitULV                   ((uint8_t)0x65)
#define PPSMC_CACLongTermAvgEnable          ((uint8_t)0x6E)
#define PPSMC_CACLongTermAvgDisable         ((uint8_t)0x6F)
#define PPSMC_MSG_CollectCAC_PowerCorreln   ((uint8_t)0x7A)
#define PPSMC_FlushDataCache                ((uint8_t)0x80)
#define PPSMC_MSG_SetEnabledLevels          ((uint8_t)0x82)
#define PPSMC_MSG_SetForcedLevels           ((uint8_t)0x83)
#define PPSMC_MSG_ResetToDefaults           ((uint8_t)0x84)
#define PPSMC_MSG_EnableDTE                 ((uint8_t)0x87)
#define PPSMC_MSG_DisableDTE                ((uint8_t)0x88)
#define PPSMC_MSG_ThrottleOVRDSCLKDS        ((uint8_t)0x96)
#define PPSMC_MSG_CancelThrottleOVRDSCLKDS  ((uint8_t)0x97)

/* TN */
#define PPSMC_MSG_DPM_Config                ((uint32_t) 0x102)
#define PPSMC_MSG_DPM_ForceState            ((uint32_t) 0x104)
#define PPSMC_MSG_PG_SIMD_Config            ((uint32_t) 0x108)
#define PPSMC_MSG_DPM_N_LevelsDisabled      ((uint32_t) 0x112)
#define PPSMC_MSG_DCE_RemoveVoltageAdjustment   ((uint32_t) 0x11d)
#define PPSMC_MSG_DCE_AllowVoltageAdjustment    ((uint32_t) 0x11e)
#define PPSMC_MSG_EnableBAPM                ((uint32_t) 0x120)
#define PPSMC_MSG_DisableBAPM               ((uint32_t) 0x121)
#define PPSMC_MSG_UVD_DPM_Config            ((uint32_t) 0x124)


typedef uint16_t PPSMC_Msg;

#pragma pack(pop)

#endif
