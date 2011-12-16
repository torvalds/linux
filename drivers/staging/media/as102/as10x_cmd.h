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
#define AS10X_CMD_ERROR -1

#define SERVICE_PROG_ID        0x0002
#define SERVICE_PROG_VERSION   0x0001

#define HIER_NONE              0x00
#define HIER_LOW_PRIORITY      0x01

#define HEADER_SIZE (sizeof(struct as10x_cmd_header_t))

/* context request types */
#define GET_CONTEXT_DATA        1
#define SET_CONTEXT_DATA        2

/* ODSP suspend modes */
#define CFG_MODE_ODSP_RESUME  0
#define CFG_MODE_ODSP_SUSPEND 1

/* Dump memory size */
#define DUMP_BLOCK_SIZE_MAX   0x20

/*********************************/
/*     TYPE DEFINITION           */
/*********************************/
typedef enum {
   CONTROL_PROC_TURNON               = 0x0001,
   CONTROL_PROC_TURNON_RSP           = 0x0100,
   CONTROL_PROC_SET_REGISTER         = 0x0002,
   CONTROL_PROC_SET_REGISTER_RSP     = 0x0200,
   CONTROL_PROC_GET_REGISTER         = 0x0003,
   CONTROL_PROC_GET_REGISTER_RSP     = 0x0300,
   CONTROL_PROC_SETTUNE              = 0x000A,
   CONTROL_PROC_SETTUNE_RSP          = 0x0A00,
   CONTROL_PROC_GETTUNESTAT          = 0x000B,
   CONTROL_PROC_GETTUNESTAT_RSP      = 0x0B00,
   CONTROL_PROC_GETTPS               = 0x000D,
   CONTROL_PROC_GETTPS_RSP           = 0x0D00,
   CONTROL_PROC_SETFILTER            = 0x000E,
   CONTROL_PROC_SETFILTER_RSP        = 0x0E00,
   CONTROL_PROC_REMOVEFILTER         = 0x000F,
   CONTROL_PROC_REMOVEFILTER_RSP     = 0x0F00,
   CONTROL_PROC_GET_IMPULSE_RESP     = 0x0012,
   CONTROL_PROC_GET_IMPULSE_RESP_RSP = 0x1200,
   CONTROL_PROC_START_STREAMING      = 0x0013,
   CONTROL_PROC_START_STREAMING_RSP  = 0x1300,
   CONTROL_PROC_STOP_STREAMING       = 0x0014,
   CONTROL_PROC_STOP_STREAMING_RSP   = 0x1400,
   CONTROL_PROC_GET_DEMOD_STATS      = 0x0015,
   CONTROL_PROC_GET_DEMOD_STATS_RSP  = 0x1500,
   CONTROL_PROC_ELNA_CHANGE_MODE     = 0x0016,
   CONTROL_PROC_ELNA_CHANGE_MODE_RSP = 0x1600,
   CONTROL_PROC_ODSP_CHANGE_MODE     = 0x0017,
   CONTROL_PROC_ODSP_CHANGE_MODE_RSP = 0x1700,
   CONTROL_PROC_AGC_CHANGE_MODE      = 0x0018,
   CONTROL_PROC_AGC_CHANGE_MODE_RSP  = 0x1800,

   CONTROL_PROC_CONTEXT              = 0x00FC,
   CONTROL_PROC_CONTEXT_RSP          = 0xFC00,
   CONTROL_PROC_DUMP_MEMORY          = 0x00FD,
   CONTROL_PROC_DUMP_MEMORY_RSP      = 0xFD00,
   CONTROL_PROC_DUMPLOG_MEMORY       = 0x00FE,
   CONTROL_PROC_DUMPLOG_MEMORY_RSP   = 0xFE00,
   CONTROL_PROC_TURNOFF              = 0x00FF,
   CONTROL_PROC_TURNOFF_RSP          = 0xFF00
} control_proc;


#pragma pack(1)
typedef union {
   /* request */
   struct {
      /* request identifier */
      uint16_t proc_id;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* error */
      uint8_t error;
   } rsp;
} TURN_ON;

typedef union {
   /* request */
   struct {
      /* request identifier */
      uint16_t proc_id;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* error */
      uint8_t err;
   } rsp;
} TURN_OFF;

typedef union {
   /* request */
   struct {
      /* request identifier */
      uint16_t proc_id;
      /* tune params */
      struct as10x_tune_args args;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* response error */
      uint8_t error;
   } rsp;
} SET_TUNE;

typedef union {
   /* request */
   struct {
      /* request identifier */
      uint16_t proc_id;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* response error */
      uint8_t error;
      /* tune status */
      struct as10x_tune_status sts;
   } rsp;
} GET_TUNE_STATUS;

typedef union {
   /* request */
   struct {
      /* request identifier */
      uint16_t proc_id;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* response error */
      uint8_t error;
      /* tps details */
      struct as10x_tps tps;
   } rsp;
} GET_TPS;

typedef union {
   /* request */
   struct {
      /* request identifier */
      uint16_t  proc_id;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* response error */
      uint8_t error;
   } rsp;
} COMMON;

typedef union {
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
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* response error */
      uint8_t error;
      /* Filter id */
      uint8_t filter_id;
   } rsp;
} ADD_PID_FILTER;

typedef union {
   /* request */
   struct {
      /* request identifier */
      uint16_t  proc_id;
      /* PID to remove */
      uint16_t  pid;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* response error */
      uint8_t error;
   } rsp;
} DEL_PID_FILTER;

typedef union {
   /* request */
   struct {
      /* request identifier */
      uint16_t proc_id;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* error */
      uint8_t error;
   } rsp;
} START_STREAMING;

typedef union {
   /* request */
   struct {
      /* request identifier */
      uint16_t proc_id;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* error */
      uint8_t error;
   } rsp;
} STOP_STREAMING;

typedef union {
   /* request */
   struct {
      /* request identifier */
      uint16_t proc_id;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* error */
      uint8_t error;
      /* demod stats */
      struct as10x_demod_stats stats;
   } rsp;
} GET_DEMOD_STATS;

typedef union {
   /* request */
   struct {
      /* request identifier */
      uint16_t proc_id;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* error */
      uint8_t error;
      /* impulse response ready */
      uint8_t is_ready;
   } rsp;
} GET_IMPULSE_RESP;

typedef union {
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
   } req;
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
   } rsp;
} FW_CONTEXT;

typedef union {
   /* request */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* register description */
      struct as10x_register_addr reg_addr;
      /* register content */
      struct as10x_register_value reg_val;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* error */
      uint8_t error;
   } rsp;
} SET_REGISTER;

typedef union {
   /* request */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* register description */
      struct as10x_register_addr reg_addr;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* error */
      uint8_t error;
      /* register content */
      struct as10x_register_value reg_val;
   } rsp;
} GET_REGISTER;

typedef union {
   /* request */
   struct {
      /* request identifier */
      uint16_t proc_id;
      /* mode */
      uint8_t mode;
   } req;
   /* response */
   struct {
      /* response identifier */
      uint16_t proc_id;
      /* error */
      uint8_t error;
   } rsp;
} CFG_CHANGE_MODE;

struct as10x_cmd_header_t {
   uint16_t req_id;
   uint16_t prog;
   uint16_t version;
   uint16_t data_len;
};

#define DUMP_BLOCK_SIZE 16
typedef union {
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
   } req;
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
      } u;
   } rsp;
} DUMP_MEMORY;

typedef union {
   struct {
      /* request identifier */
      uint16_t proc_id;
      /* dump memory type request */
      uint8_t dump_req;
   } req;
   struct {
      /* request identifier */
      uint16_t proc_id;
      /* error */
      uint8_t error;
      /* dump response */
      uint8_t dump_rsp;
      /* dump data */
      uint8_t data[DUMP_BLOCK_SIZE];
   } rsp;
} DUMPLOG_MEMORY;

typedef union {
   /* request */
   struct {
      uint16_t proc_id;
      uint8_t data[64 - sizeof(struct as10x_cmd_header_t) -2 /* proc_id */];
   } req;
   /* response */
   struct {
      uint16_t proc_id;
      uint8_t error;
      uint8_t data[64 - sizeof(struct as10x_cmd_header_t) /* header */
		      - 2 /* proc_id */ - 1 /* rc */];
   } rsp;
} RAW_DATA;

struct as10x_cmd_t {
   /* header */
   struct as10x_cmd_header_t header;
   /* body */
   union {
      TURN_ON           turn_on;
      TURN_OFF          turn_off;
      SET_TUNE          set_tune;
      GET_TUNE_STATUS   get_tune_status;
      GET_TPS           get_tps;
      COMMON            common;
      ADD_PID_FILTER    add_pid_filter;
      DEL_PID_FILTER    del_pid_filter;
      START_STREAMING   start_streaming;
      STOP_STREAMING    stop_streaming;
      GET_DEMOD_STATS   get_demod_stats;
      GET_IMPULSE_RESP  get_impulse_rsp;
      FW_CONTEXT        context;
      SET_REGISTER      set_register;
      GET_REGISTER      get_register;
      CFG_CHANGE_MODE   cfg_change_mode;
      DUMP_MEMORY       dump_memory;
      DUMPLOG_MEMORY    dumplog_memory;
      RAW_DATA          raw_data;
   } body;
};

struct as10x_token_cmd_t {
   /* token cmd */
   struct as10x_cmd_t c;
   /* token response */
   struct as10x_cmd_t r;
};
#pragma pack()


/**************************/
/* FUNCTION DECLARATION   */
/**************************/

void as10x_cmd_build(struct as10x_cmd_t *pcmd, uint16_t proc_id,
		      uint16_t cmd_len);
int as10x_rsp_parse(struct as10x_cmd_t *r, uint16_t proc_id);

#ifdef __cplusplus
extern "C" {
#endif

/* as10x cmd */
int as10x_cmd_turn_on(as10x_handle_t *phandle);
int as10x_cmd_turn_off(as10x_handle_t *phandle);

int as10x_cmd_set_tune(as10x_handle_t *phandle,
		       struct as10x_tune_args *ptune);

int as10x_cmd_get_tune_status(as10x_handle_t *phandle,
			      struct as10x_tune_status *pstatus);

int as10x_cmd_get_tps(as10x_handle_t *phandle,
		      struct as10x_tps *ptps);

int as10x_cmd_get_demod_stats(as10x_handle_t  *phandle,
			      struct as10x_demod_stats *pdemod_stats);

int as10x_cmd_get_impulse_resp(as10x_handle_t *phandle,
			       uint8_t *is_ready);

/* as10x cmd stream */
int as10x_cmd_add_PID_filter(as10x_handle_t *phandle,
			     struct as10x_ts_filter *filter);
int as10x_cmd_del_PID_filter(as10x_handle_t *phandle,
			     uint16_t pid_value);

int as10x_cmd_start_streaming(as10x_handle_t *phandle);
int as10x_cmd_stop_streaming(as10x_handle_t *phandle);

/* as10x cmd cfg */
int as10x_cmd_set_context(as10x_handle_t *phandle,
			  uint16_t tag,
			  uint32_t value);
int as10x_cmd_get_context(as10x_handle_t *phandle,
			  uint16_t tag,
			  uint32_t *pvalue);

int as10x_cmd_eLNA_change_mode(as10x_handle_t *phandle, uint8_t mode);
int as10x_context_rsp_parse(struct as10x_cmd_t *prsp, uint16_t proc_id);
#ifdef __cplusplus
}
#endif
#endif
/* EOF - vim: set textwidth=80 ts=3 sw=3 sts=3 et: */
