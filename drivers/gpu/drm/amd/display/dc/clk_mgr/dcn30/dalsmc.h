/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

// TEMPORARY until this exists in the proper location
#ifndef DALSMC_H
#define DALSMC_H

#define DALSMC_VERSION 0x1

// SMU Response Codes:
#define DALSMC_Result_OK                   0x1
#define DALSMC_Result_Failed               0xFF
#define DALSMC_Result_UnknownCmd           0xFE
#define DALSMC_Result_CmdRejectedPrereq    0xFD
#define DALSMC_Result_CmdRejectedBusy      0xFC



// Message Definitions:
#define DALSMC_MSG_TestMessage                    0x1
#define DALSMC_MSG_GetSmuVersion                  0x2
#define DALSMC_MSG_GetDriverIfVersion             0x3
#define DALSMC_MSG_GetMsgHeaderVersion            0x4
#define DALSMC_MSG_SetDalDramAddrHigh             0x5
#define DALSMC_MSG_SetDalDramAddrLow              0x6
#define DALSMC_MSG_TransferTableSmu2Dram          0x7
#define DALSMC_MSG_TransferTableDram2Smu          0x8
#define DALSMC_MSG_SetHardMinByFreq               0x9
#define DALSMC_MSG_SetHardMaxByFreq               0xA
#define DALSMC_MSG_GetDpmFreqByIndex              0xB
#define DALSMC_MSG_GetDcModeMaxDpmFreq            0xC
#define DALSMC_MSG_SetMinDeepSleepDcefclk         0xD
#define DALSMC_MSG_NumOfDisplays                  0xE
#define DALSMC_MSG_SetExternalClientDfCstateAllow 0x10
#define DALSMC_MSG_BacoAudioD3PME                 0x11
#define DALSMC_Message_Count                      0x12

#endif
