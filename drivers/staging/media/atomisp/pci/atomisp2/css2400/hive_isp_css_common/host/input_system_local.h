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

#ifndef __INPUT_SYSTEM_LOCAL_H_INCLUDED__
#define __INPUT_SYSTEM_LOCAL_H_INCLUDED__

#include <type_support.h>

#include "input_system_global.h"

#include "input_system_defs.h"		/* HIVE_ISYS_GPREG_MULTICAST_A_IDX,... */
#include "css_receiver_2400_defs.h"	/* _HRT_CSS_RECEIVER_2400_TWO_PIXEL_EN_REG_IDX, _HRT_CSS_RECEIVER_2400_CSI2_FUNC_PROG_REG_IDX,... */
#if defined(IS_ISP_2400_MAMOIADA_SYSTEM)
#include "isp_capture_defs.h"
#elif defined(IS_ISP_2401_MAMOIADA_SYSTEM)
/* Same name, but keep the distinction,it is a different device */
#include "isp_capture_defs.h"
#else
#error "input_system_local.h: 2400_SYSTEM must be one of {2400, 2401 }"
#endif
#include "isp_acquisition_defs.h"
#include "input_system_ctrl_defs.h"


typedef enum {
	INPUT_SYSTEM_ERR_NO_ERROR = 0,
	INPUT_SYSTEM_ERR_GENERIC,
	INPUT_SYSTEM_ERR_CHANNEL_ALREADY_SET,
	INPUT_SYSTEM_ERR_CONFLICT_ON_RESOURCE,
	INPUT_SYSTEM_ERR_PARAMETER_NOT_SUPPORTED,
	N_INPUT_SYSTEM_ERR
} input_system_error_t;

typedef enum {
	INPUT_SYSTEM_PORT_A = 0,
	INPUT_SYSTEM_PORT_B,
	INPUT_SYSTEM_PORT_C,
	N_INPUT_SYSTEM_PORTS
} input_system_csi_port_t;

typedef struct ctrl_unit_cfg_s			ctrl_unit_cfg_t;
typedef struct input_system_network_cfg_s	input_system_network_cfg_t;
typedef struct target_cfg2400_s 		target_cfg2400_t;
typedef struct channel_cfg_s 			channel_cfg_t;
typedef struct backend_channel_cfg_s 		backend_channel_cfg_t;
typedef struct input_system_cfg2400_s 		input_system_cfg2400_t;
typedef struct mipi_port_state_s		mipi_port_state_t;
typedef struct rx_channel_state_s		rx_channel_state_t;
typedef struct input_switch_cfg_channel_s 	input_switch_cfg_channel_t;
typedef struct input_switch_cfg_s 		input_switch_cfg_t;

struct ctrl_unit_cfg_s {
	ib_buffer_t		buffer_mipi[N_CAPTURE_UNIT_ID];
	ib_buffer_t		buffer_acquire[N_ACQUISITION_UNIT_ID];
};

struct input_system_network_cfg_s {
	input_system_connection_t	multicast_cfg[N_CAPTURE_UNIT_ID];
	input_system_multiplex_t	mux_cfg;
	ctrl_unit_cfg_t				ctrl_unit_cfg[N_CTRL_UNIT_ID];
};

typedef struct {
// TBD.
	uint32_t 	dummy_parameter;
} target_isp_cfg_t;


typedef struct {
// TBD.
	uint32_t 	dummy_parameter;
} target_sp_cfg_t;


typedef struct {
// TBD.
	uint32_t 	dummy_parameter;
} target_strm2mem_cfg_t;

struct input_switch_cfg_channel_s {
	uint32_t hsync_data_reg[2];
	uint32_t vsync_data_reg;
};

struct target_cfg2400_s {
	input_switch_cfg_channel_t 		input_switch_channel_cfg;
	target_isp_cfg_t	target_isp_cfg;
	target_sp_cfg_t		target_sp_cfg;
	target_strm2mem_cfg_t	target_strm2mem_cfg;
};

struct backend_channel_cfg_s {
	uint32_t	fmt_control_word_1; // Format config.
	uint32_t	fmt_control_word_2;
	uint32_t	no_side_band;
};

typedef union  {
	csi_cfg_t	csi_cfg;
	tpg_cfg_t	tpg_cfg;
	prbs_cfg_t	prbs_cfg;
	gpfifo_cfg_t	gpfifo_cfg;
} source_cfg_t;


struct input_switch_cfg_s {
	uint32_t hsync_data_reg[N_RX_CHANNEL_ID * 2];
	uint32_t vsync_data_reg;
};

// Configuration of a channel.
struct channel_cfg_s {
	uint32_t		ch_id;
	backend_channel_cfg_t	backend_ch;
	input_system_source_t	source_type;
	source_cfg_t		source_cfg;
	target_cfg2400_t	target_cfg;
};


// Complete configuration for input system.
struct input_system_cfg2400_s {

	input_system_source_t source_type;				input_system_config_flags_t	source_type_flags;
	//channel_cfg_t		channel[N_CHANNELS];
	input_system_config_flags_t	ch_flags[N_CHANNELS];
	//  This is the place where the buffers' settings are collected, as given.
	csi_cfg_t			csi_value[N_CSI_PORTS];		input_system_config_flags_t	csi_flags[N_CSI_PORTS];

	// Possible another struct for ib.
	// This buffers set at the end, based on the all configurations.
	ib_buffer_t			csi_buffer[N_CSI_PORTS];	input_system_config_flags_t	csi_buffer_flags[N_CSI_PORTS];
	ib_buffer_t			acquisition_buffer_unique;	input_system_config_flags_t	acquisition_buffer_unique_flags;
	uint32_t			unallocated_ib_mem_words; // Used for check.DEFAULT = IB_CAPACITY_IN_WORDS.
	//uint32_t			acq_allocated_ib_mem_words;

	input_system_connection_t		multicast[N_CSI_PORTS];
	input_system_multiplex_t		multiplexer;   					input_system_config_flags_t		multiplexer_flags;


	tpg_cfg_t			tpg_value;			input_system_config_flags_t	tpg_flags;
	prbs_cfg_t			prbs_value;			input_system_config_flags_t	prbs_flags;
	gpfifo_cfg_t		gpfifo_value;		input_system_config_flags_t	gpfifo_flags;


	input_switch_cfg_t		input_switch_cfg;


	target_isp_cfg_t		target_isp      [N_CHANNELS];	input_system_config_flags_t	target_isp_flags      [N_CHANNELS];
	target_sp_cfg_t			target_sp       [N_CHANNELS];	input_system_config_flags_t	target_sp_flags       [N_CHANNELS];
	target_strm2mem_cfg_t	target_strm2mem [N_CHANNELS];	input_system_config_flags_t	target_strm2mem_flags [N_CHANNELS];

	input_system_config_flags_t		session_flags;

};

/*
 * For each MIPI port
 */
#define _HRT_CSS_RECEIVER_DEVICE_READY_REG_IDX			_HRT_CSS_RECEIVER_2400_DEVICE_READY_REG_IDX
#define _HRT_CSS_RECEIVER_IRQ_STATUS_REG_IDX			_HRT_CSS_RECEIVER_2400_IRQ_STATUS_REG_IDX
#define _HRT_CSS_RECEIVER_IRQ_ENABLE_REG_IDX			_HRT_CSS_RECEIVER_2400_IRQ_ENABLE_REG_IDX
#define _HRT_CSS_RECEIVER_TIMEOUT_COUNT_REG_IDX		    _HRT_CSS_RECEIVER_2400_CSI2_FUNC_PROG_REG_IDX
#define _HRT_CSS_RECEIVER_INIT_COUNT_REG_IDX			_HRT_CSS_RECEIVER_2400_INIT_COUNT_REG_IDX
/* new regs for each MIPI port w.r.t. 2300 */
#define _HRT_CSS_RECEIVER_RAW16_18_DATAID_REG_IDX       _HRT_CSS_RECEIVER_2400_RAW16_18_DATAID_REG_IDX
#define _HRT_CSS_RECEIVER_SYNC_COUNT_REG_IDX            _HRT_CSS_RECEIVER_2400_SYNC_COUNT_REG_IDX
#define _HRT_CSS_RECEIVER_RX_COUNT_REG_IDX              _HRT_CSS_RECEIVER_2400_RX_COUNT_REG_IDX

/* _HRT_CSS_RECEIVER_2400_COMP_FORMAT_REG_IDX is not defined per MIPI port but per channel */
/* _HRT_CSS_RECEIVER_2400_COMP_PREDICT_REG_IDX is not defined per MIPI port but per channel */
#define _HRT_CSS_RECEIVER_FS_TO_LS_DELAY_REG_IDX        _HRT_CSS_RECEIVER_2400_FS_TO_LS_DELAY_REG_IDX
#define _HRT_CSS_RECEIVER_LS_TO_DATA_DELAY_REG_IDX      _HRT_CSS_RECEIVER_2400_LS_TO_DATA_DELAY_REG_IDX
#define _HRT_CSS_RECEIVER_DATA_TO_LE_DELAY_REG_IDX      _HRT_CSS_RECEIVER_2400_DATA_TO_LE_DELAY_REG_IDX
#define _HRT_CSS_RECEIVER_LE_TO_FE_DELAY_REG_IDX        _HRT_CSS_RECEIVER_2400_LE_TO_FE_DELAY_REG_IDX
#define _HRT_CSS_RECEIVER_FE_TO_FS_DELAY_REG_IDX        _HRT_CSS_RECEIVER_2400_FE_TO_FS_DELAY_REG_IDX
#define _HRT_CSS_RECEIVER_LE_TO_LS_DELAY_REG_IDX        _HRT_CSS_RECEIVER_2400_LE_TO_LS_DELAY_REG_IDX
#define _HRT_CSS_RECEIVER_TWO_PIXEL_EN_REG_IDX			_HRT_CSS_RECEIVER_2400_TWO_PIXEL_EN_REG_IDX
#define _HRT_CSS_RECEIVER_BACKEND_RST_REG_IDX           _HRT_CSS_RECEIVER_2400_BACKEND_RST_REG_IDX
#define _HRT_CSS_RECEIVER_RAW18_REG_IDX                 _HRT_CSS_RECEIVER_2400_RAW18_REG_IDX
#define _HRT_CSS_RECEIVER_FORCE_RAW8_REG_IDX            _HRT_CSS_RECEIVER_2400_FORCE_RAW8_REG_IDX
#define _HRT_CSS_RECEIVER_RAW16_REG_IDX                 _HRT_CSS_RECEIVER_2400_RAW16_REG_IDX

/* Previously MIPI port regs, now 2x2 logical channel regs */
#define _HRT_CSS_RECEIVER_COMP_SCHEME_VC0_REG0_IDX		_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC0_REG0_IDX
#define _HRT_CSS_RECEIVER_COMP_SCHEME_VC0_REG1_IDX		_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC0_REG1_IDX
#define _HRT_CSS_RECEIVER_COMP_SCHEME_VC1_REG0_IDX		_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC1_REG0_IDX
#define _HRT_CSS_RECEIVER_COMP_SCHEME_VC1_REG1_IDX		_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC1_REG1_IDX
#define _HRT_CSS_RECEIVER_COMP_SCHEME_VC2_REG0_IDX		_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC2_REG0_IDX
#define _HRT_CSS_RECEIVER_COMP_SCHEME_VC2_REG1_IDX		_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC2_REG1_IDX
#define _HRT_CSS_RECEIVER_COMP_SCHEME_VC3_REG0_IDX		_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC3_REG0_IDX
#define _HRT_CSS_RECEIVER_COMP_SCHEME_VC3_REG1_IDX		_HRT_CSS_RECEIVER_2400_COMP_SCHEME_VC3_REG1_IDX

/* Second backend is at offset 0x0700 w.r.t. the first port at offset 0x0100 */
#define _HRT_CSS_BE_OFFSET                              448
#define _HRT_CSS_RECEIVER_BE_GSP_ACC_OVL_REG_IDX        (_HRT_CSS_RECEIVER_2400_BE_GSP_ACC_OVL_REG_IDX + _HRT_CSS_BE_OFFSET)
#define _HRT_CSS_RECEIVER_BE_SRST_REG_IDX               (_HRT_CSS_RECEIVER_2400_BE_SRST_REG_IDX + _HRT_CSS_BE_OFFSET)
#define _HRT_CSS_RECEIVER_BE_TWO_PPC_REG_IDX            (_HRT_CSS_RECEIVER_2400_BE_TWO_PPC_REG_IDX + _HRT_CSS_BE_OFFSET)
#define _HRT_CSS_RECEIVER_BE_COMP_FORMAT_REG0_IDX       (_HRT_CSS_RECEIVER_2400_BE_COMP_FORMAT_REG0_IDX + _HRT_CSS_BE_OFFSET)
#define _HRT_CSS_RECEIVER_BE_COMP_FORMAT_REG1_IDX       (_HRT_CSS_RECEIVER_2400_BE_COMP_FORMAT_REG1_IDX + _HRT_CSS_BE_OFFSET)
#define _HRT_CSS_RECEIVER_BE_COMP_FORMAT_REG2_IDX       (_HRT_CSS_RECEIVER_2400_BE_COMP_FORMAT_REG2_IDX + _HRT_CSS_BE_OFFSET)
#define _HRT_CSS_RECEIVER_BE_COMP_FORMAT_REG3_IDX       (_HRT_CSS_RECEIVER_2400_BE_COMP_FORMAT_REG3_IDX + _HRT_CSS_BE_OFFSET)
#define _HRT_CSS_RECEIVER_BE_SEL_REG_IDX                (_HRT_CSS_RECEIVER_2400_BE_SEL_REG_IDX + _HRT_CSS_BE_OFFSET)
#define _HRT_CSS_RECEIVER_BE_RAW16_CONFIG_REG_IDX       (_HRT_CSS_RECEIVER_2400_BE_RAW16_CONFIG_REG_IDX + _HRT_CSS_BE_OFFSET)
#define _HRT_CSS_RECEIVER_BE_RAW18_CONFIG_REG_IDX       (_HRT_CSS_RECEIVER_2400_BE_RAW18_CONFIG_REG_IDX + _HRT_CSS_BE_OFFSET)
#define _HRT_CSS_RECEIVER_BE_FORCE_RAW8_REG_IDX         (_HRT_CSS_RECEIVER_2400_BE_FORCE_RAW8_REG_IDX + _HRT_CSS_BE_OFFSET)
#define _HRT_CSS_RECEIVER_BE_IRQ_STATUS_REG_IDX         (_HRT_CSS_RECEIVER_2400_BE_IRQ_STATUS_REG_IDX + _HRT_CSS_BE_OFFSET)
#define _HRT_CSS_RECEIVER_BE_IRQ_CLEAR_REG_IDX          (_HRT_CSS_RECEIVER_2400_BE_IRQ_CLEAR_REG_IDX + _HRT_CSS_BE_OFFSET)


#define _HRT_CSS_RECEIVER_IRQ_OVERRUN_BIT		_HRT_CSS_RECEIVER_2400_IRQ_OVERRUN_BIT
#define _HRT_CSS_RECEIVER_IRQ_INIT_TIMEOUT_BIT		_HRT_CSS_RECEIVER_2400_IRQ_RESERVED_BIT
#define _HRT_CSS_RECEIVER_IRQ_SLEEP_MODE_ENTRY_BIT	_HRT_CSS_RECEIVER_2400_IRQ_SLEEP_MODE_ENTRY_BIT
#define _HRT_CSS_RECEIVER_IRQ_SLEEP_MODE_EXIT_BIT	_HRT_CSS_RECEIVER_2400_IRQ_SLEEP_MODE_EXIT_BIT
#define _HRT_CSS_RECEIVER_IRQ_ERR_SOT_HS_BIT		_HRT_CSS_RECEIVER_2400_IRQ_ERR_SOT_HS_BIT
#define _HRT_CSS_RECEIVER_IRQ_ERR_SOT_SYNC_HS_BIT	_HRT_CSS_RECEIVER_2400_IRQ_ERR_SOT_SYNC_HS_BIT
#define _HRT_CSS_RECEIVER_IRQ_ERR_CONTROL_BIT		_HRT_CSS_RECEIVER_2400_IRQ_ERR_CONTROL_BIT
#define _HRT_CSS_RECEIVER_IRQ_ERR_ECC_DOUBLE_BIT	_HRT_CSS_RECEIVER_2400_IRQ_ERR_ECC_DOUBLE_BIT
#define _HRT_CSS_RECEIVER_IRQ_ERR_ECC_CORRECTED_BIT	_HRT_CSS_RECEIVER_2400_IRQ_ERR_ECC_CORRECTED_BIT
#define _HRT_CSS_RECEIVER_IRQ_ERR_ECC_NO_CORRECTION_BIT	_HRT_CSS_RECEIVER_2400_IRQ_ERR_ECC_NO_CORRECTION_BIT
#define _HRT_CSS_RECEIVER_IRQ_ERR_CRC_BIT		_HRT_CSS_RECEIVER_2400_IRQ_ERR_CRC_BIT
#define _HRT_CSS_RECEIVER_IRQ_ERR_ID_BIT		_HRT_CSS_RECEIVER_2400_IRQ_ERR_ID_BIT
#define _HRT_CSS_RECEIVER_IRQ_ERR_FRAME_SYNC_BIT	_HRT_CSS_RECEIVER_2400_IRQ_ERR_FRAME_SYNC_BIT
#define _HRT_CSS_RECEIVER_IRQ_ERR_FRAME_DATA_BIT	_HRT_CSS_RECEIVER_2400_IRQ_ERR_FRAME_DATA_BIT
#define _HRT_CSS_RECEIVER_IRQ_DATA_TIMEOUT_BIT		_HRT_CSS_RECEIVER_2400_IRQ_DATA_TIMEOUT_BIT
#define _HRT_CSS_RECEIVER_IRQ_ERR_ESCAPE_BIT		_HRT_CSS_RECEIVER_2400_IRQ_ERR_ESCAPE_BIT
#define _HRT_CSS_RECEIVER_IRQ_ERR_LINE_SYNC_BIT		_HRT_CSS_RECEIVER_2400_IRQ_ERR_LINE_SYNC_BIT

#define _HRT_CSS_RECEIVER_FUNC_PROG_REG_IDX		_HRT_CSS_RECEIVER_2400_CSI2_FUNC_PROG_REG_IDX
#define	_HRT_CSS_RECEIVER_DATA_TIMEOUT_IDX		_HRT_CSS_RECEIVER_2400_CSI2_DATA_TIMEOUT_IDX
#define	_HRT_CSS_RECEIVER_DATA_TIMEOUT_BITS		_HRT_CSS_RECEIVER_2400_CSI2_DATA_TIMEOUT_BITS

typedef struct capture_unit_state_s	capture_unit_state_t;
typedef struct acquisition_unit_state_s	acquisition_unit_state_t;
typedef struct ctrl_unit_state_s	ctrl_unit_state_t;

/*
 * In 2300 ports can be configured independently and stream
 * formats need to be specified. In 2400, there are only 8
 * supported configurations but the HW is fused to support
 * only a single one.
 *
 * In 2300 the compressed format types are programmed by the
 * user. In 2400 all stream formats are encoded on the stream.
 *
 * Use the enum to check validity of a user configuration
 */
typedef enum {
	MONO_4L_1L_0L = 0,
	MONO_3L_1L_0L,
	MONO_2L_1L_0L,
	MONO_1L_1L_0L,
	STEREO_2L_1L_2L,
	STEREO_3L_1L_1L,
	STEREO_2L_1L_1L,
	STEREO_1L_1L_1L,
	N_RX_MODE
} rx_mode_t;

typedef enum {
	MIPI_PREDICTOR_NONE = 0,
	MIPI_PREDICTOR_TYPE1,
	MIPI_PREDICTOR_TYPE2,
	N_MIPI_PREDICTOR_TYPES
} mipi_predictor_t;

typedef enum {
	MIPI_COMPRESSOR_NONE = 0,
	MIPI_COMPRESSOR_10_6_10,
	MIPI_COMPRESSOR_10_7_10,
	MIPI_COMPRESSOR_10_8_10,
	MIPI_COMPRESSOR_12_6_12,
	MIPI_COMPRESSOR_12_7_12,
	MIPI_COMPRESSOR_12_8_12,
	N_MIPI_COMPRESSOR_METHODS
} mipi_compressor_t;

typedef enum {
	MIPI_FORMAT_RGB888 = 0,
	MIPI_FORMAT_RGB555,
	MIPI_FORMAT_RGB444,
	MIPI_FORMAT_RGB565,
	MIPI_FORMAT_RGB666,
	MIPI_FORMAT_RAW8,		/* 5 */
	MIPI_FORMAT_RAW10,
	MIPI_FORMAT_RAW6,
	MIPI_FORMAT_RAW7,
	MIPI_FORMAT_RAW12,
	MIPI_FORMAT_RAW14,		/* 10 */
	MIPI_FORMAT_YUV420_8,
	MIPI_FORMAT_YUV420_10,
	MIPI_FORMAT_YUV422_8,
	MIPI_FORMAT_YUV422_10,
	MIPI_FORMAT_CUSTOM0,	/* 15 */
	MIPI_FORMAT_YUV420_8_LEGACY,
	MIPI_FORMAT_EMBEDDED,
	MIPI_FORMAT_CUSTOM1,
	MIPI_FORMAT_CUSTOM2,
	MIPI_FORMAT_CUSTOM3,	/* 20 */
	MIPI_FORMAT_CUSTOM4,
	MIPI_FORMAT_CUSTOM5,
	MIPI_FORMAT_CUSTOM6,
	MIPI_FORMAT_CUSTOM7,
	MIPI_FORMAT_YUV420_8_SHIFT,	/* 25 */
	MIPI_FORMAT_YUV420_10_SHIFT,
	MIPI_FORMAT_RAW16,
	MIPI_FORMAT_RAW18,
	N_MIPI_FORMAT,
} mipi_format_t;

#define MIPI_FORMAT_JPEG		MIPI_FORMAT_CUSTOM0
#define MIPI_FORMAT_BINARY_8	MIPI_FORMAT_CUSTOM0
#define N_MIPI_FORMAT_CUSTOM	8

/* The number of stores for compressed format types */
#define	N_MIPI_COMPRESSOR_CONTEXT	(N_RX_CHANNEL_ID * N_MIPI_FORMAT_CUSTOM)

typedef enum {
	RX_IRQ_INFO_BUFFER_OVERRUN   = 1UL << _HRT_CSS_RECEIVER_IRQ_OVERRUN_BIT,
	RX_IRQ_INFO_INIT_TIMEOUT     = 1UL << _HRT_CSS_RECEIVER_IRQ_INIT_TIMEOUT_BIT,
	RX_IRQ_INFO_ENTER_SLEEP_MODE = 1UL << _HRT_CSS_RECEIVER_IRQ_SLEEP_MODE_ENTRY_BIT,
	RX_IRQ_INFO_EXIT_SLEEP_MODE  = 1UL << _HRT_CSS_RECEIVER_IRQ_SLEEP_MODE_EXIT_BIT,
	RX_IRQ_INFO_ECC_CORRECTED    = 1UL << _HRT_CSS_RECEIVER_IRQ_ERR_ECC_CORRECTED_BIT,
	RX_IRQ_INFO_ERR_SOT          = 1UL << _HRT_CSS_RECEIVER_IRQ_ERR_SOT_HS_BIT,
	RX_IRQ_INFO_ERR_SOT_SYNC     = 1UL << _HRT_CSS_RECEIVER_IRQ_ERR_SOT_SYNC_HS_BIT,
	RX_IRQ_INFO_ERR_CONTROL      = 1UL << _HRT_CSS_RECEIVER_IRQ_ERR_CONTROL_BIT,
	RX_IRQ_INFO_ERR_ECC_DOUBLE   = 1UL << _HRT_CSS_RECEIVER_IRQ_ERR_ECC_DOUBLE_BIT,
/*	RX_IRQ_INFO_NO_ERR           = 1UL << _HRT_CSS_RECEIVER_IRQ_ERR_ECC_NO_CORRECTION_BIT, */
	RX_IRQ_INFO_ERR_CRC          = 1UL << _HRT_CSS_RECEIVER_IRQ_ERR_CRC_BIT,
	RX_IRQ_INFO_ERR_UNKNOWN_ID   = 1UL << _HRT_CSS_RECEIVER_IRQ_ERR_ID_BIT,
	RX_IRQ_INFO_ERR_FRAME_SYNC   = 1UL << _HRT_CSS_RECEIVER_IRQ_ERR_FRAME_SYNC_BIT,
	RX_IRQ_INFO_ERR_FRAME_DATA   = 1UL << _HRT_CSS_RECEIVER_IRQ_ERR_FRAME_DATA_BIT,
	RX_IRQ_INFO_ERR_DATA_TIMEOUT = 1UL << _HRT_CSS_RECEIVER_IRQ_DATA_TIMEOUT_BIT,
	RX_IRQ_INFO_ERR_UNKNOWN_ESC  = 1UL << _HRT_CSS_RECEIVER_IRQ_ERR_ESCAPE_BIT,
	RX_IRQ_INFO_ERR_LINE_SYNC    = 1UL << _HRT_CSS_RECEIVER_IRQ_ERR_LINE_SYNC_BIT,
}  rx_irq_info_t;

typedef struct rx_cfg_s		rx_cfg_t;

/*
 * Applied per port
 */
struct rx_cfg_s {
	rx_mode_t			mode;	/* The HW config */
	mipi_port_ID_t		port;	/* The port ID to apply the control on */
	unsigned int		timeout;
	unsigned int		initcount;
	unsigned int		synccount;
	unsigned int		rxcount;
	mipi_predictor_t	comp;	/* Just for backward compatibility */
	bool                is_two_ppc;
};

/* NOTE: The base has already an offset of 0x0100 */
static const hrt_address MIPI_PORT_OFFSET[N_MIPI_PORT_ID] = {
	0x00000000UL,
	0x00000100UL,
	0x00000200UL};

static const mipi_lane_cfg_t MIPI_PORT_MAXLANES[N_MIPI_PORT_ID] = {
	MIPI_4LANE_CFG,
	MIPI_1LANE_CFG,
	MIPI_2LANE_CFG};

static const bool MIPI_PORT_ACTIVE[N_RX_MODE][N_MIPI_PORT_ID] = {
	{true, true, false},
	{true, true, false},
	{true, true, false},
	{true, true, false},
	{true, true, true},
	{true, true, true},
	{true, true, true},
	{true, true, true}};

static const mipi_lane_cfg_t MIPI_PORT_LANES[N_RX_MODE][N_MIPI_PORT_ID] = {
	{MIPI_4LANE_CFG, MIPI_1LANE_CFG, MIPI_0LANE_CFG},
	{MIPI_3LANE_CFG, MIPI_1LANE_CFG, MIPI_0LANE_CFG},
	{MIPI_2LANE_CFG, MIPI_1LANE_CFG, MIPI_0LANE_CFG},
	{MIPI_1LANE_CFG, MIPI_1LANE_CFG, MIPI_0LANE_CFG},
	{MIPI_2LANE_CFG, MIPI_1LANE_CFG, MIPI_2LANE_CFG},
	{MIPI_3LANE_CFG, MIPI_1LANE_CFG, MIPI_1LANE_CFG},
	{MIPI_2LANE_CFG, MIPI_1LANE_CFG, MIPI_1LANE_CFG},
	{MIPI_1LANE_CFG, MIPI_1LANE_CFG, MIPI_1LANE_CFG}};

static const hrt_address SUB_SYSTEM_OFFSET[N_SUB_SYSTEM_ID] = {
	0x00001000UL,
	0x00002000UL,
	0x00003000UL,
	0x00004000UL,
	0x00005000UL,
	0x00009000UL,
	0x0000A000UL,
	0x0000B000UL,
	0x0000C000UL};

struct capture_unit_state_s {
	int	Packet_Length;
	int	Received_Length;
	int	Received_Short_Packets;
	int	Received_Long_Packets;
	int	Last_Command;
	int	Next_Command;
	int	Last_Acknowledge;
	int	Next_Acknowledge;
	int	FSM_State_Info;
	int	StartMode;
	int	Start_Addr;
	int	Mem_Region_Size;
	int	Num_Mem_Regions;
/*	int	Init;   write-only registers
	int	Start;
	int	Stop;      */
};

struct acquisition_unit_state_s {
/*	int	Init;   write-only register */
	int	Received_Short_Packets;
	int	Received_Long_Packets;
	int	Last_Command;
	int	Next_Command;
	int	Last_Acknowledge;
	int	Next_Acknowledge;
	int	FSM_State_Info;
	int	Int_Cntr_Info;
	int	Start_Addr;
	int	Mem_Region_Size;
	int	Num_Mem_Regions;
};

struct ctrl_unit_state_s {
	int	last_cmd;
	int	next_cmd;
	int	last_ack;
	int	next_ack;
	int	top_fsm_state;
	int	captA_fsm_state;
	int	captB_fsm_state;
	int	captC_fsm_state;
	int	acq_fsm_state;
	int	captA_start_addr;
	int	captB_start_addr;
	int	captC_start_addr;
	int	captA_mem_region_size;
	int	captB_mem_region_size;
	int	captC_mem_region_size;
	int	captA_num_mem_regions;
	int	captB_num_mem_regions;
	int	captC_num_mem_regions;
	int	acq_start_addr;
	int	acq_mem_region_size;
	int	acq_num_mem_regions;
/*	int	ctrl_init;  write only register */
	int	capt_reserve_one_mem_region;
};

struct input_system_state_s {
	int	str_multicastA_sel;
	int	str_multicastB_sel;
	int	str_multicastC_sel;
	int	str_mux_sel;
	int	str_mon_status;
	int	str_mon_irq_cond;
	int	str_mon_irq_en;
	int	isys_srst;
	int	isys_slv_reg_srst;
	int	str_deint_portA_cnt;
	int	str_deint_portB_cnt;
	struct capture_unit_state_s		capture_unit[N_CAPTURE_UNIT_ID];
	struct acquisition_unit_state_s	acquisition_unit[N_ACQUISITION_UNIT_ID];
	struct ctrl_unit_state_s		ctrl_unit_state[N_CTRL_UNIT_ID];
};

struct mipi_port_state_s {
	int	device_ready;
	int	irq_status;
	int	irq_enable;
	uint32_t	timeout_count;
	uint16_t	init_count;
	uint16_t	raw16_18;
	uint32_t	sync_count;		/*4 x uint8_t */
	uint32_t	rx_count;		/*4 x uint8_t */
	uint8_t		lane_sync_count[MIPI_4LANE_CFG];
	uint8_t		lane_rx_count[MIPI_4LANE_CFG];
};

struct rx_channel_state_s {
	uint32_t	comp_scheme0;
	uint32_t	comp_scheme1;
	mipi_predictor_t		pred[N_MIPI_FORMAT_CUSTOM];
	mipi_compressor_t		comp[N_MIPI_FORMAT_CUSTOM];
};

struct receiver_state_s {
	uint8_t	fs_to_ls_delay;
	uint8_t	ls_to_data_delay;
	uint8_t	data_to_le_delay;
	uint8_t	le_to_fe_delay;
	uint8_t	fe_to_fs_delay;
	uint8_t	le_to_fs_delay;
	bool	is_two_ppc;
	int	backend_rst;
	uint16_t	raw18;
	bool		force_raw8;
	uint16_t	raw16;
	struct mipi_port_state_s	mipi_port_state[N_MIPI_PORT_ID];
	struct rx_channel_state_s	rx_channel_state[N_RX_CHANNEL_ID];
	int	be_gsp_acc_ovl;
	int	be_srst;
	int	be_is_two_ppc;
	int	be_comp_format0;
	int	be_comp_format1;
	int	be_comp_format2;
	int	be_comp_format3;
	int	be_sel;
	int	be_raw16_config;
	int	be_raw18_config;
	int	be_force_raw8;
	int	be_irq_status;
	int	be_irq_clear;
};

#endif /* __INPUT_SYSTEM_LOCAL_H_INCLUDED__ */
