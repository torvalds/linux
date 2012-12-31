/*
 * linux/arch/arm/mach-exynos/include/mach/regs-mfc.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Register definition for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __REGS_MFC_H
#define __REGS_MFC_H __FILE__

#define S5P_MFCREG(x)			(x)

#define MFC_START_ADDR			S5P_MFCREG(0x0000)
#define MFC_END_ADDR			S5P_MFCREG(0xe008)

#define MFC_SW_RESET			S5P_MFCREG(0x0000)
#define MFC_RISC_HOST_INT		S5P_MFCREG(0x0008)

/* Command from HOST to RISC */
#define MFC_HOST2RISC_CMD		S5P_MFCREG(0x0030)
#define MFC_HOST2RISC_ARG1		S5P_MFCREG(0x0034)
#define MFC_HOST2RISC_ARG2		S5P_MFCREG(0x0038)
#define MFC_HOST2RISC_ARG3		S5P_MFCREG(0x003c)
#define MFC_HOST2RISC_ARG4		S5P_MFCREG(0x0040)

/* Command from RISC to HOST */
#define MFC_RISC2HOST_CMD		S5P_MFCREG(0x0044)
#define MFC_RISC2HOST_ARG1		S5P_MFCREG(0x0048)
#define MFC_RISC2HOST_ARG2		S5P_MFCREG(0x004c)
#define MFC_RISC2HOST_ARG3		S5P_MFCREG(0x0050)
#define MFC_RISC2HOST_ARG4		S5P_MFCREG(0x0054)

#define MFC_FW_VERSION			S5P_MFCREG(0x0058)
#define MFC_SYS_MEM_SZ			S5P_MFCREG(0x005c)
#define MFC_FW_STATUS			S5P_MFCREG(0x0080)

/* Memory controller register */
#define MFC_MC_DRAMBASE_ADR_A		S5P_MFCREG(0x0508)
#define MFC_MC_DRAMBASE_ADR_B		S5P_MFCREG(0x050c)
#define MFC_MC_STATUS			S5P_MFCREG(0x0510)

/* Common register */
#define MFC_SYS_MEM_ADR			S5P_MFCREG(0x0600) /* firmware buffer */
#define MFC_CPB_BUF_ADR			S5P_MFCREG(0x0604) /* stream buffer */
#define MFC_DESC_BUF_ADR		S5P_MFCREG(0x0608) /* descriptor buffer */
#define MFC_LUMA_ADR			S5P_MFCREG(0x0700) /* Luma0 ~ Luma18 */
#define MFC_CHROMA_ADR			S5P_MFCREG(0x0600) /* Chroma0 ~ Chroma18 */

#define MFC_B_RECON_LUMA_ADR		S5P_MFCREG(0x062c)
#define MFC_B_RECON_CHROMA_ADR		S5P_MFCREG(0x0630)

/* H264 decoding */
#define MFC_VERT_NB_MV_ADR		S5P_MFCREG(0x068c) /* vertical neighbor motion vector */
#define MFC_VERT_NB_IP_ADR		S5P_MFCREG(0x0690) /* neighbor pixels for intra pred */
#define MFC_MV_ADR			S5P_MFCREG(0x0780) /* H264 motion vector */

/* H263/MPEG4/MPEG2/VC-1 decoding */
#define MFC_NB_DCAC_ADR			S5P_MFCREG(0x068c) /* neighbor AC/DC coeff. buffer */
#define MFC_UP_NB_MV_ADR		S5P_MFCREG(0x0690) /* upper neighbor motion vector buffer */
#define MFC_SA_MV_ADR			S5P_MFCREG(0x0694) /* subseq. anchor motion vector buffer */
#define MFC_OT_LINE_ADR			S5P_MFCREG(0x0698) /* overlap transform line buffer */
#define MFC_BITPLANE3_ADR		S5P_MFCREG(0x069c) /* bitplane3 addr */
#define MFC_BITPLANE2_ADR		S5P_MFCREG(0x06a0) /* bitplane2 addr */
#define MFC_BITPLANE1_ADR		S5P_MFCREG(0x06a4) /* bitplane1 addr */
#define MFC_SP_ADR			S5P_MFCREG(0x06a8) /* syntax parser addr */

/* Encoder register */
#define MFC_UP_MV_ADR			S5P_MFCREG(0x0600) /* upper motion vector addr */
#define MFC_COLZERO_FLAG_ADR		S5P_MFCREG(0x0610) /* direct colocaated zero flag addr */
#define MFC_UP_INTRA_MD_ADR		S5P_MFCREG(0x0608) /* upper intra MD addr */
#define MFC_UP_INTRA_PRED_ADR		S5P_MFCREG(0x0740) /* upper intra PRED addr */
#define MFC_NBOR_INFO_ADR		S5P_MFCREG(0x0604) /* entropy engine's neighbor inform and AC/DC coeff. */

#define MFC_ENC_REF0_LUMA_ADR		S5P_MFCREG(0x061c) /* ref0 Luma addr */
#define MFC_ENC_REF0_CHROMA_ADR		S5P_MFCREG(0x0700) /* ref0 Chroma addr */
#define MFC_ENC_REF1_LUMA_ADR		S5P_MFCREG(0x0620) /* ref1 Luma addr */
#define MFC_ENC_REF1_CHROMA_ADR		S5P_MFCREG(0x0704) /* ref1 Chroma addr */
#define MFC_ENC_REF2_LUMA_ADR		S5P_MFCREG(0x0710) /* ref2 Luma addr */
#define MFC_ENC_REF2_CHROMA_ADR		S5P_MFCREG(0x0708) /* ref2 Chroma addr */
#define MFC_ENC_REF3_LUMA_ADR		S5P_MFCREG(0x0714) /* ref3 Luma addr */
#define MFC_ENC_REF3_CHROMA_ADR		S5P_MFCREG(0x070c) /* ref3 Chroma addr */

/* Codec common register */
#define MFC_ENC_HSIZE_PX		S5P_MFCREG(0x0818) /* frame width at encoder */
#define MFC_ENC_VSIZE_PX		S5P_MFCREG(0x081c) /* frame height at encoder */
#define MFC_ENC_PROFILE			S5P_MFCREG(0x0830) /* profile register */
#define MFC_ENC_PIC_STRUCT		S5P_MFCREG(0x083c) /* picture field/frame flag */
#define MFC_ENC_LF_CTRL			S5P_MFCREG(0x0848) /* loop filter control */
#define MFC_ENC_ALPHA_OFF		S5P_MFCREG(0x084c) /* loop filter alpha offset */
#define MFC_ENC_BETA_OFF		S5P_MFCREG(0x0850) /* loop filter beta offset */
#define MFC_MR_BUSIF_CTRL		S5P_MFCREG(0x0854) /* hidden, bus interface ctrl */
#define MFC_ENC_PXL_CACHE_CTRL		S5P_MFCREG(0x0a00) /* pixel cache control */

/* Channel & stream interface register */
#define MFC_SI_RTN_CHID			S5P_MFCREG(0x2000) /* Return CH instance ID register */
#define MFC_SI_CH1_INST_ID		S5P_MFCREG(0x2040) /* codec instance ID */
#define MFC_SI_CH2_INST_ID		S5P_MFCREG(0x2080) /* codec instance ID */

/* Decoder */
#define MFC_SI_VRESOL			S5P_MFCREG(0x2004) /* vertical resolution of decoder */
#define MFC_SI_HRESOL			S5P_MFCREG(0x2008) /* horizontal resolution of decoder */
#define MFC_SI_BUF_NUMBER		S5P_MFCREG(0x200c) /* number of frames in the decoded pic */
#define MFC_SI_DISPLAY_Y_ADR		S5P_MFCREG(0x2010) /* luma address of displayed pic */
#define MFC_SI_DISPLAY_C_ADR		S5P_MFCREG(0x2014) /* chroma address of displayed pic */
#define MFC_SI_FRM_COUNT		S5P_MFCREG(0x2018) /* the number of frames so far decoded */
#define MFC_SI_DISPLAY_STATUS		S5P_MFCREG(0x201c) /* Display status of decoded picture */
#define MFC_SI_FRAME_TYPE		S5P_MFCREG(0x2020) /* frame type such as skip/I/P/B */
#define MFC_SI_DECODE_Y_ADR		S5P_MFCREG(0x2024) /* luma address of decoded pic */
#define MFC_SI_DECODE_C_ADR		S5P_MFCREG(0x2028) /* chroma address of decoded pic */
#define MFC_SI_DECODE_STATUS		S5P_MFCREG(0x202c) /* decoded status */

#define MFC_SI_CH1_ES_ADR		S5P_MFCREG(0x2044) /* start addr of stream buf */
#define MFC_SI_CH1_ES_SIZE		S5P_MFCREG(0x2048) /* size of stream buf */
#define MFC_SI_CH1_DESC_ADR		S5P_MFCREG(0x204c) /* addr of descriptor buf */
#define MFC_SI_CH1_CPB_SIZE		S5P_MFCREG(0x2058) /* max size of coded pic. buf */
#define MFC_SI_CH1_DESC_SIZE		S5P_MFCREG(0x205c) /* max size of descriptor buf */
#define MFC_SI_CH1_RELEASE_BUF		S5P_MFCREG(0x2060) /* release buffer register */
#define MFC_SI_CH1_HOST_WR_ADR		S5P_MFCREG(0x2064) /* shared memory address */
#define MFC_SI_CH1_DPB_CONF_CTRL	S5P_MFCREG(0x2068) /* DPB Configuration Control Register */

#define MFC_SI_CH2_ES_ADR		S5P_MFCREG(0x2084) /* start addr of stream buf */
#define MFC_SI_CH2_ES_SIZE		S5P_MFCREG(0x2088) /* size of stream buf */
#define MFC_SI_CH2_DESC_ADR		S5P_MFCREG(0x208c) /* addr of descriptor buf */
#define MFC_SI_CH2_CPB_SIZE		S5P_MFCREG(0x2098) /* max size of coded pic. buf */
#define MFC_SI_CH2_DESC_SIZE		S5P_MFCREG(0x209c) /* max size of descriptor buf */
#define MFC_SI_CH2_RELEASE_BUF		S5P_MFCREG(0x20a0) /* release buffer register */
#define MFC_SI_CH2_HOST_WR_ADR		S5P_MFCREG(0x20a4) /* shared memory address */
#define MFC_SI_CH2_DPB_CONF_CTRL	S5P_MFCREG(0x20a8) /* DPB Configuration Control Register */

#define MFC_SI_FIMV1_VRESOL		S5P_MFCREG(0x2050) /* vertical resolution */
#define MFC_SI_FIMV1_HRESOL		S5P_MFCREG(0x2054) /* horizontal resolution */
#define MFC_CRC_LUMA0			S5P_MFCREG(0x2030) /* luma crc data per frame(or top field) */
#define MFC_CRC_CHROMA0			S5P_MFCREG(0x2034) /* chroma crc data per frame(or top field) */
#define MFC_CRC_LUMA1			S5P_MFCREG(0x2038) /* luma crc data per bottom field */
#define MFC_CRC_CHROMA1			S5P_MFCREG(0x203c) /* chroma crc data per bottom field */

/* Encoder */
#define MFC_ENC_SI_STRM_SIZE		S5P_MFCREG(0x2004) /* stream size */
#define MFC_ENC_SI_PIC_CNT		S5P_MFCREG(0x2008) /* picture count */
#define MFC_ENC_SI_WRITE_PTR		S5P_MFCREG(0x200c) /* write pointer */
#define MFC_ENC_SI_SLICE_TYPE		S5P_MFCREG(0x2010) /* slice type(I/P/B/IDR) */
#define MFC_ENCODED_Y_ADDR		S5P_MFCREG(0x2014) /* the address of the encoded luminance picture */
#define MFC_ENCODED_C_ADDR		S5P_MFCREG(0x2018) /* the address of the encoded chrominance picture */

#define MFC_ENC_SI_CH1_SB_ADR		S5P_MFCREG(0x2044) /* addr of stream buf */
#define MFC_ENC_SI_CH1_SB_SIZE		S5P_MFCREG(0x204c) /* size of stream buf */
#define MFC_ENC_SI_CH1_CUR_Y_ADR	S5P_MFCREG(0x2050) /* current Luma addr */
#define MFC_ENC_SI_CH1_CUR_C_ADR	S5P_MFCREG(0x2054) /* current Chroma addr */
#define MFC_ENC_SI_CH1_FRAME_INS	S5P_MFCREG(0x2058) /* frame insertion control register */
#define MFC_ENC_SI_CH1_INPUT_FLUSH	S5P_MFCREG(0x2068) /* flusing input buffer */

#define MFC_ENC_SI_CH2_SB_ADR		S5P_MFCREG(0x2084) /* addr of stream buf */
#define MFC_ENC_SI_CH2_SB_SIZE		S5P_MFCREG(0x208c) /* size of stream buf */
#define MFC_ENC_SI_CH2_CUR_Y_ADR	S5P_MFCREG(0x2090) /* current Luma addr */
#define MFC_ENC_SI_CH2_CUR_C_ADR	S5P_MFCREG(0x2094) /* current Chroma addr */
#define MFC_ENC_SI_CH2_FRAME_INS	S5P_MFCREG(0x2098) /* frame insertion control register */
#define MFC_ENC_SI_CH2_INPUT_FLUSH	S5P_MFCREG(0x20A8) /* flusing input buffer */

#define MFC_ENC_PIC_TYPE_CTRL		S5P_MFCREG(0xc504) /* pic type level control */
#define MFC_ENC_B_RECON_WRITE_ON	S5P_MFCREG(0xc508) /* B frame recon data write cotrl */
#define MFC_ENC_MSLICE_CTRL		S5P_MFCREG(0xc50c) /* multi slice control */
#define MFC_ENC_MSLICE_MB		S5P_MFCREG(0xc510) /* MB number in the one slice */
#define MFC_ENC_MSLICE_BIT		S5P_MFCREG(0xc514) /* bit count number for one slice */
#define MFC_ENC_CIR_CTRL		S5P_MFCREG(0xc518) /* number of intra refresh MB */
#define MFC_ENC_MAP_FOR_CUR		S5P_MFCREG(0xc51c) /* linear or 64x32 tiled mode */
#define MFC_ENC_PADDING_CTRL		S5P_MFCREG(0xc520) /* padding control */

#define MFC_ENC_NV21_SEL		S5P_MFCREG(0xc548) /* chroma interleaving order */

#define MFC_ENC_INTRA_BIAS		S5P_MFCREG(0xc588) /* intra mode bias for the MB mode */
#define MFC_ENC_BI_DIRECT_BIAS		S5P_MFCREG(0xc58c) /* bi-directional mode bias for the MB mode */

#define MFC_ENC_RC_CONFIG		S5P_MFCREG(0xc5a0) /* RC config */
#define MFC_ENC_RC_BIT_RATE		S5P_MFCREG(0xc5a8) /* bit rate */
#define MFC_ENC_RC_QBOUND		S5P_MFCREG(0xc5ac) /* max/min QP */
#define MFC_ENC_RC_RPARA		S5P_MFCREG(0xc5b0) /* rate control reaction coeff. */
#define MFC_ENC_RC_MB_CTRL		S5P_MFCREG(0xc5b4) /* MB adaptive scaling */

/* Encoder for H264 */
#define MFC_ENC_H264_ENTRP_MODE		S5P_MFCREG(0xd004) /* CAVLC or CABAC */
#define MFC_ENC_H264_ALPHA_OFF		S5P_MFCREG(0xd008) /* loop filter alpha offset */
#define MFC_ENC_H264_BETA_OFF		S5P_MFCREG(0xd00c) /* loop filter beta offset */
#define MFC_ENC_H264_NUM_OF_REF		S5P_MFCREG(0xd010) /* number of reference for P/B */
#define MFC_ENC_H264_TRANS_FLAG		S5P_MFCREG(0xd034) /* 8x8 transform flag in PPS & high profile */

#define MFC_ENC_RC_FRAME_RATE		S5P_MFCREG(0xd0d0) /* frame rate */

/* Encoder for MPEG4 */
#define MFC_ENC_MPEG4_QUART_PXL		S5P_MFCREG(0xe008) /* quarter pel interpolation control */

#endif /* __REGS_MFC_H */
