/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef _DMUB_CMD_H_
#define _DMUB_CMD_H_

#include "dmub_types.h"
#include "dmub_cmd_dal.h"
#include "dmub_cmd_vbios.h"
#include "atomfirmware.h"

#define DMUB_RB_CMD_SIZE 64
#define DMUB_RB_MAX_ENTRY 128
#define DMUB_RB_SIZE (DMUB_RB_CMD_SIZE * DMUB_RB_MAX_ENTRY)
#define REG_SET_MASK 0xFFFF


/*
 * Command IDs should be treated as stable ABI.
 * Do not reuse or modify IDs.
 */

enum dmub_cmd_type {
	DMUB_CMD__NULL = 0,
	DMUB_CMD__REG_SEQ_READ_MODIFY_WRITE = 1,
	DMUB_CMD__REG_SEQ_FIELD_UPDATE_SEQ = 2,
	DMUB_CMD__REG_SEQ_BURST_WRITE = 3,
	DMUB_CMD__REG_REG_WAIT = 4,
	DMUB_CMD__PLAT_54186_WA = 5,
	DMUB_CMD__PSR = 64,
	DMUB_CMD__VBIOS = 128,
};

#pragma pack(push, 1)

struct dmub_cmd_header {
	unsigned int type : 8;
	unsigned int sub_type : 8;
	unsigned int reserved0 : 8;
	unsigned int payload_bytes : 6;  /* up to 60 bytes */
	unsigned int reserved1 : 2;
};

/*
 * Read modify write
 *
 * 60 payload bytes can hold up to 5 sets of read modify writes,
 * each take 3 dwords.
 *
 * number of sequences = header.payload_bytes / sizeof(struct dmub_cmd_read_modify_write_sequence)
 *
 * modify_mask = 0xffff'ffff means all fields are going to be updated.  in this case
 * command parser will skip the read and we can use modify_mask = 0xffff'ffff as reg write
 */
struct dmub_cmd_read_modify_write_sequence {
	uint32_t addr;
	uint32_t modify_mask;
	uint32_t modify_value;
};

#define DMUB_READ_MODIFY_WRITE_SEQ__MAX		5
struct dmub_rb_cmd_read_modify_write {
	struct dmub_cmd_header header;  // type = DMUB_CMD__REG_SEQ_READ_MODIFY_WRITE
	struct dmub_cmd_read_modify_write_sequence seq[DMUB_READ_MODIFY_WRITE_SEQ__MAX];
};

/*
 * Update a register with specified masks and values sequeunce
 *
 * 60 payload bytes can hold address + up to 7 sets of mask/value combo, each take 2 dword
 *
 * number of field update sequence = (header.payload_bytes - sizeof(addr)) / sizeof(struct read_modify_write_sequence)
 *
 *
 * USE CASE:
 *   1. auto-increment register where additional read would update pointer and produce wrong result
 *   2. toggle a bit without read in the middle
 */

struct dmub_cmd_reg_field_update_sequence {
	uint32_t modify_mask;  // 0xffff'ffff to skip initial read
	uint32_t modify_value;
};

#define DMUB_REG_FIELD_UPDATE_SEQ__MAX		7

struct dmub_rb_cmd_reg_field_update_sequence {
	struct dmub_cmd_header header;
	uint32_t addr;
	struct dmub_cmd_reg_field_update_sequence seq[DMUB_REG_FIELD_UPDATE_SEQ__MAX];
};


/*
 * Burst write
 *
 * support use case such as writing out LUTs.
 *
 * 60 payload bytes can hold up to 14 values to write to given address
 *
 * number of payload = header.payload_bytes / sizeof(struct read_modify_write_sequence)
 */
#define DMUB_BURST_WRITE_VALUES__MAX  14
struct dmub_rb_cmd_burst_write {
	struct dmub_cmd_header header;  // type = DMUB_CMD__REG_SEQ_BURST_WRITE
	uint32_t addr;
	uint32_t write_values[DMUB_BURST_WRITE_VALUES__MAX];
};


struct dmub_rb_cmd_common {
	struct dmub_cmd_header header;
	uint8_t cmd_buffer[DMUB_RB_CMD_SIZE - sizeof(struct dmub_cmd_header)];
};

struct dmub_cmd_reg_wait_data {
	uint32_t addr;
	uint32_t mask;
	uint32_t condition_field_value;
	uint32_t time_out_us;
};

struct dmub_rb_cmd_reg_wait {
	struct dmub_cmd_header header;
	struct dmub_cmd_reg_wait_data reg_wait;
};

#ifndef PHYSICAL_ADDRESS_LOC
#define PHYSICAL_ADDRESS_LOC union large_integer
#endif

struct dmub_cmd_PLAT_54186_wa {
	uint32_t DCSURF_SURFACE_CONTROL;
	uint32_t DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH;
	uint32_t DCSURF_PRIMARY_SURFACE_ADDRESS;
	uint32_t DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH_C;
	uint32_t DCSURF_PRIMARY_SURFACE_ADDRESS_C;
	struct {
		uint8_t hubp_inst : 4;
		uint8_t tmz_surface : 1;
		uint8_t immediate :1;
		uint8_t vmid : 4;
		uint8_t grph_stereo : 1;
		uint32_t reserved : 21;
	} flip_params;
	uint32_t reserved[9];
};

struct dmub_rb_cmd_PLAT_54186_wa {
	struct dmub_cmd_header header;
	struct dmub_cmd_PLAT_54186_wa flip;
};

struct dmub_cmd_digx_encoder_control_data {
	union dig_encoder_control_parameters_v1_5 dig;
};

struct dmub_rb_cmd_digx_encoder_control {
	struct dmub_cmd_header header;
	struct dmub_cmd_digx_encoder_control_data encoder_control;
};

struct dmub_cmd_set_pixel_clock_data {
	struct set_pixel_clock_parameter_v1_7 clk;
};

struct dmub_rb_cmd_set_pixel_clock {
	struct dmub_cmd_header header;
	struct dmub_cmd_set_pixel_clock_data pixel_clock;
};

struct dmub_cmd_enable_disp_power_gating_data {
	struct enable_disp_power_gating_parameters_v2_1 pwr;
};

struct dmub_rb_cmd_enable_disp_power_gating {
	struct dmub_cmd_header header;
	struct dmub_cmd_enable_disp_power_gating_data power_gating;
};

struct dmub_cmd_dig1_transmitter_control_data {
	struct dig_transmitter_control_parameters_v1_6 dig;
};

struct dmub_rb_cmd_dig1_transmitter_control {
	struct dmub_cmd_header header;
	struct dmub_cmd_dig1_transmitter_control_data transmitter_control;
};

struct dmub_rb_cmd_dpphy_init {
	struct dmub_cmd_header header;
	uint8_t reserved[60];
};

struct dmub_cmd_psr_copy_settings_data {
	uint16_t psr_level;
	uint8_t hubp_inst;
	uint8_t dpp_inst;
	uint8_t mpcc_inst;
	uint8_t opp_inst;
	uint8_t otg_inst;
	uint8_t digfe_inst;
	uint8_t digbe_inst;
	uint8_t dpphy_inst;
	uint8_t aux_inst;
	uint8_t hyst_frames;
	uint8_t hyst_lines;
	uint8_t phy_num;
	uint8_t phy_type;
	uint8_t aux_repeat;
	uint8_t smu_optimizations_en;
	uint8_t skip_wait_for_pll_lock;
	uint8_t frame_delay;
	uint8_t smu_phy_id;
	uint8_t num_of_controllers;
	uint8_t link_rate;
	uint8_t frame_cap_ind;
};

struct dmub_rb_cmd_psr_copy_settings {
	struct dmub_cmd_header header;
	struct dmub_cmd_psr_copy_settings_data psr_copy_settings_data;
};

struct dmub_cmd_psr_set_level_data {
	uint16_t psr_level;
};

struct dmub_rb_cmd_psr_set_level {
	struct dmub_cmd_header header;
	struct dmub_cmd_psr_set_level_data psr_set_level_data;
};

struct dmub_rb_cmd_psr_enable {
	struct dmub_cmd_header header;
};

struct dmub_cmd_psr_setup_data {
	enum psr_version version; // PSR version 1 or 2
};

struct dmub_rb_cmd_psr_setup {
	struct dmub_cmd_header header;
	struct dmub_cmd_psr_setup_data psr_setup_data;
};

union dmub_rb_cmd {
	struct dmub_rb_cmd_read_modify_write read_modify_write;
	struct dmub_rb_cmd_reg_field_update_sequence reg_field_update_seq;
	struct dmub_rb_cmd_burst_write burst_write;
	struct dmub_rb_cmd_reg_wait reg_wait;
	struct dmub_rb_cmd_common cmd_common;
	struct dmub_rb_cmd_digx_encoder_control digx_encoder_control;
	struct dmub_rb_cmd_set_pixel_clock set_pixel_clock;
	struct dmub_rb_cmd_enable_disp_power_gating enable_disp_power_gating;
	struct dmub_rb_cmd_dpphy_init dpphy_init;
	struct dmub_rb_cmd_dig1_transmitter_control dig1_transmitter_control;
	struct dmub_rb_cmd_psr_enable psr_enable;
	struct dmub_rb_cmd_psr_copy_settings psr_copy_settings;
	struct dmub_rb_cmd_psr_set_level psr_set_level;
	struct dmub_rb_cmd_PLAT_54186_wa PLAT_54186_wa;
	struct dmub_rb_cmd_psr_setup psr_setup;
};

#pragma pack(pop)

#endif /* _DMUB_CMD_H_ */
