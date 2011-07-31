#ifndef QDSP5AUDPREPROCCMDI_H
#define QDSP5AUDPREPROCCMDI_H

/*====*====*====*====*====*====*====*====*====*====*====*====*====*====*====*

    A U D I O   P R E   P R O C E S S I N G  I N T E R N A L  C O M M A N D S

GENERAL DESCRIPTION
  This file contains defintions of format blocks of commands
  that are accepted by AUDPREPROC Task

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

$Header: //source/qcom/qct/multimedia2/Audio/drivers/QDSP5Driver/QDSP5Interface/main/latest/qdsp5audpreproccmdi.h#2 $

===========================================================================*/

/*
 * AUDIOPREPROC COMMANDS:
 * ARM uses uPAudPreProcCmdQueue to communicate with AUDPREPROCTASK
 * Location : MEMB
 * Buffer size : 51
 * Number of buffers in a queue : 3
 */

/*
 * Command to configure the parameters of AGC
 */

#define	AUDPREPROC_CMD_CFG_AGC_PARAMS	0x0000
#define	AUDPREPROC_CMD_CFG_AGC_PARAMS_LEN	\
	sizeof(audpreproc_cmd_cfg_agc_params)

#define	AUDPREPROC_CMD_TX_AGC_PARAM_MASK_COMP_SLOPE	0x0009
#define	AUDPREPROC_CMD_TX_AGC_PARAM_MASK_COMP_TH	0x000A
#define	AUDPREPROC_CMD_TX_AGC_PARAM_MASK_EXP_SLOPE	0x000B
#define	AUDPREPROC_CMD_TX_AGC_PARAM_MASK_EXP_TH		0x000C
#define	AUDPREPROC_CMD_TX_AGC_PARAM_MASK_COMP_AIG_FLAG		0x000D
#define	AUDPREPROC_CMD_TX_AGC_PARAM_MASK_COMP_STATIC_GAIN	0x000E
#define	AUDPREPROC_CMD_TX_AGC_PARAM_MASK_TX_AGC_ENA_FLAG	0x000F

#define	AUDPREPROC_CMD_TX_AGC_ENA_FLAG_ENA	-1
#define	AUDPREPROC_CMD_TX_AGC_ENA_FLAG_DIS	0x0000

#define	AUDPREPROC_CMD_ADP_GAIN_FLAG_ENA_ADP_GAIN	-1
#define	AUDPREPROC_CMD_ADP_GAIN_FLAG_ENA_STATIC_GAIN	0x0000

#define	AUDPREPROC_CMD_PARAM_MASK_RMS_TAY	0x0004
#define	AUDPREPROC_CMD_PARAM_MASK_RELEASEK	0x0005
#define	AUDPREPROC_CMD_PARAM_MASK_DELAY		0x0006
#define	AUDPREPROC_CMD_PARAM_MASK_ATTACKK	0x0007
#define	AUDPREPROC_CMD_PARAM_MASK_LEAKRATE_SLOW	0x0008
#define	AUDPREPROC_CMD_PARAM_MASK_LEAKRATE_FAST	0x0009
#define	AUDPREPROC_CMD_PARAM_MASK_AIG_RELEASEK 	0x000A
#define	AUDPREPROC_CMD_PARAM_MASK_AIG_MIN	0x000B
#define	AUDPREPROC_CMD_PARAM_MASK_AIG_MAX	0x000C
#define	AUDPREPROC_CMD_PARAM_MASK_LEAK_UP	0x000D
#define	AUDPREPROC_CMD_PARAM_MASK_LEAK_DOWN	0x000E
#define	AUDPREPROC_CMD_PARAM_MASK_AIG_ATTACKK	0x000F

typedef struct {
	unsigned short	cmd_id;
	unsigned short	tx_agc_param_mask;
	unsigned short	tx_agc_enable_flag;
	unsigned short	static_gain;
	signed short	adaptive_gain_flag;
	unsigned short	expander_th;
	unsigned short	expander_slope;
	unsigned short	compressor_th;
	unsigned short	compressor_slope;
	unsigned short	param_mask;
	unsigned short	aig_attackk;
	unsigned short	aig_leak_down;
	unsigned short	aig_leak_up;
	unsigned short	aig_max;
	unsigned short	aig_min;
	unsigned short	aig_releasek;
	unsigned short	aig_leakrate_fast;
	unsigned short	aig_leakrate_slow;
	unsigned short	attackk_msw;
	unsigned short	attackk_lsw;
	unsigned short	delay;
	unsigned short	releasek_msw;
	unsigned short	releasek_lsw;
	unsigned short	rms_tav;
} __attribute__((packed)) audpreproc_cmd_cfg_agc_params;


/*
 * Command to configure the params of Advanved AGC
 */

#define	AUDPREPROC_CMD_CFG_AGC_PARAMS_2		0x0001
#define	AUDPREPROC_CMD_CFG_AGC_PARAMS_2_LEN		\
	sizeof(audpreproc_cmd_cfg_agc_params_2)

#define	AUDPREPROC_CMD_2_TX_AGC_ENA_FLAG_ENA	-1;
#define	AUDPREPROC_CMD_2_TX_AGC_ENA_FLAG_DIS	0x0000;

typedef struct {
	unsigned short	cmd_id;
	unsigned short	agc_param_mask;
	signed short	tx_agc_enable_flag;
	unsigned short	comp_static_gain;
	unsigned short	exp_th;
	unsigned short	exp_slope;
	unsigned short	comp_th;
	unsigned short	comp_slope;
	unsigned short	comp_rms_tav;
	unsigned short	comp_samp_mask;
	unsigned short	comp_attackk_msw;
	unsigned short	comp_attackk_lsw;
	unsigned short	comp_releasek_msw;
	unsigned short	comp_releasek_lsw;
	unsigned short	comp_delay;
	unsigned short	comp_makeup_gain;
} __attribute__((packed)) audpreproc_cmd_cfg_agc_params_2;

/*
 * Command to configure params for ns
 */

#define	AUDPREPROC_CMD_CFG_NS_PARAMS		0x0002
#define	AUDPREPROC_CMD_CFG_NS_PARAMS_LEN	\
	sizeof(audpreproc_cmd_cfg_ns_params)

#define	AUDPREPROC_CMD_EC_MODE_NEW_NLMS_ENA	0x0001
#define	AUDPREPROC_CMD_EC_MODE_NEW_NLMS_DIS 	0x0000
#define	AUDPREPROC_CMD_EC_MODE_NEW_DES_ENA	0x0002
#define	AUDPREPROC_CMD_EC_MODE_NEW_DES_DIS	0x0000
#define	AUDPREPROC_CMD_EC_MODE_NEW_NS_ENA	0x0004
#define	AUDPREPROC_CMD_EC_MODE_NEW_NS_DIS	0x0000
#define	AUDPREPROC_CMD_EC_MODE_NEW_CNI_ENA	0x0008
#define	AUDPREPROC_CMD_EC_MODE_NEW_CNI_DIS	0x0000

#define	AUDPREPROC_CMD_EC_MODE_NEW_NLES_ENA	0x0010
#define	AUDPREPROC_CMD_EC_MODE_NEW_NLES_DIS	0x0000
#define	AUDPREPROC_CMD_EC_MODE_NEW_HB_ENA	0x0020
#define	AUDPREPROC_CMD_EC_MODE_NEW_HB_DIS	0x0000
#define	AUDPREPROC_CMD_EC_MODE_NEW_VA_ENA	0x0040
#define	AUDPREPROC_CMD_EC_MODE_NEW_VA_DIS	0x0000
#define	AUDPREPROC_CMD_EC_MODE_NEW_PCD_ENA	0x0080
#define	AUDPREPROC_CMD_EC_MODE_NEW_PCD_DIS	0x0000
#define	AUDPREPROC_CMD_EC_MODE_NEW_FEHI_ENA	0x0100
#define	AUDPREPROC_CMD_EC_MODE_NEW_FEHI_DIS 	0x0000
#define	AUDPREPROC_CMD_EC_MODE_NEW_NEHI_ENA	0x0200
#define	AUDPREPROC_CMD_EC_MODE_NEW_NEHI_DIS 	0x0000
#define	AUDPREPROC_CMD_EC_MODE_NEW_NLPP_ENA	0x0400
#define	AUDPREPROC_CMD_EC_MODE_NEW_NLPP_DIS	0x0000
#define	AUDPREPROC_CMD_EC_MODE_NEW_FNE_ENA	0x0800
#define	AUDPREPROC_CMD_EC_MODE_NEW_FNE_DIS	0x0000
#define	AUDPREPROC_CMD_EC_MODE_NEW_PRENLMS_ENA 	0x1000
#define	AUDPREPROC_CMD_EC_MODE_NEW_PRENLMS_DIS 	0x0000

typedef struct {
	unsigned short	cmd_id;
	unsigned short	ec_mode_new;
	unsigned short	dens_gamma_n;
	unsigned short	dens_nfe_block_size;
	unsigned short	dens_limit_ns;
	unsigned short	dens_limit_ns_d;
	unsigned short	wb_gamma_e;
	unsigned short	wb_gamma_n;
} __attribute__((packed)) audpreproc_cmd_cfg_ns_params;

/*
 * Command to configure parameters for IIR tuning filter
 */

#define	AUDPREPROC_CMD_CFG_IIR_TUNING_FILTER_PARAMS		0x0003
#define	AUDPREPROC_CMD_CFG_IIR_TUNING_FILTER_PARAMS_LEN	\
	sizeof(audpreproc_cmd_cfg_iir_tuning_filter_params)

#define	AUDPREPROC_CMD_IIR_ACTIVE_FLAG_DIS	0x0000
#define	AUDPREPROC_CMD_IIR_ACTIVE_FLAG_ENA	0x0001

typedef struct {
	unsigned short	cmd_id;
	unsigned short	active_flag;
	unsigned short	num_bands;
	unsigned short	numerator_coeff_b0_filter0_lsw;
	unsigned short	numerator_coeff_b0_filter0_msw;
	unsigned short	numerator_coeff_b1_filter0_lsw;
	unsigned short	numerator_coeff_b1_filter0_msw;
	unsigned short	numerator_coeff_b2_filter0_lsw;
	unsigned short	numerator_coeff_b2_filter0_msw;
	unsigned short	numerator_coeff_b0_filter1_lsw;
	unsigned short	numerator_coeff_b0_filter1_msw;
	unsigned short	numerator_coeff_b1_filter1_lsw;
	unsigned short	numerator_coeff_b1_filter1_msw;
	unsigned short	numerator_coeff_b2_filter1_lsw;
	unsigned short	numerator_coeff_b2_filter1_msw;
	unsigned short	numerator_coeff_b0_filter2_lsw;
	unsigned short	numerator_coeff_b0_filter2_msw;
	unsigned short	numerator_coeff_b1_filter2_lsw;
	unsigned short	numerator_coeff_b1_filter2_msw;
	unsigned short	numerator_coeff_b2_filter2_lsw;
	unsigned short	numerator_coeff_b2_filter2_msw;
	unsigned short	numerator_coeff_b0_filter3_lsw;
	unsigned short	numerator_coeff_b0_filter3_msw;
	unsigned short	numerator_coeff_b1_filter3_lsw;
	unsigned short	numerator_coeff_b1_filter3_msw;
	unsigned short	numerator_coeff_b2_filter3_lsw;
	unsigned short	numerator_coeff_b2_filter3_msw;
	unsigned short 	denominator_coeff_a0_filter0_lsw;
	unsigned short 	denominator_coeff_a0_filter0_msw;
	unsigned short 	denominator_coeff_a1_filter0_lsw;
	unsigned short 	denominator_coeff_a1_filter0_msw;
	unsigned short 	denominator_coeff_a0_filter1_lsw;
	unsigned short 	denominator_coeff_a0_filter1_msw;
	unsigned short 	denominator_coeff_a1_filter1_lsw;
	unsigned short 	denominator_coeff_a1_filter1_msw;
  unsigned short 	denominator_coeff_a0_filter2_lsw;
	unsigned short 	denominator_coeff_a0_filter2_msw;
	unsigned short 	denominator_coeff_a1_filter2_lsw;
	unsigned short 	denominator_coeff_a1_filter2_msw;
  unsigned short 	denominator_coeff_a0_filter3_lsw;
	unsigned short 	denominator_coeff_a0_filter3_msw;
	unsigned short 	denominator_coeff_a1_filter3_lsw;
	unsigned short 	denominator_coeff_a1_filter3_msw;

	unsigned short	shift_factor_filter0;
	unsigned short	shift_factor_filter1;
	unsigned short	shift_factor_filter2;
	unsigned short	shift_factor_filter3;

	unsigned short	channel_selected0;
	unsigned short	channel_selected1;
	unsigned short	channel_selected2;
	unsigned short	channel_selected3;
} __attribute__((packed))audpreproc_cmd_cfg_iir_tuning_filter_params;

#endif
