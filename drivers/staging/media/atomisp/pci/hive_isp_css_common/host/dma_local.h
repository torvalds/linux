/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
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

#ifndef __DMA_LOCAL_H_INCLUDED__
#define __DMA_LOCAL_H_INCLUDED__

#include <type_support.h>
#include "dma_global.h"

#include <defs.h>				/* HRTCAT() */
#include <bits.h>				/* _hrt_get_bits() */
#include <hive_isp_css_defs.h>		/* HIVE_DMA_NUM_CHANNELS */
#include <dma_v2_defs.h>

#define _DMA_FSM_GROUP_CMD_IDX						_DMA_V2_FSM_GROUP_CMD_IDX
#define _DMA_FSM_GROUP_ADDR_A_IDX					_DMA_V2_FSM_GROUP_ADDR_SRC_IDX
#define _DMA_FSM_GROUP_ADDR_B_IDX					_DMA_V2_FSM_GROUP_ADDR_DEST_IDX

#define _DMA_FSM_GROUP_CMD_CTRL_IDX					_DMA_V2_FSM_GROUP_CMD_CTRL_IDX

#define _DMA_FSM_GROUP_FSM_CTRL_IDX					_DMA_V2_FSM_GROUP_FSM_CTRL_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_STATE_IDX			_DMA_V2_FSM_GROUP_FSM_CTRL_STATE_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_REQ_DEV_IDX			_DMA_V2_FSM_GROUP_FSM_CTRL_REQ_DEV_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_REQ_ADDR_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_REQ_ADDR_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_REQ_STRIDE_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_REQ_STRIDE_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_REQ_XB_IDX			_DMA_V2_FSM_GROUP_FSM_CTRL_REQ_XB_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_REQ_YB_IDX			_DMA_V2_FSM_GROUP_FSM_CTRL_REQ_YB_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_REQ_DEV_IDX	_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_REQ_DEV_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_WR_DEV_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_WR_DEV_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_WR_ADDR_IDX			_DMA_V2_FSM_GROUP_FSM_CTRL_WR_ADDR_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_WR_STRIDE_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_WR_STRIDE_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_REQ_XB_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_REQ_XB_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_WR_YB_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_WR_YB_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_WR_XB_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_WR_XB_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_ELEM_REQ_IDX	_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_ELEM_REQ_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_ELEM_WR_IDX	_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_ELEM_WR_IDX
#define _DMA_FSM_GROUP_FSM_CTRL_PACK_S_Z_IDX		_DMA_V2_FSM_GROUP_FSM_CTRL_PACK_S_Z_IDX

#define _DMA_FSM_GROUP_FSM_PACK_IDX					_DMA_V2_FSM_GROUP_FSM_PACK_IDX
#define _DMA_FSM_GROUP_FSM_PACK_STATE_IDX			_DMA_V2_FSM_GROUP_FSM_PACK_STATE_IDX
#define _DMA_FSM_GROUP_FSM_PACK_CNT_YB_IDX			_DMA_V2_FSM_GROUP_FSM_PACK_CNT_YB_IDX
#define _DMA_FSM_GROUP_FSM_PACK_CNT_XB_REQ_IDX		_DMA_V2_FSM_GROUP_FSM_PACK_CNT_XB_REQ_IDX
#define _DMA_FSM_GROUP_FSM_PACK_CNT_XB_WR_IDX		_DMA_V2_FSM_GROUP_FSM_PACK_CNT_XB_WR_IDX

#define _DMA_FSM_GROUP_FSM_REQ_IDX					_DMA_V2_FSM_GROUP_FSM_REQ_IDX
#define _DMA_FSM_GROUP_FSM_REQ_STATE_IDX			_DMA_V2_FSM_GROUP_FSM_REQ_STATE_IDX
#define _DMA_FSM_GROUP_FSM_REQ_CNT_YB_IDX			_DMA_V2_FSM_GROUP_FSM_REQ_CNT_YB_IDX
#define _DMA_FSM_GROUP_FSM_REQ_CNT_XB_IDX			_DMA_V2_FSM_GROUP_FSM_REQ_CNT_XB_IDX

#define _DMA_FSM_GROUP_FSM_WR_IDX					_DMA_V2_FSM_GROUP_FSM_WR_IDX
#define _DMA_FSM_GROUP_FSM_WR_STATE_IDX				_DMA_V2_FSM_GROUP_FSM_WR_STATE_IDX
#define _DMA_FSM_GROUP_FSM_WR_CNT_YB_IDX			_DMA_V2_FSM_GROUP_FSM_WR_CNT_YB_IDX
#define _DMA_FSM_GROUP_FSM_WR_CNT_XB_IDX			_DMA_V2_FSM_GROUP_FSM_WR_CNT_XB_IDX

#define _DMA_DEV_INTERF_MAX_BURST_IDX			_DMA_V2_DEV_INTERF_MAX_BURST_IDX

/*
 * Macro's to compute the DMA parameter register indices
 */
#define DMA_SEL_COMP(comp)     (((comp)  & _hrt_ones(_DMA_V2_ADDR_SEL_COMP_BITS))            << _DMA_V2_ADDR_SEL_COMP_IDX)
#define DMA_SEL_CH(ch)         (((ch)    & _hrt_ones(_DMA_V2_ADDR_SEL_CH_REG_BITS))          << _DMA_V2_ADDR_SEL_CH_REG_IDX)
#define DMA_SEL_PARAM(param)   (((param) & _hrt_ones(_DMA_V2_ADDR_SEL_PARAM_BITS))           << _DMA_V2_ADDR_SEL_PARAM_IDX)
/* CG = Connection Group */
#define DMA_SEL_CG_INFO(info)  (((info)  & _hrt_ones(_DMA_V2_ADDR_SEL_GROUP_COMP_INFO_BITS)) << _DMA_V2_ADDR_SEL_GROUP_COMP_INFO_IDX)
#define DMA_SEL_CG_COMP(comp)  (((comp)  & _hrt_ones(_DMA_V2_ADDR_SEL_GROUP_COMP_BITS))      << _DMA_V2_ADDR_SEL_GROUP_COMP_IDX)
#define DMA_SEL_DEV_INFO(info) (((info)  & _hrt_ones(_DMA_V2_ADDR_SEL_DEV_INTERF_INFO_BITS)) << _DMA_V2_ADDR_SEL_DEV_INTERF_INFO_IDX)
#define DMA_SEL_DEV_ID(dev)    (((dev)   & _hrt_ones(_DMA_V2_ADDR_SEL_DEV_INTERF_IDX_BITS))  << _DMA_V2_ADDR_SEL_DEV_INTERF_IDX_IDX)

#define DMA_COMMAND_FSM_REG_IDX					(DMA_SEL_COMP(_DMA_V2_SEL_FSM_CMD) >> 2)
#define DMA_CHANNEL_PARAM_REG_IDX(ch, param)	((DMA_SEL_COMP(_DMA_V2_SEL_CH_REG) | DMA_SEL_CH(ch) | DMA_SEL_PARAM(param)) >> 2)
#define DMA_CG_INFO_REG_IDX(info_id, comp_id)	((DMA_SEL_COMP(_DMA_V2_SEL_CONN_GROUP) | DMA_SEL_CG_INFO(info_id) | DMA_SEL_CG_COMP(comp_id)) >> 2)
#define DMA_DEV_INFO_REG_IDX(info_id, dev_id)	((DMA_SEL_COMP(_DMA_V2_SEL_DEV_INTERF) | DMA_SEL_DEV_INFO(info_id) | DMA_SEL_DEV_ID(dev_id)) >> 2)
#define DMA_RST_REG_IDX							(DMA_SEL_COMP(_DMA_V2_SEL_RESET) >> 2)

#define DMA_GET_CONNECTION(val)    _hrt_get_bits(val, _DMA_V2_CONNECTION_IDX,    _DMA_V2_CONNECTION_BITS)
#define DMA_GET_EXTENSION(val)     _hrt_get_bits(val, _DMA_V2_EXTENSION_IDX,     _DMA_V2_EXTENSION_BITS)
#define DMA_GET_ELEMENTS(val)      _hrt_get_bits(val, _DMA_V2_ELEMENTS_IDX,      _DMA_V2_ELEMENTS_BITS)
#define DMA_GET_CROPPING(val)      _hrt_get_bits(val, _DMA_V2_LEFT_CROPPING_IDX, _DMA_V2_LEFT_CROPPING_BITS)

typedef enum {
	DMA_CTRL_STATE_IDLE,
	DMA_CTRL_STATE_REQ_RCV,
	DMA_CTRL_STATE_RCV,
	DMA_CTRL_STATE_RCV_REQ,
	DMA_CTRL_STATE_INIT,
	N_DMA_CTRL_STATES
} dma_ctrl_states_t;

typedef enum {
	DMA_COMMAND_READ,
	DMA_COMMAND_WRITE,
	DMA_COMMAND_SET_CHANNEL,
	DMA_COMMAND_SET_PARAM,
	DMA_COMMAND_READ_SPECIFIC,
	DMA_COMMAND_WRITE_SPECIFIC,
	DMA_COMMAND_INIT,
	DMA_COMMAND_INIT_SPECIFIC,
	DMA_COMMAND_RST,
	N_DMA_COMMANDS
} dma_commands_t;

typedef enum {
	DMA_RW_STATE_IDLE,
	DMA_RW_STATE_REQ,
	DMA_RW_STATE_NEXT_LINE,
	DMA_RW_STATE_UNLOCK_CHANNEL,
	N_DMA_RW_STATES
} dma_rw_states_t;

typedef enum {
	DMA_FIFO_STATE_WILL_BE_FULL,
	DMA_FIFO_STATE_FULL,
	DMA_FIFO_STATE_EMPTY,
	N_DMA_FIFO_STATES
} dma_fifo_states_t;

/* typedef struct dma_state_s			dma_state_t; */
typedef struct dma_channel_state_s	dma_channel_state_t;
typedef struct dma_port_state_s		dma_port_state_t;

struct dma_port_state_s {
	bool                       req_cs;
	bool                       req_we_n;
	bool                       req_run;
	bool                       req_ack;
	bool                       send_cs;
	bool                       send_we_n;
	bool                       send_run;
	bool                       send_ack;
	dma_fifo_states_t          fifo_state;
	int                        fifo_counter;
};

struct dma_channel_state_s {
	int                        connection;
	bool                       sign_extend;
	int                        height;
	int                        stride_a;
	int                        elems_a;
	int                        cropping_a;
	int                        width_a;
	int                        stride_b;
	int                        elems_b;
	int                        cropping_b;
	int                        width_b;
};

struct dma_state_s {
	bool                       fsm_command_idle;
	bool                       fsm_command_run;
	bool                       fsm_command_stalling;
	bool                       fsm_command_error;
	dma_commands_t             last_command;
	int                        last_command_channel;
	int                        last_command_param;
	dma_commands_t             current_command;
	int                        current_addr_a;
	int                        current_addr_b;
	bool                       fsm_ctrl_idle;
	bool                       fsm_ctrl_run;
	bool                       fsm_ctrl_stalling;
	bool                       fsm_ctrl_error;
	dma_ctrl_states_t          fsm_ctrl_state;
	int                        fsm_ctrl_source_dev;
	int                        fsm_ctrl_source_addr;
	int                        fsm_ctrl_source_stride;
	int                        fsm_ctrl_source_width;
	int                        fsm_ctrl_source_height;
	int                        fsm_ctrl_pack_source_dev;
	int                        fsm_ctrl_pack_dest_dev;
	int                        fsm_ctrl_dest_addr;
	int                        fsm_ctrl_dest_stride;
	int                        fsm_ctrl_pack_source_width;
	int                        fsm_ctrl_pack_dest_height;
	int                        fsm_ctrl_pack_dest_width;
	int                        fsm_ctrl_pack_source_elems;
	int                        fsm_ctrl_pack_dest_elems;
	int                        fsm_ctrl_pack_extension;
	int						   pack_idle;
	int			       pack_run;
	int				   pack_stalling;
	int				   pack_error;
	int                        pack_cnt_height;
	int                        pack_src_cnt_width;
	int                        pack_dest_cnt_width;
	dma_rw_states_t            read_state;
	int                        read_cnt_height;
	int                        read_cnt_width;
	dma_rw_states_t            write_state;
	int                        write_height;
	int                        write_width;
	dma_port_state_t           port_states[HIVE_ISP_NUM_DMA_CONNS];
	dma_channel_state_t        channel_states[HIVE_DMA_NUM_CHANNELS];
};

#endif /* __DMA_LOCAL_H_INCLUDED__ */
