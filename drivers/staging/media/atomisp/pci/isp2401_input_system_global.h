/* SPDX-License-Identifier: GPL-2.0 */
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

/* CSI reveiver has 3 ports. */
#define		N_CSI_PORTS (3)

#include "system_local.h"
#include "isys_dma_global.h"	/*	isys2401_dma_channel,
				 *	isys2401_dma_cfg_t
				 */

#include "ibuf_ctrl_local.h"	/*	ibuf_cfg_t,
				 *	ibuf_ctrl_cfg_t
				 */

#include "isys_stream2mmio.h"	/*	stream2mmio_cfg_t */

#include "csi_rx.h"		/*	csi_rx_frontend_cfg_t,
				 *	csi_rx_backend_cfg_t,
				 *	csi_rx_backend_lut_entry_t
				 */
#include "pixelgen.h"

#define INPUT_SYSTEM_N_STREAM_ID  6	/* maximum number of simultaneous
					virtual channels supported*/

typedef enum {
	INPUT_SYSTEM_SOURCE_TYPE_UNDEFINED = 0,
	INPUT_SYSTEM_SOURCE_TYPE_SENSOR,
	INPUT_SYSTEM_SOURCE_TYPE_TPG,
	INPUT_SYSTEM_SOURCE_TYPE_PRBS,
	N_INPUT_SYSTEM_SOURCE_TYPE
} input_system_source_type_t;

typedef struct input_system_channel_s input_system_channel_t;
struct input_system_channel_s {
	stream2mmio_ID_t	stream2mmio_id;
	stream2mmio_sid_ID_t	stream2mmio_sid_id;

	ibuf_ctrl_ID_t		ibuf_ctrl_id;
	isp2401_ib_buffer_t	ib_buffer;

	isys2401_dma_ID_t	dma_id;
	isys2401_dma_channel	dma_channel;
};

typedef struct input_system_channel_cfg_s input_system_channel_cfg_t;
struct input_system_channel_cfg_s {
	stream2mmio_cfg_t	stream2mmio_cfg;
	ibuf_ctrl_cfg_t		ibuf_ctrl_cfg;
	isys2401_dma_cfg_t	dma_cfg;
	isys2401_dma_port_cfg_t	dma_src_port_cfg;
	isys2401_dma_port_cfg_t	dma_dest_port_cfg;
};

typedef struct input_system_input_port_s input_system_input_port_t;
struct input_system_input_port_s {
	input_system_source_type_t	source_type;

	struct {
		csi_rx_frontend_ID_t		frontend_id;
		csi_rx_backend_ID_t		backend_id;
		csi_mipi_packet_type_t		packet_type;
		csi_rx_backend_lut_entry_t	backend_lut_entry;
	} csi_rx;

	struct {
		csi_mipi_packet_type_t		packet_type;
		csi_rx_backend_lut_entry_t	backend_lut_entry;
	} metadata;

	struct {
		pixelgen_ID_t			pixelgen_id;
	} pixelgen;
};

typedef struct input_system_input_port_cfg_s input_system_input_port_cfg_t;
struct input_system_input_port_cfg_s {
	struct {
		csi_rx_frontend_cfg_t	frontend_cfg;
		csi_rx_backend_cfg_t	backend_cfg;
		csi_rx_backend_cfg_t	md_backend_cfg;
	} csi_rx_cfg;

	struct {
		pixelgen_tpg_cfg_t	tpg_cfg;
		pixelgen_prbs_cfg_t	prbs_cfg;
	} pixelgen_cfg;
};

typedef struct isp2401_input_system_cfg_s isp2401_input_system_cfg_t;
struct isp2401_input_system_cfg_s {
	input_system_input_port_ID_t	input_port_id;

	input_system_source_type_t	mode;

	bool online;
	bool raw_packed;
	s8 linked_isys_stream_id;

	struct {
		bool	comp_enable;
		s32	active_lanes;
		s32	fmt_type;
		s32	ch_id;
		s32 comp_predictor;
		s32 comp_scheme;
	} csi_port_attr;

	pixelgen_tpg_cfg_t	tpg_port_attr;

	pixelgen_prbs_cfg_t prbs_port_attr;

	struct {
		s32 align_req_in_bytes;
		s32 bits_per_pixel;
		s32 pixels_per_line;
		s32 lines_per_frame;
	} input_port_resolution;

	struct {
		s32 left_padding;
		s32 max_isp_input_width;
	} output_port_attr;

	struct {
		bool    enable;
		s32 fmt_type;
		s32 align_req_in_bytes;
		s32 bits_per_pixel;
		s32 pixels_per_line;
		s32 lines_per_frame;
	} metadata;
};

typedef struct virtual_input_system_stream_s virtual_input_system_stream_t;
struct virtual_input_system_stream_s {
	u32 id;				/*Used when multiple MIPI data types and/or virtual channels are used.
								Must be unique within one CSI RX
								and lower than SH_CSS_MAX_ISYS_CHANNEL_NODES */
	u8 enable_metadata;
	input_system_input_port_t	input_port;
	input_system_channel_t		channel;
	input_system_channel_t		md_channel; /* metadata channel */
	u8 online;
	s8 linked_isys_stream_id;
	u8 valid;
};

typedef struct virtual_input_system_stream_cfg_s
	virtual_input_system_stream_cfg_t;
struct virtual_input_system_stream_cfg_s {
	u8 enable_metadata;
	input_system_input_port_cfg_t	input_port_cfg;
	input_system_channel_cfg_t	channel_cfg;
	input_system_channel_cfg_t	md_channel_cfg;
	u8 valid;
};

#define ISP_INPUT_BUF_START_ADDR	0
#define NUM_OF_INPUT_BUF		2
#define NUM_OF_LINES_PER_BUF		2
#define LINES_OF_ISP_INPUT_BUF		(NUM_OF_INPUT_BUF * NUM_OF_LINES_PER_BUF)
#define ISP_INPUT_BUF_STRIDE		SH_CSS_MAX_SENSOR_WIDTH
