#ifndef QDSP5AUDRECCMDI_H
#define QDSP5AUDRECCMDI_H

/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

    A U D I O   R E C O R D  I N T E R N A L  C O M M A N D S

GENERAL DESCRIPTION
  This file contains defintions of format blocks of commands
  that are accepted by AUDREC Task

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

 $Header: //source/qcom/qct/multimedia2/Audio/drivers/QDSP5Driver/QDSP5Interface/main/latest/qdsp5audreccmdi.h#3 $

============================================================================*/

/*
 * AUDRECTASK COMMANDS
 * ARM uses 2 queues to communicate with the AUDRECTASK
 * 1.uPAudRecCmdQueue
 * Location :MEMC
 * Buffer Size : 8
 * No of Buffers in a queue : 3
 * 2.audRecUpBitStreamQueue
 * Location : MEMC
 * Buffer Size : 4
 * No of buffers in a queue : 2
 */

/*
 * Commands on uPAudRecCmdQueue
 */

/*
 * Command to initiate and terminate the audio recording section
 */

#define AUDREC_CMD_CFG		0x0000
#define	AUDREC_CMD_CFG_LEN	sizeof(audrec_cmd_cfg)

#define	AUDREC_CMD_TYPE_0_INDEX_WAV	0x0000
#define	AUDREC_CMD_TYPE_0_INDEX_AAC	0x0001

#define AUDREC_CMD_TYPE_0_ENA		0x4000
#define AUDREC_CMD_TYPE_0_DIS		0x0000

#define AUDREC_CMD_TYPE_0_NOUPDATE	0x0000
#define AUDREC_CMD_TYPE_0_UPDATE	0x8000

#define	AUDREC_CMD_TYPE_1_INDEX_SBC	0x0002

#define AUDREC_CMD_TYPE_1_ENA		0x4000
#define AUDREC_CMD_TYPE_1_DIS		0x0000

#define AUDREC_CMD_TYPE_1_NOUPDATE	0x0000
#define AUDREC_CMD_TYPE_1_UPDATE	0x8000

typedef struct {
	unsigned short 	cmd_id;
	unsigned short	type_0;
	unsigned short	type_1;
} __attribute__((packed)) audrec_cmd_cfg;


/*
 * Command to configure the recording parameters for RecType0(AAC/WAV) encoder
 */

#define	AUDREC_CMD_AREC0PARAM_CFG	0x0001
#define	AUDREC_CMD_AREC0PARAM_CFG_LEN	\
	sizeof(audrec_cmd_arec0param_cfg)

#define	AUDREC_CMD_SAMP_RATE_INDX_8000		0x000B
#define	AUDREC_CMD_SAMP_RATE_INDX_11025		0x000A
#define	AUDREC_CMD_SAMP_RATE_INDX_12000		0x0009
#define	AUDREC_CMD_SAMP_RATE_INDX_16000		0x0008
#define	AUDREC_CMD_SAMP_RATE_INDX_22050		0x0007
#define	AUDREC_CMD_SAMP_RATE_INDX_24000		0x0006
#define	AUDREC_CMD_SAMP_RATE_INDX_32000		0x0005
#define	AUDREC_CMD_SAMP_RATE_INDX_44100		0x0004
#define	AUDREC_CMD_SAMP_RATE_INDX_48000		0x0003

#define AUDREC_CMD_STEREO_MODE_MONO		0x0000
#define AUDREC_CMD_STEREO_MODE_STEREO		0x0001

typedef struct {
	unsigned short 	cmd_id;
	unsigned short	ptr_to_extpkt_buffer_msw;
	unsigned short	ptr_to_extpkt_buffer_lsw;
	unsigned short	buf_len;
	unsigned short	samp_rate_index;
	unsigned short	stereo_mode;
	unsigned short 	rec_quality;
} __attribute__((packed)) audrec_cmd_arec0param_cfg;

/*
 * Command to configure the recording parameters for RecType1(SBC) encoder
 */

#define AUDREC_CMD_AREC1PARAM_CFG	0x0002
#define AUDREC_CMD_AREC1PARAM_CFG_LEN	\
	sizeof(audrec_cmd_arec1param_cfg)

#define AUDREC_CMD_PARAM_BUF_BLOCKS_4	0x0000
#define AUDREC_CMD_PARAM_BUF_BLOCKS_8	0x0001
#define AUDREC_CMD_PARAM_BUF_BLOCKS_12	0x0002
#define AUDREC_CMD_PARAM_BUF_BLOCKS_16	0x0003

#define AUDREC_CMD_PARAM_BUF_SUB_BANDS_8	0x0010
#define AUDREC_CMD_PARAM_BUF_MODE_MONO		0x0000
#define AUDREC_CMD_PARAM_BUF_MODE_DUAL		0x0040
#define AUDREC_CMD_PARAM_BUF_MODE_STEREO	0x0050
#define AUDREC_CMD_PARAM_BUF_MODE_JSTEREO	0x0060
#define AUDREC_CMD_PARAM_BUF_LOUDNESS		0x0000
#define AUDREC_CMD_PARAM_BUF_SNR		0x0100
#define AUDREC_CMD_PARAM_BUF_BASIC_VER		0x0000

typedef struct {
	unsigned short 	cmd_id;
	unsigned short	ptr_to_extpkt_buffer_msw;
	unsigned short	ptr_to_extpkt_buffer_lsw;
	unsigned short	buf_len;
	unsigned short	param_buf;
	unsigned short	bit_rate_0;
	unsigned short	bit_rate_1;
} __attribute__((packed)) audrec_cmd_arec1param_cfg;


/*
 * Commands on audRecUpBitStreamQueue
 */

/*
 * Command to indicate the current packet read count
 */

#define AUDREC_CMD_PACKET_EXT_PTR		0x0000
#define AUDREC_CMD_PACKET_EXT_PTR_LEN	\
	sizeof(audrec_cmd_packet_ext_ptr)

#define AUDREC_CMD_TYPE_0	0x0000
#define AUDREC_CMD_TYPE_1	0x0001

typedef struct {
	unsigned short  cmd_id;
	unsigned short	type;
	unsigned short 	curr_rec_count_msw;
	unsigned short 	curr_rec_count_lsw;
} __attribute__((packed)) audrec_cmd_packet_ext_ptr;

#endif
