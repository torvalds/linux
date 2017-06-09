/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2016, Intel Corporation.
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

#include <stddef.h>		/* NULL */

#include "dma.h"

#include "assert_support.h"

#ifndef __INLINE_DMA__
#include "dma_private.h"
#endif /* __INLINE_DMA__ */

void dma_get_state(const dma_ID_t ID, dma_state_t *state)
{
	int			i;
	hrt_data	tmp;

	assert(ID < N_DMA_ID);
	assert(state != NULL);

	tmp = dma_reg_load(ID, DMA_COMMAND_FSM_REG_IDX);
	//reg  [3:0] : flags error [3], stall, run, idle [0]
	//reg  [9:4] : command
	//reg[14:10] : channel
	//reg [23:15] : param
	state->fsm_command_idle = tmp & 0x1;
	state->fsm_command_run = tmp & 0x2;
	state->fsm_command_stalling = tmp & 0x4;
	state->fsm_command_error    = tmp & 0x8;
	state->last_command_channel = (tmp>>10 & 0x1F);
	state->last_command_param =  (tmp>>15 & 0x0F);
	tmp = (tmp>>4) & 0x3F;
/* state->last_command = (dma_commands_t)tmp; */
/* if the enumerator is made non-linear */
	/* AM: the list below does not cover all the cases*/
	/*  and these are not correct */
	/* therefore for just dumpinmg this command*/
	state->last_command = tmp;

/*
	if (tmp == 0)
		state->last_command = DMA_COMMAND_READ;
	if (tmp == 1)
		state->last_command = DMA_COMMAND_WRITE;
	if (tmp == 2)
		state->last_command = DMA_COMMAND_SET_CHANNEL;
	if (tmp == 3)
		state->last_command = DMA_COMMAND_SET_PARAM;
	if (tmp == 4)
		state->last_command = DMA_COMMAND_READ_SPECIFIC;
	if (tmp == 5)
		state->last_command = DMA_COMMAND_WRITE_SPECIFIC;
	if (tmp == 8)
		state->last_command = DMA_COMMAND_INIT;
	if (tmp == 12)
		state->last_command = DMA_COMMAND_INIT_SPECIFIC;
	if (tmp == 15)
		state->last_command = DMA_COMMAND_RST;
*/

/* No sub-fields, idx = 0 */
	state->current_command = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(0, _DMA_FSM_GROUP_CMD_IDX));
	state->current_addr_a = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(0, _DMA_FSM_GROUP_ADDR_A_IDX));
	state->current_addr_b = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(0, _DMA_FSM_GROUP_ADDR_B_IDX));

	tmp =  dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_STATE_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_idle = tmp & 0x1;
	state->fsm_ctrl_run = tmp & 0x2;
	state->fsm_ctrl_stalling = tmp & 0x4;
	state->fsm_ctrl_error = tmp & 0x8;
	tmp = tmp >> 4;
/* state->fsm_ctrl_state = (dma_ctrl_states_t)tmp; */
	if (tmp == 0)
		state->fsm_ctrl_state = DMA_CTRL_STATE_IDLE;
	if (tmp == 1)
		state->fsm_ctrl_state = DMA_CTRL_STATE_REQ_RCV;
	if (tmp == 2)
		state->fsm_ctrl_state = DMA_CTRL_STATE_RCV;
	if (tmp == 3)
		state->fsm_ctrl_state = DMA_CTRL_STATE_RCV_REQ;
	if (tmp == 4)
		state->fsm_ctrl_state = DMA_CTRL_STATE_INIT;
	state->fsm_ctrl_source_dev = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_REQ_DEV_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_source_addr = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_REQ_ADDR_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_source_stride = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_REQ_STRIDE_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_source_width = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_REQ_XB_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_source_height = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_REQ_YB_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_pack_source_dev = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_PACK_REQ_DEV_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_pack_dest_dev = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_PACK_WR_DEV_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_dest_addr = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_WR_ADDR_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_dest_stride = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_WR_STRIDE_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_pack_source_width = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_PACK_REQ_XB_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_pack_dest_height = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_PACK_WR_YB_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_pack_dest_width = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_PACK_WR_XB_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_pack_source_elems = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_PACK_ELEM_REQ_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_pack_dest_elems = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_PACK_ELEM_WR_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));
	state->fsm_ctrl_pack_extension = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_CTRL_PACK_S_Z_IDX,
		_DMA_FSM_GROUP_FSM_CTRL_IDX));

	tmp = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_PACK_STATE_IDX,
		_DMA_FSM_GROUP_FSM_PACK_IDX));
	state->pack_idle     = tmp & 0x1;
	state->pack_run      = tmp & 0x2;
	state->pack_stalling = tmp & 0x4;
	state->pack_error    = tmp & 0x8;
	state->pack_cnt_height = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_PACK_CNT_YB_IDX,
		_DMA_FSM_GROUP_FSM_PACK_IDX));
	state->pack_src_cnt_width = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_PACK_CNT_XB_REQ_IDX,
		_DMA_FSM_GROUP_FSM_PACK_IDX));
	state->pack_dest_cnt_width = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_PACK_CNT_XB_WR_IDX,
		_DMA_FSM_GROUP_FSM_PACK_IDX));

	tmp = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_REQ_STATE_IDX,
		_DMA_FSM_GROUP_FSM_REQ_IDX));
/* state->read_state = (dma_rw_states_t)tmp; */
	if (tmp == 0)
		state->read_state = DMA_RW_STATE_IDLE;
	if (tmp == 1)
		state->read_state = DMA_RW_STATE_REQ;
	if (tmp == 2)
		state->read_state = DMA_RW_STATE_NEXT_LINE;
	if (tmp == 3)
		state->read_state = DMA_RW_STATE_UNLOCK_CHANNEL;
	state->read_cnt_height = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_REQ_CNT_YB_IDX,
		_DMA_FSM_GROUP_FSM_REQ_IDX));
	state->read_cnt_width = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_REQ_CNT_XB_IDX,
		_DMA_FSM_GROUP_FSM_REQ_IDX));

	tmp = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_WR_STATE_IDX,
		_DMA_FSM_GROUP_FSM_WR_IDX));
/* state->write_state = (dma_rw_states_t)tmp; */
	if (tmp == 0)
		state->write_state = DMA_RW_STATE_IDLE;
	if (tmp == 1)
		state->write_state = DMA_RW_STATE_REQ;
	if (tmp == 2)
		state->write_state = DMA_RW_STATE_NEXT_LINE;
	if (tmp == 3)
		state->write_state = DMA_RW_STATE_UNLOCK_CHANNEL;
	state->write_height = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_WR_CNT_YB_IDX,
		_DMA_FSM_GROUP_FSM_WR_IDX));
	state->write_width = dma_reg_load(ID,
		DMA_CG_INFO_REG_IDX(
		_DMA_FSM_GROUP_FSM_WR_CNT_XB_IDX,
		_DMA_FSM_GROUP_FSM_WR_IDX));

	for (i = 0; i < HIVE_ISP_NUM_DMA_CONNS; i++) {
		dma_port_state_t *port = &(state->port_states[i]);

		tmp = dma_reg_load(ID, DMA_DEV_INFO_REG_IDX(0, i));
		port->req_cs   = ((tmp & 0x1) != 0);
		port->req_we_n = ((tmp & 0x2) != 0);
		port->req_run  = ((tmp & 0x4) != 0);
		port->req_ack  = ((tmp & 0x8) != 0);

		tmp = dma_reg_load(ID, DMA_DEV_INFO_REG_IDX(1, i));
		port->send_cs   = ((tmp & 0x1) != 0);
		port->send_we_n = ((tmp & 0x2) != 0);
		port->send_run  = ((tmp & 0x4) != 0);
		port->send_ack  = ((tmp & 0x8) != 0);

		tmp = dma_reg_load(ID, DMA_DEV_INFO_REG_IDX(2, i));
		if (tmp & 0x1)
			port->fifo_state = DMA_FIFO_STATE_WILL_BE_FULL;
		if (tmp & 0x2)
			port->fifo_state = DMA_FIFO_STATE_FULL;
		if (tmp & 0x4)
			port->fifo_state = DMA_FIFO_STATE_EMPTY;
		port->fifo_counter = tmp >> 3;
	}

	for (i = 0; i < HIVE_DMA_NUM_CHANNELS; i++) {
		dma_channel_state_t *ch = &(state->channel_states[i]);

		ch->connection = DMA_GET_CONNECTION(dma_reg_load(ID,
			DMA_CHANNEL_PARAM_REG_IDX(i,
			_DMA_PACKING_SETUP_PARAM)));
		ch->sign_extend = DMA_GET_EXTENSION(dma_reg_load(ID,
			DMA_CHANNEL_PARAM_REG_IDX(i,
			_DMA_PACKING_SETUP_PARAM)));
		ch->height = dma_reg_load(ID,
			DMA_CHANNEL_PARAM_REG_IDX(i,
			_DMA_HEIGHT_PARAM));
		ch->stride_a = dma_reg_load(ID,
			DMA_CHANNEL_PARAM_REG_IDX(i,
			_DMA_STRIDE_A_PARAM));
		ch->elems_a = DMA_GET_ELEMENTS(dma_reg_load(ID,
			DMA_CHANNEL_PARAM_REG_IDX(i,
			_DMA_ELEM_CROPPING_A_PARAM)));
		ch->cropping_a = DMA_GET_CROPPING(dma_reg_load(ID,
			DMA_CHANNEL_PARAM_REG_IDX(i,
			_DMA_ELEM_CROPPING_A_PARAM)));
		ch->width_a = dma_reg_load(ID,
			DMA_CHANNEL_PARAM_REG_IDX(i,
			_DMA_WIDTH_A_PARAM));
		ch->stride_b = dma_reg_load(ID,
			DMA_CHANNEL_PARAM_REG_IDX(i,
			_DMA_STRIDE_B_PARAM));
		ch->elems_b = DMA_GET_ELEMENTS(dma_reg_load(ID,
			DMA_CHANNEL_PARAM_REG_IDX(i,
			_DMA_ELEM_CROPPING_B_PARAM)));
		ch->cropping_b = DMA_GET_CROPPING(dma_reg_load(ID,
			DMA_CHANNEL_PARAM_REG_IDX(i,
			_DMA_ELEM_CROPPING_B_PARAM)));
		ch->width_b = dma_reg_load(ID,
			DMA_CHANNEL_PARAM_REG_IDX(i,
			_DMA_WIDTH_B_PARAM));
	}
}

void
dma_set_max_burst_size(const dma_ID_t ID, dma_connection conn,
		       uint32_t max_burst_size)
{
	assert(ID < N_DMA_ID);
	assert(max_burst_size > 0);
	dma_reg_store(ID, DMA_DEV_INFO_REG_IDX(_DMA_DEV_INTERF_MAX_BURST_IDX, conn),
		      max_burst_size - 1);
}
