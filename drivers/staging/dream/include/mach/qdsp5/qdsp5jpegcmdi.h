#ifndef QDSP5VIDJPEGCMDI_H
#define QDSP5VIDJPEGCMDI_H

/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

    J P E G  I N T E R N A L  C O M M A N D S

GENERAL DESCRIPTION
  This file contains defintions of format blocks of commands
  that are accepted by JPEG Task

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

$Header: //source/qcom/qct/multimedia2/AdspSvc/7XXX/qdsp5cmd/video/qdsp5jpegcmdi.h#2 $ $DateTime: 2008/07/30 10:50:23 $ $Author: pavanr $
Revision History:
when       who     what, where, why
--------   ---     ----------------------------------------------------------
06/09/08   sv      initial version
===========================================================================*/

/*
 * ARM to JPEG configuration commands are passed through the
 * uPJpegCfgCmdQueue
 */

/*
 * Command to configure JPEG Encoder
 */

#define	JPEG_CMD_ENC_CFG		0x0000
#define	JPEG_CMD_ENC_CFG_LEN	sizeof(jpeg_cmd_enc_cfg)

#define	JPEG_CMD_ENC_PROCESS_CFG_OP_ROTATION_0		0x0000
#define	JPEG_CMD_ENC_PROCESS_CFG_OP_ROTATION_90		0x0100
#define	JPEG_CMD_ENC_PROCESS_CFG_OP_ROTATION_180	0x0200
#define	JPEG_CMD_ENC_PROCESS_CFG_OP_ROTATION_270	0x0300
#define	JPEG_CMD_ENC_PROCESS_CFG_IP_DATA_FORMAT_M	0x0003
#define	JPEG_CMD_ENC_PROCESS_CFG_IP_DATA_FORMAT_H2V2	0x0000
#define	JPEG_CMD_ENC_PROCESS_CFG_IP_DATA_FORMAT_H2V1	0x0001
#define	JPEG_CMD_ENC_PROCESS_CFG_IP_DATA_FORMAT_H1V2	0x0002

#define	JPEG_CMD_IP_SIZE_CFG_LUMA_HEIGHT_M		0x0000FFFF
#define	JPEG_CMD_IP_SIZE_CFG_LUMA_WIDTH_M		0xFFFF0000
#define	JPEG_CMD_ENC_UPSAMP_IP_SIZE_CFG_ENA		0x0001
#define	JPEG_CMD_ENC_UPSAMP_IP_SIZE_CFG_DIS		0x0000

#define	JPEG_CMD_FRAG_SIZE_LUMA_HEIGHT_M		0xFFFF

typedef struct {
	unsigned int	cmd_id;
	unsigned int	process_cfg;
	unsigned int	ip_size_cfg;
	unsigned int	op_size_cfg;
	unsigned int	frag_cfg;
	unsigned int	frag_cfg_part[16];

	unsigned int    part_num;

	unsigned int	op_buf_0_cfg_part1;
	unsigned int	op_buf_0_cfg_part2;
	unsigned int	op_buf_1_cfg_part1;
	unsigned int	op_buf_1_cfg_part2;

	unsigned int	luma_qunt_table[32];
	unsigned int	chroma_qunt_table[32];

	unsigned int	upsamp_ip_size_cfg;
	unsigned int	upsamp_ip_frame_off;
	unsigned int	upsamp_pp_filter_coeff[64];
} __attribute__((packed)) jpeg_cmd_enc_cfg;

/*
 * Command to configure JPEG Decoder
 */

#define	JPEG_CMD_DEC_CFG		0x0001
#define	JPEG_CMD_DEC_CFG_LEN		sizeof(jpeg_cmd_dec_cfg)

#define	JPEG_CMD_DEC_OP_DATA_FORMAT_M		0x0001
#define JPEG_CMD_DEC_OP_DATA_FORMAT_H2V2	0x0000
#define JPEG_CMD_DEC_OP_DATA_FORMAT_H2V1	0x0001

#define JPEG_CMD_DEC_OP_DATA_FORMAT_SCALE_FACTOR_8	0x000000
#define JPEG_CMD_DEC_OP_DATA_FORMAT_SCALE_FACTOR_4	0x010000
#define JPEG_CMD_DEC_OP_DATA_FORMAT_SCALE_FACTOR_2	0x020000
#define JPEG_CMD_DEC_OP_DATA_FORMAT_SCALE_FACTOR_1	0x030000

#define	JPEG_CMD_DEC_IP_STREAM_BUF_CFG_PART3_NOT_FINAL	0x0000
#define	JPEG_CMD_DEC_IP_STREAM_BUF_CFG_PART3_FINAL	0x0001


typedef struct {
	unsigned int	cmd_id;
	unsigned int	img_dimension_cfg;
	unsigned int	op_data_format;
	unsigned int	restart_interval;
	unsigned int	ip_buf_partition_num;
	unsigned int	ip_stream_buf_cfg_part1;
	unsigned int	ip_stream_buf_cfg_part2;
	unsigned int	ip_stream_buf_cfg_part3;
	unsigned int	op_stream_buf_0_cfg_part1;
	unsigned int	op_stream_buf_0_cfg_part2;
	unsigned int	op_stream_buf_0_cfg_part3;
	unsigned int	op_stream_buf_1_cfg_part1;
	unsigned int	op_stream_buf_1_cfg_part2;
	unsigned int	op_stream_buf_1_cfg_part3;
	unsigned int	luma_qunt_table_0_3;
	unsigned int	luma_qunt_table_4_7;
	unsigned int	luma_qunt_table_8_11;
	unsigned int	luma_qunt_table_12_15;
	unsigned int	luma_qunt_table_16_19;
	unsigned int	luma_qunt_table_20_23;
	unsigned int	luma_qunt_table_24_27;
	unsigned int	luma_qunt_table_28_31;
	unsigned int	luma_qunt_table_32_35;
	unsigned int	luma_qunt_table_36_39;
	unsigned int	luma_qunt_table_40_43;
	unsigned int	luma_qunt_table_44_47;
	unsigned int	luma_qunt_table_48_51;
	unsigned int	luma_qunt_table_52_55;
	unsigned int	luma_qunt_table_56_59;
	unsigned int	luma_qunt_table_60_63;
	unsigned int	chroma_qunt_table_0_3;
	unsigned int	chroma_qunt_table_4_7;
	unsigned int	chroma_qunt_table_8_11;
	unsigned int	chroma_qunt_table_12_15;
	unsigned int	chroma_qunt_table_16_19;
	unsigned int	chroma_qunt_table_20_23;
	unsigned int	chroma_qunt_table_24_27;
	unsigned int	chroma_qunt_table_28_31;
	unsigned int	chroma_qunt_table_32_35;
	unsigned int	chroma_qunt_table_36_39;
	unsigned int	chroma_qunt_table_40_43;
	unsigned int	chroma_qunt_table_44_47;
	unsigned int	chroma_qunt_table_48_51;
	unsigned int	chroma_qunt_table_52_55;
	unsigned int	chroma_qunt_table_56_59;
	unsigned int	chroma_qunt_table_60_63;
	unsigned int	luma_dc_hm_code_cnt_table_0_3;
	unsigned int	luma_dc_hm_code_cnt_table_4_7;
	unsigned int	luma_dc_hm_code_cnt_table_8_11;
	unsigned int	luma_dc_hm_code_cnt_table_12_15;
	unsigned int	luma_dc_hm_code_val_table_0_3;
	unsigned int	luma_dc_hm_code_val_table_4_7;
	unsigned int	luma_dc_hm_code_val_table_8_11;
	unsigned int	chroma_dc_hm_code_cnt_table_0_3;
	unsigned int	chroma_dc_hm_code_cnt_table_4_7;
	unsigned int	chroma_dc_hm_code_cnt_table_8_11;
	unsigned int	chroma_dc_hm_code_cnt_table_12_15;
	unsigned int	chroma_dc_hm_code_val_table_0_3;
	unsigned int	chroma_dc_hm_code_val_table_4_7;
	unsigned int	chroma_dc_hm_code_val_table_8_11;
	unsigned int	luma_ac_hm_code_cnt_table_0_3;
	unsigned int	luma_ac_hm_code_cnt_table_4_7;
	unsigned int	luma_ac_hm_code_cnt_table_8_11;
	unsigned int	luma_ac_hm_code_cnt_table_12_15;
	unsigned int	luma_ac_hm_code_val_table_0_3;
	unsigned int	luma_ac_hm_code_val_table_4_7;
	unsigned int	luma_ac_hm_code_val_table_8_11;
	unsigned int	luma_ac_hm_code_val_table_12_15;
	unsigned int	luma_ac_hm_code_val_table_16_19;
	unsigned int	luma_ac_hm_code_val_table_20_23;
	unsigned int	luma_ac_hm_code_val_table_24_27;
	unsigned int	luma_ac_hm_code_val_table_28_31;
	unsigned int	luma_ac_hm_code_val_table_32_35;
	unsigned int	luma_ac_hm_code_val_table_36_39;
	unsigned int	luma_ac_hm_code_val_table_40_43;
	unsigned int	luma_ac_hm_code_val_table_44_47;
	unsigned int	luma_ac_hm_code_val_table_48_51;
	unsigned int	luma_ac_hm_code_val_table_52_55;
	unsigned int	luma_ac_hm_code_val_table_56_59;
	unsigned int	luma_ac_hm_code_val_table_60_63;
	unsigned int	luma_ac_hm_code_val_table_64_67;
	unsigned int	luma_ac_hm_code_val_table_68_71;
	unsigned int	luma_ac_hm_code_val_table_72_75;
	unsigned int	luma_ac_hm_code_val_table_76_79;
	unsigned int	luma_ac_hm_code_val_table_80_83;
	unsigned int	luma_ac_hm_code_val_table_84_87;
	unsigned int	luma_ac_hm_code_val_table_88_91;
	unsigned int	luma_ac_hm_code_val_table_92_95;
	unsigned int	luma_ac_hm_code_val_table_96_99;
	unsigned int	luma_ac_hm_code_val_table_100_103;
	unsigned int	luma_ac_hm_code_val_table_104_107;
	unsigned int	luma_ac_hm_code_val_table_108_111;
	unsigned int	luma_ac_hm_code_val_table_112_115;
	unsigned int	luma_ac_hm_code_val_table_116_119;
	unsigned int	luma_ac_hm_code_val_table_120_123;
	unsigned int	luma_ac_hm_code_val_table_124_127;
	unsigned int	luma_ac_hm_code_val_table_128_131;
	unsigned int	luma_ac_hm_code_val_table_132_135;
	unsigned int	luma_ac_hm_code_val_table_136_139;
	unsigned int	luma_ac_hm_code_val_table_140_143;
	unsigned int	luma_ac_hm_code_val_table_144_147;
	unsigned int	luma_ac_hm_code_val_table_148_151;
	unsigned int	luma_ac_hm_code_val_table_152_155;
	unsigned int	luma_ac_hm_code_val_table_156_159;
	unsigned int	luma_ac_hm_code_val_table_160_161;
	unsigned int	chroma_ac_hm_code_cnt_table_0_3;
	unsigned int	chroma_ac_hm_code_cnt_table_4_7;
	unsigned int	chroma_ac_hm_code_cnt_table_8_11;
	unsigned int	chroma_ac_hm_code_cnt_table_12_15;
	unsigned int	chroma_ac_hm_code_val_table_0_3;
	unsigned int	chroma_ac_hm_code_val_table_4_7;
	unsigned int	chroma_ac_hm_code_val_table_8_11;
	unsigned int	chroma_ac_hm_code_val_table_12_15;
	unsigned int	chroma_ac_hm_code_val_table_16_19;
	unsigned int	chroma_ac_hm_code_val_table_20_23;
	unsigned int	chroma_ac_hm_code_val_table_24_27;
	unsigned int	chroma_ac_hm_code_val_table_28_31;
	unsigned int	chroma_ac_hm_code_val_table_32_35;
	unsigned int	chroma_ac_hm_code_val_table_36_39;
	unsigned int	chroma_ac_hm_code_val_table_40_43;
	unsigned int	chroma_ac_hm_code_val_table_44_47;
	unsigned int	chroma_ac_hm_code_val_table_48_51;
	unsigned int	chroma_ac_hm_code_val_table_52_55;
	unsigned int	chroma_ac_hm_code_val_table_56_59;
	unsigned int	chroma_ac_hm_code_val_table_60_63;
	unsigned int	chroma_ac_hm_code_val_table_64_67;
	unsigned int	chroma_ac_hm_code_val_table_68_71;
	unsigned int	chroma_ac_hm_code_val_table_72_75;
	unsigned int	chroma_ac_hm_code_val_table_76_79;
	unsigned int	chroma_ac_hm_code_val_table_80_83;
	unsigned int	chroma_ac_hm_code_val_table_84_87;
	unsigned int	chroma_ac_hm_code_val_table_88_91;
	unsigned int	chroma_ac_hm_code_val_table_92_95;
	unsigned int	chroma_ac_hm_code_val_table_96_99;
	unsigned int	chroma_ac_hm_code_val_table_100_103;
	unsigned int	chroma_ac_hm_code_val_table_104_107;
	unsigned int	chroma_ac_hm_code_val_table_108_111;
	unsigned int	chroma_ac_hm_code_val_table_112_115;
	unsigned int	chroma_ac_hm_code_val_table_116_119;
	unsigned int	chroma_ac_hm_code_val_table_120_123;
	unsigned int	chroma_ac_hm_code_val_table_124_127;
	unsigned int	chroma_ac_hm_code_val_table_128_131;
	unsigned int	chroma_ac_hm_code_val_table_132_135;
	unsigned int	chroma_ac_hm_code_val_table_136_139;
	unsigned int	chroma_ac_hm_code_val_table_140_143;
	unsigned int	chroma_ac_hm_code_val_table_144_147;
	unsigned int	chroma_ac_hm_code_val_table_148_151;
	unsigned int	chroma_ac_hm_code_val_table_152_155;
	unsigned int	chroma_ac_hm_code_val_table_156_159;
	unsigned int	chroma_ac_hm_code_val_table_160_161;
} __attribute__((packed)) jpeg_cmd_dec_cfg;


/*
 * ARM to JPEG configuration commands are passed through the
 * uPJpegActionCmdQueue
 */

/*
 * Command to start the encode process
 */

#define	JPEG_CMD_ENC_ENCODE		0x0000
#define	JPEG_CMD_ENC_ENCODE_LEN		sizeof(jpeg_cmd_enc_encode)


typedef struct {
	unsigned short	cmd_id;
} __attribute__((packed)) jpeg_cmd_enc_encode;


/*
 * Command to transition from current state of encoder to IDLE state
 */

#define	JPEG_CMD_ENC_IDLE		0x0001
#define	JPEG_CMD_ENC_IDLE_LEN		sizeof(jpeg_cmd_enc_idle)


typedef struct {
	unsigned short	cmd_id;
} __attribute__((packed)) jpeg_cmd_enc_idle;


/*
 * Command to inform the encoder that another buffer is ready
 */

#define	JPEG_CMD_ENC_OP_CONSUMED	0x0002
#define	JPEG_CMD_ENC_OP_CONSUMED_LEN	sizeof(jpeg_cmd_enc_op_consumed)


typedef struct {
	unsigned int	cmd_id;
	unsigned int	op_buf_addr;
	unsigned int	op_buf_size;
} __attribute__((packed)) jpeg_cmd_enc_op_consumed;


/*
 * Command to start the decoding process
 */

#define	JPEG_CMD_DEC_DECODE		0x0003
#define	JPEG_CMD_DEC_DECODE_LEN	sizeof(jpeg_cmd_dec_decode)


typedef struct {
	unsigned short	cmd_id;
} __attribute__((packed)) jpeg_cmd_dec_decode;


/*
 * Command to transition from the current state of decoder to IDLE
 */

#define	JPEG_CMD_DEC_IDLE	0x0004
#define	JPEG_CMD_DEC_IDLE_LEN	sizeof(jpeg_cmd_dec_idle)


typedef struct {
	unsigned short	cmd_id;
} __attribute__((packed)) jpeg_cmd_dec_idle;


/*
 * Command to inform that an op buffer is ready for use
 */

#define	JPEG_CMD_DEC_OP_CONSUMED	0x0005
#define	JPEG_CMD_DEC_OP_CONSUMED_LEN	sizeof(jpeg_cmd_dec_op_consumed)


typedef struct {
	unsigned int	cmd_id;
	unsigned int	luma_op_buf_addr;
	unsigned int	luma_op_buf_size;
	unsigned int	chroma_op_buf_addr;
} __attribute__((packed)) jpeg_cmd_dec_op_consumed;


/*
 * Command to pass a new ip buffer to the jpeg decoder
 */

#define	JPEG_CMD_DEC_IP	0x0006
#define	JPEG_CMD_DEC_IP_LEN	sizeof(jpeg_cmd_dec_ip_len)

#define	JPEG_CMD_EOI_INDICATOR_NOT_END	0x0000
#define	JPEG_CMD_EOI_INDICATOR_END	0x0001

typedef struct {
	unsigned int	cmd_id;
	unsigned int	ip_buf_addr;
	unsigned int	ip_buf_size;
	unsigned int	eoi_indicator;
} __attribute__((packed)) jpeg_cmd_dec_ip;



#endif
