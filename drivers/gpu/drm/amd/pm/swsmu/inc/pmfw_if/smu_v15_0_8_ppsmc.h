/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#ifndef SMU_15_0_8_PPSMC_H
#define SMU_15_0_8_PPSMC_H

/* SMU Response Codes */
#define PPSMC_Result_OK                             0x1
#define PPSMC_Result_Failed                         0xFF
#define PPSMC_Result_UnknownCmd                     0xFE
#define PPSMC_Result_CmdRejectedPrereq              0xFD
#define PPSMC_Result_CmdRejectedBusy                0xFC

/* Message Definitions */
#define PPSMC_MSG_TestMessage	                    0x1
#define PPSMC_MSG_GetSmuVersion	                    0x2
#define PPSMC_MSG_GfxDriverReset	                0x3
#define PPSMC_MSG_GetDriverIfVersion	            0x4
#define PPSMC_MSG_EnableAllSmuFeatures	            0x5
#define PPSMC_MSG_GetMetricsVersion	                0x6
#define PPSMC_MSG_GetMetricsTable	                0x7
#define PPSMC_MSG_GetEnabledSmuFeatures	            0x8
#define PPSMC_MSG_SetDriverDramAddr	                0x9 //ARG0: low address, ARG1: high address
#define PPSMC_MSG_SetToolsDramAddr	                0xA //ARG0: low address, ARG1: high address
//#define PPSMC_MSG_SetSystemVirtualDramAddr	        0xB
#define PPSMC_MSG_SetSoftMaxByFreq	                0xC
#define PPSMC_MSG_SetPptLimit	                    0xD
#define PPSMC_MSG_GetPptLimit	                    0xE
#define PPSMC_MSG_DramLogSetDramAddr	            0xF //ARG0: low address, ARG1: high address, ARG2: size
#define PPSMC_MSG_HeavySBR	                        0x10
#define PPSMC_MSG_DFCstateControl	                0x11
#define PPSMC_MSG_GfxDriverResetRecovery	        0x12
#define PPSMC_MSG_TriggerVFFLR	                    0x13
#define PPSMC_MSG_SetSoftMinGfxClk	                0x14
#define PPSMC_MSG_SetSoftMaxGfxClk	                0x15
#define PPSMC_MSG_PrepareForDriverUnload	        0x16
#define PPSMC_MSG_QueryValidMcaCount	            0x17
#define PPSMC_MSG_McaBankDumpDW	                    0x18
#define PPSMC_MSG_ClearMcaOnRead	                0x19
#define PPSMC_MSG_QueryValidMcaCeCount	            0x1A
#define PPSMC_MSG_McaBankCeDumpDW	                0x1B
#define PPSMC_MSG_SelectPLPDMode	                0x1C
#define PPSMC_MSG_SetThrottlingPolicy	            0x1D
#define PPSMC_MSG_ResetSDMA	                        0x1E
#define PPSMC_MSG_GetRasTableVersion	            0x1F
#define PPSMC_MSG_GetRmaStatus	                    0x20
#define PPSMC_MSG_GetBadPageCount	                0x21
#define PPSMC_MSG_GetBadPageMcaAddress	            0x22
#define PPSMC_MSG_GetBadPagePaAddress	            0x23
#define PPSMC_MSG_SetTimestamp	                    0x24
#define PPSMC_MSG_GetTimestamp	                    0x25
#define PPSMC_MSG_GetRasPolicy	                    0x26
#define PPSMC_MSG_GetBadPageIpIdLoHi	            0x27
#define PPSMC_MSG_EraseRasTable	                    0x28
#define PPSMC_MSG_GetStaticMetricsTable	            0x29
#define PPSMC_MSG_ResetVfArbitersByIndex	        0x2A
#define PPSMC_MSG_GetBadPageSeverity	            0x2B
#define PPSMC_MSG_GetSystemMetricsTable	            0x2C
#define PPSMC_MSG_GetSystemMetricsVersion	        0x2D
#define PPSMC_MSG_ResetVCN	                        0x2E
#define PPSMC_MSG_SetFastPptLimit	                0x2F
#define PPSMC_MSG_GetFastPptLimit	                0x30
#define PPSMC_MSG_SetSoftMinGl2clk	                0x31
#define PPSMC_MSG_SetSoftMaxGl2clk	                0x32
#define PPSMC_MSG_SetSoftMinFclk	                0x33
#define PPSMC_MSG_SetSoftMaxFclk	                0x34
#define PPSMC_Message_Count                         0x35

/* PSMC Reset Types for driver msg argument */
#define PPSMC_RESET_TYPE_DRIVER_MODE_1_RESET        0x1
#define PPSMC_RESET_TYPE_DRIVER_MODE_2_RESET        0x2
#define PPSMC_RESET_TYPE_DRIVER_MODE_3_RESET        0x3

/* PLPD modes */
#define PPSMC_PLPD_MODE_DEFAULT                     0x1
#define PPSMC_PLPD_MODE_OPTIMIZED                   0x2

typedef uint32_t PPSMC_Result;
typedef uint32_t PPSMC_MSG;

#endif
