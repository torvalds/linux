/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef CZ_PP_SMC_H
#define CZ_PP_SMC_H

#pragma pack(push, 1)

/* Fan control algorithm:*/
#define FDO_MODE_HARDWARE 0
#define FDO_MODE_PIECE_WISE_LINEAR 1

enum FAN_CONTROL {
    FAN_CONTROL_FUZZY,
    FAN_CONTROL_TABLE
};

enum DPM_ARRAY {
    DPM_ARRAY_HARD_MAX,
    DPM_ARRAY_HARD_MIN,
    DPM_ARRAY_SOFT_MAX,
    DPM_ARRAY_SOFT_MIN
};

/*
 * Return codes for driver to SMC communication.
 * Leave these #define-s, enums might not be exactly 8-bits on the microcontroller.
 */
#define PPSMC_Result_OK             ((uint16_t)0x01)
#define PPSMC_Result_NoMore         ((uint16_t)0x02)
#define PPSMC_Result_NotNow         ((uint16_t)0x03)
#define PPSMC_Result_Failed         ((uint16_t)0xFF)
#define PPSMC_Result_UnknownCmd     ((uint16_t)0xFE)
#define PPSMC_Result_UnknownVT      ((uint16_t)0xFD)

#define PPSMC_isERROR(x)            ((uint16_t)0x80 & (x))

/*
 * Supported driver messages
 */
#define PPSMC_MSG_Test                        ((uint16_t) 0x1)
#define PPSMC_MSG_GetFeatureStatus            ((uint16_t) 0x2)
#define PPSMC_MSG_EnableAllSmuFeatures        ((uint16_t) 0x3)
#define PPSMC_MSG_DisableAllSmuFeatures       ((uint16_t) 0x4)
#define PPSMC_MSG_OptimizeBattery             ((uint16_t) 0x5)
#define PPSMC_MSG_MaximizePerf                ((uint16_t) 0x6)
#define PPSMC_MSG_UVDPowerOFF                 ((uint16_t) 0x7)
#define PPSMC_MSG_UVDPowerON                  ((uint16_t) 0x8)
#define PPSMC_MSG_VCEPowerOFF                 ((uint16_t) 0x9)
#define PPSMC_MSG_VCEPowerON                  ((uint16_t) 0xA)
#define PPSMC_MSG_ACPPowerOFF                 ((uint16_t) 0xB)
#define PPSMC_MSG_ACPPowerON                  ((uint16_t) 0xC)
#define PPSMC_MSG_SDMAPowerOFF                ((uint16_t) 0xD)
#define PPSMC_MSG_SDMAPowerON                 ((uint16_t) 0xE)
#define PPSMC_MSG_XDMAPowerOFF                ((uint16_t) 0xF)
#define PPSMC_MSG_XDMAPowerON                 ((uint16_t) 0x10)
#define PPSMC_MSG_SetMinDeepSleepSclk         ((uint16_t) 0x11)
#define PPSMC_MSG_SetSclkSoftMin              ((uint16_t) 0x12)
#define PPSMC_MSG_SetSclkSoftMax              ((uint16_t) 0x13)
#define PPSMC_MSG_SetSclkHardMin              ((uint16_t) 0x14)
#define PPSMC_MSG_SetSclkHardMax              ((uint16_t) 0x15)
#define PPSMC_MSG_SetLclkSoftMin              ((uint16_t) 0x16)
#define PPSMC_MSG_SetLclkSoftMax              ((uint16_t) 0x17)
#define PPSMC_MSG_SetLclkHardMin              ((uint16_t) 0x18)
#define PPSMC_MSG_SetLclkHardMax              ((uint16_t) 0x19)
#define PPSMC_MSG_SetUvdSoftMin               ((uint16_t) 0x1A)
#define PPSMC_MSG_SetUvdSoftMax               ((uint16_t) 0x1B)
#define PPSMC_MSG_SetUvdHardMin               ((uint16_t) 0x1C)
#define PPSMC_MSG_SetUvdHardMax               ((uint16_t) 0x1D)
#define PPSMC_MSG_SetEclkSoftMin              ((uint16_t) 0x1E)
#define PPSMC_MSG_SetEclkSoftMax              ((uint16_t) 0x1F)
#define PPSMC_MSG_SetEclkHardMin              ((uint16_t) 0x20)
#define PPSMC_MSG_SetEclkHardMax              ((uint16_t) 0x21)
#define PPSMC_MSG_SetAclkSoftMin              ((uint16_t) 0x22)
#define PPSMC_MSG_SetAclkSoftMax              ((uint16_t) 0x23)
#define PPSMC_MSG_SetAclkHardMin              ((uint16_t) 0x24)
#define PPSMC_MSG_SetAclkHardMax              ((uint16_t) 0x25)
#define PPSMC_MSG_SetNclkSoftMin              ((uint16_t) 0x26)
#define PPSMC_MSG_SetNclkSoftMax              ((uint16_t) 0x27)
#define PPSMC_MSG_SetNclkHardMin              ((uint16_t) 0x28)
#define PPSMC_MSG_SetNclkHardMax              ((uint16_t) 0x29)
#define PPSMC_MSG_SetPstateSoftMin            ((uint16_t) 0x2A)
#define PPSMC_MSG_SetPstateSoftMax            ((uint16_t) 0x2B)
#define PPSMC_MSG_SetPstateHardMin            ((uint16_t) 0x2C)
#define PPSMC_MSG_SetPstateHardMax            ((uint16_t) 0x2D)
#define PPSMC_MSG_DisableLowMemoryPstate      ((uint16_t) 0x2E)
#define PPSMC_MSG_EnableLowMemoryPstate       ((uint16_t) 0x2F)
#define PPSMC_MSG_UcodeAddressLow             ((uint16_t) 0x30)
#define PPSMC_MSG_UcodeAddressHigh            ((uint16_t) 0x31)
#define PPSMC_MSG_UcodeLoadStatus             ((uint16_t) 0x32)
#define PPSMC_MSG_DriverDramAddrHi            ((uint16_t) 0x33)
#define PPSMC_MSG_DriverDramAddrLo            ((uint16_t) 0x34)
#define PPSMC_MSG_CondExecDramAddrHi          ((uint16_t) 0x35)
#define PPSMC_MSG_CondExecDramAddrLo          ((uint16_t) 0x36)
#define PPSMC_MSG_LoadUcodes                  ((uint16_t) 0x37)
#define PPSMC_MSG_DriverResetMode             ((uint16_t) 0x38)
#define PPSMC_MSG_PowerStateNotify            ((uint16_t) 0x39)
#define PPSMC_MSG_SetDisplayPhyConfig         ((uint16_t) 0x3A)
#define PPSMC_MSG_GetMaxSclkLevel             ((uint16_t) 0x3B)
#define PPSMC_MSG_GetMaxLclkLevel             ((uint16_t) 0x3C)
#define PPSMC_MSG_GetMaxUvdLevel              ((uint16_t) 0x3D)
#define PPSMC_MSG_GetMaxEclkLevel             ((uint16_t) 0x3E)
#define PPSMC_MSG_GetMaxAclkLevel             ((uint16_t) 0x3F)
#define PPSMC_MSG_GetMaxNclkLevel             ((uint16_t) 0x40)
#define PPSMC_MSG_GetMaxPstate                ((uint16_t) 0x41)
#define PPSMC_MSG_DramAddrHiVirtual           ((uint16_t) 0x42)
#define PPSMC_MSG_DramAddrLoVirtual           ((uint16_t) 0x43)
#define PPSMC_MSG_DramAddrHiPhysical          ((uint16_t) 0x44)
#define PPSMC_MSG_DramAddrLoPhysical          ((uint16_t) 0x45)
#define PPSMC_MSG_DramBufferSize              ((uint16_t) 0x46)
#define PPSMC_MSG_SetMmPwrLogDramAddrHi       ((uint16_t) 0x47)
#define PPSMC_MSG_SetMmPwrLogDramAddrLo       ((uint16_t) 0x48)
#define PPSMC_MSG_SetClkTableAddrHi           ((uint16_t) 0x49)
#define PPSMC_MSG_SetClkTableAddrLo           ((uint16_t) 0x4A)
#define PPSMC_MSG_GetConservativePowerLimit   ((uint16_t) 0x4B)

#define PPSMC_MSG_InitJobs                    ((uint16_t) 0x252)
#define PPSMC_MSG_ExecuteJob                  ((uint16_t) 0x254)

#define PPSMC_MSG_NBDPM_Enable                ((uint16_t) 0x140)
#define PPSMC_MSG_NBDPM_Disable               ((uint16_t) 0x141)

#define PPSMC_MSG_DPM_FPS_Mode                ((uint16_t) 0x15d)
#define PPSMC_MSG_DPM_Activity_Mode           ((uint16_t) 0x15e)

#define PPSMC_MSG_PmStatusLogStart            ((uint16_t) 0x170)
#define PPSMC_MSG_PmStatusLogSample           ((uint16_t) 0x171)

#define PPSMC_MSG_AllowLowSclkInterrupt       ((uint16_t) 0x184)
#define PPSMC_MSG_MmPowerMonitorStart         ((uint16_t) 0x18F)
#define PPSMC_MSG_MmPowerMonitorStop          ((uint16_t) 0x190)
#define PPSMC_MSG_MmPowerMonitorRestart       ((uint16_t) 0x191)

#define PPSMC_MSG_SetClockGateMask            ((uint16_t) 0x260)
#define PPSMC_MSG_SetFpsThresholdLo           ((uint16_t) 0x264)
#define PPSMC_MSG_SetFpsThresholdHi           ((uint16_t) 0x265)
#define PPSMC_MSG_SetLowSclkIntrThreshold     ((uint16_t) 0x266)

#define PPSMC_MSG_ClkTableXferToDram          ((uint16_t) 0x267)
#define PPSMC_MSG_ClkTableXferToSmu           ((uint16_t) 0x268)
#define PPSMC_MSG_GetAverageGraphicsActivity  ((uint16_t) 0x269)
#define PPSMC_MSG_GetAverageGioActivity       ((uint16_t) 0x26A)
#define PPSMC_MSG_SetLoggerBufferSize         ((uint16_t) 0x26B)
#define PPSMC_MSG_SetLoggerAddressHigh        ((uint16_t) 0x26C)
#define PPSMC_MSG_SetLoggerAddressLow         ((uint16_t) 0x26D)
#define PPSMC_MSG_SetWatermarkFrequency       ((uint16_t) 0x26E)
#define PPSMC_MSG_SetDisplaySizePowerParams   ((uint16_t) 0x26F)

/* REMOVE LATER*/
#define PPSMC_MSG_DPM_ForceState              ((uint16_t) 0x104)

/* Feature Enable Masks*/
#define NB_DPM_MASK             0x00000800
#define VDDGFX_MASK             0x00800000
#define VCE_DPM_MASK            0x00400000
#define ACP_DPM_MASK            0x00040000
#define UVD_DPM_MASK            0x00010000
#define GFX_CU_PG_MASK          0x00004000
#define SCLK_DPM_MASK           0x00080000

#if !defined(SMC_MICROCODE)
#pragma pack(pop)

#endif

#endif
