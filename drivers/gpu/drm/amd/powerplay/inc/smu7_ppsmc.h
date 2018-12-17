/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef DGPU_VI_PP_SMC_H
#define DGPU_VI_PP_SMC_H


#pragma pack(push, 1)

#define PPSMC_MSG_SetGBDroopSettings          ((uint16_t) 0x305)

#define PPSMC_SWSTATE_FLAG_DC                           0x01
#define PPSMC_SWSTATE_FLAG_UVD                          0x02
#define PPSMC_SWSTATE_FLAG_VCE                          0x04

#define PPSMC_THERMAL_PROTECT_TYPE_INTERNAL             0x00
#define PPSMC_THERMAL_PROTECT_TYPE_EXTERNAL             0x01
#define PPSMC_THERMAL_PROTECT_TYPE_NONE                 0xff

#define PPSMC_SYSTEMFLAG_GPIO_DC                        0x01
#define PPSMC_SYSTEMFLAG_STEPVDDC                       0x02
#define PPSMC_SYSTEMFLAG_GDDR5                          0x04

#define PPSMC_SYSTEMFLAG_DISABLE_BABYSTEP               0x08

#define PPSMC_SYSTEMFLAG_REGULATOR_HOT                  0x10
#define PPSMC_SYSTEMFLAG_REGULATOR_HOT_ANALOG           0x20

#define PPSMC_EXTRAFLAGS_AC2DC_ACTION_MASK              0x07
#define PPSMC_EXTRAFLAGS_AC2DC_DONT_WAIT_FOR_VBLANK     0x08

#define PPSMC_EXTRAFLAGS_AC2DC_ACTION_GOTODPMLOWSTATE   0x00
#define PPSMC_EXTRAFLAGS_AC2DC_ACTION_GOTOINITIALSTATE  0x01


#define PPSMC_DPM2FLAGS_TDPCLMP                         0x01
#define PPSMC_DPM2FLAGS_PWRSHFT                         0x02
#define PPSMC_DPM2FLAGS_OCP                             0x04


#define PPSMC_DISPLAY_WATERMARK_LOW                     0
#define PPSMC_DISPLAY_WATERMARK_HIGH                    1


#define PPSMC_STATEFLAG_AUTO_PULSE_SKIP    0x01
#define PPSMC_STATEFLAG_POWERBOOST         0x02
#define PPSMC_STATEFLAG_PSKIP_ON_TDP_FAULT 0x04
#define PPSMC_STATEFLAG_POWERSHIFT         0x08
#define PPSMC_STATEFLAG_SLOW_READ_MARGIN   0x10
#define PPSMC_STATEFLAG_DEEPSLEEP_THROTTLE 0x20
#define PPSMC_STATEFLAG_DEEPSLEEP_BYPASS   0x40


#define FDO_MODE_HARDWARE 0
#define FDO_MODE_PIECE_WISE_LINEAR 1

enum FAN_CONTROL {
	FAN_CONTROL_FUZZY,
	FAN_CONTROL_TABLE
};


#define PPSMC_Result_OK             ((uint16_t)0x01)
#define PPSMC_Result_NoMore         ((uint16_t)0x02)

#define PPSMC_Result_NotNow         ((uint16_t)0x03)
#define PPSMC_Result_Failed         ((uint16_t)0xFF)
#define PPSMC_Result_UnknownCmd     ((uint16_t)0xFE)
#define PPSMC_Result_UnknownVT      ((uint16_t)0xFD)

typedef uint16_t PPSMC_Result;

#define PPSMC_isERROR(x) ((uint16_t)0x80 & (x))


#define PPSMC_MSG_Halt                      ((uint16_t)0x10)
#define PPSMC_MSG_Resume                    ((uint16_t)0x11)
#define PPSMC_MSG_EnableDPMLevel            ((uint16_t)0x12)
#define PPSMC_MSG_ZeroLevelsDisabled        ((uint16_t)0x13)
#define PPSMC_MSG_OneLevelsDisabled         ((uint16_t)0x14)
#define PPSMC_MSG_TwoLevelsDisabled         ((uint16_t)0x15)
#define PPSMC_MSG_EnableThermalInterrupt    ((uint16_t)0x16)
#define PPSMC_MSG_RunningOnAC               ((uint16_t)0x17)
#define PPSMC_MSG_LevelUp                   ((uint16_t)0x18)
#define PPSMC_MSG_LevelDown                 ((uint16_t)0x19)
#define PPSMC_MSG_ResetDPMCounters          ((uint16_t)0x1a)
#define PPSMC_MSG_SwitchToSwState           ((uint16_t)0x20)
#define PPSMC_MSG_SwitchToSwStateLast       ((uint16_t)0x3f)
#define PPSMC_MSG_SwitchToInitialState      ((uint16_t)0x40)
#define PPSMC_MSG_NoForcedLevel             ((uint16_t)0x41)
#define PPSMC_MSG_ForceHigh                 ((uint16_t)0x42)
#define PPSMC_MSG_ForceMediumOrHigh         ((uint16_t)0x43)
#define PPSMC_MSG_SwitchToMinimumPower      ((uint16_t)0x51)
#define PPSMC_MSG_ResumeFromMinimumPower    ((uint16_t)0x52)
#define PPSMC_MSG_EnableCac                 ((uint16_t)0x53)
#define PPSMC_MSG_DisableCac                ((uint16_t)0x54)
#define PPSMC_DPMStateHistoryStart          ((uint16_t)0x55)
#define PPSMC_DPMStateHistoryStop           ((uint16_t)0x56)
#define PPSMC_CACHistoryStart               ((uint16_t)0x57)
#define PPSMC_CACHistoryStop                ((uint16_t)0x58)
#define PPSMC_TDPClampingActive             ((uint16_t)0x59)
#define PPSMC_TDPClampingInactive           ((uint16_t)0x5A)
#define PPSMC_StartFanControl               ((uint16_t)0x5B)
#define PPSMC_StopFanControl                ((uint16_t)0x5C)
#define PPSMC_NoDisplay                     ((uint16_t)0x5D)
#define PPSMC_HasDisplay                    ((uint16_t)0x5E)
#define PPSMC_MSG_UVDPowerOFF               ((uint16_t)0x60)
#define PPSMC_MSG_UVDPowerON                ((uint16_t)0x61)
#define PPSMC_MSG_EnableULV                 ((uint16_t)0x62)
#define PPSMC_MSG_DisableULV                ((uint16_t)0x63)
#define PPSMC_MSG_EnterULV                  ((uint16_t)0x64)
#define PPSMC_MSG_ExitULV                   ((uint16_t)0x65)
#define PPSMC_PowerShiftActive              ((uint16_t)0x6A)
#define PPSMC_PowerShiftInactive            ((uint16_t)0x6B)
#define PPSMC_OCPActive                     ((uint16_t)0x6C)
#define PPSMC_OCPInactive                   ((uint16_t)0x6D)
#define PPSMC_CACLongTermAvgEnable          ((uint16_t)0x6E)
#define PPSMC_CACLongTermAvgDisable         ((uint16_t)0x6F)
#define PPSMC_MSG_InferredStateSweep_Start  ((uint16_t)0x70)
#define PPSMC_MSG_InferredStateSweep_Stop   ((uint16_t)0x71)
#define PPSMC_MSG_SwitchToLowestInfState    ((uint16_t)0x72)
#define PPSMC_MSG_SwitchToNonInfState       ((uint16_t)0x73)
#define PPSMC_MSG_AllStateSweep_Start       ((uint16_t)0x74)
#define PPSMC_MSG_AllStateSweep_Stop        ((uint16_t)0x75)
#define PPSMC_MSG_SwitchNextLowerInfState   ((uint16_t)0x76)
#define PPSMC_MSG_SwitchNextHigherInfState  ((uint16_t)0x77)
#define PPSMC_MSG_MclkRetrainingTest        ((uint16_t)0x78)
#define PPSMC_MSG_ForceTDPClamping          ((uint16_t)0x79)
#define PPSMC_MSG_CollectCAC_PowerCorreln   ((uint16_t)0x7A)
#define PPSMC_MSG_CollectCAC_WeightCalib    ((uint16_t)0x7B)
#define PPSMC_MSG_CollectCAC_SQonly         ((uint16_t)0x7C)
#define PPSMC_MSG_CollectCAC_TemperaturePwr ((uint16_t)0x7D)

#define PPSMC_MSG_ExtremitiesTest_Start     ((uint16_t)0x7E)
#define PPSMC_MSG_ExtremitiesTest_Stop      ((uint16_t)0x7F)
#define PPSMC_FlushDataCache                ((uint16_t)0x80)
#define PPSMC_FlushInstrCache               ((uint16_t)0x81)

#define PPSMC_MSG_SetEnabledLevels          ((uint16_t)0x82)
#define PPSMC_MSG_SetForcedLevels           ((uint16_t)0x83)

#define PPSMC_MSG_ResetToDefaults           ((uint16_t)0x84)

#define PPSMC_MSG_SetForcedLevelsAndJump      ((uint16_t)0x85)
#define PPSMC_MSG_SetCACHistoryMode           ((uint16_t)0x86)
#define PPSMC_MSG_EnableDTE                   ((uint16_t)0x87)
#define PPSMC_MSG_DisableDTE                  ((uint16_t)0x88)

#define PPSMC_MSG_SmcSpaceSetAddress          ((uint16_t)0x89)
#define PPSM_MSG_SmcSpaceWriteDWordInc        ((uint16_t)0x8A)
#define PPSM_MSG_SmcSpaceWriteWordInc         ((uint16_t)0x8B)
#define PPSM_MSG_SmcSpaceWriteByteInc         ((uint16_t)0x8C)

#define PPSMC_MSG_BREAK                       ((uint16_t)0xF8)

#define PPSMC_MSG_Test                        ((uint16_t) 0x100)
#define PPSMC_MSG_DPM_Voltage_Pwrmgt          ((uint16_t) 0x101)
#define PPSMC_MSG_DPM_Config                  ((uint16_t) 0x102)
#define PPSMC_MSG_PM_Controller_Start         ((uint16_t) 0x103)
#define PPSMC_MSG_DPM_ForceState              ((uint16_t) 0x104)
#define PPSMC_MSG_PG_PowerDownSIMD            ((uint16_t) 0x105)
#define PPSMC_MSG_PG_PowerUpSIMD              ((uint16_t) 0x106)
#define PPSMC_MSG_PM_Controller_Stop          ((uint16_t) 0x107)
#define PPSMC_MSG_PG_SIMD_Config              ((uint16_t) 0x108)
#define PPSMC_MSG_Voltage_Cntl_Enable         ((uint16_t) 0x109)
#define PPSMC_MSG_Thermal_Cntl_Enable         ((uint16_t) 0x10a)
#define PPSMC_MSG_Reset_Service               ((uint16_t) 0x10b)
#define PPSMC_MSG_VCEPowerOFF                 ((uint16_t) 0x10e)
#define PPSMC_MSG_VCEPowerON                  ((uint16_t) 0x10f)
#define PPSMC_MSG_DPM_Disable_VCE_HS          ((uint16_t) 0x110)
#define PPSMC_MSG_DPM_Enable_VCE_HS           ((uint16_t) 0x111)
#define PPSMC_MSG_DPM_N_LevelsDisabled        ((uint16_t) 0x112)
#define PPSMC_MSG_DCEPowerOFF                 ((uint16_t) 0x113)
#define PPSMC_MSG_DCEPowerON                  ((uint16_t) 0x114)
#define PPSMC_MSG_PCIE_DDIPowerDown           ((uint16_t) 0x117)
#define PPSMC_MSG_PCIE_DDIPowerUp             ((uint16_t) 0x118)
#define PPSMC_MSG_PCIE_CascadePLLPowerDown    ((uint16_t) 0x119)
#define PPSMC_MSG_PCIE_CascadePLLPowerUp      ((uint16_t) 0x11a)
#define PPSMC_MSG_SYSPLLPowerOff              ((uint16_t) 0x11b)
#define PPSMC_MSG_SYSPLLPowerOn               ((uint16_t) 0x11c)
#define PPSMC_MSG_DCE_RemoveVoltageAdjustment ((uint16_t) 0x11d)
#define PPSMC_MSG_DCE_AllowVoltageAdjustment  ((uint16_t) 0x11e)
#define PPSMC_MSG_DISPLAYPHYStatusNotify      ((uint16_t) 0x11f)
#define PPSMC_MSG_EnableBAPM                  ((uint16_t) 0x120)
#define PPSMC_MSG_DisableBAPM                 ((uint16_t) 0x121)
#define PPSMC_MSG_Spmi_Enable                 ((uint16_t) 0x122)
#define PPSMC_MSG_Spmi_Timer                  ((uint16_t) 0x123)
#define PPSMC_MSG_LCLK_DPM_Config             ((uint16_t) 0x124)
#define PPSMC_MSG_VddNB_Request               ((uint16_t) 0x125)
#define PPSMC_MSG_PCIE_DDIPhyPowerDown        ((uint32_t) 0x126)
#define PPSMC_MSG_PCIE_DDIPhyPowerUp          ((uint32_t) 0x127)
#define PPSMC_MSG_MCLKDPM_Config              ((uint16_t) 0x128)

#define PPSMC_MSG_UVDDPM_Config               ((uint16_t) 0x129)
#define PPSMC_MSG_VCEDPM_Config               ((uint16_t) 0x12A)
#define PPSMC_MSG_ACPDPM_Config               ((uint16_t) 0x12B)
#define PPSMC_MSG_SAMUDPM_Config              ((uint16_t) 0x12C)
#define PPSMC_MSG_UVDDPM_SetEnabledMask       ((uint16_t) 0x12D)
#define PPSMC_MSG_VCEDPM_SetEnabledMask       ((uint16_t) 0x12E)
#define PPSMC_MSG_ACPDPM_SetEnabledMask       ((uint16_t) 0x12F)
#define PPSMC_MSG_SAMUDPM_SetEnabledMask      ((uint16_t) 0x130)
#define PPSMC_MSG_MCLKDPM_ForceState          ((uint16_t) 0x131)
#define PPSMC_MSG_MCLKDPM_NoForcedLevel       ((uint16_t) 0x132)
#define PPSMC_MSG_Thermal_Cntl_Disable        ((uint16_t) 0x133)
#define PPSMC_MSG_SetTDPLimit                 ((uint16_t) 0x134)
#define PPSMC_MSG_Voltage_Cntl_Disable        ((uint16_t) 0x135)
#define PPSMC_MSG_PCIeDPM_Enable              ((uint16_t) 0x136)
#define PPSMC_MSG_ACPPowerOFF                 ((uint16_t) 0x137)
#define PPSMC_MSG_ACPPowerON                  ((uint16_t) 0x138)
#define PPSMC_MSG_SAMPowerOFF                 ((uint16_t) 0x139)
#define PPSMC_MSG_SAMPowerON                  ((uint16_t) 0x13a)
#define PPSMC_MSG_SDMAPowerOFF                ((uint16_t) 0x13b)
#define PPSMC_MSG_SDMAPowerON                 ((uint16_t) 0x13c)
#define PPSMC_MSG_PCIeDPM_Disable             ((uint16_t) 0x13d)
#define PPSMC_MSG_IOMMUPowerOFF               ((uint16_t) 0x13e)
#define PPSMC_MSG_IOMMUPowerON                ((uint16_t) 0x13f)
#define PPSMC_MSG_NBDPM_Enable                ((uint16_t) 0x140)
#define PPSMC_MSG_NBDPM_Disable               ((uint16_t) 0x141)
#define PPSMC_MSG_NBDPM_ForceNominal          ((uint16_t) 0x142)
#define PPSMC_MSG_NBDPM_ForcePerformance      ((uint16_t) 0x143)
#define PPSMC_MSG_NBDPM_UnForce               ((uint16_t) 0x144)
#define PPSMC_MSG_SCLKDPM_SetEnabledMask      ((uint16_t) 0x145)
#define PPSMC_MSG_MCLKDPM_SetEnabledMask      ((uint16_t) 0x146)
#define PPSMC_MSG_PCIeDPM_ForceLevel          ((uint16_t) 0x147)
#define PPSMC_MSG_PCIeDPM_UnForceLevel        ((uint16_t) 0x148)
#define PPSMC_MSG_EnableACDCGPIOInterrupt     ((uint16_t) 0x149)
#define PPSMC_MSG_EnableVRHotGPIOInterrupt    ((uint16_t) 0x14a)
#define PPSMC_MSG_SwitchToAC                  ((uint16_t) 0x14b)
#define PPSMC_MSG_XDMAPowerOFF                ((uint16_t) 0x14c)
#define PPSMC_MSG_XDMAPowerON                 ((uint16_t) 0x14d)

#define PPSMC_MSG_DPM_Enable                  ((uint16_t) 0x14e)
#define PPSMC_MSG_DPM_Disable                 ((uint16_t) 0x14f)
#define PPSMC_MSG_MCLKDPM_Enable              ((uint16_t) 0x150)
#define PPSMC_MSG_MCLKDPM_Disable             ((uint16_t) 0x151)
#define PPSMC_MSG_LCLKDPM_Enable              ((uint16_t) 0x152)
#define PPSMC_MSG_LCLKDPM_Disable             ((uint16_t) 0x153)
#define PPSMC_MSG_UVDDPM_Enable               ((uint16_t) 0x154)
#define PPSMC_MSG_UVDDPM_Disable              ((uint16_t) 0x155)
#define PPSMC_MSG_SAMUDPM_Enable              ((uint16_t) 0x156)
#define PPSMC_MSG_SAMUDPM_Disable             ((uint16_t) 0x157)
#define PPSMC_MSG_ACPDPM_Enable               ((uint16_t) 0x158)
#define PPSMC_MSG_ACPDPM_Disable              ((uint16_t) 0x159)
#define PPSMC_MSG_VCEDPM_Enable               ((uint16_t) 0x15a)
#define PPSMC_MSG_VCEDPM_Disable              ((uint16_t) 0x15b)
#define PPSMC_MSG_LCLKDPM_SetEnabledMask      ((uint16_t) 0x15c)
#define PPSMC_MSG_DPM_FPS_Mode                ((uint16_t) 0x15d)
#define PPSMC_MSG_DPM_Activity_Mode           ((uint16_t) 0x15e)
#define PPSMC_MSG_VddC_Request                ((uint16_t) 0x15f)
#define PPSMC_MSG_MCLKDPM_GetEnabledMask      ((uint16_t) 0x160)
#define PPSMC_MSG_LCLKDPM_GetEnabledMask      ((uint16_t) 0x161)
#define PPSMC_MSG_SCLKDPM_GetEnabledMask      ((uint16_t) 0x162)
#define PPSMC_MSG_UVDDPM_GetEnabledMask       ((uint16_t) 0x163)
#define PPSMC_MSG_SAMUDPM_GetEnabledMask      ((uint16_t) 0x164)
#define PPSMC_MSG_ACPDPM_GetEnabledMask       ((uint16_t) 0x165)
#define PPSMC_MSG_VCEDPM_GetEnabledMask       ((uint16_t) 0x166)
#define PPSMC_MSG_PCIeDPM_SetEnabledMask      ((uint16_t) 0x167)
#define PPSMC_MSG_PCIeDPM_GetEnabledMask      ((uint16_t) 0x168)
#define PPSMC_MSG_TDCLimitEnable              ((uint16_t) 0x169)
#define PPSMC_MSG_TDCLimitDisable             ((uint16_t) 0x16a)
#define PPSMC_MSG_DPM_AutoRotate_Mode         ((uint16_t) 0x16b)
#define PPSMC_MSG_DISPCLK_FROM_FCH            ((uint16_t) 0x16c)
#define PPSMC_MSG_DISPCLK_FROM_DFS            ((uint16_t) 0x16d)
#define PPSMC_MSG_DPREFCLK_FROM_FCH           ((uint16_t) 0x16e)
#define PPSMC_MSG_DPREFCLK_FROM_DFS           ((uint16_t) 0x16f)
#define PPSMC_MSG_PmStatusLogStart            ((uint16_t) 0x170)
#define PPSMC_MSG_PmStatusLogSample           ((uint16_t) 0x171)
#define PPSMC_MSG_SCLK_AutoDPM_ON             ((uint16_t) 0x172)
#define PPSMC_MSG_MCLK_AutoDPM_ON             ((uint16_t) 0x173)
#define PPSMC_MSG_LCLK_AutoDPM_ON             ((uint16_t) 0x174)
#define PPSMC_MSG_UVD_AutoDPM_ON              ((uint16_t) 0x175)
#define PPSMC_MSG_SAMU_AutoDPM_ON             ((uint16_t) 0x176)
#define PPSMC_MSG_ACP_AutoDPM_ON              ((uint16_t) 0x177)
#define PPSMC_MSG_VCE_AutoDPM_ON              ((uint16_t) 0x178)
#define PPSMC_MSG_PCIe_AutoDPM_ON             ((uint16_t) 0x179)
#define PPSMC_MSG_MASTER_AutoDPM_ON           ((uint16_t) 0x17a)
#define PPSMC_MSG_MASTER_AutoDPM_OFF          ((uint16_t) 0x17b)
#define PPSMC_MSG_DYNAMICDISPPHYPOWER         ((uint16_t) 0x17c)
#define PPSMC_MSG_CAC_COLLECTION_ON           ((uint16_t) 0x17d)
#define PPSMC_MSG_CAC_COLLECTION_OFF          ((uint16_t) 0x17e)
#define PPSMC_MSG_CAC_CORRELATION_ON          ((uint16_t) 0x17f)
#define PPSMC_MSG_CAC_CORRELATION_OFF         ((uint16_t) 0x180)
#define PPSMC_MSG_PM_STATUS_TO_DRAM_ON        ((uint16_t) 0x181)
#define PPSMC_MSG_PM_STATUS_TO_DRAM_OFF       ((uint16_t) 0x182)
#define PPSMC_MSG_ALLOW_LOWSCLK_INTERRUPT     ((uint16_t) 0x184)
#define PPSMC_MSG_PkgPwrLimitEnable           ((uint16_t) 0x185)
#define PPSMC_MSG_PkgPwrLimitDisable          ((uint16_t) 0x186)
#define PPSMC_MSG_PkgPwrSetLimit              ((uint16_t) 0x187)
#define PPSMC_MSG_OverDriveSetTargetTdp       ((uint16_t) 0x188)
#define PPSMC_MSG_SCLKDPM_FreezeLevel         ((uint16_t) 0x189)
#define PPSMC_MSG_SCLKDPM_UnfreezeLevel       ((uint16_t) 0x18A)
#define PPSMC_MSG_MCLKDPM_FreezeLevel         ((uint16_t) 0x18B)
#define PPSMC_MSG_MCLKDPM_UnfreezeLevel       ((uint16_t) 0x18C)
#define PPSMC_MSG_START_DRAM_LOGGING          ((uint16_t) 0x18D)
#define PPSMC_MSG_STOP_DRAM_LOGGING           ((uint16_t) 0x18E)
#define PPSMC_MSG_MASTER_DeepSleep_ON         ((uint16_t) 0x18F)
#define PPSMC_MSG_MASTER_DeepSleep_OFF        ((uint16_t) 0x190)
#define PPSMC_MSG_Remove_DC_Clamp             ((uint16_t) 0x191)
#define PPSMC_MSG_DisableACDCGPIOInterrupt    ((uint16_t) 0x192)
#define PPSMC_MSG_OverrideVoltageControl_SetVddc       ((uint16_t) 0x193)
#define PPSMC_MSG_OverrideVoltageControl_SetVddci      ((uint16_t) 0x194)
#define PPSMC_MSG_SetVidOffset_1              ((uint16_t) 0x195)
#define PPSMC_MSG_SetVidOffset_2              ((uint16_t) 0x207)
#define PPSMC_MSG_GetVidOffset_1              ((uint16_t) 0x196)
#define PPSMC_MSG_GetVidOffset_2              ((uint16_t) 0x208)
#define PPSMC_MSG_THERMAL_OVERDRIVE_Enable    ((uint16_t) 0x197)
#define PPSMC_MSG_THERMAL_OVERDRIVE_Disable   ((uint16_t) 0x198)
#define PPSMC_MSG_SetTjMax                    ((uint16_t) 0x199)
#define PPSMC_MSG_SetFanPwmMax                ((uint16_t) 0x19A)
#define PPSMC_MSG_WaitForMclkSwitchFinish     ((uint16_t) 0x19B)
#define PPSMC_MSG_ENABLE_THERMAL_DPM          ((uint16_t) 0x19C)
#define PPSMC_MSG_DISABLE_THERMAL_DPM         ((uint16_t) 0x19D)

#define PPSMC_MSG_API_GetSclkFrequency        ((uint16_t) 0x200)
#define PPSMC_MSG_API_GetMclkFrequency        ((uint16_t) 0x201)
#define PPSMC_MSG_API_GetSclkBusy             ((uint16_t) 0x202)
#define PPSMC_MSG_API_GetMclkBusy             ((uint16_t) 0x203)
#define PPSMC_MSG_API_GetAsicPower            ((uint16_t) 0x204)
#define PPSMC_MSG_SetFanRpmMax                ((uint16_t) 0x205)
#define PPSMC_MSG_SetFanSclkTarget            ((uint16_t) 0x206)
#define PPSMC_MSG_SetFanMinPwm                ((uint16_t) 0x209)
#define PPSMC_MSG_SetFanTemperatureTarget     ((uint16_t) 0x20A)

#define PPSMC_MSG_BACO_StartMonitor           ((uint16_t) 0x240)
#define PPSMC_MSG_BACO_Cancel                 ((uint16_t) 0x241)
#define PPSMC_MSG_EnableVddGfx                ((uint16_t) 0x242)
#define PPSMC_MSG_DisableVddGfx               ((uint16_t) 0x243)
#define PPSMC_MSG_UcodeAddressLow             ((uint16_t) 0x244)
#define PPSMC_MSG_UcodeAddressHigh            ((uint16_t) 0x245)
#define PPSMC_MSG_UcodeLoadStatus             ((uint16_t) 0x246)

#define PPSMC_MSG_DRV_DRAM_ADDR_HI            ((uint16_t) 0x250)
#define PPSMC_MSG_DRV_DRAM_ADDR_LO            ((uint16_t) 0x251)
#define PPSMC_MSG_SMU_DRAM_ADDR_HI            ((uint16_t) 0x252)
#define PPSMC_MSG_SMU_DRAM_ADDR_LO            ((uint16_t) 0x253)
#define PPSMC_MSG_LoadUcodes                  ((uint16_t) 0x254)
#define PPSMC_MSG_PowerStateNotify            ((uint16_t) 0x255)
#define PPSMC_MSG_COND_EXEC_DRAM_ADDR_HI      ((uint16_t) 0x256)
#define PPSMC_MSG_COND_EXEC_DRAM_ADDR_LO      ((uint16_t) 0x257)
#define PPSMC_MSG_VBIOS_DRAM_ADDR_HI          ((uint16_t) 0x258)
#define PPSMC_MSG_VBIOS_DRAM_ADDR_LO          ((uint16_t) 0x259)
#define PPSMC_MSG_LoadVBios                   ((uint16_t) 0x25A)
#define PPSMC_MSG_GetUcodeVersion             ((uint16_t) 0x25B)
#define DMCUSMC_MSG_PSREntry                  ((uint16_t) 0x25C)
#define DMCUSMC_MSG_PSRExit                   ((uint16_t) 0x25D)
#define PPSMC_MSG_EnableClockGatingFeature    ((uint16_t) 0x260)
#define PPSMC_MSG_DisableClockGatingFeature   ((uint16_t) 0x261)
#define PPSMC_MSG_IsDeviceRunning             ((uint16_t) 0x262)
#define PPSMC_MSG_LoadMetaData                ((uint16_t) 0x263)
#define PPSMC_MSG_TMON_AutoCaliberate_Enable  ((uint16_t) 0x264)
#define PPSMC_MSG_TMON_AutoCaliberate_Disable ((uint16_t) 0x265)
#define PPSMC_MSG_GetTelemetry1Slope          ((uint16_t) 0x266)
#define PPSMC_MSG_GetTelemetry1Offset         ((uint16_t) 0x267)
#define PPSMC_MSG_GetTelemetry2Slope          ((uint16_t) 0x268)
#define PPSMC_MSG_GetTelemetry2Offset         ((uint16_t) 0x269)
#define PPSMC_MSG_EnableAvfs                  ((uint16_t) 0x26A)
#define PPSMC_MSG_DisableAvfs                 ((uint16_t) 0x26B)

#define PPSMC_MSG_PerformBtc                  ((uint16_t) 0x26C)
#define PPSMC_MSG_LedConfig                   ((uint16_t) 0x274)
#define PPSMC_MSG_VftTableIsValid             ((uint16_t) 0x275)
#define PPSMC_MSG_UseNewGPIOScheme            ((uint16_t) 0x277)
#define PPSMC_MSG_GetEnabledPsm               ((uint16_t) 0x400)
#define PPSMC_MSG_AgmStartPsm                 ((uint16_t) 0x401)
#define PPSMC_MSG_AgmReadPsm                  ((uint16_t) 0x402)
#define PPSMC_MSG_AgmResetPsm                 ((uint16_t) 0x403)
#define PPSMC_MSG_ReadVftCell                 ((uint16_t) 0x404)

#define PPSMC_MSG_ApplyAvfsCksOffVoltage      ((uint16_t) 0x415)

#define PPSMC_MSG_GFX_CU_PG_ENABLE            ((uint16_t) 0x280)
#define PPSMC_MSG_GFX_CU_PG_DISABLE           ((uint16_t) 0x281)
#define PPSMC_MSG_GetCurrPkgPwr               ((uint16_t) 0x282)

#define PPSMC_MSG_SetGpuPllDfsForSclk         ((uint16_t) 0x300)
#define PPSMC_MSG_Didt_Block_Function		  ((uint16_t) 0x301)

#define PPSMC_MSG_SetVBITimeout               ((uint16_t) 0x306)

#define PPSMC_MSG_EnableDpmDidt               ((uint16_t) 0x309)
#define PPSMC_MSG_DisableDpmDidt              ((uint16_t) 0x30A)

#define PPSMC_MSG_SecureSRBMWrite             ((uint16_t) 0x600)
#define PPSMC_MSG_SecureSRBMRead              ((uint16_t) 0x601)
#define PPSMC_MSG_SetAddress                  ((uint16_t) 0x800)
#define PPSMC_MSG_GetData                     ((uint16_t) 0x801)
#define PPSMC_MSG_SetData                     ((uint16_t) 0x802)

typedef uint16_t PPSMC_Msg;

#define PPSMC_EVENT_STATUS_THERMAL          0x00000001
#define PPSMC_EVENT_STATUS_REGULATORHOT     0x00000002
#define PPSMC_EVENT_STATUS_DC               0x00000004

#pragma pack(pop)

#endif

