/*
 * Abilis Systems Single DVB-T Receiver
 * Copyright (C) 2008 Pierrick Hascoet <pierrick.hascoet@abilis.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef _AS10X_CMD_H_
#define _AS10X_CMD_H_

#ifdef __KERNEL__
#include <linux/kernel.h>
#endif

#include "as10x_types.h"

/*********************************/
/*       MACRO DEFINITIONS       */
/*********************************/
#define AS10X_CMD_ERROR		-1

#define SERVICE_PROG_ID		0x0002
#define SERVICE_PROG_VERSION	0x0001

#define HIER_NONE		0x00
#define HIER_LOW_PRIORITY	0x01

#define HEADER_SIZE (sizeof(struct as10x_cmd_header_t))

/* context request types */
#define GET_CONTEXT_DATA	1
#define SET_CONTEXT_DATA	2

/* ODSP suspend modes */
#define CFG_MODE_ODSP_RESUME	0
#define CFG_MODE_ODSP_SUSPEND	1

/* Dump memory size */
#define DUMP_BLOCK_SIZE_MAX	0x20

/*********************************/
/*     TYPE DEFINITION           */
/*********************************/
enum control_proc {
	CONTROL_PROC_TURNON			= 0x0001,
	CONTROL_PROC_TURNON_RSP			= 0x0100,
	CONTROL_PROC_SET_REGISTER		= 0x0002,
	CONTROL_PROC_SET_REGISTER_RSP		= 0x0200,
	CONTROL_PROC_GET_REGISTER		= 0x0003,
	CONTROL_PROC_GET_REGISTER_RSP		= 0x0300,
	CONTROL_PROC_SETTUNE			= 0x000A,
	CONTROL_PROC_SETTUNE_RSP		= 0x0A00,
	CONTROL_PROC_GETTUNESTAT		= 0x000B,
	CONTROL_PROC_GETTUNESTAT_RSP		= 0x0B00,
	CONTROL_PROC_GETTPS			= 0x000D,
	CONTROL_PROC_GETTPS_RSP			= 0x0D00,
	CONTROL_PROC_SETFILTER			= 0x000E,
	CONTROL_PROC_SETFILTER_RSP		= 0x0E00,
	CONTROL_PROC_REMOVEFILTER		= 0x000F,
	CONTROL_PROC_REMOVEFILTER_RSP		= 0x0F00,
	CONTROL_PROC_GET_IMPULSE_RESP		= 0x0012,
	CONTROL_PROC_GET_IMPULSE_RESP_RSP	= 0x1200,
	CONTROL_PROC_START_STREAMING		= 0x0013,
	CONTROL_PROC_START_STREAMING_RSP	= 0x1300,
	CONTROL_PROC_STOP_STREAMING		= 0x0014,
	CONTROL_PROC_STOP_STREAMING_RSP		= 0x1400,
	CONTROL_PROC_GET_DEMOD_STATS		= 0x0015,
	CONTROL_PROC_GET_DEMOD_STATS_RSP	= 0x1500,
	CONTROL_PROC_ELNA_CHANGE_MODE		= 0x0016,
	CONTROL_PROC_ELNA_CHANGE_MODE_RSP	= 0x1600,
	CONTROL_PROC_ODSP_CHANGE_MODE		= 0x0017,
	CONTROL_PROC_ODSP_CHANGE_MODE_RSP	= 0x1700,
	CONTROL_PROC_AGC_CHANGE_MODE		= 0x0018,
	CONTROL_PROC_AGC_CHANGE_MODE_RSP	= 0x1800,

	CONTROL_PROC_CONTEXT			= 0x00FC,
	CONTROL_PROC_CONTEXT_RSP		= 0xFC00,
	CONTROL_PROC_DUMP_MEMORY		= 0x00FD,
	CONTROL_PROC_DUMP_MEMORY_RSP		= 0xFD00,
	CONTROL_PROC_DUMPLOG_MEMORY		= 0x00FE,
	CONTROL_PROC_DUMPLOG_MEMORY_RSP		= 0xFE00,
	CONTROL_PROC_TURNOFF			= 0x00FF,
	CONTROL_PROC_TURNOFF_RSP		= 0xFF00
};

union as10x_turn_on {
	/* request */
	struct {
		/* request identifier */
		uint16_t proc_id;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* error */
		uint8_t error;
	} __packed rsp;
} __packed;

union as10x_turn_off {
	/* request */
	struct {
		/* request identifier */
		uint16_t proc_id;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* error */
		uint8_t err;
	} __packed rsp;
} __packed;

union as10x_set_tune {
	/* request */
	struct {
		/* request identifier */
		uint16_t proc_id;
		/* tune params */
		struct as10x_tune_args args;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* response error */
		uint8_t error;
	} __packed rsp;
} __packed;

union as10x_get_tune_status {
	/* request */
	struct {
		/* request identifier */
		uint16_t proc_id;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* response error */
		uint8_t error;
		/* tune status */
		struct as10x_tune_status sts;
	} __packed rsp;
} __packed;

union as10x_get_tps {
	/* request */
	struct {
		/* request identifier */
		uint16_t proc_id;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* response error */
		uint8_t error;
		/* tps details */
		struct as10x_tps tps;
	} __packed rsp;
} __packed;

union as10x_common {
	/* request */
	struct {
		/* request identifier */
		uint16_t  proc_id;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* response error */
		uint8_t error;
	} __packed rsp;
} __packed;

union as10x_add_pid_filter {
	/* request */
	struct {
		/* request identifier */
		uint16_t  proc_id;
		/* PID to filter */
		uint16_t  pid;
		/* stream type (MPE, PSI/SI or PES )*/
		uint8_t stream_type;
		/* PID index in filter table */
		uint8_t idx;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* response error */
		uint8_t error;
		/* Filter id */
		uint8_t filter_id;
	} __packed rsp;
} __packed;

union as10x_del_pid_filter {
	/* request */
	struct {
		/* request identifier */
		uint16_t  proc_id;
		/* PID to remove */
		uint16_t  pid;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* response error */
		uint8_t error;
	} __packed rsp;
} __packed;

union as10x_start_streaming {
	/* request */
	struct {
		/* request identifier */
		uint16_t proc_id;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* error */
		uint8_t error;
	} __packed rsp;
} __packed;

union as10x_stop_streaming {
	/* request */
	struct {
		/* request identifier */
		uint16_t proc_id;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* error */
		uint8_t error;
	} __packed rsp;
} __packed;

union as10x_get_demod_stats {
	/* request */
	struct {
		/* request identifier */
		uint16_t proc_id;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* error */
		uint8_t error;
		/* demod stats */
		struct as10x_demod_stats stats;
	} __packed rsp;
} __packed;

union as10x_get_impulse_resp {
	/* request */
	struct {
		/* request identifier */
		uint16_t proc_id;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* error */
		uint8_t error;
		/* impulse response ready */
		uint8_t is_ready;
	} __packed rsp;
} __packed;

union as10x_fw_context {
	/* request */
	struct {
		/* request identifier */
		uint16_t proc_id;
		/* value to write (for set context)*/
		struct as10x_register_value reg_val;
		/* context tag */
		uint16_t tag;
		/* context request type */
		uint16_t type;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* value read (for get context) */
		struct as10x_register_value reg_val;
		/* context request type */
		uint16_t type;
		/* error */
		uint8_t error;
	} __packed rsp;
} __packed;

union as10x_set_register {
	/* request */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* register description */
		struct as10x_register_addr reg_addr;
		/* register content */
		struct as10x_register_value reg_val;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* error */
		uint8_t error;
	} __packed rsp;
} __packed;

union as10x_get_register {
	/* request */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* register description */
		struct as10x_register_addr reg_addr;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* error */
		uint8_t error;
		/* register content */
		struct as10x_register_value reg_val;
	} __packed rsp;
} __packed;

union as10x_cfg_change_mode {
	/* request */
	struct {
		/* request identifier */
		uint16_t proc_id;
		/* mode */
		uint8_t mode;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* error */
		uint8_t error;
	} __packed rsp;
} __packed;

struct as10x_cmd_header_t {
	uint16_t req_id;
	uint16_t prog;
	uint16_t version;
	uint16_t data_len;
} __packed;

#define DUMP_BLOCK_SIZE 16

union as10x_dump_memory {
	/* request */
	struct {
		/* request identifier */
		uint16_t proc_id;
		/* dump memory type request */
		uint8_t dump_req;
		/* register description */
		struct as10x_register_addr reg_addr;
		/* nb blocks to read */
		uint16_t num_blocks;
	} __packed req;
	/* response */
	struct {
		/* response identifier */
		uint16_t proc_id;
		/* error */
		uint8_t error;
		/* dump response */
		uint8_t dump_rsp;
		/* data */
		union {
			uint8_t  data8[DUMP_BLOCK_SIZE];
			uint16_t data16[DUMP_BLOCK_SIZE / sizeof(uint16_t)];
			uint32_t data32[DUMP_BLOCK_SIZE / sizeof(uint32_t)];
		} __packed u;
	} __packed rsp;
} __packed;

union as10x_dumplog_memory {
	struct {
		/* request identifier */
		uint16_t proc_id;
		/* dump memory type request */
		uint8_t dump_req;
	} __packed req;
	struct {
		/* request identifier */
		uint16_t proc_id;
		/* error */
		uint8_t error;
		/* dump response */
		uint8_t dump_rsp;
		/* dump data */
		uint8_t data[DUMP_BLOCK_SIZE];
	} __packed rsp;
} __packed;

union as10x_raw_data {
	/* request */
	struct {
		uint16_t proc_id;
		uint8_t data[64 - sizeof(struct as10x_cmd_header_t)
			     - 2 /* proc_id */];
	} __packed req;
	/* response */
	struct {
		uint16_t proc_id;
		uint8_t error;
		uint8_t data[64 - sizeof(struct as10x_cmd_header_t)
			     - 2 /* proc_id */ - 1 /* rc */];
	} __packed rsp;
} __packed;

struct as10x_cmd_t {
	struct as10x_cmd_header_t header;
	union {
		union as10x_turn_on		turn_on;
		union as10x_turn_off		turn_off;
		union as10x_set_tune		set_tune;
		union as10x_get_tune_status	get_tune_status;
		union as10x_get_tps		get_tps;
		union as10x_common		common;
		union as10x_add_pid_filter	add_pid_filter;
		union as10x_del_pid_filter	del_pid_filter;
		union as10x_start_streaming	start_streaming;
		union as10x_stop_streaming	stop_streaming;
		union as10x_get_demod_stats	get_demod_stats;
		union as10x_get_impulse_resp	get_impulse_rsp;
		union as10x_fw_context		context;
		union as10x_set_register	set_register;
		union as10x_get_register	get_register;
		union as10x_cfg_change_mode	cfg_change_mode;
		union as10x_dump_memory		dump_memory;
		union as10x_dumplog_memory	dumplog_memory;
		union as10x_raw_data		raw_data;
	} __packed body;
} __packed;

struct as10x_token_cmd_t {
	/* token cmd */
	struct as10x_cmd_t c;
	/* token response */
	struct as10x_cmd_t r;
} __packed;


/**************************/
/* FUNCTION DECLARATION   */
/**************************/

void as10x_cmd_build(struct as10x_cmd_t *pcmd, uint16_t proc_id,
		      uint16_t cmd_len);
int as10x_rsp_parse(struct as10x_cmd_t *r, uint16_t proc_id);

/* as10x cmd */
int as10x_cmd_turn_on(struct as10x_bus_adapter_t *adap);
int as10x_cmd_turn_off(struct as10x_bus_adapter_t *adap);

int as10x_cmd_set_tune(struct as10x_bus_adapter_t *adap,
		       struct as10x_tune_args *ptune);

int as10x_cmd_get_tune_status(struct as10x_bus_adapter_t *adap,
			      struct as10x_tune_status *pstatus);

int as10x_cmd_get_tps(struct as10x_bus_adapter_t *adap,
		      struct as10x_tps *ptps);

int as10x_cmd_get_demod_stats(struct as10x_bus_adapter_t  *adap,
			      struct as10x_demod_stats *pdemod_stats);

int as10x_cmd_get_impulse_resp(struct as10x_bus_adapter_t *adap,
			       uint8_t *is_ready);

/* as10x cmd stream */
int as10x_cmd_add_PID_filter(struct as10x_bus_adapter_t *adap,
			     struct as10x_ts_filter *filter);
int as10x_cmd_del_PID_filter(struct as10x_bus_adapter_t *adap,
			     uint16_t pid_value);

int as10x_cmd_start_streaming(struct as10x_bus_adapter_t *adap);
int as10x_cmd_stop_streaming(struct as10x_bus_adapter_t *adap);

/* as10x cmd cfg */
int as10x_cmd_set_context(struct as10x_bus_adapter_t *adap,
			  uint16_t tag,
			  uint32_t value);
int as10x_cmd_get_context(struct as10x_bus_adapter_t *adap,
			  uint16_t tag,
			  uint32_t *pvalue);

int as10x_cmd_eLNA_change_mode(struct as10x_bus_adapter_t *adap, uint8_t mode);
int as10x_context_rsp_parse(struct as10x_cmd_t *prsp, uint16_t proc_id);
#endif
