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

#ifndef __IBUF_CTRL_LOCAL_H_INCLUDED__
#define __IBUF_CTRL_LOCAL_H_INCLUDED__

#include "ibuf_ctrl_global.h"

typedef struct ibuf_ctrl_proc_state_s	ibuf_ctrl_proc_state_t;
typedef struct ibuf_ctrl_state_s		ibuf_ctrl_state_t;

struct ibuf_ctrl_proc_state_s {
	hrt_data num_items;
	hrt_data num_stores;
	hrt_data dma_channel;
	hrt_data dma_command;
	hrt_data ibuf_st_addr;
	hrt_data ibuf_stride;
	hrt_data ibuf_end_addr;
	hrt_data dest_st_addr;
	hrt_data dest_stride;
	hrt_data dest_end_addr;
	hrt_data sync_frame;
	hrt_data sync_command;
	hrt_data store_command;
	hrt_data shift_returned_items;
	hrt_data elems_ibuf;
	hrt_data elems_dest;
	hrt_data cur_stores;
	hrt_data cur_acks;
	hrt_data cur_s2m_ibuf_addr;
	hrt_data cur_dma_ibuf_addr;
	hrt_data cur_dma_dest_addr;
	hrt_data cur_isp_dest_addr;
	hrt_data dma_cmds_send;
	hrt_data main_cntrl_state;
	hrt_data dma_sync_state;
	hrt_data isp_sync_state;
};

struct ibuf_ctrl_state_s {
	hrt_data	recalc_words;
	hrt_data	arbiters;
	ibuf_ctrl_proc_state_t	proc_state[N_STREAM2MMIO_SID_ID];
};

#endif /* __IBUF_CTRL_LOCAL_H_INCLUDED__ */
