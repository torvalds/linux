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

#ifndef __INPUT_FORMATTER_LOCAL_H_INCLUDED__
#define __INPUT_FORMATTER_LOCAL_H_INCLUDED__

#include "input_formatter_global.h"

#include "isp.h"		/* ISP_VEC_ALIGN */

typedef struct input_formatter_switch_state_s	input_formatter_switch_state_t;
typedef struct input_formatter_state_s			input_formatter_state_t;
typedef struct input_formatter_bin_state_s		input_formatter_bin_state_t;

#define HIVE_IF_FSM_SYNC_STATUS                 0x100
#define HIVE_IF_FSM_SYNC_COUNTER                0x104
#define HIVE_IF_FSM_DEINTERLEAVING_IDX          0x114
#define HIVE_IF_FSM_DECIMATION_H_COUNTER        0x118
#define HIVE_IF_FSM_DECIMATION_V_COUNTER        0x11C
#define HIVE_IF_FSM_DECIMATION_BLOCK_V_COUNTER  0x120
#define HIVE_IF_FSM_PADDING_STATUS              0x124
#define HIVE_IF_FSM_PADDING_ELEMENT_COUNTER     0x128
#define HIVE_IF_FSM_VECTOR_SUPPORT_ERROR        0x12C
#define HIVE_IF_FSM_VECTOR_SUPPORT_BUFF_FULL    0x130
#define HIVE_IF_FSM_VECTOR_SUPPORT              0x134
#define HIVE_IF_FIFO_SENSOR_STATUS              0x138

/*
 * The switch LUT's coding defines a sink for each
 * single channel ID + channel format type. Conversely
 * the sink (i.e. an input formatter) can be reached
 * from multiple channel & format type combinations
 *
 * LUT[0,1] channel=0, format type {0,1,...31}
 * LUT[2,3] channel=1, format type {0,1,...31}
 * LUT[4,5] channel=2, format type {0,1,...31}
 * LUT[6,7] channel=3, format type {0,1,...31}
 *
 * Each register hold 16 2-bit fields encoding the sink
 * {0,1,2,3}, "0" means unconnected.
 *
 * The single FSYNCH register uses four 3-bit fields of 1-hot
 * encoded sink information, "0" means unconnected.
 *
 * The encoding is redundant. The FSYNCH setting will connect
 * a channel to a sink. At that point the LUT's belonging to
 * that channel can be directed to another sink. Thus the data
 * goes to another place than the synch
 */
struct input_formatter_switch_state_s {
	int	if_input_switch_lut_reg[8];
	int	if_input_switch_fsync_lut;
	int	if_input_switch_ch_id_fmt_type;
	bool if_input_switch_map[HIVE_SWITCH_N_CHANNELS][HIVE_SWITCH_N_FORMATTYPES];
};

struct input_formatter_state_s {
	/*	int	reset; */
	int	start_line;
	int	start_column;
	int	cropped_height;
	int	cropped_width;
	int	ver_decimation;
	int	hor_decimation;
	int	ver_deinterleaving;
	int	hor_deinterleaving;
	int	left_padding;
	int	eol_offset;
	int	vmem_start_address;
	int	vmem_end_address;
	int	vmem_increment;
	int	is_yuv420;
	int	vsync_active_low;
	int	hsync_active_low;
	int	allow_fifo_overflow;
	int block_fifo_when_no_req;
	int	fsm_sync_status;
	int	fsm_sync_counter;
	int	fsm_crop_status;
	int	fsm_crop_line_counter;
	int	fsm_crop_pixel_counter;
	int	fsm_deinterleaving_index;
	int	fsm_dec_h_counter;
	int	fsm_dec_v_counter;
	int	fsm_dec_block_v_counter;
	int	fsm_padding_status;
	int	fsm_padding_elem_counter;
	int	fsm_vector_support_error;
	int	fsm_vector_buffer_full;
	int	vector_support;
	int	sensor_data_lost;
};

struct input_formatter_bin_state_s {
	u32	reset;
	u32	input_endianness;
	u32	output_endianness;
	u32	bitswap;
	u32	block_synch;
	u32	packet_synch;
	u32	readpostwrite_synch;
	u32	is_2ppc;
	u32	en_status_update;
};

static const unsigned int input_formatter_alignment[N_INPUT_FORMATTER_ID] = {
	ISP_VEC_ALIGN, ISP_VEC_ALIGN, HIVE_ISP_CTRL_DATA_BYTES
};

#endif /* __INPUT_FORMATTER_LOCAL_H_INCLUDED__ */
