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

#ifndef FIJI_PP_SMC_H
#define FIJI_PP_SMC_H

#pragma pack(push, 1)

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

#define PPSMC_STATEFLAG_AUTO_PULSE_SKIP    		0x01
#define PPSMC_STATEFLAG_POWERBOOST         		0x02
#define PPSMC_STATEFLAG_PSKIP_ON_TDP_FAULT 		0x04
#define PPSMC_STATEFLAG_POWERSHIFT         		0x08
#define PPSMC_STATEFLAG_SLOW_READ_MARGIN   		0x10
#define PPSMC_STATEFLAG_DEEPSLEEP_THROTTLE 		0x20
#define PPSMC_STATEFLAG_DEEPSLEEP_BYPASS   		0x40

#define FDO_MODE_HARDWARE 0
#define FDO_MODE_PIECE_WISE_LINEAR 1

enum FAN_CONTROL {
	FAN_CONTROL_FUZZY,
	FAN_CONTROL_TABLE
};

//Gemini Modes
#define PPSMC_GeminiModeNone   0  //Single GPU board
#define PPSMC_GeminiModeMaster 1  //Master GPU on a Gemini board
#define PPSMC_GeminiModeSlave  2  //Slave GPU on a Gemini board

#define PPSMC_Result_OK             			((uint16_t)0x01)
#define PPSMC_Result_NoMore         			((uint16_t)0x02)
#define PPSMC_Result_NotNow         			((uint16_t)0x03)
#define PPSMC_Result_Failed         			((uint16_t)0xFF)
#define PPSMC_Result_UnknownCmd     			((uint16_t)0xFE)
#define PPSMC_Result_UnknownVT      			((uint16_t)0xFD)

typedef uint16_t PPSMC_Result;

#define PPSMC_isERROR(x) ((uint16_t)0x80 & (x))

#define PPSMC_MSG_Halt                      		((uint16_t)0x10)
#define PPSMC_MSG_Resume                    		((uint16_t)0x11)
#define PPSMC_MSG_EnableDPMLevel            		((uint16_t)0x12)
#define PPSMC_MSG_ZeroLevelsDisabled        		((uint16_t)0x13)
#define PPSMC_MSG_OneLevelsDisabled         		((uint16_t)0x14)
#define PPSMC_MSG_TwoLevelsDisabled         		((uint16_t)0x15)
#define PPSMC_MSG_EnableThermalInterrupt    		((uint16_t)0x16)
#define PPSMC_MSG_RunningOnAC               		((uint16_t)0x17)
#define PPSMC_MSG_LevelUp                   		((uint16_t)0x18)
#define PPSMC_MSG_LevelDown                 		((uint16_t)0x19)
#define PPSMC_MSG_ResetDPMCounters          		((uint16_t)0x1a)
#define PPSMC_MSG_SwitchToSwState           		((uint16_t)0x20)
#define PPSMC_MSG_SwitchToSwStateLast       		((uint16_t)0x3f)
#define PPSMC_MSG_SwitchToInitialState      		((uint16_t)0x40)
#define PPSMC_MSG_NoForcedLevel             		((uint16_t)0x41)
#define PPSMC_MSG_ForceHigh                 		((uint16_t)0x42)
#define PPSMC_MSG_ForceMediumOrHigh         		((uint16_t)0x43)
#define PPSMC_MSG_SwitchToMinimumPower      		((uint16_t)0x51)
#define PPSMC_MSG_ResumeFromMinimumPower    		((uint16_t)0x52)
#define PPSMC_MSG_EnableCac                 		((uint16_t)0x53)
#define PPSMC_MSG_DisableCac                		((uint16_t)0x54)
#define PPSMC_DPMStateHistoryStart          		((uint16_t)0x55)
#define PPSMC_DPMStateHistoryStop           		((uint16_t)0x56)
#define PPSMC_CACHistoryStart               		((uint16_t)0x57)
#define PPSMC_CACHistoryStop                		((uint16_t)0x58)
#define PPSMC_TDPClampingActive             		((uint16_t)0x59)
#define PPSMC_TDPClampingInactive           		((uint16_t)0x5A)
#define PPSMC_StartFanControl               		((uint16_t)0x5B)
#define PPSMC_StopFanControl                		((uint16_t)0x5C)
#define PPSMC_NoDisplay                     		((uint16_t)0x5D)
#define PPSMC_HasDisplay                    		((uint16_t)0x5E)
#define PPSMC_MSG_UVDPowerOFF               		((uint16_t)0x60)
#define PPSMC_MSG_UVDPowerON                		((uint16_t)0x61)
#define PPSMC_MSG_EnableULV                 		((uint16_t)0x62)
#define PPSMC_MSG_DisableULV                		((uint16_t)0x63)
#define PPSMC_MSG_EnterULV                  		((uint16_t)0x64)
#define PPSMC_MSG_ExitULV                   		((uint16_t)0x65)
#define PPSMC_PowerShiftActive              		((uint16_t)0x6A)
#define PPSMC_PowerShiftInactive            		((uint16_t)0x6B)
#define PPSMC_OCPActive                     		((uint16_t)0x6C)
#define PPSMC_OCPInactive                   		((uint16_t)0x6D)
#define PPSMC_CACLongTermAvgEnable          		((uint16_t)0x6E)
#define PPSMC_CACLongTermAvgDisable         		((uint16_t)0x6F)
#define PPSMC_MSG_InferredStateSweep_Start  		((uint16_t)0x70)
#define PPSMC_MSG_InferredStateSweep_Stop   		((uint16_t)0x71)
#define PPSMC_MSG_SwitchToLowestInfState    		((uint16_t)0x72)
#define PPSMC_MSG_SwitchToNonInfState       		((uint16_t)0x73)
#define PPSMC_MSG_AllStateSweep_Start       		((uint16_t)0x74)
#define PPSMC_MSG_AllStateSweep_Stop        		((uint16_t)0x75)
#define PPSMC_MSG_SwitchNextLowerInfState   		((uint16_t)0x76)
#define PPSMC_MSG_SwitchNextHigherInfState  		((uint16_t)0x77)
#define PPSMC_MSG_MclkRetrainingTest        		((uint16_t)0x78)
#define PPSMC_MSG_ForceTDPClamping          		((uint16_t)0x79)
#define PPSMC_MSG_CollectCAC_PowerCorreln   		((uint16_t)0x7A)
#define PPSMC_MSG_CollectCAC_WeightCalib    		((uint16_t)0x7B)
#define PPSMC_MSG_CollectCAC_SQonly         		((uint16_t)0x7C)
#define PPSMC_MSG_CollectCAC_TemperaturePwr 		((uint16_t)0x7D)
#define PPSMC_MSG_ExtremitiesTest_Start     		((uint16_t)0x7E)
#define PPSMC_MSG_ExtremitiesTest_Stop      		((uint16_t)0x7F)
#define PPSMC_FlushDataCache                		((uint16_t)0x80)
#define PPSMC_FlushInstrCache               		((uint16_t)0x81)
#define PPSMC_MSG_SetEnabledLevels          		((uint16_t)0x82)
#define PPSMC_MSG_SetForcedLevels           		((uint16_t)0x83)
#define PPSMC_MSG_ResetToDefaults           		((uint16_t)0x84)
#define PPSMC_MSG_SetForcedLevelsAndJump    		((uint16_t)0x85)
#define PPSMC_MSG_SetCACHistoryMode         		((uint16_t)0x86)
#define PPSMC_MSG_EnableDTE                 		((uint16_t)0x87)
#define PPSMC_MSG_DisableDTE                		((uint16_t)0x88)
#define PPSMC_MSG_SmcSpaceSetAddress        		((uint16_t)0x89)
#define PPSMC_MSG_SmcSpaceWriteDWordInc     		((uint16_t)0x8A)
#define PPSMC_MSG_SmcSpaceWriteWordInc      		((uint16_t)0x8B)
#define PPSMC_MSG_SmcSpaceWriteByteInc      		((uint16_t)0x8C)

#define PPSMC_MSG_BREAK                     		((uint16_t)0xF8)

#define PPSMC_MSG_Test                      		((uint16_t)0x100)
#define PPSMC_MSG_DRV_DRAM_ADDR_HI            		((uint16_t)0x250)
#define PPSMC_MSG_DRV_DRAM_ADDR_LO            		((uint16_t)0x251)
#define PPSMC_MSG_SMU_DRAM_ADDR_HI            		((uint16_t)0x252)
#define PPSMC_MSG_SMU_DRAM_ADDR_LO            		((uint16_t)0x253)
#define PPSMC_MSG_LoadUcodes                  		((uint16_t)0x254)

typedef uint16_t PPSMC_Msg;

#define PPSMC_EVENT_STATUS_THERMAL          		0x00000001
#define PPSMC_EVENT_STATUS_REGULATORHOT     		0x00000002
#define PPSMC_EVENT_STATUS_DC               		0x00000004
#define PPSMC_EVENT_STATUS_GPIO17           		0x00000008

#pragma pack(pop)

#endif
