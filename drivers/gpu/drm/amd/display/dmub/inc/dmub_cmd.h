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

#if defined(_TEST_HARNESS) || defined(FPGA_USB4)
#include "dmub_fw_types.h"
#include "include_legacy/atomfirmware.h"

#if defined(_TEST_HARNESS)
#include <string.h>
#endif
#else

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <stdarg.h>

#include "atomfirmware.h"

#endif // defined(_TEST_HARNESS) || defined(FPGA_USB4)

/* Firmware versioning. */
#ifdef DMUB_EXPOSE_VERSION
#define DMUB_FW_VERSION_GIT_HASH 0x920aff8b2
#define DMUB_FW_VERSION_MAJOR 0
#define DMUB_FW_VERSION_MINOR 0
#define DMUB_FW_VERSION_REVISION 55
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

#define __forceinline inline

/**
 * Flag from driver to indicate that ABM should be disabled gradually
 * by slowly reversing all backlight programming and pixel compensation.
 */
#define SET_ABM_PIPE_GRADUALLY_DISABLE           0

/**
 * Flag from driver to indicate that ABM should be disabled immediately
 * and undo all backlight programming and pixel compensation.
 */
#define SET_ABM_PIPE_IMMEDIATELY_DISABLE         255

/**
 * Flag from driver to indicate that ABM should be disabled immediately
 * and keep the current backlight programming and pixel compensation.
 */
#define SET_ABM_PIPE_IMMEDIATE_KEEP_GAIN_DISABLE 254

/**
 * Flag from driver to set the current ABM pipe index or ABM operating level.
 */
#define SET_ABM_PIPE_NORMAL                      1

/**
 * Number of ambient light levels in ABM algorithm.
 */
#define NUM_AMBI_LEVEL                  5

/**
 * Number of operating/aggression levels in ABM algorithm.
 */
#define NUM_AGGR_LEVEL                  4

/**
 * Number of segments in the gamma curve.
 */
#define NUM_POWER_FN_SEGS               8

/**
 * Number of segments in the backlight curve.
 */
#define NUM_BL_CURVE_SEGS               16

/* Maximum number of streams on any ASIC. */
#define DMUB_MAX_STREAMS 6

/* Maximum number of planes on any ASIC. */
#define DMUB_MAX_PLANES 6

/* Trace buffer offset for entry */
#define TRACE_BUFFER_ENTRY_OFFSET  16

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

/**
 * Flags that can be set by driver to change some PSR behaviour.
 */
union dmub_psr_debug_flags {
	/**
	 * Debug flags.
	 */
	struct {
		/**
		 * Enable visual confirm in FW.
		 */
		uint32_t visual_confirm : 1;
		/**
		 * Use HW Lock Mgr object to do HW locking in FW.
		 */
		uint32_t use_hw_lock_mgr : 1;

		/**
		 * Unused.
		 * TODO: Remove.
		 */
		uint32_t log_line_nums : 1;
	} bitfields;

	/**
	 * Union for debug flags.
	 */
	uint32_t u32All;
};

/**
 * DMUB feature capabilities.
 * After DMUB init, driver will query FW capabilities prior to enabling certain features.
 */
struct dmub_feature_caps {
	/**
	 * Max PSR version supported by FW.
	 */
	uint8_t psr;

	/**
	 * Reserved.
	 */
	uint8_t reserved[7];
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
//< DMUB Trace Buffer>================================================================
//==============================================================================
typedef uint32_t dmub_trace_code_t;

struct dmcub_trace_buf_entry {
	dmub_trace_code_t trace_code;
	uint32_t tick_count;
	uint32_t param0;
	uint32_t param1;
};

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
		uint32_t skip_phy_init_panel_sequence: 1;
		uint32_t reserved : 26;
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
	/**
	 * DESC: Get PSR state from FW.
	 * RETURN: PSR state enum. This enum may need to be converted to the legacy PSR state value.
	 */
	DMUB_GPINT__GET_PSR_STATE = 7,
	/**
	 * DESC: Notifies DMCUB of the currently active streams.
	 * ARGS: Stream mask, 1 bit per active stream index.
	 */
	DMUB_GPINT__IDLE_OPT_NOTIFY_STREAM_MASK = 8,
	/**
	 * DESC: Start PSR residency counter. Stop PSR resdiency counter and get value.
	 * ARGS: We can measure residency from various points. The argument will specify the residency mode.
	 *       By default, it is measured from after we powerdown the PHY, to just before we powerup the PHY.
	 * RETURN: PSR residency in milli-percent.
	 */
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
	/**
	 * Command type used to query FW feature caps.
	 */
	DMUB_CMD__QUERY_FEATURE_CAPS = 6,
	/**
	 * Command type used for all PSR commands.
	 */
	DMUB_CMD__PSR = 64,
	DMUB_CMD__MALL = 65,
	/**
	 * Command type used for all ABM commands.
	 */
	DMUB_CMD__ABM = 66,
	/**
	 * Command type used for HW locking in FW.
	 */
	DMUB_CMD__HW_LOCK = 69,
	/**
	 * Command type used to access DP AUX.
	 */
	DMUB_CMD__DP_AUX_ACCESS = 70,
	/**
	 * Command type used for OUTBOX1 notification enable
	 */
	DMUB_CMD__OUTBOX1_ENABLE = 71,
	DMUB_CMD__VBIOS = 128,
};

enum dmub_out_cmd_type {
	DMUB_OUT_CMD__NULL = 0,
	/**
	 * Command type used for DP AUX Reply data notification
	 */
	DMUB_OUT_CMD__DP_AUX_REPLY = 1,
	/**
	 * Command type used for DP HPD event notification
	 */
	DMUB_OUT_CMD__DP_HPD_NOTIFY = 2,
};

#pragma pack(push, 1)

struct dmub_cmd_header {
	unsigned int type : 8;
	unsigned int sub_type : 8;
	unsigned int ret_status : 1;
	unsigned int reserved0 : 7;
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
	uint8_t debug_bits;

	uint8_t reserved1;
	uint8_t reserved2;
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

struct dmub_dig_transmitter_control_data_v1_7 {
	uint8_t phyid; /**< 0=UNIPHYA, 1=UNIPHYB, 2=UNIPHYC, 3=UNIPHYD, 4=UNIPHYE, 5=UNIPHYF */
	uint8_t action; /**< Defined as ATOM_TRANSMITER_ACTION_xxx */
	union {
		uint8_t digmode; /**< enum atom_encode_mode_def */
		uint8_t dplaneset; /**< DP voltage swing and pre-emphasis value, "DP_LANE_SET__xDB_y_zV" */
	} mode_laneset;
	uint8_t lanenum; /**< Number of lanes */
	union {
		uint32_t symclk_10khz; /**< Symbol Clock in 10Khz */
	} symclk_units;
	uint8_t hpdsel; /**< =1: HPD1, =2: HPD2, ..., =6: HPD6, =0: HPD is not assigned */
	uint8_t digfe_sel; /**< DIG front-end selection, bit0 means DIG0 FE is enabled */
	uint8_t connobj_id; /**< Connector Object Id defined in ObjectId.h */
	uint8_t reserved0; /**< For future use */
	uint8_t reserved1; /**< For future use */
	uint8_t reserved2[3]; /**< For future use */
	uint32_t reserved3[11]; /**< For future use */
};

union dmub_cmd_dig1_transmitter_control_data {
	struct dig_transmitter_control_parameters_v1_6 dig;
	struct dmub_dig_transmitter_control_data_v1_7 dig_v1_7;
};

struct dmub_rb_cmd_dig1_transmitter_control {
	struct dmub_cmd_header header;
	union dmub_cmd_dig1_transmitter_control_data transmitter_control;
};

struct dmub_rb_cmd_dpphy_init {
	struct dmub_cmd_header header;
	uint8_t reserved[60];
};

/**
 * enum dp_aux_request_action - DP AUX request command listing.
 *
 * 4 AUX request command bits are shifted to high nibble.
 */
enum dp_aux_request_action {
	/** I2C-over-AUX write request */
	DP_AUX_REQ_ACTION_I2C_WRITE		= 0x00,
	/** I2C-over-AUX read request */
	DP_AUX_REQ_ACTION_I2C_READ		= 0x10,
	/** I2C-over-AUX write status request */
	DP_AUX_REQ_ACTION_I2C_STATUS_REQ	= 0x20,
	/** I2C-over-AUX write request with MOT=1 */
	DP_AUX_REQ_ACTION_I2C_WRITE_MOT		= 0x40,
	/** I2C-over-AUX read request with MOT=1 */
	DP_AUX_REQ_ACTION_I2C_READ_MOT		= 0x50,
	/** I2C-over-AUX write status request with MOT=1 */
	DP_AUX_REQ_ACTION_I2C_STATUS_REQ_MOT	= 0x60,
	/** Native AUX write request */
	DP_AUX_REQ_ACTION_DPCD_WRITE		= 0x80,
	/** Native AUX read request */
	DP_AUX_REQ_ACTION_DPCD_READ		= 0x90
};

/**
 * enum aux_return_code_type - DP AUX process return code listing.
 */
enum aux_return_code_type {
	/** AUX process succeeded */
	AUX_RET_SUCCESS = 0,
	/** AUX process failed with unknown reason */
	AUX_RET_ERROR_UNKNOWN,
	/** AUX process completed with invalid reply */
	AUX_RET_ERROR_INVALID_REPLY,
	/** AUX process timed out */
	AUX_RET_ERROR_TIMEOUT,
	/** HPD was low during AUX process */
	AUX_RET_ERROR_HPD_DISCON,
	/** Failed to acquire AUX engine */
	AUX_RET_ERROR_ENGINE_ACQUIRE,
	/** AUX request not supported */
	AUX_RET_ERROR_INVALID_OPERATION,
	/** AUX process not available */
	AUX_RET_ERROR_PROTOCOL_ERROR,
};

/**
 * enum aux_channel_type - DP AUX channel type listing.
 */
enum aux_channel_type {
	/** AUX thru Legacy DP AUX */
	AUX_CHANNEL_LEGACY_DDC,
	/** AUX thru DPIA DP tunneling */
	AUX_CHANNEL_DPIA
};

/**
 * struct aux_transaction_parameters - DP AUX request transaction data
 */
struct aux_transaction_parameters {
	uint8_t is_i2c_over_aux; /**< 0=native AUX, 1=I2C-over-AUX */
	uint8_t action; /**< enum dp_aux_request_action */
	uint8_t length; /**< DP AUX request data length */
	uint8_t reserved; /**< For future use */
	uint32_t address; /**< DP AUX address */
	uint8_t data[16]; /**< DP AUX write data */
};

/**
 * Data passed from driver to FW in a DMUB_CMD__DP_AUX_ACCESS command.
 */
struct dmub_cmd_dp_aux_control_data {
	uint8_t instance; /**< AUX instance or DPIA instance */
	uint8_t manual_acq_rel_enable; /**< manual control for acquiring or releasing AUX channel */
	uint8_t sw_crc_enabled; /**< Use software CRC for tunneling packet instead of hardware CRC */
	uint8_t reserved0; /**< For future use */
	uint16_t timeout; /**< timeout time in us */
	uint16_t reserved1; /**< For future use */
	enum aux_channel_type type; /**< enum aux_channel_type */
	struct aux_transaction_parameters dpaux; /**< struct aux_transaction_parameters */
};

/**
 * Definition of a DMUB_CMD__DP_AUX_ACCESS command.
 */
struct dmub_rb_cmd_dp_aux_access {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__DP_AUX_ACCESS command.
	 */
	struct dmub_cmd_dp_aux_control_data aux_control;
};

/**
 * Definition of a DMUB_CMD__OUTBOX1_ENABLE command.
 */
struct dmub_rb_cmd_outbox1_enable {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 *  enable: 0x0 -> disable outbox1 notification (default value)
	 *			0x1 -> enable outbox1 notification
	 */
	uint32_t enable;
};

/* DP AUX Reply command - OutBox Cmd */
/**
 * Data passed to driver from FW in a DMUB_OUT_CMD__DP_AUX_REPLY command.
 */
struct aux_reply_data {
	/**
	 * Aux cmd
	 */
	uint8_t command;
	/**
	 * Aux reply data length (max: 16 bytes)
	 */
	uint8_t length;
	/**
	 * Alignment only
	 */
	uint8_t pad[2];
	/**
	 * Aux reply data
	 */
	uint8_t data[16];
};

/**
 * Control Data passed to driver from FW in a DMUB_OUT_CMD__DP_AUX_REPLY command.
 */
struct aux_reply_control_data {
	/**
	 * Reserved for future use
	 */
	uint32_t handle;
	/**
	 * Aux Instance
	 */
	uint8_t instance;
	/**
	 * Aux transaction result: definition in enum aux_return_code_type
	 */
	uint8_t result;
	/**
	 * Alignment only
	 */
	uint16_t pad;
};

/**
 * Definition of a DMUB_OUT_CMD__DP_AUX_REPLY command.
 */
struct dmub_rb_cmd_dp_aux_reply {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Control Data passed to driver from FW in a DMUB_OUT_CMD__DP_AUX_REPLY command.
	 */
	struct aux_reply_control_data control;
	/**
	 * Data passed to driver from FW in a DMUB_OUT_CMD__DP_AUX_REPLY command.
	 */
	struct aux_reply_data reply_data;
};

/* DP HPD Notify command - OutBox Cmd */
/**
 * DP HPD Type
 */
enum dp_hpd_type {
	/**
	 * Normal DP HPD
	 */
	DP_HPD = 0,
	/**
	 * DP HPD short pulse
	 */
	DP_IRQ
};

/**
 * DP HPD Status
 */
enum dp_hpd_status {
	/**
	 * DP_HPD status low
	 */
	DP_HPD_UNPLUG = 0,
	/**
	 * DP_HPD status high
	 */
	DP_HPD_PLUG
};

/**
 * Data passed to driver from FW in a DMUB_OUT_CMD__DP_HPD_NOTIFY command.
 */
struct dp_hpd_data {
	/**
	 * DP HPD instance
	 */
	uint8_t instance;
	/**
	 * HPD type
	 */
	uint8_t hpd_type;
	/**
	 * HPD status: only for type: DP_HPD to indicate status
	 */
	uint8_t hpd_status;
	/**
	 * Alignment only
	 */
	uint8_t pad;
};

/**
 * Definition of a DMUB_OUT_CMD__DP_HPD_NOTIFY command.
 */
struct dmub_rb_cmd_dp_hpd_notify {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed to driver from FW in a DMUB_OUT_CMD__DP_HPD_NOTIFY command.
	 */
	struct dp_hpd_data hpd_data;
};

/*
 * Command IDs should be treated as stable ABI.
 * Do not reuse or modify IDs.
 */

/**
 * PSR command sub-types.
 */
enum dmub_cmd_psr_type {
	/**
	 * Set PSR version support.
	 */
	DMUB_CMD__PSR_SET_VERSION		= 0,
	/**
	 * Copy driver-calculated parameters to PSR state.
	 */
	DMUB_CMD__PSR_COPY_SETTINGS		= 1,
	/**
	 * Enable PSR.
	 */
	DMUB_CMD__PSR_ENABLE			= 2,

	/**
	 * Disable PSR.
	 */
	DMUB_CMD__PSR_DISABLE			= 3,

	/**
	 * Set PSR level.
	 * PSR level is a 16-bit value dicated by driver that
	 * will enable/disable different functionality.
	 */
	DMUB_CMD__PSR_SET_LEVEL			= 4,

	/**
	 * Forces PSR enabled until an explicit PSR disable call.
	 */
	DMUB_CMD__PSR_FORCE_STATIC		= 5,
};

/**
 * PSR versions.
 */
enum psr_version {
	/**
	 * PSR version 1.
	 */
	PSR_VERSION_1				= 0,
	/**
	 * PSR not supported.
	 */
	PSR_VERSION_UNSUPPORTED			= 0xFFFFFFFF,
};

enum dmub_cmd_mall_type {
	DMUB_CMD__MALL_ACTION_ALLOW = 0,
	DMUB_CMD__MALL_ACTION_DISALLOW = 1,
	DMUB_CMD__MALL_ACTION_COPY_CURSOR = 2,
	DMUB_CMD__MALL_ACTION_NO_DF_REQ = 3,
};

/**
 * Data passed from driver to FW in a DMUB_CMD__PSR_COPY_SETTINGS command.
 */
struct dmub_cmd_psr_copy_settings_data {
	/**
	 * Flags that can be set by driver to change some PSR behaviour.
	 */
	union dmub_psr_debug_flags debug;
	/**
	 * 16-bit value dicated by driver that will enable/disable different functionality.
	 */
	uint16_t psr_level;
	/**
	 * DPP HW instance.
	 */
	uint8_t dpp_inst;
	/**
	 * MPCC HW instance.
	 * Not used in dmub fw,
	 * dmub fw will get active opp by reading odm registers.
	 */
	uint8_t mpcc_inst;
	/**
	 * OPP HW instance.
	 * Not used in dmub fw,
	 * dmub fw will get active opp by reading odm registers.
	 */
	uint8_t opp_inst;
	/**
	 * OTG HW instance.
	 */
	uint8_t otg_inst;
	/**
	 * DIG FE HW instance.
	 */
	uint8_t digfe_inst;
	/**
	 * DIG BE HW instance.
	 */
	uint8_t digbe_inst;
	/**
	 * DP PHY HW instance.
	 */
	uint8_t dpphy_inst;
	/**
	 * AUX HW instance.
	 */
	uint8_t aux_inst;
	/**
	 * Determines if SMU optimzations are enabled/disabled.
	 */
	uint8_t smu_optimizations_en;
	/**
	 * Unused.
	 * TODO: Remove.
	 */
	uint8_t frame_delay;
	/**
	 * If RFB setup time is greater than the total VBLANK time,
	 * it is not possible for the sink to capture the video frame
	 * in the same frame the SDP is sent. In this case,
	 * the frame capture indication bit should be set and an extra
	 * static frame should be transmitted to the sink.
	 */
	uint8_t frame_cap_ind;
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];
	/**
	 * Multi-display optimizations are implemented on certain ASICs.
	 */
	uint8_t multi_disp_optimizations_en;
	/**
	 * The last possible line SDP may be transmitted without violating
	 * the RFB setup time or entering the active video frame.
	 */
	uint16_t init_sdp_deadline;
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint16_t pad2;
	/**
	 * Length of each horizontal line in us.
	 */
	uint32_t line_time_in_us;
};

/**
 * Definition of a DMUB_CMD__PSR_COPY_SETTINGS command.
 */
struct dmub_rb_cmd_psr_copy_settings {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__PSR_COPY_SETTINGS command.
	 */
	struct dmub_cmd_psr_copy_settings_data psr_copy_settings_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__PSR_SET_LEVEL command.
 */
struct dmub_cmd_psr_set_level_data {
	/**
	 * 16-bit value dicated by driver that will enable/disable different functionality.
	 */
	uint16_t psr_level;
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad[2];
};

/**
 * Definition of a DMUB_CMD__PSR_SET_LEVEL command.
 */
struct dmub_rb_cmd_psr_set_level {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Definition of a DMUB_CMD__PSR_SET_LEVEL command.
	 */
	struct dmub_cmd_psr_set_level_data psr_set_level_data;
};

/**
 * Definition of a DMUB_CMD__PSR_ENABLE command.
 * PSR enable/disable is controlled using the sub_type.
 */
struct dmub_rb_cmd_psr_enable {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__PSR_SET_VERSION command.
 */
struct dmub_cmd_psr_set_version_data {
	/**
	 * PSR version that FW should implement.
	 */
	enum psr_version version;
};

/**
 * Definition of a DMUB_CMD__PSR_SET_VERSION command.
 */
struct dmub_rb_cmd_psr_set_version {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__PSR_SET_VERSION command.
	 */
	struct dmub_cmd_psr_set_version_data psr_set_version_data;
};

/**
 * Definition of a DMUB_CMD__PSR_FORCE_STATIC command.
 */
struct dmub_rb_cmd_psr_force_static {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
};

/**
 * Set of HW components that can be locked.
 */
union dmub_hw_lock_flags {
	/**
	 * Set of HW components that can be locked.
	 */
	struct {
		/**
		 * Lock/unlock OTG master update lock.
		 */
		uint8_t lock_pipe   : 1;
		/**
		 * Lock/unlock cursor.
		 */
		uint8_t lock_cursor : 1;
		/**
		 * Lock/unlock global update lock.
		 */
		uint8_t lock_dig    : 1;
		/**
		 * Triple buffer lock requires additional hw programming to usual OTG master lock.
		 */
		uint8_t triple_buffer_lock : 1;
	} bits;

	/**
	 * Union for HW Lock flags.
	 */
	uint8_t u8All;
};

/**
 * Instances of HW to be locked.
 */
struct dmub_hw_lock_inst_flags {
	/**
	 * OTG HW instance for OTG master update lock.
	 */
	uint8_t otg_inst;
	/**
	 * OPP instance for cursor lock.
	 */
	uint8_t opp_inst;
	/**
	 * OTG HW instance for global update lock.
	 * TODO: Remove, and re-use otg_inst.
	 */
	uint8_t dig_inst;
	/**
	 * Explicit pad to 4 byte boundary.
	 */
	uint8_t pad;
};

/**
 * Clients that can acquire the HW Lock Manager.
 */
enum hw_lock_client {
	/**
	 * Driver is the client of HW Lock Manager.
	 */
	HW_LOCK_CLIENT_DRIVER = 0,
	/**
	 * FW is the client of HW Lock Manager.
	 */
	HW_LOCK_CLIENT_FW,
	/**
	 * Invalid client.
	 */
	HW_LOCK_CLIENT_INVALID = 0xFFFFFFFF,
};

/**
 * Data passed to HW Lock Mgr in a DMUB_CMD__HW_LOCK command.
 */
struct dmub_cmd_lock_hw_data {
	/**
	 * Specifies the client accessing HW Lock Manager.
	 */
	enum hw_lock_client client;
	/**
	 * HW instances to be locked.
	 */
	struct dmub_hw_lock_inst_flags inst_flags;
	/**
	 * Which components to be locked.
	 */
	union dmub_hw_lock_flags hw_locks;
	/**
	 * Specifies lock/unlock.
	 */
	uint8_t lock;
	/**
	 * HW can be unlocked separately from releasing the HW Lock Mgr.
	 * This flag is set if the client wishes to release the object.
	 */
	uint8_t should_release;
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad;
};

/**
 * Definition of a DMUB_CMD__HW_LOCK command.
 * Command is used by driver and FW.
 */
struct dmub_rb_cmd_lock_hw {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed to HW Lock Mgr in a DMUB_CMD__HW_LOCK command.
	 */
	struct dmub_cmd_lock_hw_data lock_hw_data;
};

/**
 * ABM command sub-types.
 */
enum dmub_cmd_abm_type {
	/**
	 * Initialize parameters for ABM algorithm.
	 * Data is passed through an indirect buffer.
	 */
	DMUB_CMD__ABM_INIT_CONFIG	= 0,
	/**
	 * Set OTG and panel HW instance.
	 */
	DMUB_CMD__ABM_SET_PIPE		= 1,
	/**
	 * Set user requested backklight level.
	 */
	DMUB_CMD__ABM_SET_BACKLIGHT	= 2,
	/**
	 * Set ABM operating/aggression level.
	 */
	DMUB_CMD__ABM_SET_LEVEL		= 3,
	/**
	 * Set ambient light level.
	 */
	DMUB_CMD__ABM_SET_AMBIENT_LEVEL	= 4,
	/**
	 * Enable/disable fractional duty cycle for backlight PWM.
	 */
	DMUB_CMD__ABM_SET_PWM_FRAC	= 5,
};

/**
 * Parameters for ABM2.4 algorithm. Passed from driver to FW via an indirect buffer.
 * Requirements:
 *  - Padded explicitly to 32-bit boundary.
 *  - Must ensure this structure matches the one on driver-side,
 *    otherwise it won't be aligned.
 */
struct abm_config_table {
	/**
	 * Gamma curve thresholds, used for crgb conversion.
	 */
	uint16_t crgb_thresh[NUM_POWER_FN_SEGS];                 // 0B
	/**
	 * Gamma curve offsets, used for crgb conversion.
	 */
	uint16_t crgb_offset[NUM_POWER_FN_SEGS];                 // 16B
	/**
	 * Gamma curve slopes, used for crgb conversion.
	 */
	uint16_t crgb_slope[NUM_POWER_FN_SEGS];                  // 32B
	/**
	 * Custom backlight curve thresholds.
	 */
	uint16_t backlight_thresholds[NUM_BL_CURVE_SEGS];        // 48B
	/**
	 * Custom backlight curve offsets.
	 */
	uint16_t backlight_offsets[NUM_BL_CURVE_SEGS];           // 78B
	/**
	 * Ambient light thresholds.
	 */
	uint16_t ambient_thresholds_lux[NUM_AMBI_LEVEL];         // 112B
	/**
	 * Minimum programmable backlight.
	 */
	uint16_t min_abm_backlight;                              // 122B
	/**
	 * Minimum reduction values.
	 */
	uint8_t min_reduction[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL];   // 124B
	/**
	 * Maximum reduction values.
	 */
	uint8_t max_reduction[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL];   // 144B
	/**
	 * Bright positive gain.
	 */
	uint8_t bright_pos_gain[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL]; // 164B
	/**
	 * Dark negative gain.
	 */
	uint8_t dark_pos_gain[NUM_AMBI_LEVEL][NUM_AGGR_LEVEL];   // 184B
	/**
	 * Hybrid factor.
	 */
	uint8_t hybrid_factor[NUM_AGGR_LEVEL];                   // 204B
	/**
	 * Contrast factor.
	 */
	uint8_t contrast_factor[NUM_AGGR_LEVEL];                 // 208B
	/**
	 * Deviation gain.
	 */
	uint8_t deviation_gain[NUM_AGGR_LEVEL];                  // 212B
	/**
	 * Minimum knee.
	 */
	uint8_t min_knee[NUM_AGGR_LEVEL];                        // 216B
	/**
	 * Maximum knee.
	 */
	uint8_t max_knee[NUM_AGGR_LEVEL];                        // 220B
	/**
	 * Unused.
	 */
	uint8_t iir_curve[NUM_AMBI_LEVEL];                       // 224B
	/**
	 * Explicit padding to 4 byte boundary.
	 */
	uint8_t pad3[3];                                         // 229B
	/**
	 * Backlight ramp reduction.
	 */
	uint16_t blRampReduction[NUM_AGGR_LEVEL];                // 232B
	/**
	 * Backlight ramp start.
	 */
	uint16_t blRampStart[NUM_AGGR_LEVEL];                    // 240B
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_PIPE command.
 */
struct dmub_cmd_abm_set_pipe_data {
	/**
	 * OTG HW instance.
	 */
	uint8_t otg_inst;

	/**
	 * Panel Control HW instance.
	 */
	uint8_t panel_inst;

	/**
	 * Controls how ABM will interpret a set pipe or set level command.
	 */
	uint8_t set_pipe_option;

	/**
	 * Unused.
	 * TODO: Remove.
	 */
	uint8_t ramping_boundary;
};

/**
 * Definition of a DMUB_CMD__ABM_SET_PIPE command.
 */
struct dmub_rb_cmd_abm_set_pipe {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_PIPE command.
	 */
	struct dmub_cmd_abm_set_pipe_data abm_set_pipe_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_BACKLIGHT command.
 */
struct dmub_cmd_abm_set_backlight_data {
	/**
	 * Number of frames to ramp to backlight user level.
	 */
	uint32_t frame_ramp;

	/**
	 * Requested backlight level from user.
	 */
	uint32_t backlight_user_level;
};

/**
 * Definition of a DMUB_CMD__ABM_SET_BACKLIGHT command.
 */
struct dmub_rb_cmd_abm_set_backlight {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_BACKLIGHT command.
	 */
	struct dmub_cmd_abm_set_backlight_data abm_set_backlight_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_LEVEL command.
 */
struct dmub_cmd_abm_set_level_data {
	/**
	 * Set current ABM operating/aggression level.
	 */
	uint32_t level;
};

/**
 * Definition of a DMUB_CMD__ABM_SET_LEVEL command.
 */
struct dmub_rb_cmd_abm_set_level {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_LEVEL command.
	 */
	struct dmub_cmd_abm_set_level_data abm_set_level_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_AMBIENT_LEVEL command.
 */
struct dmub_cmd_abm_set_ambient_level_data {
	/**
	 * Ambient light sensor reading from OS.
	 */
	uint32_t ambient_lux;
};

/**
 * Definition of a DMUB_CMD__ABM_SET_AMBIENT_LEVEL command.
 */
struct dmub_rb_cmd_abm_set_ambient_level {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_AMBIENT_LEVEL command.
	 */
	struct dmub_cmd_abm_set_ambient_level_data abm_set_ambient_level_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_PWM_FRAC command.
 */
struct dmub_cmd_abm_set_pwm_frac_data {
	/**
	 * Enable/disable fractional duty cycle for backlight PWM.
	 * TODO: Convert to uint8_t.
	 */
	uint32_t fractional_pwm;
};

/**
 * Definition of a DMUB_CMD__ABM_SET_PWM_FRAC command.
 */
struct dmub_rb_cmd_abm_set_pwm_frac {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_SET_PWM_FRAC command.
	 */
	struct dmub_cmd_abm_set_pwm_frac_data abm_set_pwm_frac_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__ABM_INIT_CONFIG command.
 */
struct dmub_cmd_abm_init_config_data {
	/**
	 * Location of indirect buffer used to pass init data to ABM.
	 */
	union dmub_addr src;

	/**
	 * Indirect buffer length.
	 */
	uint16_t bytes;
};

/**
 * Definition of a DMUB_CMD__ABM_INIT_CONFIG command.
 */
struct dmub_rb_cmd_abm_init_config {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;

	/**
	 * Data passed from driver to FW in a DMUB_CMD__ABM_INIT_CONFIG command.
	 */
	struct dmub_cmd_abm_init_config_data abm_init_config_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__QUERY_FEATURE_CAPS command.
 */
struct dmub_cmd_query_feature_caps_data {
	/**
	 * DMUB feature capabilities.
	 * After DMUB init, driver will query FW capabilities prior to enabling certain features.
	 */
	struct dmub_feature_caps feature_caps;
};

/**
 * Definition of a DMUB_CMD__QUERY_FEATURE_CAPS command.
 */
struct dmub_rb_cmd_query_feature_caps {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__QUERY_FEATURE_CAPS command.
	 */
	struct dmub_cmd_query_feature_caps_data query_feature_caps_data;
};

/**
 * Data passed from driver to FW in a DMUB_CMD__VBIOS_LVTMA_CONTROL command.
 */
struct dmub_cmd_lvtma_control_data {
	uint8_t uc_pwr_action; /**< LVTMA_ACTION */
	uint8_t reserved_0[3]; /**< For future use */
	uint8_t panel_inst; /**< LVTMA control instance */
	uint8_t reserved_1[3]; /**< For future use */
};

/**
 * Definition of a DMUB_CMD__VBIOS_LVTMA_CONTROL command.
 */
struct dmub_rb_cmd_lvtma_control {
	/**
	 * Command header.
	 */
	struct dmub_cmd_header header;
	/**
	 * Data passed from driver to FW in a DMUB_CMD__VBIOS_LVTMA_CONTROL command.
	 */
	struct dmub_cmd_lvtma_control_data data;
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
	/**
	 * Definition of a DMUB_CMD__PSR_SET_VERSION command.
	 */
	struct dmub_rb_cmd_psr_set_version psr_set_version;
	/**
	 * Definition of a DMUB_CMD__PSR_COPY_SETTINGS command.
	 */
	struct dmub_rb_cmd_psr_copy_settings psr_copy_settings;
	/**
	 * Definition of a DMUB_CMD__PSR_ENABLE command.
	 */
	struct dmub_rb_cmd_psr_enable psr_enable;
	/**
	 * Definition of a DMUB_CMD__PSR_SET_LEVEL command.
	 */
	struct dmub_rb_cmd_psr_set_level psr_set_level;
	/**
	 * Definition of a DMUB_CMD__PSR_FORCE_STATIC command.
	 */
	struct dmub_rb_cmd_psr_force_static psr_force_static;
	struct dmub_rb_cmd_PLAT_54186_wa PLAT_54186_wa;
	struct dmub_rb_cmd_mall mall;
	/**
	 * Definition of a DMUB_CMD__ABM_SET_PIPE command.
	 */
	struct dmub_rb_cmd_abm_set_pipe abm_set_pipe;

	/**
	 * Definition of a DMUB_CMD__ABM_SET_BACKLIGHT command.
	 */
	struct dmub_rb_cmd_abm_set_backlight abm_set_backlight;

	/**
	 * Definition of a DMUB_CMD__ABM_SET_LEVEL command.
	 */
	struct dmub_rb_cmd_abm_set_level abm_set_level;

	/**
	 * Definition of a DMUB_CMD__ABM_SET_AMBIENT_LEVEL command.
	 */
	struct dmub_rb_cmd_abm_set_ambient_level abm_set_ambient_level;

	/**
	 * Definition of a DMUB_CMD__ABM_SET_PWM_FRAC command.
	 */
	struct dmub_rb_cmd_abm_set_pwm_frac abm_set_pwm_frac;

	/**
	 * Definition of a DMUB_CMD__ABM_INIT_CONFIG command.
	 */
	struct dmub_rb_cmd_abm_init_config abm_init_config;

	/**
	 * Definition of a DMUB_CMD__DP_AUX_ACCESS command.
	 */
	struct dmub_rb_cmd_dp_aux_access dp_aux_access;

	struct dmub_rb_cmd_outbox1_enable outbox1_enable;
	/**
	 * Definition of a DMUB_CMD__qyert command.
	 */
	struct dmub_rb_cmd_query_feature_caps query_feature_caps;
	/**
	 * Definition of a DMUB_CMD__VBIOS_LVTMA_CONTROL command.
	 */
	struct dmub_rb_cmd_lvtma_control lvtma_control;
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
	uint8_t i;

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
				 union dmub_rb_cmd  **cmd)
{
	uint8_t *rb_cmd = (uint8_t *)(rb->base_address) + rb->rptr;

	if (dmub_rb_empty(rb))
		return false;

	*cmd = (union dmub_rb_cmd *)rb_cmd;

	return true;
}

static inline bool dmub_rb_out_front(struct dmub_rb *rb,
				 union dmub_rb_out_cmd  *cmd)
{
	const uint64_t volatile *src = (const uint64_t volatile *)(rb->base_address) + rb->rptr / sizeof(uint64_t);
	uint64_t *dst = (uint64_t *)cmd;
	uint8_t i;

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
		uint8_t i;

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

static inline void dmub_rb_get_return_data(struct dmub_rb *rb,
					   union dmub_rb_cmd *cmd)
{
	// Copy rb entry back into command
	uint8_t *rd_ptr = (rb->rptr == 0) ?
		(uint8_t *)rb->base_address + rb->capacity - DMUB_RB_CMD_SIZE :
		(uint8_t *)rb->base_address + rb->rptr - DMUB_RB_CMD_SIZE;

	dmub_memcpy(cmd, rd_ptr, DMUB_RB_CMD_SIZE);
}

#if defined(__cplusplus)
}
#endif

//==============================================================================
//</DMUB_RB>====================================================================
//==============================================================================

#endif /* _DMUB_CMD_H_ */
