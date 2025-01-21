/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __CSS_TRACE_H_
#define __CSS_TRACE_H_

#include <type_support.h>
#include "sh_css_internal.h"	/* for SH_CSS_MAX_SP_THREADS */

/*
	structs and constants for tracing
*/

/* one tracer item: major, minor and counter. The counter value can be used for GP data */
struct trace_item_t {
	u8   major;
	u8   minor;
	u16  counter;
};

#define MAX_SCRATCH_DATA	4
#define MAX_CMD_DATA		2

/* trace header: holds the version and the topology of the tracer. */
struct trace_header_t {
	/* 1st dword: descriptor */
	u8   version;
	u8   max_threads;
	u16  max_tracer_points;
	/* 2nd field: command + data */
	/* 2nd dword */
	u32  command;
	/* 3rd & 4th dword */
	u32  data[MAX_CMD_DATA];
	/* 3rd field: debug pointer */
	/* 5th & 6th dword: debug pointer mechanism */
	u32  debug_ptr_signature;
	u32  debug_ptr_value;
	/* Rest of the header: status & scratch data */
	u8   thr_status_byte[SH_CSS_MAX_SP_THREADS];
	u16  thr_status_word[SH_CSS_MAX_SP_THREADS];
	u32  thr_status_dword[SH_CSS_MAX_SP_THREADS];
	u32  scratch_debug[MAX_SCRATCH_DATA];
};

/* offsets for master_port read/write */
#define HDR_HDR_OFFSET              0	/* offset of the header */
#define HDR_COMMAND_OFFSET          offsetof(struct trace_header_t, command)
#define HDR_DATA_OFFSET             offsetof(struct trace_header_t, data)
#define HDR_DEBUG_SIGNATURE_OFFSET  offsetof(struct trace_header_t, debug_ptr_signature)
#define HDR_DEBUG_POINTER_OFFSET    offsetof(struct trace_header_t, debug_ptr_value)
#define HDR_STATUS_OFFSET           offsetof(struct trace_header_t, thr_status_byte)
#define HDR_STATUS_OFFSET_BYTE      offsetof(struct trace_header_t, thr_status_byte)
#define HDR_STATUS_OFFSET_WORD      offsetof(struct trace_header_t, thr_status_word)
#define HDR_STATUS_OFFSET_DWORD     offsetof(struct trace_header_t, thr_status_dword)
#define HDR_STATUS_OFFSET_SCRATCH   offsetof(struct trace_header_t, scratch_debug)

/*
Trace version history:
 1: initial version, hdr = descr, command & ptr.
 2: added ISP + 24-bit fields.
 3: added thread ID.
 4: added status in header.
*/
#define TRACER_VER			4

#define TRACE_BUFF_ADDR       0xA000
#define TRACE_BUFF_SIZE       0x1000	/* 4K allocated */

#define TRACE_ENABLE_SP0 0
#define TRACE_ENABLE_SP1 0
#define TRACE_ENABLE_ISP 0

enum TRACE_CORE_ID {
	TRACE_SP0_ID,
	TRACE_SP1_ID,
	TRACE_ISP_ID
};

/* TODO: add timing format? */
enum TRACE_DUMP_FORMAT {
	TRACE_DUMP_FORMAT_POINT_NO_TID,
	TRACE_DUMP_FORMAT_VALUE24,
	TRACE_DUMP_FORMAT_VALUE24_TIMING,
	TRACE_DUMP_FORMAT_VALUE24_TIMING_DELTA,
	TRACE_DUMP_FORMAT_POINT
};

/* currently divided as follows:*/
#if (TRACE_ENABLE_SP0 + TRACE_ENABLE_SP1 + TRACE_ENABLE_ISP == 3)
/* can be divided as needed */
#define TRACE_SP0_SIZE (TRACE_BUFF_SIZE / 4)
#define TRACE_SP1_SIZE (TRACE_BUFF_SIZE / 4)
#define TRACE_ISP_SIZE (TRACE_BUFF_SIZE / 2)
#elif (TRACE_ENABLE_SP0 + TRACE_ENABLE_SP1 + TRACE_ENABLE_ISP == 2)
#if TRACE_ENABLE_SP0
#define TRACE_SP0_SIZE (TRACE_BUFF_SIZE / 2)
#else
#define TRACE_SP0_SIZE (0)
#endif
#if TRACE_ENABLE_SP1
#define TRACE_SP1_SIZE (TRACE_BUFF_SIZE / 2)
#else
#define TRACE_SP1_SIZE (0)
#endif
#if TRACE_ENABLE_ISP
#define TRACE_ISP_SIZE (TRACE_BUFF_SIZE / 2)
#else
#define TRACE_ISP_SIZE (0)
#endif
#elif (TRACE_ENABLE_SP0 + TRACE_ENABLE_SP1 + TRACE_ENABLE_ISP == 1)
#if TRACE_ENABLE_SP0
#define TRACE_SP0_SIZE (TRACE_BUFF_SIZE)
#else
#define TRACE_SP0_SIZE (0)
#endif
#if TRACE_ENABLE_SP1
#define TRACE_SP1_SIZE (TRACE_BUFF_SIZE)
#else
#define TRACE_SP1_SIZE (0)
#endif
#if TRACE_ENABLE_ISP
#define TRACE_ISP_SIZE (TRACE_BUFF_SIZE)
#else
#define TRACE_ISP_SIZE (0)
#endif
#else
#define TRACE_SP0_SIZE (0)
#define TRACE_SP1_SIZE (0)
#define TRACE_ISP_SIZE (0)
#endif

#define TRACE_SP0_ADDR (TRACE_BUFF_ADDR)
#define TRACE_SP1_ADDR (TRACE_SP0_ADDR + TRACE_SP0_SIZE)
#define TRACE_ISP_ADDR (TRACE_SP1_ADDR + TRACE_SP1_SIZE)

/* check if it's a legal division */
#if (TRACE_BUFF_SIZE < TRACE_SP0_SIZE + TRACE_SP1_SIZE + TRACE_ISP_SIZE)
#error trace sizes are not divided correctly and are above limit
#endif

#define TRACE_SP0_HEADER_ADDR (TRACE_SP0_ADDR)
#define TRACE_SP0_HEADER_SIZE (sizeof(struct trace_header_t))
#define TRACE_SP0_ITEM_SIZE   (sizeof(struct trace_item_t))
#define TRACE_SP0_DATA_ADDR   (TRACE_SP0_HEADER_ADDR + TRACE_SP0_HEADER_SIZE)
#define TRACE_SP0_DATA_SIZE   (TRACE_SP0_SIZE - TRACE_SP0_HEADER_SIZE)
#define TRACE_SP0_MAX_POINTS  (TRACE_SP0_DATA_SIZE / TRACE_SP0_ITEM_SIZE)

#define TRACE_SP1_HEADER_ADDR (TRACE_SP1_ADDR)
#define TRACE_SP1_HEADER_SIZE (sizeof(struct trace_header_t))
#define TRACE_SP1_ITEM_SIZE   (sizeof(struct trace_item_t))
#define TRACE_SP1_DATA_ADDR   (TRACE_SP1_HEADER_ADDR + TRACE_SP1_HEADER_SIZE)
#define TRACE_SP1_DATA_SIZE   (TRACE_SP1_SIZE - TRACE_SP1_HEADER_SIZE)
#define TRACE_SP1_MAX_POINTS  (TRACE_SP1_DATA_SIZE / TRACE_SP1_ITEM_SIZE)

#define TRACE_ISP_HEADER_ADDR (TRACE_ISP_ADDR)
#define TRACE_ISP_HEADER_SIZE (sizeof(struct trace_header_t))
#define TRACE_ISP_ITEM_SIZE   (sizeof(struct trace_item_t))
#define TRACE_ISP_DATA_ADDR   (TRACE_ISP_HEADER_ADDR + TRACE_ISP_HEADER_SIZE)
#define TRACE_ISP_DATA_SIZE   (TRACE_ISP_SIZE - TRACE_ISP_HEADER_SIZE)
#define TRACE_ISP_MAX_POINTS  (TRACE_ISP_DATA_SIZE / TRACE_ISP_ITEM_SIZE)

/* common majors */
/* SP0 */
#define MAJOR_MAIN              1
#define MAJOR_ISP_STAGE_ENTRY   2
#define MAJOR_DMA_PRXY          3
#define MAJOR_START_ISP         4
/* SP1 */
#define MAJOR_OBSERVER_ISP0_EVENT          21
#define MAJOR_OBSERVER_OUTPUT_FORM_EVENT   22
#define MAJOR_OBSERVER_OUTPUT_SCAL_EVENT   23
#define MAJOR_OBSERVER_IF_ACK              24
#define MAJOR_OBSERVER_SP0_EVENT           25
#define MAJOR_OBSERVER_SP_TERMINATE_EVENT  26
#define MAJOR_OBSERVER_DMA_ACK             27
#define MAJOR_OBSERVER_ACC_ACK             28

#define DEBUG_PTR_SIGNATURE     0xABCD	/* signature for the debug parameter pointer */

/* command codes (1st byte) */
typedef enum {
	CMD_SET_ONE_MAJOR = 1,		/* mask in one major. 2nd byte in the command is the major code */
	CMD_UNSET_ONE_MAJOR = 2,	/* mask out one major. 2nd byte in the command is the major code */
	CMD_SET_ALL_MAJORS = 3,		/* set the major print mask. the full mask is in the data DWORD */
	CMD_SET_VERBOSITY = 4		/* set verbosity level */
} DBG_commands;

/* command signature */
#define CMD_SIGNATURE	0xAABBCC00

/* shared macros in traces infrastructure */
/* increment the pointer cyclicly */
#define DBG_NEXT_ITEM(x, max_items) (((x + 1) >= max_items) ? 0 : x + 1)
#define DBG_PREV_ITEM(x, max_items) ((x) ? x - 1 : max_items - 1)

#define FIELD_MASK(width) (((1 << (width)) - 1))
#define FIELD_PACK(value, mask, offset) (((value) & (mask)) << (offset))
#define FIELD_UNPACK(value, mask, offset) (((value) >> (offset)) & (mask))

#define FIELD_VALUE_OFFSET		(0)
#define FIELD_VALUE_WIDTH		(16)
#define FIELD_VALUE_MASK		FIELD_MASK(FIELD_VALUE_WIDTH)
#define FIELD_VALUE_PACK(f)		FIELD_PACK(f, FIELD_VALUE_MASK, FIELD_VALUE_OFFSET)
#define FIELD_VALUE_UNPACK(f)		FIELD_UNPACK(f, FIELD_VALUE_MASK, FIELD_VALUE_OFFSET)

#define FIELD_MINOR_OFFSET		(FIELD_VALUE_OFFSET + FIELD_VALUE_WIDTH)
#define FIELD_MINOR_WIDTH		(8)
#define FIELD_MINOR_MASK		FIELD_MASK(FIELD_MINOR_WIDTH)
#define FIELD_MINOR_PACK(f)		FIELD_PACK(f, FIELD_MINOR_MASK, FIELD_MINOR_OFFSET)
#define FIELD_MINOR_UNPACK(f)		FIELD_UNPACK(f, FIELD_MINOR_MASK, FIELD_MINOR_OFFSET)

#define FIELD_MAJOR_OFFSET		(FIELD_MINOR_OFFSET + FIELD_MINOR_WIDTH)
#define FIELD_MAJOR_WIDTH		(5)
#define FIELD_MAJOR_MASK		FIELD_MASK(FIELD_MAJOR_WIDTH)
#define FIELD_MAJOR_PACK(f)		FIELD_PACK(f, FIELD_MAJOR_MASK, FIELD_MAJOR_OFFSET)
#define FIELD_MAJOR_UNPACK(f)		FIELD_UNPACK(f, FIELD_MAJOR_MASK, FIELD_MAJOR_OFFSET)

/* for quick traces - only insertion, compatible with the regular point */
#define FIELD_FULL_MAJOR_WIDTH		(8)
#define FIELD_FULL_MAJOR_MASK		FIELD_MASK(FIELD_FULL_MAJOR_WIDTH)
#define FIELD_FULL_MAJOR_PACK(f)	FIELD_PACK(f, FIELD_FULL_MAJOR_MASK, FIELD_MAJOR_OFFSET)

/* The following 2 fields are used only when FIELD_TID value is 111b.
 * it means we don't want to use thread id, but format. In this case,
 * the last 2 MSB bits of the major field will indicates the format
 */
#define FIELD_MAJOR_W_FMT_OFFSET	FIELD_MAJOR_OFFSET
#define FIELD_MAJOR_W_FMT_WIDTH		(3)
#define FIELD_MAJOR_W_FMT_MASK		FIELD_MASK(FIELD_MAJOR_W_FMT_WIDTH)
#define FIELD_MAJOR_W_FMT_PACK(f)	FIELD_PACK(f, FIELD_MAJOR_W_FMT_MASK, FIELD_MAJOR_W_FMT_OFFSET)
#define FIELD_MAJOR_W_FMT_UNPACK(f)	FIELD_UNPACK(f, FIELD_MAJOR_W_FMT_MASK, FIELD_MAJOR_W_FMT_OFFSET)

#define FIELD_FORMAT_OFFSET		(FIELD_MAJOR_OFFSET + FIELD_MAJOR_W_FMT_WIDTH)
#define FIELD_FORMAT_WIDTH		(2)
#define FIELD_FORMAT_MASK		FIELD_MASK(FIELD_MAJOR_W_FMT_WIDTH)
#define FIELD_FORMAT_PACK(f)		FIELD_PACK(f, FIELD_FORMAT_MASK, FIELD_FORMAT_OFFSET)
#define FIELD_FORMAT_UNPACK(f)		FIELD_UNPACK(f, FIELD_FORMAT_MASK, FIELD_FORMAT_OFFSET)

#define FIELD_TID_SEL_FORMAT_PAT	(7)

#define FIELD_TID_OFFSET		(FIELD_MAJOR_OFFSET + FIELD_MAJOR_WIDTH)
#define FIELD_TID_WIDTH			(3)
#define FIELD_TID_MASK			FIELD_MASK(FIELD_TID_WIDTH)
#define FIELD_TID_PACK(f)		FIELD_PACK(f, FIELD_TID_MASK, FIELD_TID_OFFSET)
#define FIELD_TID_UNPACK(f)		FIELD_UNPACK(f, FIELD_TID_MASK, FIELD_TID_OFFSET)

#define FIELD_VALUE_24_OFFSET		(0)
#define FIELD_VALUE_24_WIDTH		(24)
#define FIELD_VALUE_24_MASK		FIELD_MASK(FIELD_VALUE_24_WIDTH)
#define FIELD_VALUE_24_PACK(f)		FIELD_PACK(f, FIELD_VALUE_24_MASK, FIELD_VALUE_24_OFFSET)
#define FIELD_VALUE_24_UNPACK(f)	FIELD_UNPACK(f, FIELD_VALUE_24_MASK, FIELD_VALUE_24_OFFSET)

#define PACK_TRACEPOINT(tid, major, minor, value)	\
	(FIELD_TID_PACK(tid) | FIELD_MAJOR_PACK(major) | FIELD_MINOR_PACK(minor) | FIELD_VALUE_PACK(value))

#define PACK_QUICK_TRACEPOINT(major, minor)	\
	(FIELD_FULL_MAJOR_PACK(major) | FIELD_MINOR_PACK(minor))

#define PACK_FORMATTED_TRACEPOINT(format, major, minor, value)	\
	(FIELD_TID_PACK(FIELD_TID_SEL_FORMAT_PAT) | FIELD_FORMAT_PACK(format) | FIELD_MAJOR_PACK(major) | FIELD_MINOR_PACK(minor) | FIELD_VALUE_PACK(value))

#define PACK_TRACE_VALUE24(major, value)	\
	(FIELD_TID_PACK(FIELD_TID_SEL_FORMAT_PAT) | FIELD_MAJOR_PACK(major) | FIELD_VALUE_24_PACK(value))

#endif /* __CSS_TRACE_H_ */
