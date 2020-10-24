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

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <stdarg.h>

#include "atomfirmware.h"

/* Firmware versioning. */
#ifdef DMUB_EXPOSE_VERSION
#define DMUB_FW_VERSION_GIT_HASH 0x9f0af34af
#define DMUB_FW_VERSION_MAJOR 0
#define DMUB_FW_VERSION_MINOR 0
#define DMUB_FW_VERSION_REVISION 40
#define DMUB_FW_VERSION_TEST 0
#define DMUB_FW_VERSION_VBIOS 0
#define DMUB_FW_VERSION_HOTFIX 0
#define DMUB_FW_VERSION_UCODE (((DMUB_FW_VERSION_MAJOR & 0xFF) << 24) | \
		((DMUB_FW_VERSION_MINOR & 0xFF) << 16) | \
		((DMUB_FW_VERSION_REVISION & 0xFF) << 8) | \
		((DMUB_FW_VERSION_TEST & 0x1) << 7) | \
		((DMUB_FW_VERSION_VBIOS & 0x1) << 6) | \
		(DMUB_FW_VERSION_HOTFIX & 0x3F))

#endif

//<DMUB_TYPES>==================================================================
/* Basic type definitions. */

#define SET_ABM_PIPE_GRADUALLY_DISABLE           0
#define SET_ABM_PIPE_IMMEDIATELY_DISABLE         255
#define SET_ABM_PIPE_IMMEDIATE_KEEP_GAIN_DISABLE 254
#define SET_ABM_PIPE_NORMAL                      1

/* Maximum number of streams on any ASIC. */
#define DMUB_MAX_STREAMS 6

/* Maximum number of planes on any ASIC. */
#define DMUB_MAX_PLANES 6

#ifndef PHYSICAL_ADDRESS_LOC
#define PHYSICAL_ADDRESS_LOC union large_integer
#endif

#ifndef dmub_memcpy
#define dmub_memcpy(dest, source, bytes) memcpy((dest), (source), (bytes))
#endif

#ifndef dmub_memset
#define dmub_memset(dest, val, bytes) memset((dest), (val), (bytes))
#endif

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef dmub_udelay
#define dmub_udelay(microseconds) udelay(microseconds)
#endif

union dmub_addr {
	struct {
		uint32_t low_part;
		uint32_t high_part;
	} u;
	uint64_t quad_part;
};

union dmub_psr_debug_flags {
	struct {
		uint32_t visual_confirm : 1;
		uint32_t use_hw_lock_mgr : 1;
		uint32_t log_line_nums : 1;
	} bitfields;

	uint32_t u32All;
};

#if defined(__cplusplus)
}
#endif



//==============================================================================
//</DMUB_TYPES>=================================================================
//==============================================================================
//< DMUB_META>==================================================================
//==============================================================================
#pragma pack(push, 1)

/* Magic value for identifying dmub_fw_meta_info */
#define DMUB_FW_META_MAGIC 0x444D5542

/* Offset from the end of the file to the dmub_fw_meta_info */
#define DMUB_FW_META_OFFSET 0x24

/**
 * struct dmub_fw_meta_info - metadata associated with fw binary
 *
 * NOTE: This should be considered a stable API. Fields should
 *       not be repurposed or reordered. New fields should be
 *       added instead to extend the structure.
 *
 * @magic_value: magic value identifying DMUB firmware meta info
 * @fw_region_size: size of the firmware state region
 * @trace_buffer_size: size of the tracebuffer region
 * @fw_version: the firmware version information
 * @dal_fw: 1 if the firmware is DAL
 */
struct dmub_fw_meta_info {
	uint32_t magic_value;
	uint32_t fw_region_size;
	uint32_t trace_buffer_size;
	uint32_t fw_version;
	uint8_t dal_fw;
	uint8_t reserved[3];
};

/* Ensure that the structure remains 64 bytes. */
union dmub_fw_meta {
	struct dmub_fw_meta_info info;
	uint8_t reserved[64];
};

#pragma pack(pop)

//==============================================================================
//< DMUB_STATUS>================================================================
//==============================================================================

/**
 * DMCUB scratch registers can be used to determine firmware status.
 * Current scratch register usage is as follows:
 *
 * SCRATCH0: FW Boot Status register
 * SCRATCH15: FW Boot Options register
 */

/* Register bit definition for SCRATCH0 */
union dmub_fw_boot_status {
	struct {
		uint32_t dal_fw : 1;
		uint32_t mailbox_rdy : 1;
		uint32_t optimized_init_done : 1;
		uint32_t restore_required : 1;
	} bits;
	uint32_t all;
};

enum dmub_fw_boot_status_bit {
	DMUB_FW_BOOT_STATUS_BIT_DAL_FIRMWARE = (1 << 0),
	DMUB_FW_BOOT_STATUS_BIT_MAILBOX_READY = (1 << 1),
	DMUB_FW_BOOT_STATUS_BIT_OPTIMIZED_INIT_DONE = (1 << 2),
	DMUB_FW_BOOT_STATUS_BIT_RESTORE_REQUIRED = (1 << 3),
};

/* Register bit definition for SCRATCH15 */
union dmub_fw_boot_options {
	struct {
		uint32_t pemu_env : 1;
		uint32_t fpga_env : 1;
		uint32_t optimized_init : 1;
		uint32_t skip_phy_access : 1;
		uint32_t disable_clk_gate: 1;
		uint32_t reserved : 27;
	} bits;
	uint32_t all;
};

enum dmub_fw_boot_options_bit {
	DMUB_FW_BOOT_OPTION_BIT_PEMU_ENV = (1 << 0),
	DMUB_FW_BOOT_OPTION_BIT_FPGA_ENV = (1 << 1),
	DMUB_FW_BOOT_OPTION_BIT_OPTIMIZED_INIT_DONE = (1 << 2),
};

//==============================================================================
//</DMUB_STATUS>================================================================
//==============================================================================
//< DMUB_VBIOS>=================================================================
//==============================================================================

/*
 * Command IDs should be treated as stable ABI.
 * Do not reuse or modify IDs.
 */

enum dmub_cmd_vbios_type {
	DMUB_CMD__VBIOS_DIGX_ENCODER_CONTROL = 0,
	DMUB_CMD__VBIOS_DIG1_TRANSMITTER_CONTROL = 1,
	DMUB_CMD__VBIOS_SET_PIXEL_CLOCK = 2,
	DMUB_CMD__VBIOS_ENABLE_DISP_POWER_GATING = 3,
	DMUB_CMD__VBIOS_LVTMA_CONTROL = 15,
};

//==============================================================================
//</DMUB_VBIOS>=================================================================
//==============================================================================
//< DMUB_GPINT>=================================================================
//==============================================================================

/**
 * The shifts and masks below may alternatively be used to format and read
 * the command register bits.
 */

#define DMUB_GPINT_DATA_PARAM_MASK 0xFFFF
#define DMUB_GPINT_DATA_PARAM_SHIFT 0

#define DMUB_GPINT_DATA_COMMAND_CODE_MASK 0xFFF
#define DMUB_GPINT_DATA_COMMAND_CODE_SHIFT 16

#define DMUB_GPINT_DATA_STATUS_MASK 0xF
#define DMUB_GPINT_DATA_STATUS_SHIFT 28

/**
 * Command responses.
 */

#define DMUB_GPINT__STOP_FW_RESPONSE 0xDEADDEAD

/**
 * The register format for sending a command via the GPINT.
 */
union dmub_gpint_data_register {
	struct {
		uint32_t param : 16;
		uint32_t command_code : 12;
		uint32_t status : 4;
	} bits;
	uint32_t all;
};

/*
 * Command IDs should be treated as stable ABI.
 * Do not reuse or modify IDs.
 */

enum dmub_gpint_command {
	DMUB_GPINT__INVALID_COMMAND = 0,
	DMUB_GPINT__GET_FW_VERSION = 1,
	DMUB_GPINT__STOP_FW = 2,
	DMUB_GPINT__GET_PSR_STATE = 7,
	/**
	 * DESC: Notifies DMCUB of the currently active streams.
	 * ARGS: Stream mask, 1 bit per active stream index.
	 */
	DMUB_GPINT__IDLE_OPT_NOTIFY_STREAM_MASK = 8,
	DMUB_GPINT__PSR_RESIDENCY = 9,
};

//==============================================================================
//</DMUB_GPINT>=================================================================
//==============================================================================
//< DMUB_CMD>===================================================================
//==============================================================================

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
	DMUB_CMD__MALL = 65,
	DMUB_CMD__ABM = 66,
	DMUB_CMD__HW_LOCK = 69,
	DMUB_CMD__DP_AUX_ACCESS = 70,
	DMUB_CMD__OUTBOX1_ENABLE = 71,
	DMUB_CMD__VBIOS = 128,
};

enum dmub_out_cmd_type {
	DMUB_OUT_CMD__NULL = 0,
	DMUB_OUT_CMD__DP_AUX_REPLY = 1,
	DMUB_OUT_CMD__DP_HPD_NOTIFY = 2,
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

struct dmub_rb_cmd_mall {
	struct dmub_cmd_header header;
	union dmub_addr cursor_copy_src;
	union dmub_addr cursor_copy_dst;
	uint32_t tmr_delay;
	uint32_t tmr_scale;
	uint16_t cursor_width;
	uint16_t cursor_pitch;
	uint16_t cursor_height;
	uint8_t cursor_bpp;
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

enum dp_aux_request_action {
	DP_AUX_REQ_ACTION_I2C_WRITE		= 0x00,
	DP_AUX_REQ_ACTION_I2C_READ		= 0x10,
	DP_AUX_REQ_ACTION_I2C_STATUS_REQ	= 0x20,
	DP_AUX_REQ_ACTION_I2C_WRITE_MOT		= 0x40,
	DP_AUX_REQ_ACTION_I2C_READ_MOT		= 0x50,
	DP_AUX_REQ_ACTION_I2C_STATUS_REQ_MOT	= 0x60,
	DP_AUX_REQ_ACTION_DPCD_WRITE		= 0x80,
	DP_AUX_REQ_ACTION_DPCD_READ		= 0x90
};

enum aux_return_code_type {
	AUX_RET_SUCCESS = 0,
	AUX_RET_ERROR_TIMEOUT,
	AUX_RET_ERROR_NO_DATA,
	AUX_RET_ERROR_INVALID_OPERATION,
	AUX_RET_ERROR_PROTOCOL_ERROR,
};

/* DP AUX command */
struct aux_transaction_parameters {
	uint8_t is_i2c_over_aux;
	uint8_t action;
	uint8_t length;
	uint8_t pad;
	uint32_t address;
	uint8_t data[16];
};

struct dmub_cmd_dp_aux_control_data {
	uint32_t handle;
	uint8_t port_index;
	uint8_t sw_crc_enabled;
	uint16_t timeout;
	struct aux_transaction_parameters dpaux;
};

struct dmub_rb_cmd_dp_aux_access {
	struct dmub_cmd_header header;
	struct dmub_cmd_dp_aux_control_data aux_control;
};

struct dmub_rb_cmd_outbox1_enable {
	struct dmub_cmd_header header;
	uint32_t enable;
};

/* DP AUX Reply command - OutBox Cmd */
struct aux_reply_data {
	uint8_t command;
	uint8_t length;
	uint8_t pad[2];
	uint8_t data[16];
};

struct aux_reply_control_data {
	uint32_t handle;
	uint8_t phy_port_index;
	uint8_t result;
	uint16_t pad;
};

struct dmub_rb_cmd_dp_aux_reply {
	struct dmub_cmd_header header;
	struct aux_reply_control_data control;
	struct aux_reply_data reply_data;
};

/* DP HPD Notify command - OutBox Cmd */
enum dp_hpd_type {
	DP_HPD = 0,
	DP_IRQ
};

enum dp_hpd_status {
	DP_HPD_UNPLUG = 0,
	DP_HPD_PLUG
};

struct dp_hpd_data {
	uint8_t phy_port_index;
	uint8_t hpd_type;
	uint8_t hpd_status;
	uint8_t pad;
};

struct dmub_rb_cmd_dp_hpd_notify {
	struct dmub_cmd_header header;
	struct dp_hpd_data hpd_data;
};

/*
 * Command IDs should be treated as stable ABI.
 * Do not reuse or modify IDs.
 */

enum dmub_cmd_psr_type {
	DMUB_CMD__PSR_SET_VERSION		= 0,
	DMUB_CMD__PSR_COPY_SETTINGS		= 1,
	DMUB_CMD__PSR_ENABLE			= 2,
	DMUB_CMD__PSR_DISABLE			= 3,
	DMUB_CMD__PSR_SET_LEVEL			= 4,
	DMUB_CMD__PSR_FORCE_STATIC		= 5,
};

enum psr_version {
	PSR_VERSION_1				= 0,
	PSR_VERSION_UNSUPPORTED			= 0xFFFFFFFF,
};

enum dmub_cmd_mall_type {
	DMUB_CMD__MALL_ACTION_ALLOW = 0,
	DMUB_CMD__MALL_ACTION_DISALLOW = 1,
	DMUB_CMD__MALL_ACTION_COPY_CURSOR = 2,
};

struct dmub_cmd_psr_copy_settings_data {
	union dmub_psr_debug_flags debug;
	uint16_t psr_level;
	uint8_t dpp_inst;
	uint8_t mpcc_inst;
	uint8_t opp_inst;
	uint8_t otg_inst;
	uint8_t digfe_inst;
	uint8_t digbe_inst;
	uint8_t dpphy_inst;
	uint8_t aux_inst;
	uint8_t smu_optimizations_en;
	uint8_t frame_delay;
	uint8_t frame_cap_ind;
	uint8_t pad[3];
	uint16_t init_sdp_deadline;
	uint16_t pad2;
};

struct dmub_rb_cmd_psr_copy_settings {
	struct dmub_cmd_header header;
	struct dmub_cmd_psr_copy_settings_data psr_copy_settings_data;
};

struct dmub_cmd_psr_set_level_data {
	uint16_t psr_level;
	uint8_t pad[2];
};

struct dmub_rb_cmd_psr_set_level {
	struct dmub_cmd_header header;
	struct dmub_cmd_psr_set_level_data psr_set_level_data;
};

struct dmub_rb_cmd_psr_enable {
	struct dmub_cmd_header header;
};

struct dmub_cmd_psr_set_version_data {
	enum psr_version version; // PSR version 1 or 2
};

struct dmub_rb_cmd_psr_set_version {
	struct dmub_cmd_header header;
	struct dmub_cmd_psr_set_version_data psr_set_version_data;
};

struct dmub_rb_cmd_psr_force_static {
	struct dmub_cmd_header header;
};

union dmub_hw_lock_flags {
	struct {
		uint8_t lock_pipe   : 1;
		uint8_t lock_cursor : 1;
		uint8_t lock_dig    : 1;
		uint8_t triple_buffer_lock : 1;
	} bits;

	uint8_t u8All;
};

struct dmub_hw_lock_inst_flags {
	uint8_t otg_inst;
	uint8_t opp_inst;
	uint8_t dig_inst;
	uint8_t pad;
};

enum hw_lock_client {
	HW_LOCK_CLIENT_DRIVER = 0,
	HW_LOCK_CLIENT_FW,
	HW_LOCK_CLIENT_INVALID = 0xFFFFFFFF,
};

struct dmub_cmd_lock_hw_data {
	enum hw_lock_client client;
	struct dmub_hw_lock_inst_flags inst_flags;
	union dmub_hw_lock_flags hw_locks;
	uint8_t lock;
	uint8_t should_release;
	uint8_t pad;
};

struct dmub_rb_cmd_lock_hw {
	struct dmub_cmd_header header;
	struct dmub_cmd_lock_hw_data lock_hw_data;
};

enum dmub_cmd_abm_type {
	DMUB_CMD__ABM_INIT_CONFIG	= 0,
	DMUB_CMD__ABM_SET_PIPE		= 1,
	DMUB_CMD__ABM_SET_BACKLIGHT	= 2,
	DMUB_CMD__ABM_SET_LEVEL		= 3,
	DMUB_CMD__ABM_SET_AMBIENT_LEVEL	= 4,
	DMUB_CMD__ABM_SET_PWM_FRAC	= 5,
};

#define NUM_AMBI_LEVEL                  5
#define NUM_AGGR_LEVEL                  4
#define NUM_POWER_FN_SEGS               8
#define NUM_BL_CURVE_SEGS               16

/*
 * Parameters for ABM2.4 algorithm.
 * Padded explicitly to 32-bit boundary.
 */
struct abm_config_table {
	/* Parameters for crgb conversion */
	uint16_t crgb_thresh[NUM_POWER_FN_SEGS];                 // 0B
	uint16_t crgb_offset[NUM_POWER_FN_SEGS];                 // 15B
	uint16_t crgb_slope[NUM_POWER_FN_SEGS];                  // 31B

	/* Parameters for custom curve */
	uint16_t backlight_thresholds[NUM_BL_CURVE_SEGS];        // 47B
	uint16_t backlight_offsets[NUM_BL_CURVE_SEGS];           // 79B

	uint16_t ambient_thresholds_lux[NUM_AMBI_LEVEL];         // 111B
	uint16_t min_abm_backlight;                              // 121B

	uint8_t min_reduction[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL];   // 123B
	uint8_t max_reduction[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL];   // 143B
	uint8_t bright_pos_gain[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL]; // 163B
	uint8_t dark_pos_gain[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL];   // 183B
	uint8_t hybrid_factor[NUM_AGGR_LEVEL];                   // 203B
	uint8_t contrast_factor[NUM_AGGR_LEVEL];                 // 207B
	uint8_t deviation_gain[NUM_AGGR_LEVEL];                  // 211B
	uint8_t min_knee[NUM_AGGR_LEVEL];                        // 215B
	uint8_t max_knee[NUM_AGGR_LEVEL];                        // 219B
	uint8_t iir_curve[NUM_AMBI_LEVEL];                       // 223B
	uint8_t pad3[3];                                         // 228B
};

struct dmub_cmd_abm_set_pipe_data {
	uint8_t otg_inst;
	uint8_t panel_inst;
	uint8_t set_pipe_option;
	uint8_t ramping_boundary; // TODO: Remove this
};

struct dmub_rb_cmd_abm_set_pipe {
	struct dmub_cmd_header header;
	struct dmub_cmd_abm_set_pipe_data abm_set_pipe_data;
};

struct dmub_cmd_abm_set_backlight_data {
	uint32_t frame_ramp;
	uint32_t backlight_user_level;
};

struct dmub_rb_cmd_abm_set_backlight {
	struct dmub_cmd_header header;
	struct dmub_cmd_abm_set_backlight_data abm_set_backlight_data;
};

struct dmub_cmd_abm_set_level_data {
	uint32_t level;
};

struct dmub_rb_cmd_abm_set_level {
	struct dmub_cmd_header header;
	struct dmub_cmd_abm_set_level_data abm_set_level_data;
};

struct dmub_cmd_abm_set_ambient_level_data {
	uint32_t ambient_lux;
};

struct dmub_rb_cmd_abm_set_ambient_level {
	struct dmub_cmd_header header;
	struct dmub_cmd_abm_set_ambient_level_data abm_set_ambient_level_data;
};

struct dmub_cmd_abm_set_pwm_frac_data {
	uint32_t fractional_pwm;
};

struct dmub_rb_cmd_abm_set_pwm_frac {
	struct dmub_cmd_header header;
	struct dmub_cmd_abm_set_pwm_frac_data abm_set_pwm_frac_data;
};

struct dmub_cmd_abm_init_config_data {
	union dmub_addr src;
	uint16_t bytes;
};

struct dmub_rb_cmd_abm_init_config {
	struct dmub_cmd_header header;
	struct dmub_cmd_abm_init_config_data abm_init_config_data;
};

union dmub_rb_cmd {
	struct dmub_rb_cmd_lock_hw lock_hw;
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
	struct dmub_rb_cmd_psr_set_version psr_set_version;
	struct dmub_rb_cmd_psr_copy_settings psr_copy_settings;
	struct dmub_rb_cmd_psr_enable psr_enable;
	struct dmub_rb_cmd_psr_set_level psr_set_level;
	struct dmub_rb_cmd_psr_force_static psr_force_static;
	struct dmub_rb_cmd_PLAT_54186_wa PLAT_54186_wa;
	struct dmub_rb_cmd_mall mall;
	struct dmub_rb_cmd_abm_set_pipe abm_set_pipe;
	struct dmub_rb_cmd_abm_set_backlight abm_set_backlight;
	struct dmub_rb_cmd_abm_set_level abm_set_level;
	struct dmub_rb_cmd_abm_set_ambient_level abm_set_ambient_level;
	struct dmub_rb_cmd_abm_set_pwm_frac abm_set_pwm_frac;
	struct dmub_rb_cmd_abm_init_config abm_init_config;
	struct dmub_rb_cmd_dp_aux_access dp_aux_access;
	struct dmub_rb_cmd_outbox1_enable outbox1_enable;
};

union dmub_rb_out_cmd {
	struct dmub_rb_cmd_common cmd_common;
	struct dmub_rb_cmd_dp_aux_reply dp_aux_reply;
	struct dmub_rb_cmd_dp_hpd_notify dp_hpd_notify;
};
#pragma pack(pop)


//==============================================================================
//</DMUB_CMD>===================================================================
//==============================================================================
//< DMUB_RB>====================================================================
//==============================================================================

#if defined(__cplusplus)
extern "C" {
#endif

struct dmub_rb_init_params {
	void *ctx;
	void *base_address;
	uint32_t capacity;
	uint32_t read_ptr;
	uint32_t write_ptr;
};

struct dmub_rb {
	void *base_address;
	uint32_t data_count;
	uint32_t rptr;
	uint32_t wrpt;
	uint32_t capacity;

	void *ctx;
	void *dmub;
};


static inline bool dmub_rb_empty(struct dmub_rb *rb)
{
	return (rb->wrpt == rb->rptr);
}

static inline bool dmub_rb_full(struct dmub_rb *rb)
{
	uint32_t data_count;

	if (rb->wrpt >= rb->rptr)
		data_count = rb->wrpt - rb->rptr;
	else
		data_count = rb->capacity - (rb->rptr - rb->wrpt);

	return (data_count == (rb->capacity - DMUB_RB_CMD_SIZE));
}

static inline bool dmub_rb_push_front(struct dmub_rb *rb,
				      const union dmub_rb_cmd *cmd)
{
	uint64_t volatile *dst = (uint64_t volatile *)(rb->base_address) + rb->wrpt / sizeof(uint64_t);
	const uint64_t *src = (const uint64_t *)cmd;
	int i;

	if (dmub_rb_full(rb))
		return false;

	// copying data
	for (i = 0; i < DMUB_RB_CMD_SIZE / sizeof(uint64_t); i++)
		*dst++ = *src++;

	rb->wrpt += DMUB_RB_CMD_SIZE;

	if (rb->wrpt >= rb->capacity)
		rb->wrpt %= rb->capacity;

	return true;
}

static inline bool dmub_rb_out_push_front(struct dmub_rb *rb,
				      const union dmub_rb_out_cmd *cmd)
{
	uint8_t *dst = (uint8_t *)(rb->base_address) + rb->wrpt;
	const uint8_t *src = (uint8_t *)cmd;

	if (dmub_rb_full(rb))
		return false;

	dmub_memcpy(dst, src, DMUB_RB_CMD_SIZE);

	rb->wrpt += DMUB_RB_CMD_SIZE;

	if (rb->wrpt >= rb->capacity)
		rb->wrpt %= rb->capacity;

	return true;
}

static inline bool dmub_rb_front(struct dmub_rb *rb,
				 union dmub_rb_cmd  *cmd)
{
	uint8_t *rd_ptr = (uint8_t *)rb->base_address + rb->rptr;

	if (dmub_rb_empty(rb))
		return false;

	dmub_memcpy(cmd, rd_ptr, DMUB_RB_CMD_SIZE);

	return true;
}

static inline bool dmub_rb_out_front(struct dmub_rb *rb,
				 union dmub_rb_out_cmd  *cmd)
{
	const uint64_t volatile *src = (const uint64_t volatile *)(rb->base_address) + rb->rptr / sizeof(uint64_t);
	uint64_t *dst = (uint64_t *)cmd;
	int i;

	if (dmub_rb_empty(rb))
		return false;

	// copying data
	for (i = 0; i < DMUB_RB_CMD_SIZE / sizeof(uint64_t); i++)
		*dst++ = *src++;

	return true;
}

static inline bool dmub_rb_pop_front(struct dmub_rb *rb)
{
	if (dmub_rb_empty(rb))
		return false;

	rb->rptr += DMUB_RB_CMD_SIZE;

	if (rb->rptr >= rb->capacity)
		rb->rptr %= rb->capacity;

	return true;
}

static inline void dmub_rb_flush_pending(const struct dmub_rb *rb)
{
	uint32_t rptr = rb->rptr;
	uint32_t wptr = rb->wrpt;

	while (rptr != wptr) {
		uint64_t volatile *data = (uint64_t volatile *)rb->base_address + rptr / sizeof(uint64_t);
		int i;

		for (i = 0; i < DMUB_RB_CMD_SIZE / sizeof(uint64_t); i++)
			*data++;

		rptr += DMUB_RB_CMD_SIZE;
		if (rptr >= rb->capacity)
			rptr %= rb->capacity;
	}
}

static inline void dmub_rb_init(struct dmub_rb *rb,
				struct dmub_rb_init_params *init_params)
{
	rb->base_address = init_params->base_address;
	rb->capacity = init_params->capacity;
	rb->rptr = init_params->read_ptr;
	rb->wrpt = init_params->write_ptr;
}

#if defined(__cplusplus)
}
#endif

//==============================================================================
//</DMUB_RB>====================================================================
//==============================================================================

#endif /* _DMUB_CMD_H_ */
