/*
 * Register definition file for Samsung MFC V5.1 Interface (FIMV) driver
 *
 * Kamil Debski, Copyright (c) 2010 Samsung Electronics
 * http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _REGS_FIMV_H
#define _REGS_FIMV_H

#define S5P_FIMV_REG_SIZE	(S5P_FIMV_END_ADDR - S5P_FIMV_START_ADDR)
#define S5P_FIMV_REG_COUNT	((S5P_FIMV_END_ADDR - S5P_FIMV_START_ADDR) / 4)

/* Number of bits that the buffer address should be shifted for particular
 * MFC buffers.  */
#define S5P_FIMV_START_ADDR		0x0000
#define S5P_FIMV_END_ADDR		0xe008

#define S5P_FIMV_SW_RESET		0x0000
#define S5P_FIMV_RISC_HOST_INT		0x0008

/* Command from HOST to RISC */
#define S5P_FIMV_HOST2RISC_CMD		0x0030
#define S5P_FIMV_HOST2RISC_ARG1		0x0034
#define S5P_FIMV_HOST2RISC_ARG2		0x0038
#define S5P_FIMV_HOST2RISC_ARG3		0x003c
#define S5P_FIMV_HOST2RISC_ARG4		0x0040

/* Command from RISC to HOST */
#define S5P_FIMV_RISC2HOST_CMD		0x0044
#define S5P_FIMV_RISC2HOST_CMD_MASK	0x1FFFF
#define S5P_FIMV_RISC2HOST_ARG1		0x0048
#define S5P_FIMV_RISC2HOST_ARG2		0x004c
#define S5P_FIMV_RISC2HOST_ARG3		0x0050
#define S5P_FIMV_RISC2HOST_ARG4		0x0054

#define S5P_FIMV_FW_VERSION		0x0058
#define S5P_FIMV_SYS_MEM_SZ		0x005c
#define S5P_FIMV_FW_STATUS		0x0080

/* Memory controller register */
#define S5P_FIMV_MC_DRAMBASE_ADR_A	0x0508
#define S5P_FIMV_MC_DRAMBASE_ADR_B	0x050c
#define S5P_FIMV_MC_STATUS		0x0510

/* Common register */
#define S5P_FIMV_COMMON_BASE_A		0x0600
#define S5P_FIMV_COMMON_BASE_B		0x0700

/* Decoder */
#define S5P_FIMV_DEC_CHROMA_ADR		(S5P_FIMV_COMMON_BASE_A)
#define S5P_FIMV_DEC_LUMA_ADR		(S5P_FIMV_COMMON_BASE_B)

/* H.264 decoding */
#define S5P_FIMV_H264_VERT_NB_MV_ADR	(S5P_FIMV_COMMON_BASE_A + 0x8c)
					/* vertical neighbor motion vector */
#define S5P_FIMV_H264_NB_IP_ADR		(S5P_FIMV_COMMON_BASE_A + 0x90)
					/* neighbor pixels for intra pred */
#define S5P_FIMV_H264_MV_ADR		(S5P_FIMV_COMMON_BASE_B + 0x80)
					/* H264 motion vector */

/* MPEG4 decoding */
#define S5P_FIMV_MPEG4_NB_DCAC_ADR	(S5P_FIMV_COMMON_BASE_A + 0x8c)
					/* neighbor AC/DC coeff. */
#define S5P_FIMV_MPEG4_UP_NB_MV_ADR	(S5P_FIMV_COMMON_BASE_A + 0x90)
					/* upper neighbor motion vector */
#define S5P_FIMV_MPEG4_SA_MV_ADR	(S5P_FIMV_COMMON_BASE_A + 0x94)
					/* subseq. anchor motion vector */
#define S5P_FIMV_MPEG4_OT_LINE_ADR	(S5P_FIMV_COMMON_BASE_A + 0x98)
					/* overlap transform line */
#define S5P_FIMV_MPEG4_SP_ADR		(S5P_FIMV_COMMON_BASE_A + 0xa8)
					/* syntax parser */

/* H.263 decoding */
#define S5P_FIMV_H263_NB_DCAC_ADR	(S5P_FIMV_COMMON_BASE_A + 0x8c)
#define S5P_FIMV_H263_UP_NB_MV_ADR	(S5P_FIMV_COMMON_BASE_A + 0x90)
#define S5P_FIMV_H263_SA_MV_ADR		(S5P_FIMV_COMMON_BASE_A + 0x94)
#define S5P_FIMV_H263_OT_LINE_ADR	(S5P_FIMV_COMMON_BASE_A + 0x98)

/* VC-1 decoding */
#define S5P_FIMV_VC1_NB_DCAC_ADR	(S5P_FIMV_COMMON_BASE_A + 0x8c)
#define S5P_FIMV_VC1_UP_NB_MV_ADR	(S5P_FIMV_COMMON_BASE_A + 0x90)
#define S5P_FIMV_VC1_SA_MV_ADR		(S5P_FIMV_COMMON_BASE_A + 0x94)
#define S5P_FIMV_VC1_OT_LINE_ADR	(S5P_FIMV_COMMON_BASE_A + 0x98)
#define S5P_FIMV_VC1_BITPLANE3_ADR	(S5P_FIMV_COMMON_BASE_A + 0x9c)
					/* bitplane3 */
#define S5P_FIMV_VC1_BITPLANE2_ADR	(S5P_FIMV_COMMON_BASE_A + 0xa0)
					/* bitplane2 */
#define S5P_FIMV_VC1_BITPLANE1_ADR	(S5P_FIMV_COMMON_BASE_A + 0xa4)
					/* bitplane1 */

/* Encoder */
#define S5P_FIMV_ENC_REF0_LUMA_ADR	(S5P_FIMV_COMMON_BASE_A + 0x1c)
#define S5P_FIMV_ENC_REF1_LUMA_ADR	(S5P_FIMV_COMMON_BASE_A + 0x20)
					/* reconstructed luma */
#define S5P_FIMV_ENC_REF0_CHROMA_ADR	(S5P_FIMV_COMMON_BASE_B)
#define S5P_FIMV_ENC_REF1_CHROMA_ADR	(S5P_FIMV_COMMON_BASE_B + 0x04)
					/* reconstructed chroma */
#define S5P_FIMV_ENC_REF2_LUMA_ADR	(S5P_FIMV_COMMON_BASE_B + 0x10)
#define S5P_FIMV_ENC_REF2_CHROMA_ADR	(S5P_FIMV_COMMON_BASE_B + 0x08)
#define S5P_FIMV_ENC_REF3_LUMA_ADR	(S5P_FIMV_COMMON_BASE_B + 0x14)
#define S5P_FIMV_ENC_REF3_CHROMA_ADR	(S5P_FIMV_COMMON_BASE_B + 0x0c)

/* H.264 encoding */
#define S5P_FIMV_H264_UP_MV_ADR		(S5P_FIMV_COMMON_BASE_A)
					/* upper motion vector */
#define S5P_FIMV_H264_NBOR_INFO_ADR	(S5P_FIMV_COMMON_BASE_A + 0x04)
					/* entropy engine's neighbor info. */
#define S5P_FIMV_H264_UP_INTRA_MD_ADR	(S5P_FIMV_COMMON_BASE_A + 0x08)
					/* upper intra MD */
#define S5P_FIMV_H264_COZERO_FLAG_ADR	(S5P_FIMV_COMMON_BASE_A + 0x10)
					/* direct cozero flag */
#define S5P_FIMV_H264_UP_INTRA_PRED_ADR	(S5P_FIMV_COMMON_BASE_B + 0x40)
					/* upper intra PRED */

/* H.263 encoding */
#define S5P_FIMV_H263_UP_MV_ADR		(S5P_FIMV_COMMON_BASE_A)
					/* upper motion vector */
#define S5P_FIMV_H263_ACDC_COEF_ADR	(S5P_FIMV_COMMON_BASE_A + 0x04)
					/* upper Q coeff. */

/* MPEG4 encoding */
#define S5P_FIMV_MPEG4_UP_MV_ADR	(S5P_FIMV_COMMON_BASE_A)
					/* upper motion vector */
#define S5P_FIMV_MPEG4_ACDC_COEF_ADR	(S5P_FIMV_COMMON_BASE_A + 0x04)
					/* upper Q coeff. */
#define S5P_FIMV_MPEG4_COZERO_FLAG_ADR	(S5P_FIMV_COMMON_BASE_A + 0x10)
					/* direct cozero flag */

#define S5P_FIMV_ENC_REF_B_LUMA_ADR     0x062c /* ref B Luma addr */
#define S5P_FIMV_ENC_REF_B_CHROMA_ADR   0x0630 /* ref B Chroma addr */

#define S5P_FIMV_ENC_CUR_LUMA_ADR	0x0718 /* current Luma addr */
#define S5P_FIMV_ENC_CUR_CHROMA_ADR	0x071C /* current Chroma addr */

/* Codec common register */
#define S5P_FIMV_ENC_HSIZE_PX		0x0818 /* frame width at encoder */
#define S5P_FIMV_ENC_VSIZE_PX		0x081c /* frame height at encoder */
#define S5P_FIMV_ENC_PROFILE		0x0830 /* profile register */
#define S5P_FIMV_ENC_PROFILE_H264_MAIN			0
#define S5P_FIMV_ENC_PROFILE_H264_HIGH			1
#define S5P_FIMV_ENC_PROFILE_H264_BASELINE		2
#define S5P_FIMV_ENC_PROFILE_MPEG4_SIMPLE		0
#define S5P_FIMV_ENC_PROFILE_MPEG4_ADVANCED_SIMPLE	1
#define S5P_FIMV_ENC_PIC_STRUCT		0x083c /* picture field/frame flag */
#define S5P_FIMV_ENC_LF_CTRL		0x0848 /* loop filter control */
#define S5P_FIMV_ENC_ALPHA_OFF		0x084c /* loop filter alpha offset */
#define S5P_FIMV_ENC_BETA_OFF		0x0850 /* loop filter beta offset */
#define S5P_FIMV_MR_BUSIF_CTRL		0x0854 /* hidden, bus interface ctrl */
#define S5P_FIMV_ENC_PXL_CACHE_CTRL	0x0a00 /* pixel cache control */

/* Channel & stream interface register */
#define S5P_FIMV_SI_RTN_CHID		0x2000 /* Return CH inst ID register */
#define S5P_FIMV_SI_CH0_INST_ID		0x2040 /* codec instance ID */
#define S5P_FIMV_SI_CH1_INST_ID		0x2080 /* codec instance ID */
/* Decoder */
#define S5P_FIMV_SI_VRESOL		0x2004 /* vertical res of decoder */
#define S5P_FIMV_SI_HRESOL		0x2008 /* horizontal res of decoder */
#define S5P_FIMV_SI_BUF_NUMBER		0x200c /* number of frames in the
								decoded pic */
#define S5P_FIMV_SI_DISPLAY_Y_ADR	0x2010 /* luma addr of displayed pic */
#define S5P_FIMV_SI_DISPLAY_C_ADR	0x2014 /* chroma addrof displayed pic */
#define S5P_FIMV_SI_CONSUMED_BYTES	0x2018 /* Consumed number of bytes to
							decode a frame */
#define S5P_FIMV_SI_DISPLAY_STATUS	0x201c /* status of decoded picture */

#define S5P_FIMV_SI_CH0_SB_ST_ADR	0x2044 /* start addr of stream buf */
#define S5P_FIMV_SI_CH0_SB_FRM_SIZE	0x2048 /* size of stream buf */
#define S5P_FIMV_SI_CH0_DESC_ADR	0x204c /* addr of descriptor buf */
#define S5P_FIMV_SI_CH0_CPB_SIZE	0x2058 /* max size of coded pic. buf */
#define S5P_FIMV_SI_CH0_DESC_SIZE	0x205c /* max size of descriptor buf */

#define S5P_FIMV_SI_CH1_SB_ST_ADR	0x2084 /* start addr of stream buf */
#define S5P_FIMV_SI_CH1_SB_FRM_SIZE	0x2088 /* size of stream buf */
#define S5P_FIMV_SI_CH1_DESC_ADR	0x208c /* addr of descriptor buf */
#define S5P_FIMV_SI_CH1_CPB_SIZE	0x2098 /* max size of coded pic. buf */
#define S5P_FIMV_SI_CH1_DESC_SIZE	0x209c /* max size of descriptor buf */

#define S5P_FIMV_CRC_LUMA0		0x2030 /* luma crc data per frame
								(top field) */
#define S5P_FIMV_CRC_CHROMA0		0x2034 /* chroma crc data per frame
								(top field) */
#define S5P_FIMV_CRC_LUMA1		0x2038 /* luma crc data per bottom
								field */
#define S5P_FIMV_CRC_CHROMA1		0x203c /* chroma crc data per bottom
								field */

/* Display status */
#define S5P_FIMV_DEC_STATUS_DECODING_ONLY		0
#define S5P_FIMV_DEC_STATUS_DECODING_DISPLAY		1
#define S5P_FIMV_DEC_STATUS_DISPLAY_ONLY		2
#define S5P_FIMV_DEC_STATUS_DECODING_EMPTY		3
#define S5P_FIMV_DEC_STATUS_DECODING_STATUS_MASK	7
#define S5P_FIMV_DEC_STATUS_PROGRESSIVE			(0<<3)
#define S5P_FIMV_DEC_STATUS_INTERLACE			(1<<3)
#define S5P_FIMV_DEC_STATUS_INTERLACE_MASK		(1<<3)
#define S5P_FIMV_DEC_STATUS_CRC_NUMBER_TWO		(0<<4)
#define S5P_FIMV_DEC_STATUS_CRC_NUMBER_FOUR		(1<<4)
#define S5P_FIMV_DEC_STATUS_CRC_NUMBER_MASK		(1<<4)
#define S5P_FIMV_DEC_STATUS_CRC_GENERATED		(1<<5)
#define S5P_FIMV_DEC_STATUS_CRC_NOT_GENERATED		(0<<5)
#define S5P_FIMV_DEC_STATUS_CRC_MASK			(1<<5)

#define S5P_FIMV_DEC_STATUS_RESOLUTION_MASK		(3<<4)
#define S5P_FIMV_DEC_STATUS_RESOLUTION_INC		(1<<4)
#define S5P_FIMV_DEC_STATUS_RESOLUTION_DEC		(2<<4)

/* Decode frame address */
#define S5P_FIMV_DECODE_Y_ADR			0x2024
#define S5P_FIMV_DECODE_C_ADR			0x2028

/* Decoded frame tpe */
#define S5P_FIMV_DECODE_FRAME_TYPE		0x2020
#define S5P_FIMV_DECODE_FRAME_MASK		7

#define S5P_FIMV_DECODE_FRAME_SKIPPED		0
#define S5P_FIMV_DECODE_FRAME_I_FRAME		1
#define S5P_FIMV_DECODE_FRAME_P_FRAME		2
#define S5P_FIMV_DECODE_FRAME_B_FRAME		3
#define S5P_FIMV_DECODE_FRAME_OTHER_FRAME	4

/* Sizes of buffers required for decoding */
#define S5P_FIMV_DEC_NB_IP_SIZE			(32 * 1024)
#define S5P_FIMV_DEC_VERT_NB_MV_SIZE		(16 * 1024)
#define S5P_FIMV_DEC_NB_DCAC_SIZE		(16 * 1024)
#define S5P_FIMV_DEC_UPNB_MV_SIZE		(68 * 1024)
#define S5P_FIMV_DEC_SUB_ANCHOR_MV_SIZE		(136 * 1024)
#define S5P_FIMV_DEC_OVERLAP_TRANSFORM_SIZE     (32 * 1024)
#define S5P_FIMV_DEC_VC1_BITPLANE_SIZE		(2 * 1024)
#define S5P_FIMV_DEC_STX_PARSER_SIZE		(68 * 1024)

#define S5P_FIMV_DEC_BUF_ALIGN			(8 * 1024)
#define S5P_FIMV_ENC_BUF_ALIGN			(8 * 1024)
#define S5P_FIMV_NV12M_HALIGN			16
#define S5P_FIMV_NV12M_LVALIGN			16
#define S5P_FIMV_NV12M_CVALIGN			8
#define S5P_FIMV_NV12MT_HALIGN			128
#define S5P_FIMV_NV12MT_VALIGN			32
#define S5P_FIMV_NV12M_SALIGN			2048
#define S5P_FIMV_NV12MT_SALIGN			8192

/* Sizes of buffers required for encoding */
#define S5P_FIMV_ENC_UPMV_SIZE		0x10000
#define S5P_FIMV_ENC_COLFLG_SIZE	0x10000
#define S5P_FIMV_ENC_INTRAMD_SIZE	0x10000
#define S5P_FIMV_ENC_INTRAPRED_SIZE	0x4000
#define S5P_FIMV_ENC_NBORINFO_SIZE	0x10000
#define S5P_FIMV_ENC_ACDCCOEF_SIZE	0x10000

/* Encoder */
#define S5P_FIMV_ENC_SI_STRM_SIZE	0x2004 /* stream size */
#define S5P_FIMV_ENC_SI_PIC_CNT		0x2008 /* picture count */
#define S5P_FIMV_ENC_SI_WRITE_PTR	0x200c /* write pointer */
#define S5P_FIMV_ENC_SI_SLICE_TYPE	0x2010 /* slice type(I/P/B/IDR) */
#define S5P_FIMV_ENC_SI_SLICE_TYPE_NON_CODED	0
#define S5P_FIMV_ENC_SI_SLICE_TYPE_I		1
#define S5P_FIMV_ENC_SI_SLICE_TYPE_P		2
#define S5P_FIMV_ENC_SI_SLICE_TYPE_B		3
#define S5P_FIMV_ENC_SI_SLICE_TYPE_SKIPPED	4
#define S5P_FIMV_ENC_SI_SLICE_TYPE_OTHERS	5
#define S5P_FIMV_ENCODED_Y_ADDR         0x2014 /* the addr of the encoded
								luma pic */
#define S5P_FIMV_ENCODED_C_ADDR         0x2018 /* the addr of the encoded
								chroma pic */

#define S5P_FIMV_ENC_SI_CH0_SB_ADR	0x2044 /* addr of stream buf */
#define S5P_FIMV_ENC_SI_CH0_SB_SIZE	0x204c /* size of stream buf */
#define S5P_FIMV_ENC_SI_CH0_CUR_Y_ADR	0x2050 /* current Luma addr */
#define S5P_FIMV_ENC_SI_CH0_CUR_C_ADR	0x2054 /* current Chroma addr */
#define S5P_FIMV_ENC_SI_CH0_FRAME_INS	0x2058 /* frame insertion */

#define S5P_FIMV_ENC_SI_CH1_SB_ADR	0x2084 /* addr of stream buf */
#define S5P_FIMV_ENC_SI_CH1_SB_SIZE	0x208c /* size of stream buf */
#define S5P_FIMV_ENC_SI_CH1_CUR_Y_ADR	0x2090 /* current Luma addr */
#define S5P_FIMV_ENC_SI_CH1_CUR_C_ADR	0x2094 /* current Chroma addr */
#define S5P_FIMV_ENC_SI_CH1_FRAME_INS	0x2098 /* frame insertion */

#define S5P_FIMV_ENC_PIC_TYPE_CTRL	0xc504 /* pic type level control */
#define S5P_FIMV_ENC_B_RECON_WRITE_ON	0xc508 /* B frame recon write ctrl */
#define S5P_FIMV_ENC_MSLICE_CTRL	0xc50c /* multi slice control */
#define S5P_FIMV_ENC_MSLICE_MB		0xc510 /* MB number in the one slice */
#define S5P_FIMV_ENC_MSLICE_BIT		0xc514 /* bit count for one slice */
#define S5P_FIMV_ENC_CIR_CTRL		0xc518 /* number of intra refresh MB */
#define S5P_FIMV_ENC_MAP_FOR_CUR	0xc51c /* linear or tiled mode */
#define S5P_FIMV_ENC_PADDING_CTRL	0xc520 /* padding control */

#define S5P_FIMV_ENC_RC_CONFIG		0xc5a0 /* RC config */
#define S5P_FIMV_ENC_RC_BIT_RATE	0xc5a8 /* bit rate */
#define S5P_FIMV_ENC_RC_QBOUND		0xc5ac /* max/min QP */
#define S5P_FIMV_ENC_RC_RPARA		0xc5b0 /* rate control reaction coeff */
#define S5P_FIMV_ENC_RC_MB_CTRL		0xc5b4 /* MB adaptive scaling */

/* Encoder for H264 only */
#define S5P_FIMV_ENC_H264_ENTROPY_MODE	0xd004 /* CAVLC or CABAC */
#define S5P_FIMV_ENC_H264_ALPHA_OFF	0xd008 /* loop filter alpha offset */
#define S5P_FIMV_ENC_H264_BETA_OFF	0xd00c /* loop filter beta offset */
#define S5P_FIMV_ENC_H264_NUM_OF_REF	0xd010 /* number of reference for P/B */
#define S5P_FIMV_ENC_H264_TRANS_FLAG	0xd034 /* 8x8 transform flag in PPS &
								high profile */

#define S5P_FIMV_ENC_RC_FRAME_RATE	0xd0d0 /* frame rate */

/* Encoder for MPEG4 only */
#define S5P_FIMV_ENC_MPEG4_QUART_PXL	0xe008 /* qpel interpolation ctrl */

/* Additional */
#define S5P_FIMV_SI_CH0_DPB_CONF_CTRL   0x2068 /* DPB Config Control Register */
#define S5P_FIMV_SLICE_INT_MASK		1
#define S5P_FIMV_SLICE_INT_SHIFT	31
#define S5P_FIMV_DDELAY_ENA_SHIFT	30
#define S5P_FIMV_DDELAY_VAL_MASK	0xff
#define S5P_FIMV_DDELAY_VAL_SHIFT	16
#define S5P_FIMV_DPB_COUNT_MASK		0xffff
#define S5P_FIMV_DPB_FLUSH_MASK		1
#define S5P_FIMV_DPB_FLUSH_SHIFT	14


#define S5P_FIMV_SI_CH0_RELEASE_BUF     0x2060 /* DPB release buffer register */
#define S5P_FIMV_SI_CH0_HOST_WR_ADR	0x2064 /* address of shared memory */

/* Codec numbers  */
#define S5P_FIMV_CODEC_NONE		-1

#define S5P_FIMV_CODEC_H264_DEC		0
#define S5P_FIMV_CODEC_VC1_DEC		1
#define S5P_FIMV_CODEC_MPEG4_DEC	2
#define S5P_FIMV_CODEC_MPEG2_DEC	3
#define S5P_FIMV_CODEC_H263_DEC		4
#define S5P_FIMV_CODEC_VC1RCV_DEC	5

#define S5P_FIMV_CODEC_H264_ENC		16
#define S5P_FIMV_CODEC_MPEG4_ENC	17
#define S5P_FIMV_CODEC_H263_ENC		18

/* Channel Control Register */
#define S5P_FIMV_CH_SEQ_HEADER		1
#define S5P_FIMV_CH_FRAME_START		2
#define S5P_FIMV_CH_LAST_FRAME		3
#define S5P_FIMV_CH_INIT_BUFS		4
#define S5P_FIMV_CH_FRAME_START_REALLOC	5
#define S5P_FIMV_CH_MASK		7
#define S5P_FIMV_CH_SHIFT		16


/* Host to RISC command */
#define S5P_FIMV_H2R_CMD_EMPTY		0
#define S5P_FIMV_H2R_CMD_OPEN_INSTANCE	1
#define S5P_FIMV_H2R_CMD_CLOSE_INSTANCE	2
#define S5P_FIMV_H2R_CMD_SYS_INIT	3
#define S5P_FIMV_H2R_CMD_FLUSH		4
#define S5P_FIMV_H2R_CMD_SLEEP		5
#define S5P_FIMV_H2R_CMD_WAKEUP		6

#define S5P_FIMV_R2H_CMD_EMPTY			0
#define S5P_FIMV_R2H_CMD_OPEN_INSTANCE_RET	1
#define S5P_FIMV_R2H_CMD_CLOSE_INSTANCE_RET	2
#define S5P_FIMV_R2H_CMD_RSV_RET		3
#define S5P_FIMV_R2H_CMD_SEQ_DONE_RET		4
#define S5P_FIMV_R2H_CMD_FRAME_DONE_RET		5
#define S5P_FIMV_R2H_CMD_SLICE_DONE_RET		6
#define S5P_FIMV_R2H_CMD_ENC_COMPLETE_RET	7
#define S5P_FIMV_R2H_CMD_SYS_INIT_RET		8
#define S5P_FIMV_R2H_CMD_FW_STATUS_RET		9
#define S5P_FIMV_R2H_CMD_SLEEP_RET		10
#define S5P_FIMV_R2H_CMD_WAKEUP_RET		11
#define S5P_FIMV_R2H_CMD_FLUSH_RET		12
#define S5P_FIMV_R2H_CMD_INIT_BUFFERS_RET	15
#define S5P_FIMV_R2H_CMD_EDFU_INIT_RET		16
#define S5P_FIMV_R2H_CMD_ERR_RET		32

/* Error handling defines */
#define S5P_FIMV_ERR_WARNINGS_START		145
#define S5P_FIMV_ERR_DEC_MASK			0xFFFF
#define S5P_FIMV_ERR_DEC_SHIFT			0
#define S5P_FIMV_ERR_DSPL_MASK			0xFFFF0000
#define S5P_FIMV_ERR_DSPL_SHIFT			16

/* Shared memory registers' offsets */

/* An offset of the start position in the stream when
 * the start position is not aligned */
#define S5P_FIMV_SHARED_CROP_INFO_H		0x0020
#define S5P_FIMV_SHARED_CROP_LEFT_MASK		0xFFFF
#define S5P_FIMV_SHARED_CROP_LEFT_SHIFT		0
#define S5P_FIMV_SHARED_CROP_RIGHT_MASK		0xFFFF0000
#define S5P_FIMV_SHARED_CROP_RIGHT_SHIFT	16
#define S5P_FIMV_SHARED_CROP_INFO_V		0x0024
#define S5P_FIMV_SHARED_CROP_TOP_MASK		0xFFFF
#define S5P_FIMV_SHARED_CROP_TOP_SHIFT		0
#define S5P_FIMV_SHARED_CROP_BOTTOM_MASK	0xFFFF0000
#define S5P_FIMV_SHARED_CROP_BOTTOM_SHIFT	16
#define S5P_FIMV_SHARED_SET_FRAME_TAG		0x0004
#define S5P_FIMV_SHARED_GET_FRAME_TAG_TOP	0x0008
#define S5P_FIMV_SHARED_GET_FRAME_TAG_BOT	0x000C
#define S5P_FIMV_SHARED_START_BYTE_NUM		0x0018
#define S5P_FIMV_SHARED_RC_VOP_TIMING		0x0030
#define S5P_FIMV_SHARED_LUMA_DPB_SIZE		0x0064
#define S5P_FIMV_SHARED_CHROMA_DPB_SIZE		0x0068
#define S5P_FIMV_SHARED_MV_SIZE			0x006C
#define S5P_FIMV_SHARED_PIC_TIME_TOP		0x0010
#define S5P_FIMV_SHARED_PIC_TIME_BOTTOM		0x0014
#define S5P_FIMV_SHARED_EXT_ENC_CONTROL		0x0028
#define S5P_FIMV_SHARED_P_B_FRAME_QP		0x0070
#define S5P_FIMV_SHARED_ASPECT_RATIO_IDC	0x0074
#define S5P_FIMV_SHARED_EXTENDED_SAR		0x0078
#define S5P_FIMV_SHARED_H264_I_PERIOD		0x009C
#define S5P_FIMV_SHARED_RC_CONTROL_CONFIG	0x00A0

#endif /* _REGS_FIMV_H */
