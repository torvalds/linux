/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 */

#ifndef __DEBUG_GLOBAL_H_INCLUDED__
#define __DEBUG_GLOBAL_H_INCLUDED__

#include <type_support.h>

#define DEBUG_BUF_SIZE	1024
#define DEBUG_BUF_MASK	(DEBUG_BUF_SIZE - 1)

#define DEBUG_DATA_ENABLE_ADDR		0x00
#define DEBUG_DATA_BUF_MODE_ADDR	0x04
#define DEBUG_DATA_HEAD_ADDR		0x08
#define DEBUG_DATA_TAIL_ADDR		0x0C
#define DEBUG_DATA_BUF_ADDR			0x10

#define DEBUG_DATA_ENABLE_DDR_ADDR		0x00
#define DEBUG_DATA_BUF_MODE_DDR_ADDR	HIVE_ISP_DDR_WORD_BYTES
#define DEBUG_DATA_HEAD_DDR_ADDR		(2 * HIVE_ISP_DDR_WORD_BYTES)
#define DEBUG_DATA_TAIL_DDR_ADDR		(3 * HIVE_ISP_DDR_WORD_BYTES)
#define DEBUG_DATA_BUF_DDR_ADDR			(4 * HIVE_ISP_DDR_WORD_BYTES)

#define DEBUG_BUFFER_ISP_DMEM_ADDR       0x0

/*
 * The linear buffer mode will accept data until the first
 * overflow and then stop accepting new data
 * The circular buffer mode will accept if there is place
 * and discard the data if the buffer is full
 */
typedef enum {
	DEBUG_BUFFER_MODE_LINEAR = 0,
	DEBUG_BUFFER_MODE_CIRCULAR,
	N_DEBUG_BUFFER_MODE
} debug_buf_mode_t;

struct debug_data_s {
	u32			enable;
	u32			bufmode;
	u32			head;
	u32			tail;
	u32			buf[DEBUG_BUF_SIZE];
};

/* thread.sp.c doesn't have a notion of HIVE_ISP_DDR_WORD_BYTES
   still one point of control is needed for debug purposes */

#ifdef HIVE_ISP_DDR_WORD_BYTES
struct debug_data_ddr_s {
	u32			enable;
	s8				padding1[HIVE_ISP_DDR_WORD_BYTES - sizeof(uint32_t)];
	u32			bufmode;
	s8				padding2[HIVE_ISP_DDR_WORD_BYTES - sizeof(uint32_t)];
	u32			head;
	s8				padding3[HIVE_ISP_DDR_WORD_BYTES - sizeof(uint32_t)];
	u32			tail;
	s8				padding4[HIVE_ISP_DDR_WORD_BYTES - sizeof(uint32_t)];
	u32			buf[DEBUG_BUF_SIZE];
};
#endif

#endif /* __DEBUG_GLOBAL_H_INCLUDED__ */
