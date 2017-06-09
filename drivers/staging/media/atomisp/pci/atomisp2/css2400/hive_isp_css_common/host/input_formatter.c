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

#include "system_global.h"

#ifdef USE_INPUT_SYSTEM_VERSION_2

#include "input_formatter.h"
#include <type_support.h>
#include "gp_device.h"

#include "assert_support.h"

#ifndef __INLINE_INPUT_FORMATTER__
#include "input_formatter_private.h"
#endif /* __INLINE_INPUT_FORMATTER__ */

const hrt_address HIVE_IF_SRST_ADDRESS[N_INPUT_FORMATTER_ID] = {
	INPUT_FORMATTER0_SRST_OFFSET,
	INPUT_FORMATTER1_SRST_OFFSET,
	INPUT_FORMATTER2_SRST_OFFSET,
	INPUT_FORMATTER3_SRST_OFFSET};

const hrt_data HIVE_IF_SRST_MASK[N_INPUT_FORMATTER_ID] = {
	INPUT_FORMATTER0_SRST_MASK,
	INPUT_FORMATTER1_SRST_MASK,
	INPUT_FORMATTER2_SRST_MASK,
	INPUT_FORMATTER3_SRST_MASK};

const uint8_t HIVE_IF_SWITCH_CODE[N_INPUT_FORMATTER_ID] = {
	HIVE_INPUT_SWITCH_SELECT_IF_PRIM,
	HIVE_INPUT_SWITCH_SELECT_IF_PRIM,
	HIVE_INPUT_SWITCH_SELECT_IF_SEC,
	HIVE_INPUT_SWITCH_SELECT_STR_TO_MEM};

/* MW Should be part of system_global.h, where we have the main enumeration */
const bool HIVE_IF_BIN_COPY[N_INPUT_FORMATTER_ID] = {
	false, false, false, true};

void input_formatter_rst(
	const input_formatter_ID_t		ID)
{
	hrt_address	addr;
	hrt_data	rst;

	assert(ID < N_INPUT_FORMATTER_ID);

	addr = HIVE_IF_SRST_ADDRESS[ID];
	rst = HIVE_IF_SRST_MASK[ID];

	/* TEMPORARY HACK: THIS RESET BREAKS THE METADATA FEATURE
	 * WICH USES THE STREAM2MEMRY BLOCK.
	 * MUST BE FIXED PROPERLY
	 */
	if (!HIVE_IF_BIN_COPY[ID]) {
		input_formatter_reg_store(ID, addr, rst);
	}

	return;
}

unsigned int input_formatter_get_alignment(
	const input_formatter_ID_t		ID)
{
	assert(ID < N_INPUT_FORMATTER_ID);

	return input_formatter_alignment[ID];
}

void input_formatter_set_fifo_blocking_mode(
	const input_formatter_ID_t		ID,
	const bool						enable)
{
	assert(ID < N_INPUT_FORMATTER_ID);

	/* cnd_input_formatter_reg_store() */
	if (!HIVE_IF_BIN_COPY[ID]) {
		input_formatter_reg_store(ID,
			 HIVE_IF_BLOCK_FIFO_NO_REQ_ADDRESS, enable);
	}
	return;
}

void input_formatter_get_switch_state(
	const input_formatter_ID_t		ID,
	input_formatter_switch_state_t	*state)
{
	assert(ID < N_INPUT_FORMATTER_ID);
	assert(state != NULL);

	/* We'll change this into an intelligent function to get switch info per IF */
	(void)ID;

	state->if_input_switch_lut_reg[0] = gp_device_reg_load(GP_DEVICE0_ID, _REG_GP_IFMT_input_switch_lut_reg0);
	state->if_input_switch_lut_reg[1] = gp_device_reg_load(GP_DEVICE0_ID, _REG_GP_IFMT_input_switch_lut_reg1);
	state->if_input_switch_lut_reg[2] = gp_device_reg_load(GP_DEVICE0_ID, _REG_GP_IFMT_input_switch_lut_reg2);
	state->if_input_switch_lut_reg[3] = gp_device_reg_load(GP_DEVICE0_ID, _REG_GP_IFMT_input_switch_lut_reg3);
	state->if_input_switch_lut_reg[4] = gp_device_reg_load(GP_DEVICE0_ID, _REG_GP_IFMT_input_switch_lut_reg4);
	state->if_input_switch_lut_reg[5] = gp_device_reg_load(GP_DEVICE0_ID, _REG_GP_IFMT_input_switch_lut_reg5);
	state->if_input_switch_lut_reg[6] = gp_device_reg_load(GP_DEVICE0_ID, _REG_GP_IFMT_input_switch_lut_reg6);
	state->if_input_switch_lut_reg[7] = gp_device_reg_load(GP_DEVICE0_ID, _REG_GP_IFMT_input_switch_lut_reg7);
	state->if_input_switch_fsync_lut = gp_device_reg_load(GP_DEVICE0_ID, _REG_GP_IFMT_input_switch_fsync_lut);
	state->if_input_switch_ch_id_fmt_type = gp_device_reg_load(GP_DEVICE0_ID, _REG_GP_IFMT_input_switch_ch_id_fmt_type);

	return;
}

void input_formatter_get_state(
	const input_formatter_ID_t		ID,
	input_formatter_state_t			*state)
{
	assert(ID < N_INPUT_FORMATTER_ID);
	assert(state != NULL);
/*
	state->reset = input_formatter_reg_load(ID,
		HIVE_IF_RESET_ADDRESS);
 */
	state->start_line = input_formatter_reg_load(ID,
		HIVE_IF_START_LINE_ADDRESS);
	state->start_column = input_formatter_reg_load(ID,
		HIVE_IF_START_COLUMN_ADDRESS);
	state->cropped_height = input_formatter_reg_load(ID,
		HIVE_IF_CROPPED_HEIGHT_ADDRESS);
	state->cropped_width = input_formatter_reg_load(ID,
		HIVE_IF_CROPPED_WIDTH_ADDRESS);
	state->ver_decimation = input_formatter_reg_load(ID,
		HIVE_IF_VERTICAL_DECIMATION_ADDRESS);
	state->hor_decimation = input_formatter_reg_load(ID,
		HIVE_IF_HORIZONTAL_DECIMATION_ADDRESS);
	state->hor_deinterleaving = input_formatter_reg_load(ID,
		HIVE_IF_H_DEINTERLEAVING_ADDRESS);
	state->left_padding = input_formatter_reg_load(ID,
		HIVE_IF_LEFTPADDING_WIDTH_ADDRESS);
	state->eol_offset = input_formatter_reg_load(ID,
		HIVE_IF_END_OF_LINE_OFFSET_ADDRESS);
	state->vmem_start_address = input_formatter_reg_load(ID,
		HIVE_IF_VMEM_START_ADDRESS_ADDRESS);
	state->vmem_end_address = input_formatter_reg_load(ID,
		HIVE_IF_VMEM_END_ADDRESS_ADDRESS);
	state->vmem_increment = input_formatter_reg_load(ID,
		HIVE_IF_VMEM_INCREMENT_ADDRESS);
	state->is_yuv420 = input_formatter_reg_load(ID,
		HIVE_IF_YUV_420_FORMAT_ADDRESS);
	state->vsync_active_low = input_formatter_reg_load(ID,
		HIVE_IF_VSYNCK_ACTIVE_LOW_ADDRESS);
	state->hsync_active_low = input_formatter_reg_load(ID,
		HIVE_IF_HSYNCK_ACTIVE_LOW_ADDRESS);
	state->allow_fifo_overflow = input_formatter_reg_load(ID,
		HIVE_IF_ALLOW_FIFO_OVERFLOW_ADDRESS);
	state->block_fifo_when_no_req = input_formatter_reg_load(ID,
		HIVE_IF_BLOCK_FIFO_NO_REQ_ADDRESS);
	state->ver_deinterleaving = input_formatter_reg_load(ID,
		HIVE_IF_V_DEINTERLEAVING_ADDRESS);
/* FSM */
	state->fsm_sync_status = input_formatter_reg_load(ID,
		HIVE_IF_FSM_SYNC_STATUS);
	state->fsm_sync_counter = input_formatter_reg_load(ID,
		HIVE_IF_FSM_SYNC_COUNTER);
	state->fsm_crop_status = input_formatter_reg_load(ID,
		HIVE_IF_FSM_CROP_STATUS);
	state->fsm_crop_line_counter = input_formatter_reg_load(ID,
		HIVE_IF_FSM_CROP_LINE_COUNTER);
	state->fsm_crop_pixel_counter = input_formatter_reg_load(ID,
		HIVE_IF_FSM_CROP_PIXEL_COUNTER);
	state->fsm_deinterleaving_index = input_formatter_reg_load(ID,
		HIVE_IF_FSM_DEINTERLEAVING_IDX);
	state->fsm_dec_h_counter = input_formatter_reg_load(ID,
		HIVE_IF_FSM_DECIMATION_H_COUNTER);
	state->fsm_dec_v_counter = input_formatter_reg_load(ID,
		HIVE_IF_FSM_DECIMATION_V_COUNTER);
	state->fsm_dec_block_v_counter = input_formatter_reg_load(ID,
		HIVE_IF_FSM_DECIMATION_BLOCK_V_COUNTER);
	state->fsm_padding_status = input_formatter_reg_load(ID,
		HIVE_IF_FSM_PADDING_STATUS);
	state->fsm_padding_elem_counter = input_formatter_reg_load(ID,
		HIVE_IF_FSM_PADDING_ELEMENT_COUNTER);
	state->fsm_vector_support_error = input_formatter_reg_load(ID,
		HIVE_IF_FSM_VECTOR_SUPPORT_ERROR);
	state->fsm_vector_buffer_full = input_formatter_reg_load(ID,
		HIVE_IF_FSM_VECTOR_SUPPORT_BUFF_FULL);
	state->vector_support = input_formatter_reg_load(ID,
		HIVE_IF_FSM_VECTOR_SUPPORT);
	state->sensor_data_lost = input_formatter_reg_load(ID,
		HIVE_IF_FIFO_SENSOR_STATUS);

	return;
}

void input_formatter_bin_get_state(
	const input_formatter_ID_t		ID,
	input_formatter_bin_state_t		*state)
{
	assert(ID < N_INPUT_FORMATTER_ID);
	assert(state != NULL);

	state->reset = input_formatter_reg_load(ID,
		HIVE_STR2MEM_SOFT_RESET_REG_ADDRESS);
	state->input_endianness = input_formatter_reg_load(ID,
		HIVE_STR2MEM_INPUT_ENDIANNESS_REG_ADDRESS);
	state->output_endianness = input_formatter_reg_load(ID,
		HIVE_STR2MEM_OUTPUT_ENDIANNESS_REG_ADDRESS);
	state->bitswap = input_formatter_reg_load(ID,
		HIVE_STR2MEM_BIT_SWAPPING_REG_ADDRESS);
	state->block_synch = input_formatter_reg_load(ID,
		HIVE_STR2MEM_BLOCK_SYNC_LEVEL_REG_ADDRESS);
	state->packet_synch = input_formatter_reg_load(ID,
		HIVE_STR2MEM_PACKET_SYNC_LEVEL_REG_ADDRESS);
	state->readpostwrite_synch = input_formatter_reg_load(ID,
		HIVE_STR2MEM_READ_POST_WRITE_SYNC_ENABLE_REG_ADDRESS);
	state->is_2ppc = input_formatter_reg_load(ID,
		HIVE_STR2MEM_DUAL_BYTE_INPUTS_ENABLED_REG_ADDRESS);
	state->en_status_update = input_formatter_reg_load(ID,
		HIVE_STR2MEM_EN_STAT_UPDATE_ADDRESS);
	return;
}
#endif
