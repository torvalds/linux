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

#define PPSMC_THERMAL_PROTECT_TYPE_INTERNAL             0x00
#define PPSMC_THERMAL_PROTECT_TYPE_EXTERNAL             0x01
#define PPSMC_THERMAL_PROTECT_TYPE_NONE                 0xff

#define PPSMC_SYSTEMFLAG_GPIO_DC                        0x01
#define PPSMC_SYSTEMFLAG_STEPVDDC                       0x02
#define PPSMC_SYSTEMFLAG_GDDR5                          0x04
#define PPSMC_SYSTEMFLAG_DISABLE_BABYSTEP               0x08
#define PPSMC_SYSTEMFLAG_REGULATOR_HOT                  0x10

#define PPSMC_EXTRAFLAGS_AC2DC_ACTION_MASK              0x07
#define PPSMC_EXTRAFLAGS_AC2DC_DONT_WAIT_FOR_VBLANK     0x08
#define PPSMC_EXTRAFLAGS_AC2DC_ACTION_GOTODPMLOWSTATE   0x00
#define PPSMC_EXTRAFLAGS_AC2DC_ACTION_GOTOINITIALSTATE  0x01

#define PPSMC_DISPLAY_WATERMARK_LOW                     0
#define PPSMC_DISPLAY_WATERMARK_HIGH                    1

#define PPSMC_STATEFLAG_AUTO_PULSE_SKIP    0x01

#define PPSMC_Result_OK             ((uint8_t)0x01)
#define PPSMC_Result_Failed         ((uint8_t)0xFF)

typedef uint8_t PPSMC_Result;

#define PPSMC_MSG_Halt                      ((uint8_t)0x10)
#define PPSMC_MSG_Resume                    ((uint8_t)0x11)
#define PPSMC_MSG_ZeroLevelsDisabled        ((uint8_t)0x13)
#define PPSMC_MSG_OneLevelsDisabled         ((uint8_t)0x14)
#define PPSMC_MSG_TwoLevelsDisabled         ((uint8_t)0x15)
#define PPSMC_MSG_EnableThermalInterrupt    ((uint8_t)0x16)
#define PPSMC_MSG_SwitchToSwState           ((uint8_t)0x20)
#define PPSMC_MSG_SwitchToInitialState      ((uint8_t)0x40)
#define PPSMC_MSG_NoForcedLevel             ((uint8_t)0x41)
#define PPSMC_MSG_SwitchToMinimumPower      ((uint8_t)0x51)
#define PPSMC_MSG_ResumeFromMinimumPower    ((uint8_t)0x52)
#define PPSMC_MSG_NoDisplay                 ((uint8_t)0x5D)
#define PPSMC_MSG_HasDisplay                ((uint8_t)0x5E)
#define PPSMC_MSG_EnableULV                 ((uint8_t)0x62)
#define PPSMC_MSG_DisableULV                ((uint8_t)0x63)
#define PPSMC_MSG_EnterULV                  ((uint8_t)0x64)
#define PPSMC_MSG_ExitULV                   ((uint8_t)0x65)
#define PPSMC_MSG_ResetToDefaults           ((uint8_t)0x84)

typedef uint8_t PPSMC_Msg;

#pragma pack(pop)

#endif
