#ifndef QDSP5VFECMDI_H
#define QDSP5VFECMDI_H

/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

    V F E   I N T E R N A L   C O M M A N D S

GENERAL DESCRIPTION
  This file contains defintions of format blocks of commands
  that are accepted by VFE Task

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

$Header: //source/qcom/qct/multimedia2/AdspSvc/7XXX/qdsp5cmd/video/qdsp5vfecmdi.h#2 $ $DateTime: 2008/07/30 10:50:23 $ $Author: pavanr $
Revision History:

when       who     what, where, why
--------   ---     ----------------------------------------------------------
06/12/08   sv      initial version
===========================================================================*/

/******************************************************************************
 * Commands through vfeCommandScaleQueue
 *****************************************************************************/

/*
 * Command to program scaler for op1 . max op of scaler is VGA
 */


#define	VFE_CMD_SCALE_OP1_CFG		0x0000
#define	VFE_CMD_SCALE_OP1_CFG_LEN	\
	sizeof(vfe_cmd_scale_op1_cfg)

#define	VFE_CMD_SCALE_OP1_SEL_IP_SEL_Y_STANDARD	0x0000
#define	VFE_CMD_SCALE_OP1_SEL_IP_SEL_Y_CASCADED	0x0001
#define	VFE_CMD_SCALE_OP1_SEL_H_Y_SCALER_DIS	0x0000
#define	VFE_CMD_SCALE_OP1_SEL_H_Y_SCALER_ENA	0x0002
#define	VFE_CMD_SCALE_OP1_SEL_H_PP_Y_SCALER_DIS	0x0000
#define	VFE_CMD_SCALE_OP1_SEL_H_PP_Y_SCALER_ENA	0x0004
#define	VFE_CMD_SCALE_OP1_SEL_V_Y_SCALER_DIS	0x0000
#define	VFE_CMD_SCALE_OP1_SEL_V_Y_SCALER_ENA	0x0008
#define	VFE_CMD_SCALE_OP1_SEL_V_PP_Y_SCALER_DIS	0x0000
#define	VFE_CMD_SCALE_OP1_SEL_V_PP_Y_SCALER_ENA	0x0010
#define	VFE_CMD_SCALE_OP1_SEL_IP_SEL_CBCR_STANDARD	0x0000
#define	VFE_CMD_SCALE_OP1_SEL_IP_SEL_CBCR_CASCADED	0x0020
#define	VFE_CMD_SCALE_OP1_SEL_H_CBCR_SCALER_DIS		0x0000
#define	VFE_CMD_SCALE_OP1_SEL_H_CBCR_SCALER_ENA		0x0040
#define	VFE_CMD_SCALE_OP1_SEL_V_CBCR_SCALER_DIS		0x0000
#define	VFE_CMD_SCALE_OP1_SEL_V_CBCR_SCALER_ENA		0x0080

#define	VFE_CMD_OP1_PP_Y_SCALER_CFG_PART1_DONT_LOAD_COEFFS	0x80000000
#define	VFE_CMD_OP1_PP_Y_SCALER_CFG_PART1_LOAD_COEFFS	0x80000000

typedef struct {
	unsigned int	cmd_id;
	unsigned int	scale_op1_sel;
	unsigned int	y_scaler_cfg_part1;
	unsigned int	y_scaler_cfg_part2;
	unsigned int	cbcr_scaler_cfg_part1;
	unsigned int	cbcr_scaler_cfg_part2;
	unsigned int	cbcr_scaler_cfg_part3;
	unsigned int	pp_y_scaler_cfg_part1;
	unsigned int	pp_y_scaler_cfg_part2;
	unsigned int	y_scaler_v_coeff_bank_part1[16];
	unsigned int	y_scaler_v_coeff_bank_part2[16];
	unsigned int	y_scaler_h_coeff_bank_part1[16];
	unsigned int	y_scaler_h_coeff_bank_part2[16];
} __attribute__((packed)) vfe_cmd_scale_op1_cfg;


/*
 * Command to program scaler for op2
 */

#define	VFE_CMD_SCALE_OP2_CFG		0x0001
#define	VFE_CMD_SCALE_OP2_CFG_LEN	\
	sizeof(vfe_cmd_scale_op2_cfg)

#define	VFE_CMD_SCALE_OP2_SEL_IP_SEL_Y_STANDARD	0x0000
#define	VFE_CMD_SCALE_OP2_SEL_IP_SEL_Y_CASCADED	0x0001
#define	VFE_CMD_SCALE_OP2_SEL_H_Y_SCALER_DIS	0x0000
#define	VFE_CMD_SCALE_OP2_SEL_H_Y_SCALER_ENA	0x0002
#define	VFE_CMD_SCALE_OP2_SEL_H_PP_Y_SCALER_DIS	0x0000
#define	VFE_CMD_SCALE_OP2_SEL_H_PP_Y_SCALER_ENA	0x0004
#define	VFE_CMD_SCALE_OP2_SEL_V_Y_SCALER_DIS	0x0000
#define	VFE_CMD_SCALE_OP2_SEL_V_Y_SCALER_ENA	0x0008
#define	VFE_CMD_SCALE_OP2_SEL_V_PP_Y_SCALER_DIS	0x0000
#define	VFE_CMD_SCALE_OP2_SEL_V_PP_Y_SCALER_ENA	0x0010
#define	VFE_CMD_SCALE_OP2_SEL_IP_SEL_CBCR_STANDARD	0x0000
#define	VFE_CMD_SCALE_OP2_SEL_IP_SEL_CBCR_CASCADED	0x0020
#define	VFE_CMD_SCALE_OP2_SEL_H_CBCR_SCALER_DIS		0x0000
#define	VFE_CMD_SCALE_OP2_SEL_H_CBCR_SCALER_ENA		0x0040
#define	VFE_CMD_SCALE_OP2_SEL_V_CBCR_SCALER_DIS		0x0000
#define	VFE_CMD_SCALE_OP2_SEL_V_CBCR_SCALER_ENA		0x0080

#define	VFE_CMD_OP2_PP_Y_SCALER_CFG_PART1_DONT_LOAD_COEFFS	0x80000000
#define	VFE_CMD_OP2_PP_Y_SCALER_CFG_PART1_LOAD_COEFFS		0x80000000

typedef struct {
	unsigned int	cmd_id;
	unsigned int	scale_op2_sel;
	unsigned int	y_scaler_cfg_part1;
	unsigned int	y_scaler_cfg_part2;
	unsigned int	cbcr_scaler_cfg_part1;
	unsigned int	cbcr_scaler_cfg_part2;
	unsigned int	cbcr_scaler_cfg_part3;
	unsigned int	pp_y_scaler_cfg_part1;
	unsigned int	pp_y_scaler_cfg_part2;
	unsigned int	y_scaler_v_coeff_bank_part1[16];
	unsigned int	y_scaler_v_coeff_bank_part2[16];
	unsigned int	y_scaler_h_coeff_bank_part1[16];
	unsigned int	y_scaler_h_coeff_bank_part2[16];
} __attribute__((packed)) vfe_cmd_scale_op2_cfg;


/******************************************************************************
 * Commands through vfeCommandTableQueue
 *****************************************************************************/

/*
 * Command to program the AXI ip paths
 */

#define	VFE_CMD_AXI_IP_CFG		0x0000
#define	VFE_CMD_AXI_IP_CFG_LEN		sizeof(vfe_cmd_axi_ip_cfg)

#define	VFE_CMD_IP_SEL_IP_FORMAT_8	0x0000
#define	VFE_CMD_IP_SEL_IP_FORMAT_10	0x0001
#define	VFE_CMD_IP_SEL_IP_FORMAT_12	0x0002

typedef struct {
	unsigned int	cmd_id;
	unsigned int	ip_sel;
	unsigned int	ip_cfg_part1;
	unsigned int	ip_cfg_part2;
	unsigned int	ip_unpack_cfg_part[6];
	unsigned int	ip_buf_addr[8];
} __attribute__ ((packed)) vfe_cmd_axi_ip_cfg;


/*
 * Command to program axi op paths
 */

#define	VFE_CMD_AXI_OP_CFG	0x0001
#define	VFE_CMD_AXI_OP_CFG_LEN	sizeof(vfe_cmd_axi_op_cfg)

#define	VFE_CMD_OP_SEL_OP1		0x0000
#define	VFE_CMD_OP_SEL_OP2		0x0001
#define	VFE_CMD_OP_SEL_OP1_OP2		0x0002
#define	VFE_CMD_OP_SEL_CTOA		0x0003
#define	VFE_CMD_OP_SEL_CTOA_OP1		0x0004
#define	VFE_CMD_OP_SEL_CTOA_OP2		0x0005
#define	VFE_CMD_OP_SEL_OP_FORMAT_8	0x0000
#define	VFE_CMD_OP_SEL_OP_FORMAT_10	0x0008
#define	VFE_CMD_OP_SEL_OP_FORMAT_12	0x0010


typedef struct {
	unsigned int	cmd_id;
	unsigned int	op_sel;
	unsigned int	op1_y_cfg_part1;
	unsigned int	op1_y_cfg_part2;
	unsigned int	op1_cbcr_cfg_part1;
	unsigned int	op1_cbcr_cfg_part2;
	unsigned int	op2_y_cfg_part1;
	unsigned int	op2_y_cfg_part2;
	unsigned int	op2_cbcr_cfg_part1;
	unsigned int	op2_cbcr_cfg_part2;
	unsigned int	op1_buf1_addr[16];
	unsigned int	op2_buf1_addr[16];
} __attribute__((packed)) vfe_cmd_axi_op_cfg;




/*
 * Command to program the roll off correction module
 */

#define	VFE_CMD_ROLLOFF_CFG	0x0002
#define	VFE_CMD_ROLLOFF_CFG_LEN	\
	sizeof(vfe_cmd_rolloff_cfg)


typedef struct {
	unsigned int	cmd_id;
	unsigned int	correction_opt_center_pos;
	unsigned int	radius_square_entry[32];
	unsigned int	red_table_entry[32];
	unsigned int	green_table_entry[32];
	unsigned int	blue_table_entry[32];
} __attribute__((packed)) vfe_cmd_rolloff_cfg;

/*
 * Command to program RGB gamma table
 */

#define	VFE_CMD_RGB_GAMMA_CFG		0x0003
#define	VFE_CMD_RGB_GAMMA_CFG_LEN	\
	sizeof(vfe_cmd_rgb_gamma_cfg)

#define	VFE_CMD_RGB_GAMMA_SEL_LINEAR		0x0000
#define	VFE_CMD_RGB_GAMMA_SEL_PW_LINEAR		0x0001
typedef struct {
	unsigned int	cmd_id;
	unsigned int	rgb_gamma_sel;
	unsigned int	rgb_gamma_entry[256];
} __attribute__((packed)) vfe_cmd_rgb_gamma_cfg;


/*
 * Command to program luma gamma table for the noise reduction path
 */

#define	VFE_CMD_Y_GAMMA_CFG		0x0004
#define	VFE_CMD_Y_GAMMA_CFG_LEN		\
	sizeof(vfe_cmd_y_gamma_cfg)

#define	VFE_CMD_Y_GAMMA_SEL_LINEAR	0x0000
#define	VFE_CMD_Y_GAMMA_SEL_PW_LINEAR	0x0001

typedef struct {
	unsigned int	cmd_id;
	unsigned int	y_gamma_sel;
	unsigned int	y_gamma_entry[256];
} __attribute__((packed)) vfe_cmd_y_gamma_cfg;



/******************************************************************************
 * Commands through vfeCommandQueue
 *****************************************************************************/

/*
 * Command to reset the VFE to a known good state.All previously programmed
 * Params will be lost
 */


#define	VFE_CMD_RESET		0x0000
#define	VFE_CMD_RESET_LEN	sizeof(vfe_cmd_reset)


typedef struct {
	unsigned short	cmd_id;
} __attribute__((packed)) vfe_cmd_reset;


/*
 * Command to start VFE processing based on the config params
 */


#define	VFE_CMD_START		0x0001
#define	VFE_CMD_START_LEN	sizeof(vfe_cmd_start)

#define	VFE_CMD_STARTUP_PARAMS_SRC_CAMIF	0x0000
#define	VFE_CMD_STARTUP_PARAMS_SRC_AXI		0x0001
#define	VFE_CMD_STARTUP_PARAMS_MODE_CONTINUOUS	0x0000
#define	VFE_CMD_STARTUP_PARAMS_MODE_SNAPSHOT	0x0002

#define	VFE_CMD_IMAGE_PL_BLACK_LVL_CORR_DIS	0x0000
#define	VFE_CMD_IMAGE_PL_BLACK_LVL_CORR_ENA	0x0001
#define	VFE_CMD_IMAGE_PL_ROLLOFF_CORR_DIS	0x0000
#define	VFE_CMD_IMAGE_PL_ROLLOFF_CORR_ENA	0x0002
#define	VFE_CMD_IMAGE_PL_WHITE_BAL_DIS		0x0000
#define	VFE_CMD_IMAGE_PL_WHITE_BAL_ENA		0x0004
#define	VFE_CMD_IMAGE_PL_RGB_GAMMA_DIS		0x0000
#define	VFE_CMD_IMAGE_PL_RGB_GAMMA_ENA		0x0008
#define	VFE_CMD_IMAGE_PL_LUMA_NOISE_RED_PATH_DIS	0x0000
#define	VFE_CMD_IMAGE_PL_LUMA_NOISE_RED_PATH_ENA	0x0010
#define	VFE_CMD_IMAGE_PL_ADP_FILTER_DIS		0x0000
#define	VFE_CMD_IMAGE_PL_ADP_FILTER_ENA		0x0020
#define	VFE_CMD_IMAGE_PL_CHROMA_SAMP_DIS	0x0000
#define	VFE_CMD_IMAGE_PL_CHROMA_SAMP_ENA	0x0040


typedef struct {
	unsigned int	cmd_id;
	unsigned int	startup_params;
	unsigned int	image_pipeline;
	unsigned int	frame_dimension;
} __attribute__((packed)) vfe_cmd_start;


/*
 * Command to halt all processing
 */

#define	VFE_CMD_STOP		0x0002
#define	VFE_CMD_STOP_LEN	sizeof(vfe_cmd_stop)

typedef struct {
	unsigned short	cmd_id;
} __attribute__((packed)) vfe_cmd_stop;


/*
 * Command to commit the params that have been programmed to take
 * effect on the next frame
 */

#define	VFE_CMD_UPDATE		0x0003
#define	VFE_CMD_UPDATE_LEN	sizeof(vfe_cmd_update)


typedef struct {
	unsigned short	cmd_id;
} __attribute__((packed)) vfe_cmd_update;


/*
 * Command to program CAMIF module
 */

#define	VFE_CMD_CAMIF_CFG	0x0004
#define	VFE_CMD_CAMIF_CFG_LEN	sizeof(vfe_cmd_camif_cfg)

#define	VFE_CMD_CFG_VSYNC_SYNC_EDGE_HIGH	0x0000
#define	VFE_CMD_CFG_VSYNC_SYNC_EDGE_LOW		0x0002
#define	VFE_CMD_CFG_HSYNC_SYNC_EDGE_HIGH	0x0000
#define	VFE_CMD_CFG_HSYNC_SYNC_EDGE_LOW		0x0004
#define	VFE_CMD_CFG_SYNC_MODE_APS		0x0000
#define	VFE_CMD_CFG_SYNC_MODE_EFS		0X0008
#define	VFE_CMD_CFG_SYNC_MODE_ELS		0x0010
#define	VFE_CMD_CFG_SYNC_MODE_RVD		0x0018
#define	VFE_CMD_CFG_VFE_SUBSAMP_EN_DIS		0x0000
#define	VFE_CMD_CFG_VFE_SUBSAMP_EN_ENA		0x0020
#define	VFE_CMD_CFG_BUS_SUBSAMP_EN_DIS		0x0000
#define	VFE_CMD_CFG_BUS_SUBSAMP_EN_ENA		0x0080
#define	VFE_CMD_CFG_IRQ_SUBSAMP_EN_DIS		0x0000
#define	VFE_CMD_CFG_IRQ_SUBSAMP_EN_ENA		0x0800

#define	VFE_CMD_SUBSAMP2_CFG_PIXEL_SKIP_16	0x0000
#define	VFE_CMD_SUBSAMP2_CFG_PIXEL_SKIP_12	0x0010

#define	VFE_CMD_EPOCH_IRQ_1_DIS			0x0000
#define	VFE_CMD_EPOCH_IRQ_1_ENA			0x4000
#define	VFE_CMD_EPOCH_IRQ_2_DIS			0x0000
#define	VFE_CMD_EPOCH_IRQ_2_ENA			0x8000

typedef struct {
	unsigned int	cmd_id;
	unsigned int	cfg;
	unsigned int	efs_cfg;
	unsigned int	frame_cfg;
	unsigned int	window_width_cfg;
	unsigned int	window_height_cfg;
	unsigned int	subsamp1_cfg;
	unsigned int	subsamp2_cfg;
	unsigned int	epoch_irq;
} __attribute__((packed)) vfe_cmd_camif_cfg;



/*
 * Command to program the black level module
 */

#define	VFE_CMD_BLACK_LVL_CFG		0x0005
#define	VFE_CMD_BLACK_LVL_CFG_LEN	sizeof(vfe_cmd_black_lvl_cfg)

#define	VFE_CMD_BL_SEL_MANUAL		0x0000
#define	VFE_CMD_BL_SEL_AUTO		0x0001

typedef struct {
	unsigned int	cmd_id;
	unsigned int	black_lvl_sel;
	unsigned int	cfg_part[3];
} __attribute__((packed)) vfe_cmd_black_lvl_cfg;


/*
 * Command to program the active region by cropping the region of interest
 */

#define	VFE_CMD_ACTIVE_REGION_CFG	0x0006
#define	VFE_CMD_ACTIVE_REGION_CFG_LEN	\
	sizeof(vfe_cmd_active_region_cfg)


typedef struct {
	unsigned int	cmd_id;
	unsigned int	cfg_part1;
	unsigned int	cfg_part2;
} __attribute__((packed)) vfe_cmd_active_region_cfg;



/*
 * Command to program the defective pixel correction(DPC) ,
 * adaptive bayer filter (ABF) and demosaic modules
 */

#define	VFE_CMD_DEMOSAIC_CFG		0x0007
#define	VFE_CMD_DEMOSAIC_CFG_LEN	sizeof(vfe_cmd_demosaic_cfg)

#define	VFE_CMD_DEMOSAIC_PART1_ABF_EN_DIS	0x0000
#define	VFE_CMD_DEMOSAIC_PART1_ABF_EN_ENA	0x0001
#define	VFE_CMD_DEMOSAIC_PART1_DPC_EN_DIS	0x0000
#define	VFE_CMD_DEMOSAIC_PART1_DPC_EN_ENA	0x0002
#define	VFE_CMD_DEMOSAIC_PART1_FORCE_ABF_OFF	0x0000
#define	VFE_CMD_DEMOSAIC_PART1_FORCE_ABF_ON	0x0004
#define	VFE_CMD_DEMOSAIC_PART1_SLOPE_SHIFT_1	0x00000000
#define	VFE_CMD_DEMOSAIC_PART1_SLOPE_SHIFT_2	0x10000000
#define	VFE_CMD_DEMOSAIC_PART1_SLOPE_SHIFT_4	0x20000000
#define	VFE_CMD_DEMOSAIC_PART1_SLOPE_SHIFT_8	0x30000000
#define	VFE_CMD_DEMOSAIC_PART1_SLOPE_SHIFT_1_2	0x50000000
#define	VFE_CMD_DEMOSAIC_PART1_SLOPE_SHIFT_1_4	0x60000000
#define	VFE_CMD_DEMOSAIC_PART1_SLOPE_SHIFT_1_8	0x70000000

typedef struct {
	unsigned int	cmd_id;
	unsigned int	demosaic_part1;
	unsigned int	demosaic_part2;
	unsigned int	demosaic_part3;
	unsigned int	demosaic_part4;
	unsigned int	demosaic_part5;
} __attribute__((packed)) vfe_cmd_demosaic_cfg;


/*
 * Command to program the ip format
 */

#define	VFE_CMD_IP_FORMAT_CFG		0x0008
#define	VFE_CMD_IP_FORMAT_CFG_LEN	\
	sizeof(vfe_cmd_ip_format_cfg)

#define	VFE_CMD_IP_FORMAT_SEL_RGRG	0x0000
#define	VFE_CMD_IP_FORMAT_SEL_GRGR	0x0001
#define	VFE_CMD_IP_FORMAT_SEL_BGBG	0x0002
#define	VFE_CMD_IP_FORMAT_SEL_GBGB	0x0003
#define	VFE_CMD_IP_FORMAT_SEL_YCBYCR	0x0004
#define	VFE_CMD_IP_FORMAT_SEL_YCRYCB	0x0005
#define	VFE_CMD_IP_FORMAT_SEL_CBYCRY	0x0006
#define	VFE_CMD_IP_FORMAT_SEL_CRYCBY	0x0007
#define	VFE_CMD_IP_FORMAT_SEL_NO_CHROMA	0x0000
#define	VFE_CMD_IP_FORMAT_SEL_CHROMA	0x0008


typedef struct {
	unsigned int	cmd_id;
	unsigned int	ip_format_sel;
	unsigned int	balance_gains_part1;
	unsigned int	balance_gains_part2;
} __attribute__((packed)) vfe_cmd_ip_format_cfg;



/*
 * Command to program max and min allowed op values
 */

#define	VFE_CMD_OP_CLAMP_CFG		0x0009
#define	VFE_CMD_OP_CLAMP_CFG_LEN	\
	sizeof(vfe_cmd_op_clamp_cfg)

typedef struct {
	unsigned int	cmd_id;
	unsigned int	op_clamp_max;
	unsigned int	op_clamp_min;
} __attribute__((packed)) vfe_cmd_op_clamp_cfg;


/*
 * Command to program chroma sub sample module
 */

#define	VFE_CMD_CHROMA_SUBSAMPLE_CFG		0x000A
#define	VFE_CMD_CHROMA_SUBSAMPLE_CFG_LEN	\
	sizeof(vfe_cmd_chroma_subsample_cfg)

#define	VFE_CMD_CHROMA_SUBSAMP_SEL_H_INTERESTIAL_SAMPS	0x0000
#define	VFE_CMD_CHROMA_SUBSAMP_SEL_H_COSITED_SAMPS	0x0001
#define	VFE_CMD_CHROMA_SUBSAMP_SEL_V_INTERESTIAL_SAMPS	0x0000
#define	VFE_CMD_CHROMA_SUBSAMP_SEL_V_COSITED_SAMPS	0x0002
#define	VFE_CMD_CHROMA_SUBSAMP_SEL_H_SUBSAMP_DIS	0x0000
#define	VFE_CMD_CHROMA_SUBSAMP_SEL_H_SUBSAMP_ENA	0x0004
#define	VFE_CMD_CHROMA_SUBSAMP_SEL_V_SUBSAMP_DIS	0x0000
#define	VFE_CMD_CHROMA_SUBSAMP_SEL_V_SUBSAMP_ENA	0x0008

typedef struct {
	unsigned int	cmd_id;
	unsigned int	chroma_subsamp_sel;
} __attribute__((packed)) vfe_cmd_chroma_subsample_cfg;


/*
 * Command to program the white balance module
 */

#define	VFE_CMD_WHITE_BALANCE_CFG	0x000B
#define	VFE_CMD_WHITE_BALANCE_CFG_LEN	\
	sizeof(vfe_cmd_white_balance_cfg)

typedef struct {
	unsigned int	cmd_id;
	unsigned int	white_balance_gains;
} __attribute__((packed)) vfe_cmd_white_balance_cfg;


/*
 * Command to program the color processing module
 */

#define	VFE_CMD_COLOR_PROCESS_CFG	0x000C
#define	VFE_CMD_COLOR_PROCESS_CFG_LEN	\
	sizeof(vfe_cmd_color_process_cfg)

#define	VFE_CMD_COLOR_CORRE_PART7_Q7_FACTORS	0x0000
#define	VFE_CMD_COLOR_CORRE_PART7_Q8_FACTORS	0x0001
#define	VFE_CMD_COLOR_CORRE_PART7_Q9_FACTORS	0x0002
#define	VFE_CMD_COLOR_CORRE_PART7_Q10_FACTORS	0x0003

typedef struct {
	unsigned int	cmd_id;
	unsigned int	color_correction_part1;
	unsigned int	color_correction_part2;
	unsigned int	color_correction_part3;
	unsigned int	color_correction_part4;
	unsigned int	color_correction_part5;
	unsigned int	color_correction_part6;
	unsigned int	color_correction_part7;
	unsigned int	chroma_enhance_part1;
	unsigned int	chroma_enhance_part2;
	unsigned int	chroma_enhance_part3;
	unsigned int	chroma_enhance_part4;
	unsigned int	chroma_enhance_part5;
	unsigned int	luma_calc_part1;
	unsigned int	luma_calc_part2;
} __attribute__((packed)) vfe_cmd_color_process_cfg;


/*
 * Command to program adaptive filter module
 */

#define	VFE_CMD_ADP_FILTER_CFG		0x000D
#define	VFE_CMD_ADP_FILTER_CFG_LEN	\
	sizeof(vfe_cmd_adp_filter_cfg)

#define	VFE_CMD_ASF_CFG_PART_SMOOTH_FILTER_DIS		0x0000
#define	VFE_CMD_ASF_CFG_PART_SMOOTH_FILTER_ENA		0x0001
#define	VFE_CMD_ASF_CFG_PART_NO_SHARP_MODE		0x0000
#define	VFE_CMD_ASF_CFG_PART_SINGLE_FILTER		0x0002
#define	VFE_CMD_ASF_CFG_PART_DUAL_FILTER		0x0004
#define	VFE_CMD_ASF_CFG_PART_SHARP_MODE			0x0007

typedef struct {
	unsigned int	cmd_id;
	unsigned int	asf_cfg_part[7];
} __attribute__((packed)) vfe_cmd_adp_filter_cfg;


/*
 * Command to program for frame skip pattern for op1 and op2
 */

#define	VFE_CMD_FRAME_SKIP_CFG		0x000E
#define	VFE_CMD_FRAME_SKIP_CFG_LEN	\
	sizeof(vfe_cmd_frame_skip_cfg)

typedef struct {
	unsigned int	cmd_id;
	unsigned int	frame_skip_pattern_op1;
	unsigned int	frame_skip_pattern_op2;
} __attribute__((packed)) vfe_cmd_frame_skip_cfg;


/*
 * Command to program field-of-view crop for digital zoom
 */

#define	VFE_CMD_FOV_CROP	0x000F
#define	VFE_CMD_FOV_CROP_LEN	sizeof(vfe_cmd_fov_crop)

typedef struct {
	unsigned int	cmd_id;
	unsigned int	fov_crop_part1;
	unsigned int	fov_crop_part2;
} __attribute__((packed)) vfe_cmd_fov_crop;



/*
 * Command to program auto focus(AF) statistics module
 */

#define	VFE_CMD_STATS_AUTOFOCUS_CFG	0x0010
#define	VFE_CMD_STATS_AUTOFOCUS_CFG_LEN	\
	sizeof(vfe_cmd_stats_autofocus_cfg)

#define	VFE_CMD_AF_STATS_SEL_STATS_DIS	0x0000
#define	VFE_CMD_AF_STATS_SEL_STATS_ENA	0x0001
#define	VFE_CMD_AF_STATS_SEL_PRI_FIXED	0x0000
#define	VFE_CMD_AF_STATS_SEL_PRI_VAR	0x0002
#define	VFE_CMD_AF_STATS_CFG_PART_METRIC_SUM	0x00000000
#define	VFE_CMD_AF_STATS_CFG_PART_METRIC_MAX	0x00200000

typedef struct {
	unsigned int	cmd_id;
	unsigned int	af_stats_sel;
	unsigned int	af_stats_cfg_part[8];
	unsigned int	af_stats_op_buf_hdr;
	unsigned int	af_stats_op_buf[3];
} __attribute__((packed)) vfe_cmd_stats_autofocus_cfg;


/*
 * Command to program White balance(wb) and exposure (exp)
 * statistics module
 */

#define	VFE_CMD_STATS_WB_EXP_CFG	0x0011
#define	VFE_CMD_STATS_WB_EXP_CFG_LEN	\
	sizeof(vfe_cmd_stats_wb_exp_cfg)

#define	VFE_CMD_WB_EXP_STATS_SEL_STATS_DIS	0x0000
#define	VFE_CMD_WB_EXP_STATS_SEL_STATS_ENA	0x0001
#define	VFE_CMD_WB_EXP_STATS_SEL_PRI_FIXED	0x0000
#define	VFE_CMD_WB_EXP_STATS_SEL_PRI_VAR	0x0002

#define	VFE_CMD_WB_EXP_STATS_CFG_PART1_EXP_REG_8_8	0x0000
#define	VFE_CMD_WB_EXP_STATS_CFG_PART1_EXP_REG_16_16	0x0001
#define	VFE_CMD_WB_EXP_STATS_CFG_PART1_EXP_SREG_8_8	0x0000
#define	VFE_CMD_WB_EXP_STATS_CFG_PART1_EXP_SREG_4_4	0x0002

typedef struct {
	unsigned int	cmd_id;
	unsigned int	wb_exp_stats_sel;
	unsigned int	wb_exp_stats_cfg_part1;
	unsigned int	wb_exp_stats_cfg_part2;
	unsigned int	wb_exp_stats_cfg_part3;
	unsigned int	wb_exp_stats_cfg_part4;
	unsigned int	wb_exp_stats_op_buf_hdr;
	unsigned int	wb_exp_stats_op_buf[3];
} __attribute__((packed)) vfe_cmd_stats_wb_exp_cfg;


/*
 * Command to program histogram(hg) stats module
 */

#define	VFE_CMD_STATS_HG_CFG		0x0012
#define	VFE_CMD_STATS_HG_CFG_LEN	\
	sizeof(vfe_cmd_stats_hg_cfg)

#define	VFE_CMD_HG_STATS_SEL_PRI_FIXED	0x0000
#define	VFE_CMD_HG_STATS_SEL_PRI_VAR	0x0002

typedef struct {
	unsigned int	cmd_id;
	unsigned int	hg_stats_sel;
	unsigned int	hg_stats_cfg_part1;
	unsigned int	hg_stats_cfg_part2;
	unsigned int	hg_stats_op_buf_hdr;
	unsigned int	hg_stats_op_buf;
} __attribute__((packed)) vfe_cmd_stats_hg_cfg;


/*
 * Command to acknowledge last MSG_VFE_OP1 message
 */

#define	VFE_CMD_OP1_ACK		0x0013
#define	VFE_CMD_OP1_ACK_LEN	sizeof(vfe_cmd_op1_ack)

typedef struct {
	unsigned int	cmd_id;
	unsigned int	op1_buf_y_addr;
	unsigned int	op1_buf_cbcr_addr;
} __attribute__((packed)) vfe_cmd_op1_ack;



/*
 * Command to acknowledge last MSG_VFE_OP2 message
 */

#define	VFE_CMD_OP2_ACK		0x0014
#define	VFE_CMD_OP2_ACK_LEN	sizeof(vfe_cmd_op2_ack)

typedef struct {
	unsigned int	cmd_id;
	unsigned int	op2_buf_y_addr;
	unsigned int	op2_buf_cbcr_addr;
} __attribute__((packed)) vfe_cmd_op2_ack;



/*
 * Command to acknowledge MSG_VFE_STATS_AUTOFOCUS msg
 */

#define	VFE_CMD_STATS_AF_ACK		0x0015
#define	VFE_CMD_STATS_AF_ACK_LEN	sizeof(vfe_cmd_stats_af_ack)


typedef struct {
	unsigned int	cmd_id;
	unsigned int	af_stats_op_buf;
} __attribute__((packed)) vfe_cmd_stats_af_ack;


/*
 * Command to acknowledge MSG_VFE_STATS_WB_EXP msg
 */

#define	VFE_CMD_STATS_WB_EXP_ACK	0x0016
#define	VFE_CMD_STATS_WB_EXP_ACK_LEN	sizeof(vfe_cmd_stats_wb_exp_ack)

typedef struct {
	unsigned int	cmd_id;
	unsigned int	wb_exp_stats_op_buf;
} __attribute__((packed)) vfe_cmd_stats_wb_exp_ack;


/*
 * Command to acknowledge MSG_VFE_EPOCH1 message
 */

#define	VFE_CMD_EPOCH1_ACK	0x0017
#define	VFE_CMD_EPOCH1_ACK_LEN	sizeof(vfe_cmd_epoch1_ack)

typedef struct {
	unsigned short cmd_id;
} __attribute__((packed)) vfe_cmd_epoch1_ack;


/*
 * Command to acknowledge MSG_VFE_EPOCH2 message
 */

#define	VFE_CMD_EPOCH2_ACK	0x0018
#define	VFE_CMD_EPOCH2_ACK_LEN	sizeof(vfe_cmd_epoch2_ack)

typedef struct {
	unsigned short cmd_id;
} __attribute__((packed)) vfe_cmd_epoch2_ack;



/*
 * Command to configure, enable or disable synchronous timer1
 */

#define	VFE_CMD_SYNC_TIMER1_CFG		0x0019
#define	VFE_CMD_SYNC_TIMER1_CFG_LEN	\
	sizeof(vfe_cmd_sync_timer1_cfg)

#define	VFE_CMD_SYNC_T1_CFG_PART1_TIMER_DIS	0x0000
#define	VFE_CMD_SYNC_T1_CFG_PART1_TIMER_ENA	0x0001
#define	VFE_CMD_SYNC_T1_CFG_PART1_POL_HIGH	0x0000
#define	VFE_CMD_SYNC_T1_CFG_PART1_POL_LOW	0x0002

typedef struct {
	unsigned int	cmd_id;
	unsigned int	sync_t1_cfg_part1;
	unsigned int	sync_t1_h_sync_countdown;
	unsigned int	sync_t1_pclk_countdown;
	unsigned int	sync_t1_duration;
} __attribute__((packed)) vfe_cmd_sync_timer1_cfg;


/*
 * Command to configure, enable or disable synchronous timer1
 */

#define	VFE_CMD_SYNC_TIMER2_CFG		0x001A
#define	VFE_CMD_SYNC_TIMER2_CFG_LEN	\
	sizeof(vfe_cmd_sync_timer2_cfg)

#define	VFE_CMD_SYNC_T2_CFG_PART1_TIMER_DIS	0x0000
#define	VFE_CMD_SYNC_T2_CFG_PART1_TIMER_ENA	0x0001
#define	VFE_CMD_SYNC_T2_CFG_PART1_POL_HIGH	0x0000
#define	VFE_CMD_SYNC_T2_CFG_PART1_POL_LOW	0x0002

typedef struct {
	unsigned int	cmd_id;
	unsigned int	sync_t2_cfg_part1;
	unsigned int	sync_t2_h_sync_countdown;
	unsigned int	sync_t2_pclk_countdown;
	unsigned int	sync_t2_duration;
} __attribute__((packed)) vfe_cmd_sync_timer2_cfg;


/*
 * Command to configure and start asynchronous timer1
 */

#define	VFE_CMD_ASYNC_TIMER1_START	0x001B
#define	VFE_CMD_ASYNC_TIMER1_START_LEN	\
	sizeof(vfe_cmd_async_timer1_start)

#define	VFE_CMD_ASYNC_T1_POLARITY_A_HIGH	0x0000
#define	VFE_CMD_ASYNC_T1_POLARITY_A_LOW		0x0001
#define	VFE_CMD_ASYNC_T1_POLARITY_B_HIGH	0x0000
#define	VFE_CMD_ASYNC_T1_POLARITY_B_LOW		0x0002

typedef struct {
	unsigned int	cmd_id;
	unsigned int	async_t1a_cfg;
	unsigned int	async_t1b_cfg;
	unsigned int	async_t1_polarity;
} __attribute__((packed)) vfe_cmd_async_timer1_start;


/*
 * Command to configure and start asynchronous timer2
 */

#define	VFE_CMD_ASYNC_TIMER2_START	0x001C
#define	VFE_CMD_ASYNC_TIMER2_START_LEN	\
	sizeof(vfe_cmd_async_timer2_start)

#define	VFE_CMD_ASYNC_T2_POLARITY_A_HIGH	0x0000
#define	VFE_CMD_ASYNC_T2_POLARITY_A_LOW		0x0001
#define	VFE_CMD_ASYNC_T2_POLARITY_B_HIGH	0x0000
#define	VFE_CMD_ASYNC_T2_POLARITY_B_LOW		0x0002

typedef struct {
	unsigned int	cmd_id;
	unsigned int	async_t2a_cfg;
	unsigned int	async_t2b_cfg;
	unsigned int	async_t2_polarity;
} __attribute__((packed)) vfe_cmd_async_timer2_start;


/*
 * Command to program partial configurations of auto focus(af)
 */

#define	VFE_CMD_STATS_AF_UPDATE		0x001D
#define	VFE_CMD_STATS_AF_UPDATE_LEN	\
	sizeof(vfe_cmd_stats_af_update)

#define	VFE_CMD_AF_UPDATE_PART1_WINDOW_ONE	0x00000000
#define	VFE_CMD_AF_UPDATE_PART1_WINDOW_MULTI	0x80000000

typedef struct {
	unsigned int	cmd_id;
	unsigned int	af_update_part1;
	unsigned int	af_update_part2;
} __attribute__((packed)) vfe_cmd_stats_af_update;


/*
 * Command to program partial cfg of wb and exp
 */

#define	VFE_CMD_STATS_WB_EXP_UPDATE	0x001E
#define	VFE_CMD_STATS_WB_EXP_UPDATE_LEN	\
	sizeof(vfe_cmd_stats_wb_exp_update)

#define	VFE_CMD_WB_EXP_UPDATE_PART1_REGIONS_8_8		0x0000
#define	VFE_CMD_WB_EXP_UPDATE_PART1_REGIONS_16_16	0x0001
#define	VFE_CMD_WB_EXP_UPDATE_PART1_SREGIONS_8_8	0x0000
#define	VFE_CMD_WB_EXP_UPDATE_PART1_SREGIONS_4_4	0x0002

typedef struct {
	unsigned int	cmd_id;
	unsigned int	wb_exp_update_part1;
	unsigned int	wb_exp_update_part2;
	unsigned int	wb_exp_update_part3;
	unsigned int	wb_exp_update_part4;
} __attribute__((packed)) vfe_cmd_stats_wb_exp_update;



/*
 * Command to re program the CAMIF FRAME CONFIG settings
 */

#define	VFE_CMD_UPDATE_CAMIF_FRAME_CFG		0x001F
#define	VFE_CMD_UPDATE_CAMIF_FRAME_CFG_LEN	\
	sizeof(vfe_cmd_update_camif_frame_cfg)

typedef struct {
	unsigned int	cmd_id;
	unsigned int	camif_frame_cfg;
} __attribute__((packed)) vfe_cmd_update_camif_frame_cfg;


#endif
