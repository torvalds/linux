#ifndef QDSP5VIDDECCMDI_H
#define QDSP5VIDDECCMDI_H

/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

    V I D E O  D E C O D E R  I N T E R N A L  C O M M A N D S

GENERAL DESCRIPTION
  This file contains defintions of format blocks of commands
  that are accepted by VIDDEC Task

REFERENCES
  None

EXTERNALIZED FUNCTIONS
  None

Copyright(c) 1992 - 2008 by QUALCOMM, Incorporated.

This software is licensed under the terms of the GNU General Public
License version 2, as published by the Free Software Foundation, and
may be copied, distributed, and modified under those terms.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*/
/*===========================================================================

                      EDIT HISTORY FOR FILE

This section contains comments describing changes made to this file.
Notice that changes are listed in reverse chronological order.

$Header: //source/qcom/qct/multimedia2/AdspSvc/7XXX/qdsp5cmd/video/qdsp5vdeccmdi.h#2 $ $DateTime: 2008/07/30 10:50:23 $ $Author: pavanr $
Revision History:

when       who     what, where, why
--------   ---     ----------------------------------------------------------
05/10/08   ac      initial version
===========================================================================*/


/*
 * Command to inform VIDDEC that new subframe packet is ready
 */

#define	VIDDEC_CMD_SUBFRAME_PKT		0x0000
#define	VIDDEC_CMD_SUBFRAME_PKT_LEN \
	sizeof(viddec_cmd_subframe_pkt)

#define	VIDDEC_CMD_SF_INFO_1_DM_DMA_STATS_EXCHANGE_FLAG_DM		0x0000
#define	VIDDEC_CMD_SF_INFO_1_DM_DMA_STATS_EXCHANGE_FLAG_DMA 	0x0001

#define	VIDDEC_CMD_SF_INFO_0_SUBFRAME_CONTI		0x0000
#define	VIDDEC_CMD_SF_INFO_0_SUBFRAME_FIRST		0x0001
#define	VIDDEC_CMD_SF_INFO_0_SUBFRAME_LAST		0x0002
#define	VIDDEC_CMD_SF_INFO_0_SUBFRAME_FIRST_AND_LAST 	0x0003

#define	VIDDEC_CMD_CODEC_SELECTION_WORD_MPEG_4		0x0000
#define	VIDDEC_CMD_CODEC_SELECTION_WORD_H_263_P0	0x0001
#define	VIDDEC_CMD_CODEC_SELECTION_WORD_H_264		0x0002
#define	VIDDEC_CMD_CODEC_SELECTION_WORD_H_263_p3	0x0003
#define	VIDDEC_CMD_CODEC_SELECTION_WORD_RV9		0x0004
#define	VIDDEC_CMD_CODEC_SELECTION_WORD_WMV9		0x0005
#define	VIDDEC_CMD_CODEC_SELECTION_WORD_SMCDB		0x0006
#define	VIDDEC_CMD_CODEC_SELECTION_WORD_QFRE		0x0007
#define	VIDDEC_CMD_CODEC_SELECTION_WORD_VLD		0x0008

typedef struct {
	unsigned short	cmd_id;
	unsigned short	packet_seq_number;
	unsigned short	codec_instance_id;
	unsigned short	subframe_packet_size_high;
	unsigned short	subframe_packet_size_low;
	unsigned short	subframe_packet_high;
	unsigned short	subframe_packet_low;
	unsigned short	subframe_packet_partition;
	unsigned short	statistics_packet_size_high;
	unsigned short	statistics_packet_size_low;
	unsigned short	statistics_packet_high;
	unsigned short	statistics_packet_low;
	unsigned short	statistics_partition;
	unsigned short	subframe_info_1;
	unsigned short	subframe_info_0;
	unsigned short	codec_selection_word;
	unsigned short	num_mbs;
} __attribute__((packed)) viddec_cmd_subframe_pkt;


/*
 * Command to inform VIDDEC task that post processing is required for the frame
 */

#define	VIDDEC_CMD_PP_ENABLE		0x0001
#define	VIDDEC_CMD_PP_ENABLE_LEN \
	sizeof(viddec_cmd_pp_enable)

#define	VIDDEC_CMD_PP_INFO_0_DM_DMA_LS_EXCHANGE_FLAG_DM		0x0000
#define	VIDDEC_CMD_PP_INFO_0_DM_DMA_LS_EXCHANGE_FLAG_DMA	0x0001

typedef struct {
	unsigned short	cmd_id;
	unsigned short	packet_seq_num;
	unsigned short	codec_instance_id;
	unsigned short	postproc_info_0;
	unsigned short	codec_selection_word;
	unsigned short	pp_output_addr_high;
	unsigned short	pp_output_addr_low;
	unsigned short	postproc_info_1;
	unsigned short	load_sharing_packet_size_high;
	unsigned short	load_sharing_packet_size_low;
	unsigned short	load_sharing_packet_high;
	unsigned short	load_sharing_packet_low;
	unsigned short	load_sharing_partition;
	unsigned short	pp_param_0;
	unsigned short	pp_param_1;
	unsigned short	pp_param_2;
	unsigned short	pp_param_3;
} __attribute__((packed)) viddec_cmd_pp_enable;


/*
 * FRAME Header Packet : It is at the start of new frame
 */

#define	VIDDEC_CMD_FRAME_HEADER_PACKET	0x0002
#define	VIDDEC_CMD_FRAME_HEADER_PACKET_LEN	\
	sizeof(viddec_cmd_frame_header_packet)

#define	VIDDEC_CMD_FRAME_INFO_0_ERROR_SKIP	0x0000
#define	VIDDEC_CMD_FRAME_INFO_0_ERROR_BLACK	0x0800

typedef struct {
	unsigned short	packet_id;
	unsigned short	x_dimension;
	unsigned short	y_dimension;
	unsigned short	line_width;
	unsigned short	frame_info_0;
	unsigned short	frame_buffer_0_high;
	unsigned short	frame_buffer_0_low;
	unsigned short	frame_buffer_1_high;
	unsigned short	frame_buffer_1_low;
	unsigned short	frame_buffer_2_high;
	unsigned short	frame_buffer_2_low;
	unsigned short	frame_buffer_3_high;
	unsigned short	frame_buffer_3_low;
	unsigned short	frame_buffer_4_high;
	unsigned short	frame_buffer_4_low;
	unsigned short	frame_buffer_5_high;
	unsigned short	frame_buffer_5_low;
	unsigned short	frame_buffer_6_high;
	unsigned short	frame_buffer_6_low;
	unsigned short	frame_buffer_7_high;
	unsigned short	frame_buffer_7_low;
	unsigned short	frame_buffer_8_high;
	unsigned short	frame_buffer_8_low;
	unsigned short	frame_buffer_9_high;
	unsigned short	frame_buffer_9_low;
	unsigned short	frame_buffer_10_high;
	unsigned short	frame_buffer_10_low;
	unsigned short	frame_buffer_11_high;
	unsigned short	frame_buffer_11_low;
	unsigned short	frame_buffer_12_high;
	unsigned short	frame_buffer_12_low;
	unsigned short	frame_buffer_13_high;
	unsigned short	frame_buffer_13_low;
	unsigned short	frame_buffer_14_high;
	unsigned short	frame_buffer_14_low;
	unsigned short	frame_buffer_15_high;
	unsigned short	frame_buffer_15_low;
	unsigned short	output_frame_buffer_high;
	unsigned short	output_frame_buffer_low;
	unsigned short	end_of_packet_marker;
} __attribute__((packed)) viddec_cmd_frame_header_packet;


/*
 * SLICE HEADER PACKET
 * I-Slice and P-Slice
 */

#define	VIDDEC_CMD_SLICE_HEADER_PKT_ISLICE		0x0003
#define	VIDDEC_CMD_SLICE_HEADER_PKT_ISLICE_LEN	\
	sizeof(viddec_cmd_slice_header_pkt_islice)

#define	VIDDEC_CMD_ISLICE_INFO_1_MOD_SLICE_TYPE_PSLICE	0x0000
#define	VIDDEC_CMD_ISLICE_INFO_1_MOD_SLICE_TYPE_BSLICE	0x0100
#define	VIDDEC_CMD_ISLICE_INFO_1_MOD_SLICE_TYPE_ISLICE	0x0200
#define	VIDDEC_CMD_ISLICE_INFO_1_MOD_SLICE_TYPE_SPSLICE	0x0300
#define	VIDDEC_CMD_ISLICE_INFO_1_MOD_SLICE_TYPE_SISLICE	0x0400
#define	VIDDEC_CMD_ISLICE_INFO_1_NOPADDING	0x0000
#define	VIDDEC_CMD_ISLICE_INFO_1_PADDING	0x0800

#define	VIDDEC_CMD_ISLICE_EOP_MARKER		0x7FFF

typedef struct {
	unsigned short	cmd_id;
	unsigned short	packet_id;
	unsigned short	slice_info_0;
	unsigned short	slice_info_1;
	unsigned short	slice_info_2;
	unsigned short	num_bytes_in_rbsp_high;
	unsigned short	num_bytes_in_rbsp_low;
	unsigned short	num_bytes_in_rbsp_consumed;
	unsigned short	end_of_packet_marker;
} __attribute__((packed)) viddec_cmd_slice_header_pkt_islice;


#define	VIDDEC_CMD_SLICE_HEADER_PKT_PSLICE		0x0003
#define	VIDDEC_CMD_SLICE_HEADER_PKT_PSLICE_LEN	\
	sizeof(viddec_cmd_slice_header_pkt_pslice)


typedef struct {
	unsigned short	cmd_id;
	unsigned short	packet_id;
	unsigned short	slice_info_0;
	unsigned short	slice_info_1;
	unsigned short	slice_info_2;
	unsigned short	slice_info_3;
	unsigned short	refidx_l0_map_tab_info_0;
	unsigned short	refidx_l0_map_tab_info_1;
	unsigned short	refidx_l0_map_tab_info_2;
	unsigned short	refidx_l0_map_tab_info_3;
	unsigned short	num_bytes_in_rbsp_high;
	unsigned short	num_bytes_in_rbsp_low;
	unsigned short	num_bytes_in_rbsp_consumed;
	unsigned short	end_of_packet_marker;
} __attribute__((packed)) viddec_cmd_slice_header_pkt_pslice;


#endif
