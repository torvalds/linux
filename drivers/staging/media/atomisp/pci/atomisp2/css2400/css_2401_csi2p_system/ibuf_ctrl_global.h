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
#define _IBUF_CNTRL_MAIN_CNTRL_MEM_INP_BUF_ALLOC	(1 << 8)
#define _IBUF_CNTRL_DMA_SYNC_WAIT_FOR_SYNC		1
#define _IBUF_CNTRL_DMA_SYNC_FSM_WAIT_FOR_ACK		(0x3 << 1)

typedef struct ib_buffer_s	ib_buffer_t;
struct	ib_buffer_s {
	uint32_t	start_addr;	/* start address of the buffer in the
					 * "input-buffer hardware block"
					 */

	uint32_t	stride;		/* stride per buffer line (in bytes) */
	uint32_t	lines;		/* lines in the buffer */
};

typedef struct ibuf_ctrl_cfg_s ibuf_ctrl_cfg_t;
struct ibuf_ctrl_cfg_s {

	bool online;

	struct {
		/* DMA configuration */
		uint32_t channel;
		uint32_t cmd; /* must be _DMA_V2_MOVE_A2B_NO_SYNC_CHK_COMMAND */

		/* DMA reconfiguration */
		uint32_t shift_returned_items;
		uint32_t elems_per_word_in_ibuf;
		uint32_t elems_per_word_in_dest;
	} dma_cfg;

	ib_buffer_t ib_buffer;

	struct {
		uint32_t stride;
		uint32_t start_addr;
		uint32_t lines;
	} dest_buf_cfg;

	uint32_t items_per_store;
	uint32_t stores_per_frame;

	struct {
		uint32_t sync_cmd;	/* must be _STREAM2MMIO_CMD_TOKEN_SYNC_FRAME */
		uint32_t store_cmd;	/* must be _STREAM2MMIO_CMD_TOKEN_STORE_PACKETS */
	} stream2mmio_cfg;
};

extern const uint32_t N_IBUF_CTRL_PROCS[N_IBUF_CTRL_ID];

#endif /* __IBUF_CTRL_GLOBAL_H_INCLUDED__ */
