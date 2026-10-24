// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#ifndef DALSMC_H
#define DALSMC_H

/**
* @file dalsmc.h
*
* @brief VBIOS and DAL to PMFW Interface
*
* Clients:  VBIOS and DAL
* Protocols:
*
* @date 2016 - 2026
*/

/**
 * @mainpage PMFW-DAL Message Interface
 * This documentation contains the subsections:
 *
 * @ref ResponseCodes
 * @ref definitions
 */

/** @def DAL_PMFW_IF_VER
* VBIOS and DAL to PMFW IF version 1.0
*/
#define DAL_PMFW_IF_VER "1.0" ///< Major.Minor

#define DALSMC_VERSION 0x1

/** @defgroup DramTables DRAM Tables
 *  @brief Bulk data structures exchanged between DAL and PMFW via system DRAM.
 *
 *  Used when the payload exceeds the four 32-bit C2PMSG argument registers
 *  (DALSMC_args_t). DAL allocates a DRAM buffer, passes its address through
 *  DALSMC_TransferTable_arg_t, and issues either a
 *  DALSMC_MSG_TransferTableSmu2Dram or DALSMC_MSG_TransferTableDram2Smu message.
 *  @{
 */
typedef struct {
	uint32_t LoadLevelCount	: 4;
	uint32_t SopCount	: 4;
	uint32_t Reserved	: 24;
} SocUtmTableHeader_t;

typedef struct {
	uint32_t UrgentRampPs;
	uint32_t TripPs;
	uint32_t MetaTripToMemPs;
	uint32_t MaxReqLatencyUrgPs;
	uint32_t AvgReqLatencyUrgPs;
	uint32_t MaxReqLatencyNonUrgPs;
	uint32_t AvgReqLatencyNonUrgPs;
	uint32_t DfResponseTimePs;
	uint32_t UrgentBandwidthKBps;
	uint32_t NominalBandwidthKBps;
	uint32_t LsdmaBandwidthKBps;
	uint32_t Reserved[1];
} SocUtmSopEntry_t;

typedef struct {
	uint32_t SmuVersion;
	uint32_t SmuDriverIfVersion;
	uint32_t Reserved[2];
} DalInitHeader_t;

#define NUM_CLOCK_LEVELS  8
typedef struct {
	uint32_t Clocks[NUM_CLOCK_LEVELS];
	uint32_t DcMaxClock;
	uint32_t NumClocks;
	uint32_t Reserved[2];
} DpmClock_t;

typedef struct {
	uint32_t NumUmcChannels;
	uint32_t Reserved[3];
} MemoryConfig_t;

/**
 * TABLE_SOC_UTM - SoC UTM QoS table.
 *
 * Provides per-load-level bandwidth and latency bounds used by the display
 * engine to meet memory access requirements.
 *
 * Normal path: embedded in DalInitTable_t, fetched once via
 * DALSMC_MSG_TransferTableSmu2Dram(TABLE_DAL_INIT).
 *
 * Override path (debug only): TABLE_SOC_UTM via
 * DALSMC_MSG_TransferTableDram2Smu to override custom QoS parameters into PMFW.
 */
#define TABLE_SOC_UTM     0xC
/* TODO: rename back to MAX_UTM_SOP_COUNT once utm_qos_model_types.h conflict is resolved */
#define DALSMC_MAX_UTM_SOP_COUNT 16
#define MAX_UTM_LOAD_LEVEL_COUNT 16
#define UTM_LOAD_LEVEL_INDEX_IDLE                   0
#define UTM_LOAD_LEVEL_INDEX_ACTIVE_ALTERNATE_PSTATE 1
#define UTM_LOAD_LEVEL_INDEX_ACTIVE                 2
#define UTM_SOP_ENTRIES_OFFSET(LoadLevel, SopIndex) \
	(sizeof(SocUtmTableHeader_t) \
	+ ((LoadLevel) * DALSMC_MAX_UTM_SOP_COUNT \
	+ (SopIndex)) * sizeof(SocUtmSopEntry_t))
typedef struct {
	SocUtmTableHeader_t Header;
	SocUtmSopEntry_t Sops[MAX_UTM_LOAD_LEVEL_COUNT][DALSMC_MAX_UTM_SOP_COUNT];// 4 and 3 for dGPU PMFW
} SocUtmTable_t;

/**
 * TABLE_DAL_INIT - Full TABLE_DAL_INIT payload transferred from SMU to DRAM.
 */
#define TABLE_DAL_INIT    0xD
#define MAX_PPCLK_COUNT   22
#define DPM_CLOCK_OFFSET(Ppclk) \
	(sizeof(DalInitHeader_t) + (Ppclk) * sizeof(DpmClock_t))
#define UTM_TABLE_OFFSET \
	(sizeof(DalInitHeader_t) + MAX_PPCLK_COUNT * sizeof(DpmClock_t))
#define MEMORY_CONFIG_OFFSET \
	(sizeof(DalInitHeader_t) + MAX_PPCLK_COUNT * sizeof(DpmClock_t) \
	+ sizeof(SocUtmTable_t))
typedef struct {
	DalInitHeader_t   Header;
	DpmClock_t        PPClocks[MAX_PPCLK_COUNT];
	SocUtmTable_t     UtmTable;
	MemoryConfig_t    MemoryConfig;
} DalInitTable_t;


/** @defgroup ResponseCodes PMFW Response Codes
*  @{
*/
// SMU Response Codes:
#define DALSMC_Result_OK                    0x01 ///< Message Response OK
#define DALSMC_Result_Failed                0xFF ///< Message Response Failed
#define DALSMC_Result_UnknownCmd            0xFE ///< Message Response Unknown Command
#define DALSMC_Result_CmdRejectedPrereq     0xFD ///< Message Response Command Failed Prerequisite
#define DALSMC_Result_CmdRejectedBusy       0xFC ///< Message Response Command Rejected due to PMFW is busy. Sender should retry sending this message
/** @}*/

// Message Definitions:
/** @defgroup definitions Message definitions
*  @{
*/
#define DALSMC_MSG_TestMessage                  0x01 ///< To check if PMFW is alive and responding. Requirement specified by PMFW team
#define DALSMC_MSG_GetPmfwVersion               0x02 ///< Get version
#define DALSMC_MSG_SetDispclkFreq               0x03 ///< Set display clock frequency in MHZ
#define DALSMC_MSG_SetDppclkFreq                0x04 ///< Set DPP clock frequency in MHZ
#define DALSMC_MSG_SetHardMinDcfclkByFreq       0x05 ///< Set DCF clock frequency hard min in MHZ
#define DALSMC_MSG_SetMinDeepSleepDcfclk        0x06 ///< Set DCF clock minimum frequency in deep sleep in MHZ
#define DALSMC_MSG_GetFclkFrequency             0x07 ///< Get FCLK frequency, return frequemcy in MHZ
#define DALSMC_MSG_UpdatePmeRestore             0x08 ///< To ask PMFW to write into Azalia for PME wake up event
#define DALSMC_MSG_SetDramAddrHigh              0x09 ///< Set DRAM address high 32 bits for WM table transfer
#define DALSMC_MSG_SetDramAddrLow               0x0A ///< Set DRAM address low 32 bits for WM table transfer
#define DALSMC_MSG_TransferTableSmu2Dram        0x0B ///< Transfer table from PMFW SRAM to system DRAM
#define DALSMC_MSG_TransferTableDram2Smu        0x0C ///< Transfer table from system DRAM to PMFW
#define DALSMC_MSG_SetDisplayIdleOptimizations  0x0D ///< Set Idle state optimization for display off
#define DALSMC_MSG_GetDprefclkFreq              0x0E ///< Get DPREF clock frequency. Return in MHZ
#define DALSMC_MSG_GetDtbclkFreq                0x0F ///< Get DTB clock frequency. Return in MHZ
#define DALSMC_MSG_AllowZstatesEntry_Spare0     0x10 ///< Inform PMFW of display allowing Zstate entry
#define DALSMC_MSG_SetDtbClk                    0x11 ///< Set DTB clock frequency in MHZ
#define DALSMC_MSG_DispIPS2Exit_Spare1          0x12 ///< Display IPS2 exit
#define DALSMC_MSG_QueryIPS2Support             0x13 ///< Return 1: support; else not supported
#define DALSMC_MSG_BandWidthRequest             0x14 ///< Arg 1-15: BwUrg; Arg 16-31: BwNonUrg
#define DALSMC_MSG_UpdateUtmQosParams           0x15
#define DALSMC_MSG_StutterEfficiency            0x16
#define DALSMC_MSG_GetMsgHeaderVersion          0x17 ///< Get message header version
#define DALSMC_MSG_SacoAudioD3PME               0x18 ///< Saco audio D3 PME
#define DALSMC_MSG_ReturnHardMinStatus          0x19 ///< Return hard min status
#define DALSMC_MSG_GetDispclkFreq               0x1A ///< Get display clock frequency. Return in MHZ
#define DALSMC_MSG_GetDppclkFreq                0x1B ///< Get DPP clock frequency. Return in MHZ
#define DALSMC_MSG_IndicatePstateStatus         0x1C ///< Set UCLK/FCLK related actions
#define DALSMC_Message_Count                    0x1D ///< Total number of VBIS and DAL messages

/** @}*/

#endif
