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
 * Protocols: dalsmc
 *
 * @date 2016 - 2026
 */

/**
 * @mainpage PMFW-DAL Message Interface
 *
 * The protocol uses six registers:
 *
 * - MSG_REG   — write the message ID (DALSMC_MSG_*) to trigger the transaction
 * - ARG_REG_0 — input argument Reg0; also carries response data on completion
 * - ARG_REG_1 — input argument Reg1
 * - ARG_REG_2 — input argument Reg2
 * - ARG_REG_3 — input argument Reg3
 * - RESP_REG  — poll until non-zero; value is a DALSMC_Result_* response code
 *
 * Programming sequence:
 * 1. Clear RESP_REG to 0
 * 2. Write input arguments to ARG_REG_0..3
 * 3. Write message ID to MSG_REG (triggers PMFW interrupt)
 * 4. Poll RESP_REG until non-zero — value is the result code
 * 5. Read response data from ARG_REG_0 (message-specific)
 *
 * For payloads too large for the four argument registers, the protocol supports
 * DRAM table transfers where DAL allocates a DRAM buffer and exchanges bulk data
 * with PMFW through system memory.
 *
 * This documentation contains the subsections:\n\n
 * @ref ResponseCodes\n
 * @ref Messages\n
 * @ref DramTables\n
 */

#define DALSMC_VERSION  0x1

/** @defgroup ResponseCodes PMFW Response Codes
 *  @{
 */
// SMU Response Codes:
#define DALSMC_Result_OK                    0x01
#define DALSMC_Result_Failed                0xFF
#define DALSMC_Result_UnknownCmd            0xFE
#define DALSMC_Result_CmdRejectedPrereq     0xFD
#define DALSMC_Result_CmdRejectedBusy       0xFC
/** @} */

/** @defgroup Messages Message definitions
 *  @{
 */

/** Generic register overlay — four 32-bit C2PMSG argument registers. */
typedef struct {
	uint32_t Reg0;
	uint32_t Reg1;
	uint32_t Reg2;
	uint32_t Reg3;
} DALSMC_args_t;

/**
 * DALSMC_MSG_TestMessage - Test interface connectivity.
 *
 * Echos back the argument value incremented by 1. Use to verify the mailbox
 * is functional before sending real messages.
 *
 * Request:  TestValue — arbitrary test integer
 * Response: Reg0      — TestValue + 1
 */
#define DALSMC_MSG_TestMessage                  0x01
typedef union {
	struct {
		uint32_t TestValue;
		uint32_t Reserved[3];
	};
	DALSMC_args_t Args;
} DALSMC_TestMessage_arg_t;

/**
 * DALSMC_MSG_GetMsgHeaderVersion - Query the DALSMC header version running on PMFW.
 *
 * DAL uses the returned version to determine which messages are supported in
 * environments that require backwards compatibility.
 *
 * Request:  (none)
 * Response: Reg0 — DALSMC_VERSION value compiled into PMFW
 */
#define DALSMC_MSG_GetMsgHeaderVersion          0x02

/**
 * DALSMC_MSG_TransferTableSmu2Dram - Transfer a PMFW table into DRAM.
 * DALSMC_MSG_TransferTableDram2Smu - Transfer a DRAM buffer into PMFW.
 *
 * Both directions use the same argument layout. The DRAM address must be set
 * beforehand (AddrLow / AddrHigh are the GPU MC address bits [31:0] / [63:32]).
 *
 * Smu2Dram supported tables: TABLE_DAL_INIT (DPM clocks + UTM QoS + memory config)
 * Dram2Smu supported tables: TABLE_SOC_UTM  (debug override of UTM QoS parameters)
 *
 * Request:  TableId  — table identifier (TABLE_* defines below)
 *           AddrLow  — GPU MC address bits [31:0]  of destination/source buffer
 *           AddrHigh — GPU MC address bits [63:32] of destination/source buffer
 * Response: (none beyond result code)
 */
#define DALSMC_MSG_TransferTableSmu2Dram        0x03
#define DALSMC_MSG_TransferTableDram2Smu        0x04
typedef union {
	struct {
		uint32_t TableId;
		uint32_t AddrLow;
		uint32_t AddrHigh;
		uint32_t Reserved;
	};
	DALSMC_args_t Args;
} DALSMC_TransferTable_arg_t;

/**
 * DALSMC_MSG_SetHardMinByFreq - Set a lower bound frequency constraint on a PPCLK.
 *
 * Response does not indicate that the effective clock has already been raised to
 * meet the minimum requirement; poll DALSMC_MSG_ReturnHardMinStatus for the status
 * of the request.
 *
 * Supported clocks: SOCCLK, DISPCLK, DPPCLK, DCFCLK, DTBCLK.
 *
 * Request:  FreqKhz[23:0] — target minimum frequency in kHz (0 to ~16.7 GHz)
 *           Ppclk[31:24]  — PPCLK_e clock identifier
 * Response: (none beyond result code)
 */
#define DALSMC_MSG_SetHardMinByFreq             0x05
typedef union {
	struct {
		uint32_t FreqKhz : 24;
		uint32_t Ppclk   : 8;
		uint32_t Reserved[3];
	};
	DALSMC_args_t Args;
} DALSMC_SetHardMinByFreq_arg_t;

/**
 * DALSMC_MSG_SetMinDeepSleepDcfclk - Set the minimum DCFCLK frequency in deep sleep.
 *
 * Request:  MinDcfclkMhz — minimum DCFCLK frequency in MHz during deep sleep
 * Response: (none beyond result code)
 */
#define DALSMC_MSG_SetMinDeepSleepDcfclk        0x06
typedef union {
	struct {
		uint32_t MinDcfclkMhz;
		uint32_t Reserved[3];
	};
	DALSMC_args_t Args;
} DALSMC_SetMinDeepSleepDcfclk_arg_t;

/**
 * DALSMC_MSG_BacoAudioD3PME - Wake the audio block from D3/BACO.
 *
 * Triggers PMFW to bring the AZ (audio) block out of its D3 power state.
 * No arguments or response data; result code indicates success.
 */
#define DALSMC_MSG_BacoAudioD3PME               0x07

/**
 * DALSMC_MSG_ReturnHardMinStatus - Query outstanding hard-min request status.
 *
 * Returns a bitmask reporting which PPCLK hard-min requests have been satisfied
 * by the arbiter. Each bit position corresponds to the matching PPCLK_e value.
 * A set bit means the arbiter has reached or exceeded the requested minimum.
 *
 * Request:  (none)
 * Response: Reg0 — bitmask of satisfied PPCLKs (bit N set ↔ PPCLK_e N is satisfied)
 */
#define DALSMC_MSG_ReturnHardMinStatus          0x08

/**
 * DALSMC_MSG_IndicatePstateStatus - Indicate to PMFW various DMU behaviors required
 * to support UCLK P-state, for example whether or not DMU needs to modulate refresh
 * rate to perform UCLK switches.
 *
 * Request:  WaitResp[0]   — DAL requires a synchronous response before proceeding
 *           DrrEnable[1]  — DRR (dynamic refresh rate modulation) is active
 *           AltCh[2]      — alternate-channel mode is active
 *           AllowUclk[16] — DCN can tolerate UCLK P-state switches
 *           AllowFclk[17] — DCN can tolerate FCLK P-state switches
 * Response: (none beyond result code)
 */
#define DALSMC_MSG_IndicatePstateStatus         0x09
typedef union {
	struct {
		uint32_t WaitResp  : 1;
		uint32_t DrrEnable : 1;
		uint32_t AltCh     : 1;
		uint32_t Reserved  : 13;
		uint32_t AllowUclk : 1;
		uint32_t AllowFclk : 1;
		uint32_t Reserved1 : 14;
		uint32_t Reserved2[3];
	};
	DALSMC_args_t Args;
} DALSMC_IndicatePstateStatus_arg_t;

/**
 * DALSMC_MSG_UpdateUTMQoSRequest - Update the active UTM QoS bandwidth/latency request.
 *
 * Passes the current display bandwidth and latency requirements to PMFW so it
 * can select the appropriate SoC operating point (UCLK/FCLK level) from the
 * UTM table. Called whenever the display configuration changes.
 *
 * The QoS requirement must take effect before PMFW sends its response.
 *
 * Request:  LatencySopIndex      — index into the UTM SOP table that satisfies latency
 *           NominalBandwidthKBps — required nominal (average) bandwidth in KB/s
 *           UrgentBandwidthKBps  — required urgent bandwidth in KB/s
 *           LsdmaBandwidthKBps   — required LSDMA bandwidth in KB/s
 * Response: (none beyond result code)
 */
#define DALSMC_MSG_UpdateUTMQoSRequest          0x0A
typedef union {
	struct {
		uint32_t LatencySopIndex;
		uint32_t NominalBandwidthKBps;
		uint32_t UrgentBandwidthKBps;
		uint32_t LsdmaBandwidthKBps;
	};
	DALSMC_args_t Args;
} DALSMC_UpdateUTMQoSRequest_arg_t;

/**
 * DALSMC_MSG_SetDisplayIdleOptimizations - Notify PMFW of DCN idle-state conditions.
 *
 * Indicates which display-side power optimizations are currently safe to apply.
 * PMFW uses these flags to gate deeper SoC power states such as S0i2.
 *
 * Request:  DfRequestDisabled[0] — DF (data fabric) requests from DCN are disabled
 *           PhyRefClkOff[1]      — PHY reference clock has been gated off
 *           S0i2Rdy[2]           — DCN is ready for the system to enter S0i2
 * Response: (none beyond result code)
 */
#define DALSMC_MSG_SetDisplayIdleOptimizations  0x0B
typedef union {
	struct {
		uint32_t DfRequestDisabled : 1;
		uint32_t PhyRefClkOff      : 1;
		uint32_t S0i2Rdy           : 1;
		uint32_t Reserved          : 29;
		uint32_t Reserved1[3];
	};
	DALSMC_args_t Args;
} DALSMC_SetDisplayIdleOptimizations_arg_t;

/**
 * DALSMC_MSG_SetStutterEfficiency - Report DCN stutter efficiency to PMFW.
 *
 * Informs PMFW of the current stutter utilisation for base and low-power stutter
 * modes so PMFW can adjust memory power policy accordingly.
 *
 * Base mode    — lower enter+exit latency (PHY LP1, no UCIE LP).
 * Low-power mode — higher enter+exit latency (PHY LP2, UCIE LP1).
 *
 * Request:  BaseEfficiencyPct[7:0]     — stutter efficiency % in base mode
 *           LowPowerEfficiencyPct[15:8] — stutter efficiency % in low-power mode
 * Response: (none beyond result code)
 */
#define DALSMC_MSG_SetStutterEfficiency         0x0C
typedef union {
	struct {
		uint32_t BaseEfficiencyPct     : 8;
		uint32_t LowPowerEfficiencyPct : 8;
		uint32_t Reserved              : 16;
		uint32_t Reserved1[3];
	};
	DALSMC_args_t Args;
} DALSMC_SetStutterEfficiency_arg_t;

#define DALSMC_Message_Count                    0x0D ///< Total number of messages

/** @} */

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
	SocUtmSopEntry_t Sops[MAX_UTM_LOAD_LEVEL_COUNT][DALSMC_MAX_UTM_SOP_COUNT];
} SocUtmTable_t;

/**
 * TABLE_DAL_INIT - Full TABLE_DAL_INIT payload transferred from SMU to DRAM.
 */
#define TABLE_DAL_INIT    0xD
#define MAX_PPCLK_COUNT   12
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

/** @} */

#endif /* DALSMC_H */
