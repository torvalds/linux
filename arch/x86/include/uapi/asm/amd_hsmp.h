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
					/* 13h Reserved */
	HSMP_GET_DDR_BANDWIDTH = 0x14,	/* 14h Get theoretical maximum and current DDR Bandwidth */
	HSMP_GET_TEMP_MONITOR,		/* 15h Get per-DIMM temperature and refresh rates */
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
static const struct hsmp_msg_desc hsmp_msg_desc_table[] = {
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
	 * HSMP_SET_XGMI_LINK_WIDTH, num_args = 1, response_sz = 0
	 * input: args[0] = min link width[15:8] + max link width[7:0]
	 */
	{1, 0, HSMP_SET},

	/*
	 * HSMP_SET_DF_PSTATE, num_args = 1, response_sz = 0
	 * input: args[0] = df pstate[7:0]
	 */
	{1, 0, HSMP_SET},

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

	/* RESERVED message */
	{0, 0, HSMP_RSVD},

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
};

/* Reset to default packing */
#pragma pack()

/* Define unique ioctl command for hsmp msgs using generic _IOWR */
#define HSMP_BASE_IOCTL_NR	0xF8
#define HSMP_IOCTL_CMD		_IOWR(HSMP_BASE_IOCTL_NR, 0, struct hsmp_message)

#endif /*_ASM_X86_AMD_HSMP_H_*/
