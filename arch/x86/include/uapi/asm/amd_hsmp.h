/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _UAPI_ASM_X86_AMD_HSMP_H_
#define _UAPI_ASM_X86_AMD_HSMP_H_

#include <linux/types.h>

#pragma pack(4)

#define HSMP_MAX_MSG_LEN 8

/*
 * HSMP client platforms (Ryzen Master SMC) accept at most four input
 * argument words and four output/response words per message.
 */
#define HSMP_CLIENT_MAX_MSG_LEN 4

/*
 * HSMP Messages supported
 */
enum hsmp_message_ids {
	HSMP_TEST = 1,			/* 01h Increments input value by 1 */
	HSMP_GET_SMU_VER,		/* 02h SMU FW version */
	HSMP_GET_PROTO_VER,		/* 03h HSMP interface version */
	HSMP_GET_SOCKET_POWER,		/* 04h average package power consumption */
	HSMP_SET_SOCKET_POWER_LIMIT,	/* 05h Set the socket power limit */
	HSMP_GET_SOCKET_POWER_LIMIT,	/* 06h Get current socket power limit */
	HSMP_GET_SOCKET_POWER_LIMIT_MAX,/* 07h Get maximum socket power value */
	HSMP_SET_BOOST_LIMIT,		/* 08h Set a core maximum frequency limit */
	HSMP_SET_BOOST_LIMIT_SOCKET,	/* 09h Set socket maximum frequency level */
	HSMP_GET_BOOST_LIMIT,		/* 0Ah Get current frequency limit */
	HSMP_GET_PROC_HOT,		/* 0Bh Get PROCHOT status */
	HSMP_SET_XGMI_LINK_WIDTH,	/* 0Ch Set max and min width of xGMI Link */
	HSMP_SET_DF_PSTATE,		/* 0Dh Alter APEnable/Disable messages behavior */
	HSMP_SET_AUTO_DF_PSTATE,	/* 0Eh Enable DF P-State Performance Boost algorithm */
	HSMP_GET_FCLK_MCLK,		/* 0Fh Get FCLK and MEMCLK for current socket */
	HSMP_GET_CCLK_THROTTLE_LIMIT,	/* 10h Get CCLK frequency limit in socket */
	HSMP_GET_C0_PERCENT,		/* 11h Get average C0 residency in socket */
	HSMP_SET_NBIO_DPM_LEVEL,	/* 12h Set max/min LCLK DPM Level for a given NBIO */
	HSMP_GET_NBIO_DPM_LEVEL,	/* 13h Get LCLK DPM level min and max for a given NBIO */
	HSMP_GET_DDR_BANDWIDTH,		/* 14h Get theoretical maximum and current DDR Bandwidth */
	HSMP_GET_TEMP_MONITOR,		/* 15h Get socket temperature */
	HSMP_GET_DIMM_TEMP_RANGE,	/* 16h Get per-DIMM temperature range and refresh rate */
	HSMP_GET_DIMM_POWER,		/* 17h Get per-DIMM power consumption */
	HSMP_GET_DIMM_THERMAL,		/* 18h Get per-DIMM thermal sensors */
	HSMP_GET_SOCKET_FREQ_LIMIT,	/* 19h Get current active frequency per socket */
	HSMP_GET_CCLK_CORE_LIMIT,	/* 1Ah Get CCLK frequency limit per core */
	HSMP_GET_RAILS_SVI,		/* 1Bh Get SVI-based Telemetry for all rails */
	HSMP_GET_SOCKET_FMAX_FMIN,	/* 1Ch Get Fmax and Fmin per socket */
	HSMP_GET_IOLINK_BANDWITH,	/* 1Dh Get current bandwidth on IO Link */
	HSMP_GET_XGMI_BANDWITH,		/* 1Eh Get current bandwidth on xGMI Link */
	HSMP_SET_GMI3_WIDTH,		/* 1Fh Set max and min GMI3 Link width */
	HSMP_SET_PCI_RATE,		/* 20h Control link rate on PCIe devices */
	HSMP_SET_POWER_MODE,		/* 21h Select power efficiency profile policy */
	HSMP_SET_PSTATE_MAX_MIN,	/* 22h Set the max and min DF P-State  */
	HSMP_GET_METRIC_TABLE_VER,	/* 23h Get metrics table version */
	HSMP_GET_METRIC_TABLE,		/* 24h Get metrics table */
	HSMP_GET_METRIC_TABLE_DRAM_ADDR,/* 25h Get metrics table dram address */
	HSMP_SET_XGMI_PSTATE_RANGE,	/* 26h Set xGMI P-state range */
	HSMP_CPU_RAIL_ISO_FREQ_POLICY,	/* 27h Get/Set Cpu Iso frequency policy */
	HSMP_DFC_ENABLE_CTRL,		/* 28h Enable/Disable DF C-state */
	HSMP_PC6_ENABLE,		/* 29h Get/Set PC6 enable/disable status */
	HSMP_CC6_ENABLE,		/* 2Ah Get/Set CC6 enable/disable status */
	HSMP_GET_RAPL_UNITS = 0x30,	/* 30h Get scaling factor for energy */
	HSMP_GET_RAPL_CORE_COUNTER,	/* 31h Get core energy counter value */
	HSMP_GET_RAPL_PACKAGE_COUNTER,	/* 32h Get package energy counter value */
	HSMP_DIMM_SB_RD,		/* 33h Get DIMM sideband data */
	HSMP_READ_CCD_POWER,		/* 34h Get average CCD power */
	HSMP_READ_TDELTA,		/* 35h Get thermal behaviour */
	HSMP_GET_SVI3_VR_CTRL_TEMP,	/* 36h Get SVI3 VR controller rail temp */
	HSMP_GET_ENABLED_HSMP_CMDS,	/* 37h Get supported HSMP commands */
	HSMP_SET_GET_FLOOR_LIMIT,	/* 38h Get/Set core floor frequency limit */
	HSMP_DIMM_SB_WR,		/* 39h Set DIMM sideband data */
	HSMP_SDPS_LIMIT,		/* 3Ah Get/Set SDPS limit */
	HSMP_PQOS_TRAFFIC_PRIORITY,	/* 3Bh Get/Set traffic priority */
	HSMP_PQOS_FLOATING_BW,		/* 3Ch Get/Set max floating bandwidth */
	HSMP_MSG_ID_MAX,
};

/*
 * The driver validates num_args and response_sz before sending a message.
 * HSMP client platforms are bound to HSMP_CLIENT_MAX_MSG_LEN in each
 * direction; server platforms are bound per-message by hsmp_msg_desc_table[].
 */
struct hsmp_message {
	__u32	msg_id;			/* Message ID */
	__u16	num_args;		/* Number of input argument words in message */
	__u16	response_sz;		/* Number of expected output/response words */
	__u32	args[HSMP_MAX_MSG_LEN];	/* argument/response buffer */
	__u16	sock_ind;		/* socket number */
};

enum hsmp_msg_type {
	HSMP_RSVD = -1,
	HSMP_SET  = 0,
	HSMP_GET  = 1,
	HSMP_SET_GET	= 2,
};

enum hsmp_proto_versions {
	HSMP_PROTO_VER2	= 2,
	HSMP_PROTO_VER3,
	HSMP_PROTO_VER4,
	HSMP_PROTO_VER5,
	HSMP_PROTO_VER6,
	HSMP_PROTO_VER7
};

/*
 * Ryzen Master SMC interface version, reported by
 * HSMP_CLIENT_GET_INTERFACE_VER. Separate numbering space from enum
 * hsmp_proto_versions, which applies to the server set only.
 */
enum ryzen_master_proto_versions {
	RYZEN_MASTER_PROTO_VER1 = 1,
};

struct hsmp_msg_desc {
	int num_args;
	int response_sz;
	enum hsmp_msg_type type;
};

/*
 * User may use these comments as reference, please find the
 * supported list of messages and message definition in the
 * HSMP chapter of respective family/model PPR.
 *
 * Not supported messages would return -ENOMSG.
 */
static const struct hsmp_msg_desc hsmp_msg_desc_table[]
				__attribute__((unused)) = {
	/* RESERVED */
	{0, 0, HSMP_RSVD},

	/*
	 * HSMP_TEST, num_args = 1, response_sz = 1
	 * input:  args[0] = xx
	 * output: args[0] = xx + 1
	 */
	{1, 1, HSMP_GET},

	/*
	 * HSMP_GET_SMU_VER, num_args = 0, response_sz = 1
	 * output: args[0] = smu fw ver
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_GET_PROTO_VER, num_args = 0, response_sz = 1
	 * output: args[0] = proto version
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_GET_SOCKET_POWER, num_args = 0, response_sz = 1
	 * output: args[0] = socket power in mWatts
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_SET_SOCKET_POWER_LIMIT, num_args = 1, response_sz = 0
	 * input: args[0] = power limit value in mWatts
	 */
	{1, 0, HSMP_SET},

	/*
	 * HSMP_GET_SOCKET_POWER_LIMIT, num_args = 0, response_sz = 1
	 * output: args[0] = socket power limit value in mWatts
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_GET_SOCKET_POWER_LIMIT_MAX, num_args = 0, response_sz = 1
	 * output: args[0] = maximuam socket power limit in mWatts
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_SET_BOOST_LIMIT, num_args = 1, response_sz = 0
	 * input: args[0] = apic id[31:16] + boost limit value in MHz[15:0]
	 */
	{1, 0, HSMP_SET},

	/*
	 * HSMP_SET_BOOST_LIMIT_SOCKET, num_args = 1, response_sz = 0
	 * input: args[0] = boost limit value in MHz
	 */
	{1, 0, HSMP_SET},

	/*
	 * HSMP_GET_BOOST_LIMIT, num_args = 1, response_sz = 1
	 * input: args[0] = apic id
	 * output: args[0] = boost limit value in MHz
	 */
	{1, 1, HSMP_GET},

	/*
	 * HSMP_GET_PROC_HOT, num_args = 0, response_sz = 1
	 * output: args[0] = proc hot status
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_SET_XGMI_LINK_WIDTH, num_args = 1, response_sz = 0/1
	 * input: args[0] = set/get XGMI Link width[31] (0 = set, 1 = get) +
	 *                  min link width[15:8] + max link width[7:0]
	 *        Link width encoding: 0 = x4, 1 = x8, 2 = x16.
	 *        On SET, max must be >= min.  On GET, [15:0] are reserved.
	 * output: args[0] = reserved[31:16] + min link width[15:8] +
	 *                   max link width[7:0]
	 */
	{1, 1, HSMP_SET_GET},

	/*
	 * HSMP_SET_DF_PSTATE (APBDisable), num_args = 1, response_sz = 0/1
	 * input: args[0] = set APB_DISABLE / get APB state[31]
	 *                  (0 = set & lock DF P-state, 1 = get) +
	 *                  reserved[30:8] +
	 *                  DF P-state[7:0] (0..2; reserved on GET)
	 * output: args[0] = reserved[31:9] +
	 *                   APB state[8] (1 = disabled, 0 = enabled) +
	 *                   locked DF P-state[7:0] if [8] = 1, else reserved
	 */
	{1, 1, HSMP_SET_GET},

	/* HSMP_SET_AUTO_DF_PSTATE, num_args = 0, response_sz = 0 */
	{0, 0, HSMP_SET},

	/*
	 * HSMP_GET_FCLK_MCLK, num_args = 0, response_sz = 2
	 * output: args[0] = fclk in MHz, args[1] = mclk in MHz
	 */
	{0, 2, HSMP_GET},

	/*
	 * HSMP_GET_CCLK_THROTTLE_LIMIT, num_args = 0, response_sz = 1
	 * output: args[0] = core clock in MHz
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_GET_C0_PERCENT, num_args = 0, response_sz = 1
	 * output: args[0] = average c0 residency
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_SET_NBIO_DPM_LEVEL, num_args = 1, response_sz = 0
	 * input: args[0] = nbioid[23:16] + max dpm level[15:8] + min dpm level[7:0]
	 */
	{1, 0, HSMP_SET},

	/*
	 * HSMP_GET_NBIO_DPM_LEVEL, num_args = 1, response_sz = 1
	 * input: args[0] = nbioid[23:16]
	 * output: args[0] = max dpm level[15:8] + min dpm level[7:0]
	 */
	{1, 1, HSMP_GET},

	/*
	 * HSMP_GET_DDR_BANDWIDTH, num_args = 0, response_sz = 1
	 * output: args[0] = max bw in Gbps[31:20] + utilised bw in Gbps[19:8] +
	 * bw in percentage[7:0]
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_GET_TEMP_MONITOR, num_args = 0, response_sz = 1
	 * output: args[0] = temperature in degree celsius. [15:8] integer part +
	 * [7:5] fractional part
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_GET_DIMM_TEMP_RANGE, num_args = 1, response_sz = 1
	 * input: args[0] = DIMM address[7:0]
	 * output: args[0] = refresh rate[3] + temperature range[2:0]
	 */
	{1, 1, HSMP_GET},

	/*
	 * HSMP_GET_DIMM_POWER, num_args = 1, response_sz = 1
	 * input: args[0] = DIMM address[7:0]
	 * output: args[0] = DIMM power in mW[31:17] + update rate in ms[16:8] +
	 * DIMM address[7:0]
	 */
	{1, 1, HSMP_GET},

	/*
	 * HSMP_GET_DIMM_THERMAL, num_args = 1, response_sz = 1
	 * input: args[0] = DIMM address[7:0]
	 * output: args[0] = temperature in degree celsius[31:21] + update rate in ms[16:8] +
	 * DIMM address[7:0]
	 */
	{1, 1, HSMP_GET},

	/*
	 * HSMP_GET_SOCKET_FREQ_LIMIT, num_args = 0, response_sz = 1
	 * output: args[0] = frequency in MHz[31:16] + frequency source[15:0]
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_GET_CCLK_CORE_LIMIT, num_args = 1, response_sz = 1
	 * input: args[0] = apic id [31:0]
	 * output: args[0] = frequency in MHz[31:0]
	 */
	{1, 1, HSMP_GET},

	/*
	 * HSMP_GET_RAILS_SVI, num_args = 0, response_sz = 1
	 * output: args[0] = power in mW[31:0]
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_GET_SOCKET_FMAX_FMIN, num_args = 0, response_sz = 1
	 * output: args[0] = fmax in MHz[31:16] + fmin in MHz[15:0]
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_GET_IOLINK_BANDWITH, num_args = 1, response_sz = 1
	 * input: args[0] = link id[15:8] + bw type[2:0]
	 * output: args[0] = io bandwidth in Mbps[31:0]
	 */
	{1, 1, HSMP_GET},

	/*
	 * HSMP_GET_XGMI_BANDWITH, num_args = 1, response_sz = 1
	 * input: args[0] = link id[15:8] + bw type[2:0]
	 * output: args[0] = xgmi bandwidth in Mbps[31:0]
	 */
	{1, 1, HSMP_GET},

	/*
	 * HSMP_SET_GMI3_WIDTH, num_args = 1, response_sz = 0
	 * input: args[0] = min link width[15:8] + max link width[7:0]
	 */
	{1, 0, HSMP_SET},

	/*
	 * HSMP_SET_PCI_RATE, num_args = 1, response_sz = 1
	 * input: args[0] = link rate control value
	 * output: args[0] = previous link rate control value
	 */
	{1, 1, HSMP_SET},

	/*
	 * HSMP_SET_POWER_MODE (PwrEfficiencyModeSelection),
	 *	num_args = 1, response_sz = 1
	 * input: args[0] = set/get policy[31] (0 = set, 1 = get) +
	 *                  high util point[30:24] +
	 *                  low util point[23:17] +
	 *                  PPT limit[16:5] +
	 *                  reserved[4:3] + mode selection[2:0]
	 *        [30:5] are valid only when [2:0] is a balanced core mode
	 *        (4 or 5). [2:0] is reserved when getting (bit[31] = 1).
	 * output: args[0] same layout, [31] reserved, [2:0] = arbitrated
	 *         current efficiency mode.
	 */
	{1, 1, HSMP_SET_GET},

	/*
	 * HSMP_SET_PSTATE_MAX_MIN (DfPstateRange), num_args = 1, response_sz = 0/1
	 * input: args[0] = set/get DF P-state range[31] (0 = set, 1 = get) +
	 *                  reserved[30:16] +
	 *                  min DF P-state[15:8] + max DF P-state[7:0]
	 *        DF P-state encoding: 0 = DFP0 (high performance),
	 *                             1 = DFP1, 2 = DFP2 (low performance).
	 *        [15:0] are reserved when getting (args[0] bit[31] = 1).
	 * output: args[0] = reserved[31:16] + min DF P-state[15:8] +
	 *                   max DF P-state[7:0]
	 */
	{1, 1, HSMP_SET_GET},

	/*
	 * HSMP_GET_METRIC_TABLE_VER, num_args = 0, response_sz = 1
	 * output: args[0] = metrics table version
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_GET_METRIC_TABLE, num_args = 0, response_sz = 0
	 */
	{0, 0, HSMP_GET},

	/*
	 * HSMP_GET_METRIC_TABLE_DRAM_ADDR, num_args = 0, response_sz = 3
	 * output: args[0] = lower 32 bits of the address
	 * output: args[1] = upper 32 bits of the address
	 * output: args[2] = DRAM region size in bytes
	 */
	{0, 3, HSMP_GET},

	/*
	 * HSMP_SET_XGMI_PSTATE_RANGE, num_args = 1, response_sz = 0
	 * input: args[0] = min xGMI p-state[15:8] + max xGMI p-state[7:0]
	 */
	{1, 0, HSMP_SET},

	/*
	 * HSMP_CPU_RAIL_ISO_FREQ_POLICY, num_args = 1, response_sz = 1
	 * input: args[0] = set/get policy[31] +
	 * disable/enable independent control[0]
	 * output: args[0] = current policy[0]
	 */
	{1, 1, HSMP_SET_GET},

	/*
	 * HSMP_DFC_ENABLE_CTRL, num_args = 1, response_sz = 1
	 * input: args[0] = set/get policy[31] + enable/disable DFC[0]
	 * output: args[0] = current policy[0]
	 */
	{1, 1, HSMP_SET_GET},

	/*
	 * HSMP_PC6_ENABLE (Pc6Enable), num_args = 1, response_sz = 0/1
	 * input: args[0] = set/get PC6 control[31] (0 = set, 1 = get) +
	 *                  reserved[30:1] +
	 *                  enable PC6[0] (0 = disable, 1 = enable;
	 *                  reserved on GET)
	 * output: args[0] = reserved[31:1] + current PC6 control[0]
	 *                   (last value configured via HSMP or APML)
	 */
	{1, 1, HSMP_SET_GET},

	/*
	 * HSMP_CC6_ENABLE (CC6Enable), num_args = 1, response_sz = 0/1
	 * Configures CC6 enable for all cores; changing the setting does
	 * not by itself transition cores in or out of CC6.
	 * input: args[0] = set/get CC6 control[31] (0 = set, 1 = get) +
	 *                  reserved[30:1] +
	 *                  enable CC6[0] (0 = disable, 1 = enable;
	 *                  reserved on GET)
	 * output: args[0] = reserved[31:1] + current CC6 control[0]
	 *                   (last value configured via HSMP or APML)
	 */
	{1, 1, HSMP_SET_GET},

	/* RESERVED(0x2B-0x2F) */
	{0, 0, HSMP_RSVD},
	{0, 0, HSMP_RSVD},
	{0, 0, HSMP_RSVD},
	{0, 0, HSMP_RSVD},
	{0, 0, HSMP_RSVD},

	/*
	 * HSMP_GET_RAPL_UNITS, response_sz = 1
	 * output: args[0] = tu value[19:16] + esu value[12:8]
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_GET_RAPL_CORE_COUNTER, num_args = 1, response_sz = 1
	 * input: args[0] = apic id[15:0]
	 * output: args[0] = lower 32 bits of energy
	 * output: args[1] = upper 32 bits of energy
	 */
	{1, 2, HSMP_GET},

	/*
	 * HSMP_GET_RAPL_PACKAGE_COUNTER, num_args = 0, response_sz = 1
	 * output: args[0] = lower 32 bits of energy
	 * output: args[1] = upper 32 bits of energy
	 */
	{0, 2, HSMP_GET},

	/*
	 * HSMP_DIMM_SB_RD, num_args = 1, response_sz = 1
	 * input: args[0] = reg space[23] + reg offset[22:12] +
	 *                  device LID[11:8] + DIMM address[7:0]
	 * output: args[0] = read data byte[3:0]
	 */
	{1, 1, HSMP_GET},

	/*
	 * HSMP_READ_CCD_POWER, num_args = 1, response_sz = 1
	 * input: args[0]  = apic id of core[15:0]
	 * output: args[0] = CCD power(mWatts)[31:0]
	 */
	{1, 1, HSMP_GET},

	/*
	 * HSMP_READ_TDELTA, num_args = 0, response_sz = 1
	 * output: args[0] = thermal behaviour[31:0]
	 */
	{0, 1, HSMP_GET},

	/*
	 * HSMP_GET_SVI3_VR_CTRL_TEMP, num_args = 1, response_sz = 1
	 * input: args[0] = SVI3 rail index[3:1] + read temperature[0]
	 * output: args[0] = SVI3 rail index[30:28] +
	 *                   rail temperature in degree C[27:0]
	 */
	{1, 1, HSMP_GET},

	/*
	 * HSMP_GET_ENABLED_HSMP_CMDS, num_args = 1, response_sz = 3
	 * input: args[0] = HSMP command mask[0]
	 * output: status of HSMP command = args[0], args[1], args[2]
	 */
	{1, 3, HSMP_GET},

	/*
	 * HSMP_SET_GET_FLOOR_LIMIT, num_args = 1, response_sz = 1
	 * input: args[0] = op[31:30] + reserved[29:28] +
	 *                  apic id[27:16] + floor frequency MHz[15:0]
	 *        op encoding: 00 = set per-core floor,
	 *                     01 = set all-cores floor (apic id reserved),
	 *                     10 = get per-core floor,
	 *                     11 = get per-core effective floor.
	 *        Floor frequency field is reserved on GET (bit[31] = 1).
	 * output: args[0] = floor frequency MHz[15:0]
	 *                   (effective for op 11, configured for op 10;
	 *                   reserved on SET)
	 */
	{1, 1, HSMP_SET_GET},

	/*
	 * HSMP_DIMM_SB_WR, num_args = 1, response_sz = 0
	 * input: args[0] = write data[31:24] + reg space[23] +
	 *                  reg offset[22:12] + device LID[11:8] +
	 *                  DIMM address[7:0]
	 */
	{1, 0, HSMP_SET},

	/*
	 * HSMP_SDPS_LIMIT, num_args = 1, response_sz = 1
	 * input: args[0] = set/get SDPS limit[31] (0 = set, 1 = get) +
	 *                  SDPS limit[30:0]
	 * output: args[0] = SDPS limit[30:0]
	 */
	{1, 1, HSMP_SET_GET},

	/*
	 * HSMP_PQOS_TRAFFIC_PRIORITY, num_args = 1, response_sz = 1
	 * input: args[0] = op[31:30] + priority sel[27:26] +
	 *                  priority val[21:20] + input[19:0]
	 * output: args[0] = supported priorities or priority val[1:0]
	 */
	{1, 1, HSMP_SET_GET},

	/*
	 * HSMP_PQOS_FLOATING_BW, num_args = 1, response_sz = 2
	 * input: args[0] = op[31] + sub-op[30:29] + params[28:0]
	 * output: args[0] = discovery bits or floating/global memory BW (Gbps)
	 * output: args[1] = reserved or config (drop adj, sampling delay,
	 *                   hysteresis)
	 */
	{1, 2, HSMP_SET_GET},
};

/* Metrics table (supported only with proto version 6) */
struct hsmp_metric_table {
	__u32 accumulation_counter;

	/* TEMPERATURE */
	__u32 max_socket_temperature;
	__u32 max_vr_temperature;
	__u32 max_hbm_temperature;
	__u64 max_socket_temperature_acc;
	__u64 max_vr_temperature_acc;
	__u64 max_hbm_temperature_acc;

	/* POWER */
	__u32 socket_power_limit;
	__u32 max_socket_power_limit;
	__u32 socket_power;

	/* ENERGY */
	__u64 timestamp;
	__u64 socket_energy_acc;
	__u64 ccd_energy_acc;
	__u64 xcd_energy_acc;
	__u64 aid_energy_acc;
	__u64 hbm_energy_acc;

	/* FREQUENCY */
	__u32 cclk_frequency_limit;
	__u32 gfxclk_frequency_limit;
	__u32 fclk_frequency;
	__u32 uclk_frequency;
	__u32 socclk_frequency[4];
	__u32 vclk_frequency[4];
	__u32 dclk_frequency[4];
	__u32 lclk_frequency[4];
	__u64 gfxclk_frequency_acc[8];
	__u64 cclk_frequency_acc[96];

	/* FREQUENCY RANGE */
	__u32 max_cclk_frequency;
	__u32 min_cclk_frequency;
	__u32 max_gfxclk_frequency;
	__u32 min_gfxclk_frequency;
	__u32 fclk_frequency_table[4];
	__u32 uclk_frequency_table[4];
	__u32 socclk_frequency_table[4];
	__u32 vclk_frequency_table[4];
	__u32 dclk_frequency_table[4];
	__u32 lclk_frequency_table[4];
	__u32 max_lclk_dpm_range;
	__u32 min_lclk_dpm_range;

	/* XGMI */
	__u32 xgmi_width;
	__u32 xgmi_bitrate;
	__u64 xgmi_read_bandwidth_acc[8];
	__u64 xgmi_write_bandwidth_acc[8];

	/* ACTIVITY */
	__u32 socket_c0_residency;
	__u32 socket_gfx_busy;
	__u32 dram_bandwidth_utilization;
	__u64 socket_c0_residency_acc;
	__u64 socket_gfx_busy_acc;
	__u64 dram_bandwidth_acc;
	__u32 max_dram_bandwidth;
	__u64 dram_bandwidth_utilization_acc;
	__u64 pcie_bandwidth_acc[4];

	/* THROTTLERS */
	__u32 prochot_residency_acc;
	__u32 ppt_residency_acc;
	__u32 socket_thm_residency_acc;
	__u32 vr_thm_residency_acc;
	__u32 hbm_thm_residency_acc;
	__u32 spare;

	/* New items at the end to maintain driver compatibility */
	__u32 gfxclk_frequency[8];
};

/**
 * struct hsmp_telemetry_data - Request descriptor for HSMP telemetry IOCTL
 * @buf:       Input. Userspace pointer (encoded as __u64 to keep the layout
 *             stable between 32-bit and 64-bit callers) to the destination
 *             buffer that receives the metric table.
 * @size:      Input. Size in bytes of the buffer pointed to by @buf, and the
 *             number of bytes copied out on success.  Must be non-zero and no
 *             larger than the metric table size firmware reports for this
 *             socket; a larger value is rejected with -EINVAL rather than
 *             short-written.  A smaller value returns the leading @size bytes
 *             of the snapshot.  The kernel does not write this field back.
 * @sock_ind:  Input. Socket index from which the metric table is read.
 * @reserved:  Reserved for future use.  Callers should set this to zero;
 *             future kernels may begin interpreting the field, so passing
 *             a non-zero value today is not forwards compatible.
 *
 * Placing @buf first lets all fields fall on their natural alignment under
 * the surrounding #pragma pack(4), so the struct is a tight 16 bytes with
 * the same wire layout on 32-bit and 64-bit userspace.
 *
 * The metric table layout depends on the HSMP protocol version reported by
 * firmware, which userspace can read from the protocol_version sysfs
 * attribute.  Protocol version 6 uses struct hsmp_metric_table, so callers on
 * that version pass sizeof(struct hsmp_metric_table).  Later version metrics
 * table layout is documented in the Public PPR.
 */
struct hsmp_telemetry_data {
	__u64	buf;
	__u32	size;
	__u16	sock_ind;
	__u16	reserved;
};

/* Reset to default packing */
#pragma pack()

/* Define unique ioctl command for hsmp msgs using generic _IOWR */
#define HSMP_BASE_IOCTL_NR	0xF8
#define HSMP_IOCTL_CMD		_IOWR(HSMP_BASE_IOCTL_NR, 0, struct hsmp_message)

/*
 * Fetch the firmware metric (telemetry) table for a given socket via the
 * HSMP character device.  This avoids the PAGE_SIZE limitation of the
 * sysfs binary attribute path for tables larger than one page (such as the
 * ~13 KB table used by HSMP protocol version 7).
 *
 * The direction is _IOW because the kernel only reads the request struct;
 * the table itself is written to the buffer that @buf points at.
 */
#define HSMP_IOCTL_GET_TELEMETRY_DATA \
	_IOW(HSMP_BASE_IOCTL_NR, 1, struct hsmp_telemetry_data)

/**
 * enum hsmp_client_message_ids - Ryzen Master SMC (RMSMC) message IDs
 * @HSMP_CLIENT_TEST: 01h. Test message. input: args[0] = xx. output:
 *	args[0] = xx + 1.
 * @HSMP_CLIENT_GET_SMU_VER: 02h. Get MP1 firmware version. output:
 *	args[0] = MP1 firmware version.
 * @HSMP_CLIENT_GET_INTERFACE_VER: 03h. Get interface version. output:
 *	args[0] = interface version.
 * @HSMP_CLIENT_GET_METRICS_TABLE_VER: 04h. Get metrics table version.
 *	output: args[0] = metrics table version.
 * @HSMP_CLIENT_GET_METRICS_TABLE: 05h. Get metrics table. No arguments.
 *	Success means firmware has written the metrics table to the DRAM
 *	address reported by @HSMP_CLIENT_GET_METRICS_TABLE_DRAM_ADDR.
 * @HSMP_CLIENT_GET_METRICS_TABLE_DRAM_ADDR: 06h. Get metrics table DRAM
 *	address. output: args[0] = address[31:0], args[1] = address[63:32],
 *	args[2] = table size in bytes.
 * @HSMP_CLIENT_SET_CORE_PSM_MARGIN: 07h. Set core voltage margin. input:
 *	args[0] = ccx number[31:28] + core number[27:20] + reserved[19:16] +
 *	margin in mV[15:0].
 * @HSMP_CLIENT_SET_ALL_CORE_PSM_MARGIN: 08h. Set voltage margin for all
 *	cores. input: args[0] = margin in mV[15:0].
 * @HSMP_CLIENT_SET_FAST_PPT_LIMIT: 09h. Set APU fast PPT limit. input:
 *	args[0] = limit in mW.
 * @HSMP_CLIENT_SET_VRM_VDD_CURRENT_LIMIT: 0Ah. Set VDDCR_VDD TDC. input:
 *	args[0] = limit in mA.
 * @HSMP_CLIENT_SET_VRM_VDD_MAX_CURRENT_LIMIT: 0Bh. Set VDDCR_VDD EDC.
 *	input: args[0] = limit in mA.
 * @HSMP_CLIENT_SET_TJ_MAX: 0Ch. Set maximum junction temperature. input:
 *	args[0] = temperature in degrees C.
 * @HSMP_CLIENT_SET_FIT_LIMIT_SCALAR: 0Dh. Set failures-in-time limit
 *	scalar. input: args[0] = scalar (0 to 100).
 * @HSMP_CLIENT_ENABLE_OVERCLOCKING: 0Eh. Enable overclocking. No
 *	arguments.
 * @HSMP_CLIENT_DISABLE_OVERCLOCKING: 0Fh. Disable overclocking. No
 *	arguments.
 * @HSMP_CLIENT_SET_OVERCLOCK_FREQ_ALL_CORES: 10h. Set all-core overclock
 *	frequency. input: args[0] = frequency in MHz[15:0].
 * @HSMP_CLIENT_SET_OVERCLOCK_FREQ_PER_CORE: 11h. Set per-core overclock
 *	frequency. input: args[0] = ccd number[31:28] + ccx number[27:24] +
 *	core number[23:20] + frequency in MHz[19:0].
 * @HSMP_CLIENT_SET_OVERCLOCK_VID: 12h. Set overclock VID. input:
 *	args[0] = reserved[31:17] + vid[16:8] + pstate[7:0].
 * @HSMP_CLIENT_SPARE_0X13: 13h. Reserved.
 * @HSMP_CLIENT_GET_CORE_PERF_ORDER: 14h. Get core performance order.
 *	input: args[0] = core performance rank index[31:0] (0 to Enabled
 *	cores-1). output: args[0] = voltage in mV[31:21] + frequency in
 *	MHz[20:9] + fll[8:6] + core number[5:2] + ccx number[1:0].
 * @HSMP_CLIENT_SET_SUSTAINED_POWER_LIMIT: 15h. Set SOC sustained power
 *	limit. input: args[0] = limit in mW.
 * @HSMP_CLIENT_SET_SLOW_PPT_LIMIT: 16h. Set APU slow PPT limit. input:
 *	args[0] = limit in mW.
 * @HSMP_CLIENT_SET_VRM_GFX_MAX_CURRENT_LIMIT: 17h. Set VDDCR_GFX EDC.
 *	input: args[0] = limit in mA.
 * @HSMP_CLIENT_SET_VRM_SOC_CURRENT_LIMIT: 18h. Set VDDCR_SOC TDC. input:
 *	args[0] = limit in mA.
 * @HSMP_CLIENT_SET_FAST_SPM_LIMIT: 19h. Set fast SPM limit. input:
 *	args[0] = limit in mW.
 * @HSMP_CLIENT_SET_SLOW_SPM_LIMIT: 1Ah. Set slow SPM limit. input:
 *	args[0] = limit in mW.
 * @HSMP_CLIENT_GET_CORE_PSM_MARGIN: 1Bh. Get core voltage margin. input:
 *	args[0] = ccx number[31:28] + core number[27:20] + reserved[19:0].
 *	output: args[0] = ccx number[31:28] + core number[27:20] +
 *	reserved[19:16] + margin in mV[15:0].
 * @HSMP_CLIENT_GET_GFX_PSM_MARGIN: 1Ch. Get graphics voltage margin.
 *	output: args[0] = margin in mV[15:0].
 * @HSMP_CLIENT_SPARE_0X1D: 1Dh. Reserved.
 * @HSMP_CLIENT_SPARE_0X1E: 1Eh. Reserved.
 * @HSMP_CLIENT_SPARE_0X1F: 1Fh. Reserved.
 * @HSMP_CLIENT_SPARE_0X20: 20h. Reserved.
 * @HSMP_CLIENT_SET_GFXCLK_OVERDRIVE_BY_FREQ_VID: 21h. Set GfxClk
 *	overdrive by frequency/VID. input: args[0] = reserved[31:25] +
 *	vid[24:16] + frequency in MHz[15:0].
 * @HSMP_CLIENT_DISABLE_GFXCLK_OVERDRIVE: 22h. Disable GfxClk overdrive.
 *	No arguments.
 * @HSMP_CLIENT_SET_GFX_PSM_MARGIN: 23h. Set graphics voltage margin.
 *	input: args[0] = margin in mV[15:0].
 * @HSMP_CLIENT_SET_CCLK_FMAX_OFFSET: 24h. Set CCLK Fmax offset. input:
 *	args[0] = maximum frequency in MHz[15:0].
 * @HSMP_CLIENT_SET_CORE_POWER_LIMIT_OFFSET: 25h. Set core power limit
 *	offset. input: args[0] = limit in mW.
 * @HSMP_CLIENT_ADD_EXTRA_PSM_GUARDBAND: 26h. Add extra core PSM
 *	guardband. input: args[0] = PsmGuardband[2][31:24] +
 *	PsmGuardband[1][23:16] + PsmGuardband[0][15:8] + TriggerUpdate[7] +
 *	AvfsType[6:4] + frequency index[3:0].
 * @HSMP_CLIENT_ADD_EXTRA_PSM_GUARDBAND_GFX: 27h. Add extra graphics PSM
 *	guardband. input: args[0] = PsmGuardband[2][31:24] +
 *	PsmGuardband[1][23:16] + PsmGuardband[0][15:8] + TriggerUpdate[7] +
 *	frequency index[6:0].
 * @HSMP_CLIENT_SET_GFXCLK_FMAX: 28h. Set GfxClk Fmax. input: args[0] =
 *	maximum frequency in MHz[15:0].
 * @HSMP_CLIENT_MSG_ID_MAX: Number of message IDs, not a valid ID itself.
 *
 * Message IDs accepted on the Family 1Ah client platforms, Models 80h-8Fh
 * and E0h-E3h. These parts drive one mailbox and speak the Ryzen Master
 * SMC message set instead of the server HSMP message set enumerated in
 * &enum hsmp_message_ids. Not all platforms support all messages; consult
 * the supported list of messages in the HSMP chapter of the respective
 * family/model PPR. Unsupported messages return -ENOMSG.
 */
enum hsmp_client_message_ids {
	HSMP_CLIENT_TEST = 1,
	HSMP_CLIENT_GET_SMU_VER,
	HSMP_CLIENT_GET_INTERFACE_VER,
	HSMP_CLIENT_GET_METRICS_TABLE_VER,
	HSMP_CLIENT_GET_METRICS_TABLE,
	HSMP_CLIENT_GET_METRICS_TABLE_DRAM_ADDR,
	HSMP_CLIENT_SET_CORE_PSM_MARGIN,
	HSMP_CLIENT_SET_ALL_CORE_PSM_MARGIN,
	HSMP_CLIENT_SET_FAST_PPT_LIMIT,
	HSMP_CLIENT_SET_VRM_VDD_CURRENT_LIMIT,
	HSMP_CLIENT_SET_VRM_VDD_MAX_CURRENT_LIMIT,
	HSMP_CLIENT_SET_TJ_MAX,
	HSMP_CLIENT_SET_FIT_LIMIT_SCALAR,
	HSMP_CLIENT_ENABLE_OVERCLOCKING,
	HSMP_CLIENT_DISABLE_OVERCLOCKING,
	HSMP_CLIENT_SET_OVERCLOCK_FREQ_ALL_CORES,
	HSMP_CLIENT_SET_OVERCLOCK_FREQ_PER_CORE,
	HSMP_CLIENT_SET_OVERCLOCK_VID,
	HSMP_CLIENT_SPARE_0X13,
	HSMP_CLIENT_GET_CORE_PERF_ORDER,
	HSMP_CLIENT_SET_SUSTAINED_POWER_LIMIT,
	HSMP_CLIENT_SET_SLOW_PPT_LIMIT,
	HSMP_CLIENT_SET_VRM_GFX_MAX_CURRENT_LIMIT,
	HSMP_CLIENT_SET_VRM_SOC_CURRENT_LIMIT,
	HSMP_CLIENT_SET_FAST_SPM_LIMIT,
	HSMP_CLIENT_SET_SLOW_SPM_LIMIT,
	HSMP_CLIENT_GET_CORE_PSM_MARGIN,
	HSMP_CLIENT_GET_GFX_PSM_MARGIN,
	HSMP_CLIENT_SPARE_0X1D,
	HSMP_CLIENT_SPARE_0X1E,
	HSMP_CLIENT_SPARE_0X1F,
	HSMP_CLIENT_SPARE_0X20,
	HSMP_CLIENT_SET_GFXCLK_OVERDRIVE_BY_FREQ_VID,
	HSMP_CLIENT_DISABLE_GFXCLK_OVERDRIVE,
	HSMP_CLIENT_SET_GFX_PSM_MARGIN,
	HSMP_CLIENT_SET_CCLK_FMAX_OFFSET,
	HSMP_CLIENT_SET_CORE_POWER_LIMIT_OFFSET,
	HSMP_CLIENT_ADD_EXTRA_PSM_GUARDBAND,
	HSMP_CLIENT_ADD_EXTRA_PSM_GUARDBAND_GFX,
	HSMP_CLIENT_SET_GFXCLK_FMAX,
	HSMP_CLIENT_MSG_ID_MAX,
};

#define HSMP_TELEMETRY_RM_MAX_CCX		4
#define HSMP_TELEMETRY_RM_CORES_PER_CCX	12
#define HSMP_TELEMETRY_RM_FREQ_TABLE_SIZE	8
#define HSMP_TELEMETRY_RM_NPU_BUSY_DOMAINS	3
#define HSMP_TELEMETRY_RM_OC_DOMAINS		5
#define HSMP_TELEMETRY_RM_OC_SUBDOMAINS	5
#define HSMP_TELEMETRY_RM_OC_GUARDBANDS	3

/* Firmware packs this table to a 4-byte boundary */
#pragma pack(push, 4)

/*
 * struct hsmp_telemetry_table_rm_iod - I/O die counters for RM telemetry.
 *
 * Accumulator (*_acc) fields are monotonic counters; consumers derive
 * deltas across two snapshots and normalise by accumulation_counter.
 */
struct hsmp_telemetry_table_rm_iod {
	__u32 accumulation_counter;

	/* SET VOLTAGES */
	__u64 vddcr_set_voltage;
	__u64 vddcr_soc_set_voltage;
	__u64 vddcr_npu_set_voltage;
	__u64 vddcr_lp_set_voltage;
	__u64 vddcr_gfx_set_voltage;
	__u64 vdd_misc_set_voltage;

	/* TELEMETRY VOLTAGES */
	__u64 vddcr_telemetry_voltage;
	__u64 vddcr_soc_telemetry_voltage;
	__u64 vddcr_npu_telemetry_voltage;
	__u64 vddcr_lp_telemetry_voltage;
	__u64 vddcr_gfx_telemetry_voltage;
	__u64 vdd_misc_telemetry_voltage;

	/* TELEMETRY POWERS */
	__u64 vddcr_telemetry_power;
	__u64 vddcr_soc_telemetry_power;
	__u64 vddcr_npu_telemetry_power;
	__u64 vddcr_lp_telemetry_power;
	__u64 vddcr_gfx_telemetry_power;
	__u64 vdd_misc_telemetry_power;

	/* THROTTLERS - FAST PPT */
	__u32 fppt_fused_limit;
	__u32 fppt_max_irm_limit;
	__u32 fppt_max_pbo_limit;
	__u32 fppt_limit;
	__u64 fppt_value_acc;
	__u32 fppt_residency_acc;

	/* THROTTLERS - SLOW PPT */
	__u32 sppt_fused_limit;
	__u32 sppt_max_irm_limit;
	__u32 sppt_max_pbo_limit;
	__u32 sppt_limit;
	__u64 sppt_value_acc;
	__u32 sppt_residency_acc;

	/* THROTTLERS - SPL */
	__u32 spl_fused_limit;
	__u32 spl_max_irm_limit;
	__u32 spl_max_pbo_limit;
	__u32 spl_limit;
	__u64 spl_value_acc;
	__u32 spl_residency_acc;

	/* THROTTLERS - TDC VDDCR */
	__u32 tdc_vddcr_fused_limit;
	__u32 tdc_vddcr_max_irm_limit;
	__u32 tdc_vddcr_max_pbo_limit;
	__u32 tdc_vddcr_limit;
	__u64 tdc_vddcr_value_acc;
	__u32 tdc_vddcr_residency_acc;

	/* THROTTLERS - TDC VDDCR SOC */
	__u32 tdc_vddcr_soc_fused_limit;
	__u32 tdc_vddcr_soc_max_irm_limit;
	__u32 tdc_vddcr_soc_max_pbo_limit;
	__u32 tdc_vddcr_soc_limit;
	__u64 tdc_vddcr_soc_value_acc;
	__u32 tdc_vddcr_soc_residency_acc;

	/* THROTTLERS - TDC VDDCR NPU */
	__u32 tdc_vddcr_npu_fused_limit;
	__u32 tdc_vddcr_npu_max_irm_limit;
	__u32 tdc_vddcr_npu_max_pbo_limit;
	__u32 tdc_vddcr_npu_limit;
	__u64 tdc_vddcr_npu_value_acc;
	__u32 tdc_vddcr_npu_residency_acc;

	/* THROTTLERS - TDC VDDCR LP */
	__u32 tdc_vddcr_lp_fused_limit;
	__u32 tdc_vddcr_lp_max_irm_limit;
	__u32 tdc_vddcr_lp_max_pbo_limit;
	__u32 tdc_vddcr_lp_limit;
	__u64 tdc_vddcr_lp_value_acc;
	__u32 tdc_vddcr_lp_residency_acc;

	/* THROTTLERS - TDC VDDCR GFX */
	__u32 tdc_vddcr_gfx_fused_limit;
	__u32 tdc_vddcr_gfx_max_irm_limit;
	__u32 tdc_vddcr_gfx_max_pbo_limit;
	__u32 tdc_vddcr_gfx_limit;
	__u64 tdc_vddcr_gfx_value_acc;
	__u32 tdc_vddcr_gfx_residency_acc;

	/* THROTTLERS - EDC VDDCR */
	__u32 edc_vddcr_fused_limit;
	__u32 edc_vddcr_max_irm_limit;
	__u32 edc_vddcr_max_pbo_limit;
	__u32 edc_vddcr_limit;

	/* THROTTLERS - THERMAL */
	__u32 thm_fused_limit;
	__u32 thm_limit;
	__u64 thm_value_acc;
	__u32 thm_residency_acc;
	__u32 prochot_residency_acc;
	__u64 gfx_temp_acc;
	__u64 soc_temp_acc;
	__u32 p3t_fused_limit;
	__u64 p3t_value_acc;

	/* POWER */
	__u64 system_power_acc;
	__u64 apu_power_acc;
	__u64 dgpu_power_acc;
	__u64 npu_power_acc;

	/* FREQUENCIES */
	__u64 fclk_freq_eff_acc;
	__u64 memclk_freq_eff_acc;
	__u64 lclk_freq_eff_acc;
	__u64 gfxclk_freq_eff_acc;
	__u64 socclk_freq_eff_acc;
	__u64 vclk_freq_eff_acc;
	__u64 vpeclk_freq_eff_acc;
	__u64 aieclk_freq_eff_acc;
	__u64 npuhclk_freq_eff_acc;

	/* BANDWIDTH */
	__u64 dram_read_bandwidth;
	__u64 dram_write_bandwidth;

	/* ACTIVITY MONITORS */
	__u64 gfx_busy_acc;
	__u64 vcn_busy_acc;
	__u64 npu_busy_acc[HSMP_TELEMETRY_RM_NPU_BUSY_DOMAINS];

	/* STT (Skin Temperature Tracking) */
	__u32 stt_min_limit;
	__u64 stt_apu_hot_spot_temp_acc;
	__u64 stt_hs2_hot_spot_temp_acc;
	__u32 stt_apu_temp_limit;
	__u64 stt_apu_skin_temp_acc;

	/* RESIDENCIES - per-CCX CPU off */
	__u64 cpu_off_residency[HSMP_TELEMETRY_RM_MAX_CCX];

	/* DF P-STATES */
	__u32 fclk_freq_table[HSMP_TELEMETRY_RM_FREQ_TABLE_SIZE];
	__u32 uclk_freq_table[HSMP_TELEMETRY_RM_FREQ_TABLE_SIZE];
	__u32 ddr_rate_table[HSMP_TELEMETRY_RM_FREQ_TABLE_SIZE];
	__u8  df_pstate_source[HSMP_TELEMETRY_RM_FREQ_TABLE_SIZE];

	/* SYSTEM */
	__u8  gfx_disabled;
	__u8  spare2[3];
	__u32 gfxclk_fmax;
	__u8  cclk_core_fuse_enable[HSMP_TELEMETRY_RM_MAX_CCX][HSMP_TELEMETRY_RM_CORES_PER_CCX];
	__u8  cclk_core_enabled[HSMP_TELEMETRY_RM_MAX_CCX][HSMP_TELEMETRY_RM_CORES_PER_CCX];
	__u32 cclk_fmax[HSMP_TELEMETRY_RM_MAX_CCX][HSMP_TELEMETRY_RM_CORES_PER_CCX];

	/* OVERCLOCK CAPABLE */
	__u8  cpu_precise_and_direct_oc_capable;
	__u8  gfx_precise_and_direct_oc_capable;
	__u8  pbo_basic_oc_capable;
	__u8  pbo_advanced_oc_capable;
	__u8  pbo_nitro_oc_capable;
	__u8  memory_and_fabric_oc_capable;
	__u8  misc_oc_capable;
	__u8  extreme_cold_oc_capable;
	__u8  down_config_control_capable;
	__u8  spare0[3];

	/* OVERCLOCK STATUS */
	__u32 fit_limit_scalar;
	__u8  ln2_enabled;
	__u8  cpu_precise_and_direct_oc_enabled;
	__u8  gfx_precise_and_direct_oc_enabled;
	__u8  spare1[2];
	__s8  psm_guardband[HSMP_TELEMETRY_RM_OC_DOMAINS]
			   [HSMP_TELEMETRY_RM_OC_SUBDOMAINS]
			   [HSMP_TELEMETRY_RM_OC_GUARDBANDS];
	__s32 core_power_limit_offset;
	__u32 max_freq_offset[HSMP_TELEMETRY_RM_OC_DOMAINS];

	__u64 npu_temp_acc;
	__u64 df_pstate_residency_acc[HSMP_TELEMETRY_RM_FREQ_TABLE_SIZE];
	__u32 cclk_fboost;

	/* PMF */
	__u32 pmf_fast_apu_ppt_limit;
	__u64 pmf_fast_apu_ppt_value_acc;
	__u32 pmf_fast_apu_ppt_residency_acc;

	__u32 pmf_slow_apu_ppt_limit;
	__u64 pmf_slow_apu_ppt_value_acc;
	__u32 pmf_slow_apu_ppt_residency_acc;

	__u32 pmf_fast_spm_limit;
	__u64 pmf_fast_spm_value_acc;
	__u32 pmf_fast_spm_residency_acc;

	__u32 pmf_slow_spm_limit;
	__u64 pmf_slow_spm_value_acc;
	__u32 pmf_slow_spm_residency_acc;

	__u32 spare3[5];
};

/*
 * struct hsmp_telemetry_table_rm_ccx - Per-CCX core counters for RM telemetry.
 *
 * Arrays are sized for the maximum cores per CCX; use the
 * cclk_core_enabled[] mask in struct hsmp_telemetry_table_rm_iod to tell
 * which entries are valid.
 */
struct hsmp_telemetry_table_rm_ccx {
	__u64 core_c0[HSMP_TELEMETRY_RM_CORES_PER_CCX];
	__u64 core_cc6[HSMP_TELEMETRY_RM_CORES_PER_CCX];
	__u64 core_freq[HSMP_TELEMETRY_RM_CORES_PER_CCX];
	__u64 core_freq_eff[HSMP_TELEMETRY_RM_CORES_PER_CCX];
	__u64 core_temp[HSMP_TELEMETRY_RM_CORES_PER_CCX];
	__u64 core_power[HSMP_TELEMETRY_RM_CORES_PER_CCX];
};

/**
 * struct hsmp_telemetry_table_rm - Top-level RM telemetry table.
 * @iod: I/O die counters and per-rail/per-throttler/SYSTEM telemetry.
 * @ccx: Per-CCX core counters, one entry per CCX.
 *
 * Layout of the table returned by HSMP_CLIENT_GET_METRICS_TABLE on the
 * Family 1Ah client (Models 80h-8Fh and E0h-E3h), reported as table
 * version 5 by HSMP_CLIENT_GET_METRICS_TABLE_VER. Units are
 * firmware-defined; see the AMD HSMP/Ryzen Master SMC spec. Spare
 * fields are firmware padding and must be preserved.
 */
struct hsmp_telemetry_table_rm {
	struct hsmp_telemetry_table_rm_iod iod;
	struct hsmp_telemetry_table_rm_ccx ccx[HSMP_TELEMETRY_RM_MAX_CCX];
};

#pragma pack(pop)

#endif /*_ASM_X86_AMD_HSMP_H_*/
