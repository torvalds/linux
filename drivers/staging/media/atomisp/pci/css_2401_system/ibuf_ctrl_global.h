/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __IBUF_CTRL_GLOBAL_H_INCLUDED__
#define __IBUF_CTRL_GLOBAL_H_INCLUDED__

#include <type_support.h>

#include <ibuf_cntrl_defs.h>	/* _IBUF_CNTRL_RECALC_WORDS_STATUS,
				 * _IBUF_CNTRL_ARBITERS_STATUS,
				 * _IBUF_CNTRL_PROC_REG_ALIGN,
				 * etc.
				 */

/* Definition of contents of main controller state register is lacking
 * in ibuf_cntrl_defs.h, so define these here:
 */
#define _IBUF_CNTRL_MAIN_CNTRL_FSM_MASK			0xf
#define _IBUF_CNTRL_MAIN_CNTRL_FSM_NEXT_COMMAND_CHECK	0x9
#define _IBUF_CNTRL_MAIN_CNTRL_MEM_INP_BUF_ALLOC	BIT(8)
#define _IBUF_CNTRL_DMA_SYNC_WAIT_FOR_SYNC		1
#define _IBUF_CNTRL_DMA_SYNC_FSM_WAIT_FOR_ACK		(0x3 << 1)

typedef struct ib_buffer_s	ib_buffer_t;
struct	ib_buffer_s {
	u32	start_addr;	/* start address of the buffer in the
					 * "input-buffer hardware block"
					 */

	u32	stride;		/* stride per buffer line (in bytes) */
	u32	lines;		/* lines in the buffer */
};

typedef struct ibuf_ctrl_cfg_s ibuf_ctrl_cfg_t;
struct ibuf_ctrl_cfg_s {
	bool online;

	struct {
		/* DMA configuration */
		u32 channel;
		u32 cmd; /* must be _DMA_V2_MOVE_A2B_NO_SYNC_CHK_COMMAND */

		/* DMA reconfiguration */
		u32 shift_returned_items;
		u32 elems_per_word_in_ibuf;
		u32 elems_per_word_in_dest;
	} dma_cfg;

	ib_buffer_t ib_buffer;

	struct {
		u32 stride;
		u32 start_addr;
		u32 lines;
	} dest_buf_cfg;

	u32 items_per_store;
	u32 stores_per_frame;

	struct {
		u32 sync_cmd;	/* must be _STREAM2MMIO_CMD_TOKEN_SYNC_FRAME */
		u32 store_cmd;	/* must be _STREAM2MMIO_CMD_TOKEN_STORE_PACKETS */
	} stream2mmio_cfg;
};

extern const u32 N_IBUF_CTRL_PROCS[N_IBUF_CTRL_ID];

#endif /* __IBUF_CTRL_GLOBAL_H_INCLUDED__ */
