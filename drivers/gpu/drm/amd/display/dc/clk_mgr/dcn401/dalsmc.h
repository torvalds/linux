// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

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
#define DALSMC_MSG_SetMinDeepSleepDcfclk          0xD
#define DALSMC_MSG_NumOfDisplays                  0xE
#define DALSMC_MSG_SetExternalClientDfCstateAllow 0xF
#define DALSMC_MSG_BacoAudioD3PME                 0x10
#define DALSMC_MSG_SetFclkSwitchAllow             0x11
#define DALSMC_MSG_SetCabForUclkPstate            0x12
#define DALSMC_MSG_SetWorstCaseUclkLatency        0x13
#define DALSMC_MSG_DcnExitReset                   0x14
#define DALSMC_MSG_ReturnHardMinStatus            0x15
#define DALSMC_MSG_SetAlwaysWaitDmcubResp         0x16
#define DALSMC_MSG_IndicateDrrStatus              0x17  // PMFW 15811
#define DALSMC_MSG_ActiveUclkFclk                 0x18
#define DALSMC_MSG_IdleUclkFclk                   0x19
#define DALSMC_Message_Count                      0x1A

typedef enum {
  FCLK_SWITCH_DISALLOW,
  FCLK_SWITCH_ALLOW,
} FclkSwitchAllow_e;

#endif
