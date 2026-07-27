/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _UAPI_ASM_X86_AMD_HSMP_H_
#define _UAPI_ASM_X86_AMD_HSMP_H_

#include <linux/types.h>

#pragma pack(4)

#define HSMP_MAX_MSG_LEN 8

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

#endif /*_ASM_X86_AMD_HSMP_H_*/
