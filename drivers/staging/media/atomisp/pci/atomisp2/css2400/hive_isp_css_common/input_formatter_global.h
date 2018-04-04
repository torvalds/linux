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

#ifndef __INPUT_FORMATTER_GLOBAL_H_INCLUDED__
#define __INPUT_FORMATTER_GLOBAL_H_INCLUDED__

#define IS_INPUT_FORMATTER_VERSION2
#define IS_INPUT_SWITCH_VERSION2

#include <type_support.h>
#include <system_types.h>
#include "if_defs.h"
#include "str2mem_defs.h"
#include "input_switch_2400_defs.h"

#define _HIVE_INPUT_SWITCH_GET_FSYNC_REG_LSB(ch_id)        ((ch_id) * 3)

#define HIVE_SWITCH_N_CHANNELS				4
#define HIVE_SWITCH_N_FORMATTYPES			32
#define HIVE_SWITCH_N_SWITCH_CODE			4
#define HIVE_SWITCH_M_CHANNELS				0x00000003
#define HIVE_SWITCH_M_FORMATTYPES			0x0000001f
#define HIVE_SWITCH_M_SWITCH_CODE			0x00000003
#define HIVE_SWITCH_M_FSYNC					0x00000007

#define HIVE_SWITCH_ENCODE_FSYNC(x) \
	(1U<<(((x)-1)&HIVE_SWITCH_M_CHANNELS))

#define _HIVE_INPUT_SWITCH_GET_LUT_FIELD(reg, bit_index) \
	(((reg) >> (bit_index)) & HIVE_SWITCH_M_SWITCH_CODE)
#define _HIVE_INPUT_SWITCH_SET_LUT_FIELD(reg, bit_index, val) \
	(((reg) & ~(HIVE_SWITCH_M_SWITCH_CODE<<(bit_index))) | (((hrt_data)(val)&HIVE_SWITCH_M_SWITCH_CODE)<<(bit_index)))
#define _HIVE_INPUT_SWITCH_GET_FSYNC_FIELD(reg, bit_index) \
	(((reg) >> (bit_index)) & HIVE_SWITCH_M_FSYNC)
#define _HIVE_INPUT_SWITCH_SET_FSYNC_FIELD(reg, bit_index, val) \
	(((reg) & ~(HIVE_SWITCH_M_FSYNC<<(bit_index))) | (((hrt_data)(val)&HIVE_SWITCH_M_FSYNC)<<(bit_index)))

typedef struct input_formatter_cfg_s	input_formatter_cfg_t;

/* Hardware registers */
/*#define HIVE_IF_RESET_ADDRESS                   0x000*/ /* deprecated */
#define HIVE_IF_START_LINE_ADDRESS              0x004
#define HIVE_IF_START_COLUMN_ADDRESS            0x008
#define HIVE_IF_CROPPED_HEIGHT_ADDRESS          0x00C
#define HIVE_IF_CROPPED_WIDTH_ADDRESS           0x010
#define HIVE_IF_VERTICAL_DECIMATION_ADDRESS     0x014
#define HIVE_IF_HORIZONTAL_DECIMATION_ADDRESS   0x018
#define HIVE_IF_H_DEINTERLEAVING_ADDRESS        0x01C
#define HIVE_IF_LEFTPADDING_WIDTH_ADDRESS       0x020
#define HIVE_IF_END_OF_LINE_OFFSET_ADDRESS      0x024
#define HIVE_IF_VMEM_START_ADDRESS_ADDRESS      0x028
#define HIVE_IF_VMEM_END_ADDRESS_ADDRESS        0x02C
#define HIVE_IF_VMEM_INCREMENT_ADDRESS          0x030
#define HIVE_IF_YUV_420_FORMAT_ADDRESS          0x034
#define HIVE_IF_VSYNCK_ACTIVE_LOW_ADDRESS       0x038
#define HIVE_IF_HSYNCK_ACTIVE_LOW_ADDRESS       0x03C
#define HIVE_IF_ALLOW_FIFO_OVERFLOW_ADDRESS     0x040
#define HIVE_IF_BLOCK_FIFO_NO_REQ_ADDRESS       0x044
#define HIVE_IF_V_DEINTERLEAVING_ADDRESS        0x048
#define HIVE_IF_FSM_CROP_PIXEL_COUNTER          0x110
#define HIVE_IF_FSM_CROP_LINE_COUNTER           0x10C
#define HIVE_IF_FSM_CROP_STATUS                 0x108

/* Registers only for simulation */
#define HIVE_IF_CRUN_MODE_ADDRESS               0x04C
#define HIVE_IF_DUMP_OUTPUT_ADDRESS             0x050

/* Follow the DMA syntax, "cmd" last */
#define IF_PACK(val, cmd)             ((val & 0x0fff) | (cmd /*& 0xf000*/))

#define HIVE_STR2MEM_SOFT_RESET_REG_ADDRESS                   (_STR2MEM_SOFT_RESET_REG_ID * _STR2MEM_REG_ALIGN)
#define HIVE_STR2MEM_INPUT_ENDIANNESS_REG_ADDRESS             (_STR2MEM_INPUT_ENDIANNESS_REG_ID * _STR2MEM_REG_ALIGN)
#define HIVE_STR2MEM_OUTPUT_ENDIANNESS_REG_ADDRESS            (_STR2MEM_OUTPUT_ENDIANNESS_REG_ID * _STR2MEM_REG_ALIGN)
#define HIVE_STR2MEM_BIT_SWAPPING_REG_ADDRESS                 (_STR2MEM_BIT_SWAPPING_REG_ID * _STR2MEM_REG_ALIGN)
#define HIVE_STR2MEM_BLOCK_SYNC_LEVEL_REG_ADDRESS             (_STR2MEM_BLOCK_SYNC_LEVEL_REG_ID * _STR2MEM_REG_ALIGN)
#define HIVE_STR2MEM_PACKET_SYNC_LEVEL_REG_ADDRESS            (_STR2MEM_PACKET_SYNC_LEVEL_REG_ID * _STR2MEM_REG_ALIGN)
#define HIVE_STR2MEM_READ_POST_WRITE_SYNC_ENABLE_REG_ADDRESS  (_STR2MEM_READ_POST_WRITE_SYNC_ENABLE_REG_ID * _STR2MEM_REG_ALIGN)
#define HIVE_STR2MEM_DUAL_BYTE_INPUTS_ENABLED_REG_ADDRESS     (_STR2MEM_DUAL_BYTE_INPUTS_ENABLED_REG_ID * _STR2MEM_REG_ALIGN)
#define HIVE_STR2MEM_EN_STAT_UPDATE_ADDRESS                   (_STR2MEM_EN_STAT_UPDATE_ID * _STR2MEM_REG_ALIGN)

/*
 * This data structure is shared between host and SP
 */
struct input_formatter_cfg_s {
	uint32_t	start_line;
	uint32_t	start_column;
	uint32_t	left_padding;
	uint32_t	cropped_height;
	uint32_t	cropped_width;
	uint32_t	deinterleaving;
	uint32_t	buf_vecs;
	uint32_t	buf_start_index;
	uint32_t	buf_increment;
	uint32_t	buf_eol_offset;
	uint32_t	is_yuv420_format;
	uint32_t	block_no_reqs;
};

extern const hrt_address HIVE_IF_SRST_ADDRESS[N_INPUT_FORMATTER_ID];
extern const hrt_data HIVE_IF_SRST_MASK[N_INPUT_FORMATTER_ID];
extern const uint8_t HIVE_IF_SWITCH_CODE[N_INPUT_FORMATTER_ID];

#endif /* __INPUT_FORMATTER_GLOBAL_H_INCLUDED__ */
