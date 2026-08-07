// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#include "dm_services.h"
#include "dc.h"

#include "dcn32/dcn32_init.h"
#include "dcn401/dcn401_init.h"
#include "dcn60/dcn60_init.h"

#include "resource.h"
#include "include/irq_service_interface.h"

#include "dcn20/dcn20_resource.h"
#include "dcn30/dcn30_resource.h"
#include "dcn32/dcn32_resource.h"
#include "dcn321/dcn321_resource.h"
#include "dcn401/dcn401_resource.h"
#include "dcn42/dcn42_resource.h"
#include "dcn60_resource.h"

#include "dcn10/dcn10_ipp.h"
#include "dcn60/dcn60_hubbub.h"
#include "dcn60/dcn60_mpc.h"
#include "dcn60/dcn60_hubp.h"
#include "irq/dcn60/irq_service_dcn60.h"
#include "dcn60/dcn60_dpp.h"
#include "dcn60/dcn60_optc.h"
#include "dcn20/dcn20_hwseq.h"
#include "dcn30/dcn30_hwseq.h"
#include "dcn10/dcn10_hwseq.h"
#include "dcn60/dcn60_opp.h"
#include "dcn60/dcn60_dsc.h"
#include "dcn30/dcn30_vpg.h"
#include "dcn31/dcn31_vpg.h"
#include "dcn30/dcn30_dio_stream_encoder.h"
#include "dcn401/dcn401_dio_stream_encoder.h"
#include "dcn60/dcn60_dio_stream_encoder.h"
#include "dcn401/dcn401_hpo_frl_stream_encoder.h"
#include "dcn30/dcn30_hpo_frl_link_encoder.h"
#include "dcn60/dcn60_hpo_frl_link_encoder.h"
#include "dcn60/dcn60_hpo_frl_stream_encoder.h"
#include "dcn31/dcn31_hpo_dp_stream_encoder.h"
#include "dcn31/dcn31_hpo_dp_link_encoder.h"
#include "dcn32/dcn32_hpo_dp_link_encoder.h"
#include "dcn31/dcn31_dio_link_encoder.h"
#include "dcn401/dcn401_dio_link_encoder.h"
#include "dcn60/dcn60_dio_link_encoder.h"
#include "dcn10/dcn10_link_encoder.h"
#include "dcn321/dcn321_dio_link_encoder.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_audio.h"
#include "dce/dce_hwseq.h"
#include "clk_mgr.h"
#include "dio/virtual/virtual_stream_encoder.h"
#include "dml/display_mode_vba.h"
#include "dcn60/dcn60_dccg.h"
#include "dcn10/dcn10_resource.h"
#include "link_service.h"
#include "link_enc_cfg.h"
#include "dcn31/dcn31_panel_cntl.h"

#include "dcn30/dcn30_dwb.h"
#include "dcn32/dcn32_mmhubbub.h"

#include "dcn/dcn_6_0_0_offset.h"
#include "dcn/dcn_6_0_0_sh_mask.h"
#include "dpcs/dpcs_6_0_0_offset.h"
#include "dpcs/dpcs_6_0_0_sh_mask.h"

#include "reg_helper.h"
#include "dce/dmub_abm.h"
#include "dce/dmub_psr.h"
#include "dce/dce_aux.h"
#include "dce/dce_i2c.h"

#include "dml/dcn30/display_mode_vba_30.h"
#include "vm_helper.h"
#include "dcn20/dcn20_vmid.h"

#include "dc_state_priv.h"

#include "dml2_0/dml2_wrapper.h"
#include "dml2_0/dml21/dml21_wrapper.h"

#define DC_LOGGER_INIT(logger)

/* begin *********************
 * macros to expend register list macro defined in HW object header file
 */

enum dcn60_clk_src_array_id {
	DCN60_CLK_SRC_PLL0,
	DCN60_CLK_SRC_PLL1,
	DCN60_CLK_SRC_PLL2,
	DCN60_CLK_SRC_PLL3,
	//DCN60_CLK_SRC_PLL4,
	DCN60_CLK_SRC_TOTAL
};

static const uint32_t MCACHE_ID_UNASSIGNED = 0xF;
static const uint32_t SPLIT_LOCATION_UNDEFINED = 0xFFFF;

/* DCN */
#define BASE_INNER(seg) ctx->dcn_reg_offsets[seg]

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
	REG_STRUCT.reg_name = BASE(reg ## reg_name ## _BASE_IDX) +  \
		reg ## reg_name
#define SR_ARR(reg_name, id)\
	REG_STRUCT[id].reg_name = BASE(reg ## reg_name ## _BASE_IDX) +  \
		reg ## reg_name
#define SR_ARR_INIT(reg_name, id, value)\
	REG_STRUCT[id].reg_name =  value

#define SRI(reg_name, block, id)\
	REG_STRUCT.reg_name = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		reg ## block ## id ## _ ## reg_name

#define SRI_ARR(reg_name, block, id)\
	REG_STRUCT[id].reg_name = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		reg ## block ## id ## _ ## reg_name

#define SRI_ARR_DDC(reg_name, block, id)\
	REG_STRUCT[id-1].reg_name = BASE(reg ## reg_name ## id ## _ ## block ## _BASE_IDX) + \
		reg ## reg_name ## id ## _ ## block

/*
 * Used when a reg_name would otherwise begin with an integer
 */
#define SRI_ARR_US(reg_name, block, id)\
	REG_STRUCT[id].reg_name = BASE(reg ## block ## id ## reg_name ## _BASE_IDX) + \
		reg ## block ## id ## reg_name
#define SR_ARR_I2C(reg_name, id) \
	REG_STRUCT[id-1].reg_name = BASE(reg##reg_name##_BASE_IDX) + reg##reg_name

#define SRI_ARR_I2C(reg_name, block, id)\
	REG_STRUCT[id-1].reg_name = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		reg ## block ## id ## _ ## reg_name

#define SRI_ARR_DME(reg_name, block, id, offset)\
	REG_STRUCT[id - offset].reg_name = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		reg ## block ## id ## _ ## reg_name

#define SRI_ARR_ALPHABET(reg_name, block, index, id)\
	REG_STRUCT[index].reg_name = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		reg ## block ## id ## _ ## reg_name

#define SRI2(reg_name, block, id)\
	.reg_name = BASE(reg ## reg_name ## _BASE_IDX) + \
		reg ## reg_name
#define SRI2_ARR(reg_name, block, id)\
	REG_STRUCT[id].reg_name = BASE(reg ## reg_name ## _BASE_IDX) + \
		reg ## reg_name

#define SRIR(var_name, reg_name, block, id)\
	.var_name = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		reg ## block ## id ## _ ## reg_name

#define SRII(reg_name, block, id)\
	REG_STRUCT.reg_name[id] = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		reg ## block ## id ## _ ## reg_name

#define SRII_ARR_2(reg_name, block, id, inst)\
	REG_STRUCT[inst].reg_name[id] = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		reg ## block ## id ## _ ## reg_name

#define SRII_MPC_RMU(reg_name, block, id)\
	.RMU##_##reg_name[id] = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		reg ## block ## id ## _ ## reg_name

#define SRII_DWB(reg_name, temp_name, block, id)\
	REG_STRUCT.reg_name[id] = BASE(reg ## block ## id ## _ ## temp_name ## _BASE_IDX) + \
		reg ## block ## id ## _ ## temp_name

#define DCCG_SRII(reg_name, block, id)\
	REG_STRUCT.block ## _ ## reg_name[id] = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
		reg ## block ## id ## _ ## reg_name

#define SF_DWB2(reg_name, block, id, field_name, post_fix) \
	.field_name = reg_name ## __ ## field_name ## post_fix

#define VUPDATE_SRII(reg_name, block, id)\
	REG_STRUCT.reg_name[id] = BASE(reg ## reg_name ## _ ## block ## id ## _BASE_IDX) + \
		reg ## reg_name ## _ ## block ## id

/* bringup_nbif_7_10_0_offset registers */
#define regBIF_BX0_BIOS_SCRATCH_3                                                                       0x003b
#define regBIF_BX0_BIOS_SCRATCH_3_BASE_IDX                                                              1
#define regBIF_BX0_BIOS_SCRATCH_6                                                                       0x003e
#define regBIF_BX0_BIOS_SCRATCH_6_BASE_IDX                                                              1

/* NBIO */
#define NBIO_BASE_INNER(seg) ctx->nbio_reg_offsets[seg]

#define NBIO_BASE(seg) \
	NBIO_BASE_INNER(seg)

#define NBIO_SR(reg_name)\
	REG_STRUCT.reg_name = NBIO_BASE(regBIF_BX0_ ## reg_name ## _BASE_IDX) + \
		regBIF_BX0_ ## reg_name
#define NBIO_SR_ARR(reg_name, id)\
	REG_STRUCT[id].reg_name = NBIO_BASE(regBIF_BX0_ ## reg_name ## _BASE_IDX) + \
		regBIF_BX0_ ## reg_name

#define CTX ctx
#define REG(reg_name) \
	(ctx->dcn_reg_offsets[reg ## reg_name ## _BASE_IDX] + reg ## reg_name)

static struct bios_registers bios_regs;

#define bios_regs_init() \
		NBIO_SR(BIOS_SCRATCH_3),\
		NBIO_SR(BIOS_SCRATCH_6)

#define clk_src_regs_init(index, pllid)\
	CS_COMMON_REG_LIST_DCN3_0_RI(index, pllid)

static struct dce110_clk_src_regs clk_src_regs[5];

static const struct dce110_clk_src_shift cs_shift = {
		CS_COMMON_MASK_SH_LIST_DCN3_2(__SHIFT)
};

static const struct dce110_clk_src_mask cs_mask = {
		CS_COMMON_MASK_SH_LIST_DCN3_2(_MASK)
};

#define abm_regs_init(id)\
		ABM_DCN42_REG_LIST_RI(id)

static struct dce_abm_registers abm_regs[4];

static const struct dce_abm_shift abm_shift = {
		ABM_MASK_SH_LIST_DCN42(__SHIFT)
};

static const struct dce_abm_mask abm_mask = {
		ABM_MASK_SH_LIST_DCN42(_MASK)
};

#define audio_regs_init(id)\
		AUD_COMMON_REG_LIST_RI(id)

static struct dce_audio_registers audio_regs[5];

#define DCE120_AUD_COMMON_MASK_SH_LIST(mask_sh)\
		SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_INDEX, AZALIA_ENDPOINT_REG_INDEX, mask_sh),\
		SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_DATA, AZALIA_ENDPOINT_REG_DATA, mask_sh),\
		SF(DCCG_AUDIO_DTO_SOURCE, DCCG_AUDIO_DTO0_SOURCE_SEL, mask_sh),\
		SF(DCCG_AUDIO_DTO_SOURCE, DCCG_AUDIO_DTO_SEL, mask_sh),\
		SF(DCCG_AUDIO_DTO_SOURCE, DCCG_AUDIO_DTO0_USE_512FBR_DTO, mask_sh),\
		SF(DCCG_AUDIO_DTO_SOURCE, DCCG_AUDIO_DTO1_USE_512FBR_DTO, mask_sh),\
		SF(DCCG_AUDIO_DTO0_MODULE, DCCG_AUDIO_DTO0_MODULE, mask_sh),\
		SF(DCCG_AUDIO_DTO0_PHASE, DCCG_AUDIO_DTO0_PHASE, mask_sh),\
		SF(DCCG_AUDIO_DTO1_MODULE, DCCG_AUDIO_DTO1_MODULE, mask_sh),\
		SF(DCCG_AUDIO_DTO1_PHASE, DCCG_AUDIO_DTO1_PHASE, mask_sh),\
		SF(AZALIA_F0_CODEC_FUNCTION_PARAMETER_SUPPORTED_SIZE_RATES, AUDIO_RATE_CAPABILITIES, mask_sh),\
		SF(AZALIA_F0_CODEC_FUNCTION_PARAMETER_POWER_STATES, CLKSTOP, mask_sh),\
		SF(AZALIA_F0_CODEC_FUNCTION_PARAMETER_POWER_STATES, EPSS, mask_sh)

static const struct dce_audio_shift audio_shift = {
		DCE120_AUD_COMMON_MASK_SH_LIST(__SHIFT)
};

static const struct dce_audio_mask audio_mask = {
		DCE120_AUD_COMMON_MASK_SH_LIST(_MASK)
};

#define vpg_regs_init(id)\
		VPG_DCN401_REG_LIST_RI(id)

static struct dcn31_vpg_registers vpg_regs[9];

static const struct dcn31_vpg_shift vpg_shift = {
	DCN31_VPG_MASK_SH_LIST(__SHIFT)
};

static const struct dcn31_vpg_mask vpg_mask = {
	DCN31_VPG_MASK_SH_LIST(_MASK)
};

#define apg_regs_init(id)\
	APG_DCN31_REG_LIST_RI(id)

static struct dcn31_apg_registers apg_regs[4];

static const struct dcn31_apg_shift apg_shift = {
	DCN31_APG_MASK_SH_LIST(__SHIFT)
};

static const struct dcn31_apg_mask apg_mask = {
	DCN31_APG_MASK_SH_LIST(_MASK)
};

#define stream_enc_regs_init(id)\
	SE_REG_LIST_DCN60_RI(id)

static struct dcn10_stream_enc_registers stream_enc_regs[4];

static const struct dcn10_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCN60(__SHIFT)
};

static const struct dcn10_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCN60(_MASK)
};

#define aux_regs_init(id, ddc_id)\
	DCN60_AUX_REG_LIST_RI(id, ddc_id)

static struct dcn10_link_enc_aux_registers link_enc_aux_regs[5];

#define hpd_regs_init(id)\
	HPD_REG_LIST_DCN60_RI(id)

static struct dcn10_link_enc_hpd_registers link_enc_hpd_regs[5];

#define link_regs_init(id, phyid)\
	LE_DCN401_REG_LIST_RI(id), \
	LE_DCN60_REG_LIST_RI(id)

static struct dcn10_link_enc_registers link_enc_regs[4];

static const struct dcn10_link_enc_shift le_shift = {
	LINK_ENCODER_MASK_SH_LIST_DCN401(__SHIFT), \
	LINK_ENCODER_MASK_SH_LIST_DCN60(__SHIFT)
};

static const struct dcn10_link_enc_mask le_mask = {
	LINK_ENCODER_MASK_SH_LIST_DCN401(_MASK), \
	LINK_ENCODER_MASK_SH_LIST_DCN60(_MASK)
};

#define hpo_frl_stream_encoder_reg_list(id)\
	DCN60_HPO_FRL_STREAM_ENC_REG_LIST_RI(id)

#define hpo_frl_stream_encoder_dme_reg_list(id)\
	DCN3_0_HPO_STREAM_ENC_DME_REG_LIST_RI(id, 4)

static struct dcn30_hpo_frl_stream_enc_registers hpo_frl_stream_enc_regs[2];

static const struct dcn401_hpo_frl_stream_encoder_shift hpo_se_shift = {
	DCN401_HPO_STREAM_ENC_MASK_SH_LIST(__SHIFT),
	DCN60_HDMI_STREAM_ENC_MASK_SH_LIST(__SHIFT)
};

static const struct dcn401_hpo_frl_stream_encoder_mask hpo_se_mask = {
	DCN401_HPO_STREAM_ENC_MASK_SH_LIST(_MASK),
	DCN60_HDMI_STREAM_ENC_MASK_SH_LIST(_MASK)
};

#define hpo_frl_link_encoder_reg_list(id)\
		DCN3_0_HPO_FRL_LINK_ENC_REG_LIST_RI(id)

static struct dcn30_hpo_frl_link_encoder_registers hpo_frl_link_enc_regs[1];

static const struct dcn30_hpo_frl_link_encoder_shift hpo_le_shift = {
	DCN3_0_HPO_FRL_LINK_ENC_MASK_SH_LIST(__SHIFT)
};

static const struct dcn30_hpo_frl_link_encoder_mask hpo_le_mask = {
	DCN3_0_HPO_FRL_LINK_ENC_MASK_SH_LIST(_MASK)
};

#define hpo_dp_stream_encoder_reg_init(id)\
	DCN3_1_HPO_DP_STREAM_ENC_REG_LIST_RI(id)

static struct dcn31_hpo_dp_stream_encoder_registers hpo_dp_stream_enc_regs[4];

static const struct dcn31_hpo_dp_stream_encoder_shift hpo_dp_se_shift = {
	DCN3_1_HPO_DP_STREAM_ENC_MASK_SH_LIST(__SHIFT)
};

static const struct dcn31_hpo_dp_stream_encoder_mask hpo_dp_se_mask = {
	DCN3_1_HPO_DP_STREAM_ENC_MASK_SH_LIST(_MASK)
};

#define hpo_dp_link_encoder_reg_init(id)\
	DCN3_1_HPO_DP_LINK_ENC_REG_LIST_RI(id)
	/*DCN3_1_RDPCSTX_REG_LIST(0),*/
	/*DCN3_1_RDPCSTX_REG_LIST(1),*/
	/*DCN3_1_RDPCSTX_REG_LIST(2),*/
	/*DCN3_1_RDPCSTX_REG_LIST(3),*/

static struct dcn31_hpo_dp_link_encoder_registers hpo_dp_link_enc_regs[4];

static const struct dcn31_hpo_dp_link_encoder_shift hpo_dp_le_shift = {
	DCN3_2_HPO_DP_LINK_ENC_MASK_SH_LIST(__SHIFT)
};

static const struct dcn31_hpo_dp_link_encoder_mask hpo_dp_le_mask = {
	DCN3_2_HPO_DP_LINK_ENC_MASK_SH_LIST(_MASK)
};

#define dpp_regs_init(id)\
	DPP_REG_LIST_DCN60_RI(id)

static struct dcn60_dpp_registers dpp_regs[4];

static const struct dcn60_dpp_shift tf_shift = {
		DPP_REG_LIST_SH_MASK_DCN60(__SHIFT)
};

static const struct dcn60_dpp_mask tf_mask = {
		DPP_REG_LIST_SH_MASK_DCN60(_MASK)
};

#define opp_regs_init(id)\
	OPP_REG_LIST_DCN401_RI(id)

static struct dcn60_opp_registers opp_regs[4];

static const struct dcn60_opp_shift opp_shift = {
	OPP_MASK_SH_LIST_DCN60(__SHIFT)
};

static const struct dcn60_opp_mask opp_mask = {
	OPP_MASK_SH_LIST_DCN60(_MASK)
};

#define aux_engine_regs_init(id) \
	AUX_COMMON_REG_LIST0_RI(id), \
	SR_ARR_INIT(AUXN_IMPCAL, id, 0), \
	SR_ARR_INIT(AUXP_IMPCAL, id, 0), \
	SR_ARR_INIT(AUX_RESET_MASK, id, DP_AUX0_AUX_CONTROL__AUX_RESET_MASK)

static struct dce110_aux_registers aux_engine_regs[4];

static const struct dce110_aux_registers_shift aux_shift = {
	DCN_AUX_MASK_SH_LIST(__SHIFT)
};

static const struct dce110_aux_registers_mask aux_mask = {
	DCN_AUX_MASK_SH_LIST(_MASK)
};

#define dsc_regs_init(id)\
	DSC_REG_LIST_DCN60_RI(id)

static struct dcn401_dsc_registers dsc_regs[4];

static const struct dcn60_dsc_shift dsc_shift = {
	DSC_REG_LIST_SH_MASK_DCN60(__SHIFT)
};

static const struct dcn60_dsc_mask dsc_mask = {
	DSC_REG_LIST_SH_MASK_DCN60(_MASK)
};

static struct dcn60_mpc_registers mpc_regs;

#define dcn_mpc_regs_init()\
	MPC_REG_LIST_DCN6_0_RI(0),\
	MPC_REG_LIST_DCN6_0_RI(1),\
	MPC_REG_LIST_DCN6_0_RI(2),\
	MPC_REG_LIST_DCN6_0_RI(3),\
	MPC_OUT_MUX_REG_LIST_DCN3_0_RI(0),\
	MPC_OUT_MUX_REG_LIST_DCN3_0_RI(1),\
	MPC_OUT_MUX_REG_LIST_DCN3_0_RI(2),\
	MPC_OUT_MUX_REG_LIST_DCN3_0_RI(3),\
	MPC_RMCM_REG_LIST_DCN42(0),\
	MPC_RMCM_REG_LIST_DCN42(1)

static const struct dcn60_mpc_shift mpc_shift = {
	MPC_COMMON_MASK_SH_LIST_DCN6_0(__SHIFT)
};

static const struct dcn60_mpc_mask mpc_mask = {
	MPC_COMMON_MASK_SH_LIST_DCN6_0(_MASK)
};

#define optc_regs_init(id)\
	OPTC_COMMON_REG_LIST_DCN60_RI(id)

static struct dcn_optc_registers optc_regs[4];

static const struct dcn_optc_shift optc_shift = {
	OPTC_COMMON_MASK_SH_LIST_DCN60(__SHIFT)
};

static const struct dcn_optc_mask optc_mask = {
	OPTC_COMMON_MASK_SH_LIST_DCN60(_MASK)
};

static struct dcn_hubp2_registers hubp_regs[4];

#define hubp_regs_init(id)\
	HUBP_REG_LIST_DCN60_RI(id)

static const struct dcn_hubp2_shift hubp_shift = {
		HUBP_MASK_SH_LIST_DCN60(__SHIFT)
};

static const struct dcn_hubp2_mask hubp_mask = {
		HUBP_MASK_SH_LIST_DCN60(_MASK)
};

static struct dcn_hubbub_registers hubbub_reg;
#define hubbub_reg_init()\
		HUBBUB_REG_LIST_DCN60_RI(0)

static const struct dcn_hubbub_shift hubbub_shift = {
		HUBBUB_MASK_SH_LIST_DCN6_0(__SHIFT)
};

static const struct dcn_hubbub_mask hubbub_mask = {
		HUBBUB_MASK_SH_LIST_DCN6_0(_MASK)
};

static struct dccg_registers dccg_regs;

#define dccg_regs_init()\
	DCCG_REG_LIST_DCN60_RI()

static const struct dccg_shift dccg_shift = {
		DCCG_MASK_SH_LIST_DCN60(__SHIFT)
};

static const struct dccg_mask dccg_mask = {
		DCCG_MASK_SH_LIST_DCN60(_MASK)
};

#define SRII2(reg_name_pre, reg_name_post, id)\
	.reg_name_pre ## _ ##  reg_name_post[id] = BASE(reg ## reg_name_pre \
			## id ## _ ## reg_name_post ## _BASE_IDX) + \
			reg ## reg_name_pre ## id ## _ ## reg_name_post

static struct dce_hwseq_registers hwseq_reg;

#define hwseq_reg_init()\
	HWSEQ_DCN60_REG_LIST()

#define HWSEQ_DCN60_MASK_SH_LIST(mask_sh)\
	HWSEQ_DCN_MASK_SH_LIST(mask_sh), \
	HWS_SF(, DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_REFDIV, mask_sh), \
	HWS_SF(, AZALIA_AUDIO_DTO, AZALIA_AUDIO_DTO_MODULE, mask_sh), \
	HWS_SF(, HPO_TOP_CLOCK_CONTROL, HPO_HDMISTREAMCLK_G_GATE_DIS, mask_sh), \
	HWS_SF(, HPO_TOP_HW_CONTROL, HPO_IO_EN, mask_sh), \
	HWS_SF(, ODM_MEM_PWR_CTRL3, ODM_MEM_UNASSIGNED_PWR_MODE, mask_sh), \
	HWS_SF(, ODM_MEM_PWR_CTRL3, ODM_MEM_VBLANK_PWR_MODE, mask_sh), \
	HWS_SF(, HDCP_INTERRUPT_DEST, DOUT_IHC_HDCP0_I2C_XFER_REQ_INTERRUPT_DEST, mask_sh), \
	HWS_SF(, HDCP_INTERRUPT_DEST, DOUT_IHC_HDCP1_I2C_XFER_REQ_INTERRUPT_DEST, mask_sh), \
	HWS_SF(, HDCP_INTERRUPT_DEST, DOUT_IHC_HDCP2_I2C_XFER_REQ_INTERRUPT_DEST, mask_sh), \
	HWS_SF(, HDCP_INTERRUPT_DEST, DOUT_IHC_HDCP3_I2C_XFER_REQ_INTERRUPT_DEST, mask_sh),

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCN60_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCN60_MASK_SH_LIST(_MASK)
};

#define vmid_regs_init(id)\
		DCN20_VMID_REG_LIST_RI(id)

static struct dcn_vmid_registers vmid_regs[16];

static const struct dcn20_vmid_shift vmid_shifts = {
		DCN20_VMID_MASK_SH_LIST(__SHIFT)
};

static const struct dcn20_vmid_mask vmid_masks = {
		DCN20_VMID_MASK_SH_LIST(_MASK)
};

static const struct resource_caps res_cap_dcn6_0 = {
	.num_timing_generator = 4,
	.num_opp = 4,
	.num_video_plane = 4,
	.num_audio = 4,
	.num_stream_encoder = 4,
	.num_hpo_frl = 1,
	.num_hpo_dp_stream_encoder = 4,
	.num_hpo_dp_link_encoder = 4,
	.num_pll = 4,
	.num_dwb = 0,
	.num_ddc = 2,
	.num_vmid = 16,
	.num_mpc_3dlut = 4,
	.num_dsc = 4,
	.num_aux = 4,
	.num_rmcm = 2,
};

static const struct dc_plane_cap plane_cap = {
	.type = DC_PLANE_TYPE_DCN_UNIVERSAL,
	.per_pixel_alpha = true,

	.pixel_format_support = {
			.argb8888 = true,
			.nv12 = true,
			.fp16 = true,
			.p010 = true,
			.ayuv = false,
	},

	.max_upscale_factor = {
			.argb8888 = 16000,
			.nv12 = 16000,
			.fp16 = 16000
	},

	// 6:1 downscaling ratio: 1000/6 = 166.666
	.max_downscale_factor = {
			.argb8888 = 167,
			.nv12 = 167,
			.fp16 = 167
	},
	64,
	64
};

static const struct dc_debug_options debug_defaults_drv = {
	.limit_ffe = 7,
	.disable_dmcu = true,
	.force_abm_enable = false,
	.clock_trace = true,
	.disable_pplib_clock_request = false,
	.pipe_split_policy = MPC_SPLIT_AVOID,
	.force_single_disp_pipe_split = false,
	.disable_dcc = DCC_ENABLE,
	.vsr_support = true,
	.performance_trace = false,
	.max_downscale_src_width = 7680,/*upto 8K*/
	.disable_pplib_wm_range = false,
	.scl_reset_length10 = true,
	.sanity_checks = false,
	.underflow_assert_delay_us = 0xFFFFFFFF,
	.dwb_fi_phase = -1, // -1 = disable,
	.dmub_command_table = true,
	.enable_mem_low_power = {
		.bits = {
			.vga = false,
			.i2c = false,
			.dmcu = false, // This is previously known to cause hang on S3 cycles if enabled
			.dscl = false,
			.cm = false,
			.mpc = false,
			.optc = true,
		}
	},
	.use_max_lb = true,
	.exit_idle_opt_for_cursor_updates = true,
	.using_dml2 = true,
	.using_dml21 = true,
	.enable_single_display_2to1_odm_policy = true,

	//must match enable_single_display_2to1_odm_policy to support dynamic ODM transitions
	.enable_double_buffered_dsc_pg_support = true,
	.enable_dp_dig_pixel_rate_div_policy = 1,
	.allow_sw_cursor_fallback = false,
	.alloc_extra_way_for_cursor = true,
	.min_prefetch_in_strobe_ns = 60000, // 60us
	.disable_unbounded_requesting = false,
	.dcc_meta_propagation_delay_us = 10,
	.fams_version = {
		.minor = 0,
		.major = 3,
	}, // v3.0
	.fams2_config = {
		.bits = {
			.enable = true,
			.enable_ppt_check = true,
			.enable_offload_flip = true,
			.enable_stall_recovery = true,
		}
	},
	.force_cositing = CHROMA_COSITING_NONE + 1,
};

static const struct dc_check_config config_defaults = {
	.enable_legacy_fast_update = false,
};

static bool is_plane1_enabled(enum surface_pixel_format format)
{
	return (format == SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA ||
		format == SURFACE_PIXEL_FORMAT_VIDEO_420_YCrCb ||
		format == SURFACE_PIXEL_FORMAT_VIDEO_420_10bpc_YCrCb);
}

static void reset_mcache_allocations(struct dml2_hubp_pipe_mcache_regs *pipe_mcache_regs)
{
	// Initialize all entries to special valid MCache ID and special valid split coordinate
	pipe_mcache_regs->main.p0.mcache_id_first = MCACHE_ID_UNASSIGNED;
	pipe_mcache_regs->main.p0.mcache_id_second = MCACHE_ID_UNASSIGNED;
	pipe_mcache_regs->main.p0.split_location = SPLIT_LOCATION_UNDEFINED;

	pipe_mcache_regs->mall.p0.mcache_id_first = MCACHE_ID_UNASSIGNED;
	pipe_mcache_regs->mall.p0.mcache_id_second = MCACHE_ID_UNASSIGNED;
	pipe_mcache_regs->mall.p0.split_location = SPLIT_LOCATION_UNDEFINED;

	pipe_mcache_regs->main.p1.mcache_id_first = MCACHE_ID_UNASSIGNED;
	pipe_mcache_regs->main.p1.mcache_id_second = MCACHE_ID_UNASSIGNED;
	pipe_mcache_regs->main.p1.split_location = SPLIT_LOCATION_UNDEFINED;

	pipe_mcache_regs->mall.p1.mcache_id_first = MCACHE_ID_UNASSIGNED;
	pipe_mcache_regs->mall.p1.mcache_id_second = MCACHE_ID_UNASSIGNED;
	pipe_mcache_regs->mall.p1.split_location = SPLIT_LOCATION_UNDEFINED;
}

static void get_plane_count(struct dc_state *context, unsigned int *plane_count)
{
	int i;
	*plane_count = 0;
	for (i = 0; i < context->stream_count; i++) {
		*plane_count += context->stream_status[i].plane_count;
	}
}

static bool get_plane_index(struct dc_state *context, struct dc_plane_state *plane_state, unsigned int *plane_idx)
{
	int i, j;
	*plane_idx = 0;
	for (i = 0; i < context->stream_count; i++) {
		for (j = 0; j < context->stream_status[i].plane_count; j++) {
			if (plane_state == context->stream_status[i].plane_states[j]) {
				return true;
			}
			(*plane_idx)++;
		}
	}
	// No planes have been mapped to this pipe, so just return false
	return false;
}

static void assign_global_mcache_ids(struct dc_state *context, const struct dc_mcache_params *mcache_params, struct dc_mcache_allocations *mcache_allocations)
{
	unsigned int plane_count = 0;
	unsigned int i;
	unsigned int plane_idx;
	uint32_t next_unused_cache_id = 0;

	//Get plane count
	get_plane_count(context, &plane_count);

	//Assign global_mcache id
	for (plane_idx = 0; plane_idx < plane_count; plane_idx++) {
		if (!mcache_params[plane_idx].valid)
			continue;

		for (i = 0; i < mcache_params[plane_idx].num_mcaches_plane0; i++) {
			mcache_allocations[plane_idx].global_mcache_ids_plane0[i] = next_unused_cache_id++;
		}
		for (i = 0; i < mcache_params[plane_idx].num_mcaches_plane1; i++) {
			mcache_allocations[plane_idx].global_mcache_ids_plane1[i] = next_unused_cache_id++;
		}

		// The "psuedo-last" slice is always wrapped around
		mcache_allocations[plane_idx].global_mcache_ids_plane0[mcache_params[plane_idx].num_mcaches_plane0] =
			mcache_allocations[plane_idx].global_mcache_ids_plane0[0];
		mcache_allocations[plane_idx].global_mcache_ids_plane1[mcache_params[plane_idx].num_mcaches_plane1] =
			mcache_allocations[plane_idx].global_mcache_ids_plane1[0];

		// If we need dedicated caches for mall requesting, then we assign them here.
		if (mcache_params[plane_idx].requires_dedicated_mall_mcache) {
			for (i = 0; i < mcache_params[plane_idx].num_mcaches_plane0; i++) {
				mcache_allocations[plane_idx].global_mcache_ids_mall_plane0[i] = next_unused_cache_id++;
			}
			for (i = 0; i < mcache_params[plane_idx].num_mcaches_plane1; i++) {
				mcache_allocations[plane_idx].global_mcache_ids_mall_plane1[i] = next_unused_cache_id++;
			}

			// The "psuedo-last" slice is always wrapped around
			mcache_allocations[plane_idx].global_mcache_ids_mall_plane0[mcache_params[plane_idx].num_mcaches_plane0] =
				mcache_allocations[plane_idx].global_mcache_ids_mall_plane0[0];
			mcache_allocations[plane_idx].global_mcache_ids_mall_plane1[mcache_params[plane_idx].num_mcaches_plane1] =
				mcache_allocations[plane_idx].global_mcache_ids_mall_plane1[0];
		}

		// If P0 and P1 are sharing caches, then it means the largest mcache IDs for p0 and p1 can be the same
		// since mcache IDs are always ascending, then it means the largest mcacheID of p1 should be the
		// largest mcacheID of P0
		if (mcache_params[plane_idx].num_mcaches_plane0 > 0 && mcache_params[plane_idx].num_mcaches_plane1 > 0 &&
			mcache_params[plane_idx].last_slice_sharing.plane0_plane1) {
			mcache_allocations[plane_idx].global_mcache_ids_plane1[mcache_params[plane_idx].num_mcaches_plane1 - 1] =
				mcache_allocations[plane_idx].global_mcache_ids_plane0[mcache_params[plane_idx].num_mcaches_plane0 - 1];
		}

		// If we need dedicated caches handle last slice sharing
		if (mcache_params[plane_idx].requires_dedicated_mall_mcache) {
			if (mcache_params[plane_idx].num_mcaches_plane0 > 0 && mcache_params[plane_idx].num_mcaches_plane1 > 0 &&
				mcache_params[plane_idx].last_slice_sharing.plane0_plane1) {
				mcache_allocations[plane_idx].global_mcache_ids_mall_plane1[mcache_params[plane_idx].num_mcaches_plane1 - 1] =
					mcache_allocations[plane_idx].global_mcache_ids_mall_plane0[mcache_params[plane_idx].num_mcaches_plane0 - 1];
			}
			// If mall_comb_mcache_l is set then it means that largest mcache ID for MALL p0 can be same as regular read p0
			if (mcache_params[plane_idx].num_mcaches_plane0 > 0 && mcache_params[plane_idx].last_slice_sharing.mall_comb_mcache_p0) {
				mcache_allocations[plane_idx].global_mcache_ids_mall_plane0[mcache_params[plane_idx].num_mcaches_plane0 - 1] =
					mcache_allocations[plane_idx].global_mcache_ids_plane0[mcache_params[plane_idx].num_mcaches_plane0 - 1];
			}
			// If mall_comb_mcache_c is set then it means that largest mcache ID for MALL p1 can be same as regular
			// read p1 (which can be same as regular read p0 if plane0_plane1 is also set)
			if (mcache_params[plane_idx].num_mcaches_plane1 > 0 && mcache_params[plane_idx].last_slice_sharing.mall_comb_mcache_p1) {
				mcache_allocations[plane_idx].global_mcache_ids_mall_plane1[mcache_params[plane_idx].num_mcaches_plane1 - 1] =
					mcache_allocations[plane_idx].global_mcache_ids_plane1[mcache_params[plane_idx].num_mcaches_plane1 - 1];
			}
		}

		// If you don't need dedicated mall mcaches, the mall mcache assignments are identical to the normal requesting
		if (!mcache_params[plane_idx].requires_dedicated_mall_mcache) {
			memcpy(mcache_allocations[plane_idx].global_mcache_ids_mall_plane0, mcache_allocations[plane_idx].global_mcache_ids_plane0,
				sizeof(mcache_allocations[plane_idx].global_mcache_ids_mall_plane0));
			memcpy(mcache_allocations[plane_idx].global_mcache_ids_mall_plane1, mcache_allocations[plane_idx].global_mcache_ids_plane1,
				sizeof(mcache_allocations[plane_idx].global_mcache_ids_mall_plane1));
		}
	}
}

static bool dc_calculate_first_second_splitting(const int *mcache_boundaries, int num_boundaries, int shift,
	int pipe_h_vp_start, int pipe_h_vp_end, int *first_offset, int *second_offset)
{
	const int MAX_VP = 0xFFFFFF;
	int left_cache_id;
	int right_cache_id;
	int range_start;
	int range_end;
	bool success = false;

	if (num_boundaries <= 1) {
		if (first_offset && second_offset) {
			*first_offset = 0;
			*second_offset = -1;
		}
		success = true;
		return success;
	} else {
		range_start = 0;
		for (left_cache_id = 0; left_cache_id < num_boundaries; left_cache_id++) {
			range_end = mcache_boundaries[left_cache_id] - shift - 1;

			if (range_start <= pipe_h_vp_start && pipe_h_vp_start <= range_end)
				break;

			range_start = range_end + 1;
		}

		range_end = MAX_VP;
		for (right_cache_id = num_boundaries - 1; right_cache_id >= -1; right_cache_id--) {
			if (right_cache_id >= 0)
				range_start = mcache_boundaries[right_cache_id] - shift;
			else
				range_start = 0;

			if (range_start <= pipe_h_vp_end && pipe_h_vp_end <= range_end) {
				break;
			}
			range_end = range_start - 1;
		}
		right_cache_id = (right_cache_id + 1) % num_boundaries;

		if (right_cache_id == left_cache_id) {
			if (first_offset && second_offset) {
				*first_offset = left_cache_id;
				*second_offset = -1;
			}
			success = true;
		} else if (right_cache_id == (left_cache_id + 1) % num_boundaries) {
			if (first_offset && second_offset) {
				*first_offset = left_cache_id;
				*second_offset = right_cache_id;
			}
			success = true;
		}
	}

	return success;
}

static bool build_mcache_pipe_regs_config(struct dc_state *context, const struct dc_mcache_params *mcache_params, struct dc_mcache_allocations *mcache_allocations)
{
	bool success = true;
	int first_offset, second_offset;
	unsigned int plane_idx;
	int pipe_idx;

	for (pipe_idx = 0; pipe_idx < MAX_PIPES; pipe_idx++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[pipe_idx];
		if (get_plane_index(context, pipe_ctx->plane_state, &plane_idx) && pipe_ctx->plane_res.hubp) {
			reset_mcache_allocations(&pipe_ctx->mcache_regs);
			if (pipe_ctx->plane_state->dcc.enable) {
				// P0 always enabled
				if (!dc_calculate_first_second_splitting(mcache_params[plane_idx].mcache_x_offsets_plane0,
					mcache_params[plane_idx].num_mcaches_plane0,
					0,
					pipe_ctx->plane_res.scl_data.viewport.x,
					pipe_ctx->plane_res.scl_data.viewport.x +
					pipe_ctx->plane_res.scl_data.viewport.width - 1,
					&first_offset, &second_offset)) {
					success = false;
					break;
				}

				pipe_ctx->mcache_regs.main.p0.mcache_id_first =
					mcache_allocations[plane_idx].global_mcache_ids_plane0[first_offset];

				pipe_ctx->mcache_regs.mall.p0.mcache_id_first =
					mcache_allocations[plane_idx].global_mcache_ids_mall_plane0[first_offset];

				if (second_offset >= 0) {
					pipe_ctx->mcache_regs.main.p0.mcache_id_second =
						mcache_allocations[plane_idx].global_mcache_ids_plane0[second_offset];
					pipe_ctx->mcache_regs.main.p0.split_location =
						mcache_params[plane_idx].mcache_x_offsets_plane0[first_offset] - 1;

					pipe_ctx->mcache_regs.mall.p0.mcache_id_second =
						mcache_allocations[plane_idx].global_mcache_ids_mall_plane0[second_offset];
					pipe_ctx->mcache_regs.mall.p0.split_location =
						mcache_params[plane_idx].mcache_x_offsets_plane0[first_offset] - 1;
				}

				// Populate P1 if enabled
				if (is_plane1_enabled(pipe_ctx->plane_state->format)) {
					if (!dc_calculate_first_second_splitting(mcache_params[plane_idx].mcache_x_offsets_plane1,
						mcache_params[plane_idx].num_mcaches_plane1,
						0,
						pipe_ctx->plane_res.scl_data.viewport_c.x,
						pipe_ctx->plane_res.scl_data.viewport_c.x +
						pipe_ctx->plane_res.scl_data.viewport_c.width - 1,
						&first_offset, &second_offset)) {
						success = false;
						break;
					}

					pipe_ctx->mcache_regs.main.p1.mcache_id_first =
						mcache_allocations[plane_idx].global_mcache_ids_plane1[first_offset];

					pipe_ctx->mcache_regs.mall.p1.mcache_id_first =
						mcache_allocations[plane_idx].global_mcache_ids_mall_plane1[first_offset];

					if (second_offset >= 0) {
						pipe_ctx->mcache_regs.main.p1.mcache_id_second =
							mcache_allocations[plane_idx].global_mcache_ids_plane1[second_offset];
						pipe_ctx->mcache_regs.main.p1.split_location =
							mcache_params[plane_idx].mcache_x_offsets_plane1[first_offset] - 1;

						pipe_ctx->mcache_regs.mall.p1.mcache_id_second =
							mcache_allocations[plane_idx].global_mcache_ids_mall_plane1[second_offset];
						pipe_ctx->mcache_regs.mall.p1.split_location =
							mcache_params[plane_idx].mcache_x_offsets_plane1[first_offset] - 1;
					}
				}
			}
		}
	}
	return success;
}

bool dcn60_program_mcache_pipe_config(struct dc_state *context, const struct dc_mcache_params *mcache_params)
{
	struct dc_mcache_allocations mcache_allocations[MAX_PLANES] = {0};
	if (!mcache_params) {
		return false;
	}

	// Step 1: Assign global mcache IDs for each plane
	assign_global_mcache_ids(context, mcache_params, mcache_allocations);

   // Step 2: Build the mcache pipe configuration
   if (!build_mcache_pipe_regs_config(context, mcache_params, mcache_allocations)) {
       return false;
   }

   return true;
}

bool dcn50_program_mcache_pipe_config(struct dc_state *context, const struct dc_mcache_params *mcache_params)
{
	int pipe_idx;

	if (!mcache_params) {
		return false;
	}

	for (pipe_idx = 0; pipe_idx < MAX_PIPES; pipe_idx++) {
		struct pipe_ctx *pipe_ctx = &context->res_ctx.pipe_ctx[pipe_idx];
		reset_mcache_allocations(&pipe_ctx->mcache_regs);
		if (pipe_ctx->plane_state && pipe_ctx->plane_state->dcc.enable) {
			pipe_ctx->mcache_regs.main.p0.mcache_id_first = pipe_ctx->pipe_idx;
			pipe_ctx->mcache_regs.mall.p0.mcache_id_first = pipe_ctx->pipe_idx;
			// Populate P1 if enabled
			if (is_plane1_enabled(pipe_ctx->plane_state->format)) {
				pipe_ctx->mcache_regs.main.p1.mcache_id_first = pipe_ctx->pipe_idx;
				pipe_ctx->mcache_regs.mall.p1.mcache_id_first = pipe_ctx->pipe_idx;
			}
		}
	}
	return true;
}

static struct dce_aux *dcn60_aux_engine_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct aux_engine_dce110 *aux_engine =
		kzalloc(sizeof(struct aux_engine_dce110), GFP_KERNEL);

	if (!aux_engine)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT aux_engine_regs
	aux_engine_regs_init(0),
	aux_engine_regs_init(1),
	aux_engine_regs_init(2),
	aux_engine_regs_init(3);

	dce110_aux_engine_construct(aux_engine, ctx, inst,
				    SW_AUX_TIMEOUT_PERIOD_MULTIPLIER * AUX_TIMEOUT_PERIOD,
				    &aux_engine_regs[inst],
					&aux_mask,
					&aux_shift,
					ctx->dc->caps.extended_aux_timeout_support);

	return &aux_engine->base;
}
#define i2c_inst_regs_init(id)\
	I2C_HW_ENGINE_COMMON_REG_LIST_DCN30_RI(id)

static struct dce_i2c_registers i2c_hw_regs[2];

static const struct dce_i2c_shift i2c_shifts = {
		I2C_COMMON_MASK_SH_LIST_DCN401(__SHIFT)
};

static const struct dce_i2c_mask i2c_masks = {
		I2C_COMMON_MASK_SH_LIST_DCN401(_MASK)
};

static struct dce_i2c_hw *dcn60_i2c_hw_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce_i2c_hw *dce_i2c_hw =
		kzalloc(sizeof(struct dce_i2c_hw), GFP_KERNEL);

	if (!dce_i2c_hw)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT i2c_hw_regs
		i2c_inst_regs_init(1),
		i2c_inst_regs_init(2);

	dcn2_i2c_hw_construct(dce_i2c_hw, ctx, inst,
				    &i2c_hw_regs[inst], &i2c_shifts, &i2c_masks);

	return dce_i2c_hw;
}

static struct clock_source *dcn60_clock_source_create(
		struct dc_context *ctx,
		struct dc_bios *bios,
		enum clock_source_id id,
		const struct dce110_clk_src_regs *regs,
		bool dp_clk_src)
{
	struct dce110_clk_src *clk_src =
		kzalloc(sizeof(struct dce110_clk_src), GFP_KERNEL);

	if (!clk_src)
		return NULL;

	if (dcn50_clk_src_construct(clk_src, ctx, bios, id,
			regs, &cs_shift, &cs_mask)) {
		clk_src->base.dp_clk_src = dp_clk_src;
		return &clk_src->base;
	}

	kfree(clk_src);
	BREAK_TO_DEBUGGER();
	return NULL;
}

static struct hubbub *dcn60_hubbub_create(struct dc_context *ctx)
{
	int i;

	struct dcn20_hubbub *hubbub2 = kzalloc(sizeof(struct dcn20_hubbub),
					  GFP_KERNEL);

	if (!hubbub2)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT hubbub_reg
	hubbub_reg_init();

#undef REG_STRUCT
#define REG_STRUCT vmid_regs
	vmid_regs_init(0),
	vmid_regs_init(1),
	vmid_regs_init(2),
	vmid_regs_init(3),
	vmid_regs_init(4),
	vmid_regs_init(5),
	vmid_regs_init(6),
	vmid_regs_init(7),
	vmid_regs_init(8),
	vmid_regs_init(9),
	vmid_regs_init(10),
	vmid_regs_init(11),
	vmid_regs_init(12),
	vmid_regs_init(13),
	vmid_regs_init(14),
	vmid_regs_init(15);

	hubbub60_construct(hubbub2, ctx,
			&hubbub_reg,
			&hubbub_shift,
			&hubbub_mask,
			DCN6_0_DEFAULT_DET_SIZE, //nominal (default) detile buffer size in kbytes,
			8, //dml2 ip_params_st.pixel_chunk_size_kbytes
			DCN6_0_CRB_SIZE_KB); //dml2 ip_params_st.config_return_buffer_size_in_kbytes

	for (i = 0; i < res_cap_dcn6_0.num_vmid; i++) {
		struct dcn20_vmid *vmid = &hubbub2->vmid[i];

		vmid->ctx = ctx;

		vmid->regs = &vmid_regs[i];
		vmid->shifts = &vmid_shifts;
		vmid->masks = &vmid_masks;
	}

	return &hubbub2->base;
}

static struct hubp *dcn60_hubp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn20_hubp *hubp2 =
		kzalloc(sizeof(struct dcn20_hubp), GFP_KERNEL);

	if (!hubp2)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT hubp_regs
		hubp_regs_init(0),
		hubp_regs_init(1),
		hubp_regs_init(2),
		hubp_regs_init(3);

	if (hubp60_construct(hubp2, ctx, inst,
			&hubp_regs[inst], &hubp_shift, &hubp_mask))
		return &hubp2->base;

	BREAK_TO_DEBUGGER();
	kfree(hubp2);
	return NULL;
}

static void dcn60_dpp_destroy(struct dpp **dpp)
{
	kfree(TO_DCN60_DPP(*dpp));
	*dpp = NULL;
}

static struct dpp *dcn60_dpp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn60_dpp *dpp60 =
		kzalloc(sizeof(struct dcn60_dpp), GFP_KERNEL);

	if (!dpp60)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT dpp_regs
	dpp_regs_init(0),
	dpp_regs_init(1),
	dpp_regs_init(2),
	dpp_regs_init(3);

	if (dpp60_construct(dpp60, ctx, inst,
			&dpp_regs[inst], &tf_shift, &tf_mask))
		return &dpp60->base;

	BREAK_TO_DEBUGGER();
	kfree(dpp60);
	return NULL;
}

static struct mpc *dcn60_mpc_create(
		struct dc_context *ctx,
		int num_mpcc,
		int num_rmu)
{
	struct dcn60_mpc *mpc60 = kzalloc(sizeof(struct dcn60_mpc),
					  GFP_KERNEL);

	if (!mpc60)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT mpc_regs
	dcn_mpc_regs_init();

	dcn60_mpc_construct(mpc60, ctx,
			&mpc_regs,
			&mpc_shift,
			&mpc_mask,
			num_mpcc,
			num_rmu);

	return &mpc60->base;
}

static struct output_pixel_processor *dcn60_opp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_opp *opp2 =
		kzalloc(sizeof(struct dcn20_opp), GFP_KERNEL);

	if (!opp2) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

#undef REG_STRUCT
#define REG_STRUCT opp_regs
	opp_regs_init(0),
	opp_regs_init(1),
	opp_regs_init(2),
	opp_regs_init(3);

	dcn60_opp_construct(opp2, ctx, inst,
			&opp_regs[inst], &opp_shift, &opp_mask);
	return &opp2->base;
}

static struct timing_generator *dcn60_timing_generator_create(
		struct dc_context *ctx,
		uint32_t instance)
{
	struct optc *tgn10 =
		kzalloc(sizeof(struct optc), GFP_KERNEL);

	if (!tgn10)
		return NULL;
#undef REG_STRUCT
#define REG_STRUCT optc_regs
	optc_regs_init(0),
	optc_regs_init(1),
	optc_regs_init(2),
	optc_regs_init(3);

	tgn10->base.inst = instance;
	tgn10->base.ctx = ctx;

	tgn10->tg_regs = &optc_regs[instance];
	tgn10->tg_shift = &optc_shift;
	tgn10->tg_mask = &optc_mask;

	dcn60_timing_generator_init(tgn10);

	return &tgn10->base;
}

static const struct encoder_feature_support link_enc_feature = {
		.max_hdmi_deep_color = COLOR_DEPTH_121212,
		.max_hdmi_pixel_clock = 600000,
		.hdmi_ycbcr420_supported = true,
		.dp_ycbcr420_supported = true,
		.fec_supported = true,
		.flags.bits.IS_HBR2_CAPABLE = true,
		.flags.bits.IS_HBR3_CAPABLE = true,
		.flags.bits.IS_TPS3_CAPABLE = true,
		.flags.bits.IS_TPS4_CAPABLE = true
};

static struct link_encoder *dcn60_link_encoder_create(
	struct dc_context *ctx,
	const struct encoder_init_data *enc_init_data)
{
	struct dcn20_link_encoder *enc20 =
		kzalloc(sizeof(struct dcn20_link_encoder), GFP_KERNEL);

	if (!enc20 || enc_init_data->hpd_source >= ARRAY_SIZE(link_enc_hpd_regs)) {
		kfree(enc20);
		return NULL;
	}

#undef REG_STRUCT
#define REG_STRUCT link_enc_aux_regs
	aux_regs_init(0, 1),
	aux_regs_init(1, 2),
	aux_regs_init(2, 3),
	aux_regs_init(3, 4);

#undef REG_STRUCT
#define REG_STRUCT link_enc_hpd_regs
	hpd_regs_init(0),
	hpd_regs_init(1),
	hpd_regs_init(2),
	hpd_regs_init(3);
#undef REG_STRUCT
#define REG_STRUCT link_enc_regs
	link_regs_init(0, A),
	link_regs_init(1, B),
	link_regs_init(2, C),
	link_regs_init(3, D);

	dcn60_link_encoder_construct(enc20,
			enc_init_data,
			&link_enc_feature,
			&link_enc_regs[enc_init_data->transmitter],
			&link_enc_aux_regs[enc_init_data->channel - 1],
			&link_enc_hpd_regs[enc_init_data->hpd_source],
			&le_shift,
			&le_mask);
	return &enc20->enc10.base;
}

static void read_dce_straps(
	struct dc_context *ctx,
	struct resource_straps *straps)
{
	generic_reg_get(ctx, regDC_PINSTRAPS + BASE(regDC_PINSTRAPS_BASE_IDX),
		FN(DC_PINSTRAPS, DC_PINSTRAPS_AUDIO), &straps->dc_pinstraps_audio);

}

static struct audio *dcn60_create_audio(
		struct dc_context *ctx, unsigned int inst)
{

#undef REG_STRUCT
#define REG_STRUCT audio_regs
	audio_regs_init(0),
	audio_regs_init(1),
	audio_regs_init(2),
	audio_regs_init(3),
	audio_regs_init(4);

	return dce_audio_create(ctx, inst,
			&audio_regs[inst], &audio_shift, &audio_mask);
}

static struct vpg *dcn60_vpg_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn31_vpg *vpg6 = kzalloc(sizeof(struct dcn31_vpg), GFP_KERNEL);

	if (!vpg6)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT vpg_regs
	vpg_regs_init(0),
	vpg_regs_init(1),
	vpg_regs_init(2),
	vpg_regs_init(3),
	vpg_regs_init(4),
	vpg_regs_init(5),
	vpg_regs_init(6),
	vpg_regs_init(7),
	vpg_regs_init(8);

	vpg31_construct(vpg6, ctx, inst,
			&vpg_regs[inst],
			&vpg_shift,
			&vpg_mask);

	return &vpg6->base;
}

static struct apg *dcn60_apg_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn31_apg *apg60 = kzalloc(sizeof(struct dcn31_apg), GFP_KERNEL);

	if (!apg60)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT apg_regs
	apg_regs_init(0),
	apg_regs_init(1),
	apg_regs_init(2),
	apg_regs_init(3);

	apg31_construct(apg60, ctx, inst,
			&apg_regs[inst],
			&apg_shift,
			&apg_mask);

	return &apg60->base;
}

static struct stream_encoder *dcn60_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dcn10_stream_encoder *enc1;
	struct vpg *vpg;
	struct apg *apg;
	int vpg_inst;
	uint32_t apg_inst;

	/* Mapping of VPG, APG, DME register blocks to DIO block instance */
	if (eng_id <= ENGINE_ID_DIGF) {
		vpg_inst = eng_id;
		apg_inst = eng_id;
	} else
		return NULL;

	enc1 = kzalloc(sizeof(struct dcn10_stream_encoder), GFP_KERNEL);
	vpg = dcn60_vpg_create(ctx, vpg_inst);
	apg = dcn60_apg_create(ctx, apg_inst);

	if (!enc1 || !vpg || !apg || eng_id >= ARRAY_SIZE(stream_enc_regs)) {
		kfree(enc1);
		kfree(vpg);
		kfree(apg);
		return NULL;
	}
#undef REG_STRUCT
#define REG_STRUCT stream_enc_regs
	stream_enc_regs_init(0),
	stream_enc_regs_init(1),
	stream_enc_regs_init(2),
	stream_enc_regs_init(3);

	dcn60_dio_stream_encoder_construct(enc1, ctx, ctx->dc_bios,
					eng_id, vpg, apg,
					&stream_enc_regs[eng_id],
					&se_shift, &se_mask);
	return &enc1->base;
}

static struct hpo_frl_stream_encoder *dcn60_hpo_frl_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dcn401_hpo_frl_stream_encoder *hpo_enc60;
	struct vpg *vpg;
	struct apg *apg;
	int vpg_inst;
	int apg_inst;

#undef REG_STRUCT
#define REG_STRUCT hpo_frl_stream_enc_regs
	hpo_frl_stream_encoder_reg_list(0),
	hpo_frl_stream_encoder_dme_reg_list(4);

	/* Mapping of VPG, APG, DME register blocks to HPO block instance */
	if (eng_id == ENGINE_ID_HPO_0) {
		vpg_inst = 4;
		apg_inst = 4;
	} else
		return NULL;

	/* allocate HPO stream encoder and create VPG, APG sub-blocks */
	hpo_enc60 = kzalloc(sizeof(struct dcn401_hpo_frl_stream_encoder), GFP_KERNEL);
	vpg = dcn60_vpg_create(ctx, vpg_inst);
	apg = dcn60_apg_create(ctx, apg_inst);

	if (!hpo_enc60 || !vpg || !apg) {
		kfree(hpo_enc60);
		kfree(vpg);
		kfree(apg);
		return NULL;
	}

	dcn60_hpo_frl_stream_encoder_construct(hpo_enc60, ctx, ctx->dc_bios,
					eng_id, vpg, apg,
					&hpo_frl_stream_enc_regs[eng_id-ENGINE_ID_HPO_0],
					&hpo_se_shift, &hpo_se_mask);

	return &hpo_enc60->base;
}

static struct hpo_frl_link_encoder *dcn60_hpo_frl_link_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dcn30_hpo_frl_link_encoder *hpo_link_enc;
	ASSERT((eng_id == ENGINE_ID_HPO_0) || (eng_id == ENGINE_ID_HPO_1));

#undef REG_STRUCT
#define REG_STRUCT hpo_frl_link_enc_regs
	hpo_frl_link_encoder_reg_list(0);

	/* allocate HPO link encoder */
	hpo_link_enc = kzalloc(sizeof(struct dcn30_hpo_frl_link_encoder), GFP_KERNEL);
	if (!hpo_link_enc)
		return NULL; /* out of memory */

	hpo_frl_link_encoder60_construct(hpo_link_enc, ctx, eng_id-ENGINE_ID_HPO_0,
					&hpo_frl_link_enc_regs[eng_id-ENGINE_ID_HPO_0],
					&hpo_le_shift, &hpo_le_mask);

	return &hpo_link_enc->base;
}

static struct hpo_dp_stream_encoder *dcn60_hpo_dp_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dcn31_hpo_dp_stream_encoder *hpo_dp_enc60;
	struct vpg *vpg;
	struct apg *apg;
	uint32_t hpo_dp_inst;
	uint32_t vpg_inst;
	uint32_t apg_inst;

	ASSERT((eng_id >= ENGINE_ID_HPO_DP_0) && (eng_id <= ENGINE_ID_HPO_DP_3));
	hpo_dp_inst = eng_id - ENGINE_ID_HPO_DP_0;

	/* Mapping of VPG register blocks to HPO DP block instance:
	 * VPG[6] -> HPO_DP[0]
	 * VPG[7] -> HPO_DP[1]
	 * VPG[8] -> HPO_DP[2]
	 * VPG[9] -> HPO_DP[3]
	 */
	vpg_inst = hpo_dp_inst + 5;

	/* Mapping of APG register blocks to HPO DP block instance:
	 * APG[0] -> HPO_DP[0]
	 * APG[1] -> HPO_DP[1]
	 * APG[2] -> HPO_DP[2]
	 * APG[3] -> HPO_DP[3]
	 */
	apg_inst = hpo_dp_inst;

	/* allocate HPO stream encoder and create VPG sub-block */
	hpo_dp_enc60 = kzalloc(sizeof(struct dcn31_hpo_dp_stream_encoder), GFP_KERNEL);
	vpg = dcn60_vpg_create(ctx, vpg_inst);
	apg = dcn60_apg_create(ctx, apg_inst);

	if (!hpo_dp_enc60 || !vpg || !apg) {
		kfree(hpo_dp_enc60);
		kfree(vpg);
		kfree(apg);
		return NULL;
	}

#undef REG_STRUCT
#define REG_STRUCT hpo_dp_stream_enc_regs
	hpo_dp_stream_encoder_reg_init(0),
	hpo_dp_stream_encoder_reg_init(1),
	hpo_dp_stream_encoder_reg_init(2),
	hpo_dp_stream_encoder_reg_init(3);

	dcn31_hpo_dp_stream_encoder_construct(hpo_dp_enc60, ctx, ctx->dc_bios,
					hpo_dp_inst, eng_id, vpg, apg,
					&hpo_dp_stream_enc_regs[hpo_dp_inst],
					&hpo_dp_se_shift, &hpo_dp_se_mask);

	return &hpo_dp_enc60->base;
}

static struct hpo_dp_link_encoder *dcn60_hpo_dp_link_encoder_create(
	uint8_t inst,
	struct dc_context *ctx)
{
	struct dcn31_hpo_dp_link_encoder *hpo_dp_enc60;

	/* allocate HPO link encoder */
	hpo_dp_enc60 = kzalloc(sizeof(struct dcn31_hpo_dp_link_encoder), GFP_KERNEL);
	if (!hpo_dp_enc60)
		return NULL; /* out of memory */

#undef REG_STRUCT
#define REG_STRUCT hpo_dp_link_enc_regs
	hpo_dp_link_encoder_reg_init(0),
	hpo_dp_link_encoder_reg_init(1),
	hpo_dp_link_encoder_reg_init(2),
	hpo_dp_link_encoder_reg_init(3);

	hpo_dp_link_encoder32_construct(hpo_dp_enc60, ctx, inst,
					&hpo_dp_link_enc_regs[inst],
					&hpo_dp_le_shift, &hpo_dp_le_mask);

	return &hpo_dp_enc60->base;
}

static struct dce_hwseq *dcn60_hwseq_create(
	struct dc_context *ctx)
{
	struct dce_hwseq *hws = kzalloc(sizeof(struct dce_hwseq), GFP_KERNEL);

#undef REG_STRUCT
#define REG_STRUCT hwseq_reg
	hwseq_reg_init();

	if (hws) {
		hws->ctx = ctx;
		hws->regs = &hwseq_reg;
		hws->shifts = &hwseq_shift;
		hws->masks = &hwseq_mask;
	}

	return hws;
}
static const struct resource_create_funcs res_create_funcs = {
	.read_dce_straps = read_dce_straps,
	.create_audio = dcn60_create_audio,
	.create_stream_encoder = dcn60_stream_encoder_create,
	.create_hpo_frl_stream_encoder = dcn60_hpo_frl_stream_encoder_create,
	.create_hpo_dp_stream_encoder = dcn60_hpo_dp_stream_encoder_create,
	.create_hpo_dp_link_encoder = dcn60_hpo_dp_link_encoder_create,
	.create_hwseq = dcn60_hwseq_create,
};

static void dcn60_dsc_destroy(struct display_stream_compressor **dsc)
{
	kfree(container_of(*dsc, struct dcn60_dsc, base));
	*dsc = NULL;
}

static void dcn60_resource_destruct(struct dcn60_resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL) {
			if (pool->base.stream_enc[i]->vpg != NULL) {
				kfree(DCN31_VPG_FROM_VPG(pool->base.stream_enc[i]->vpg));
				pool->base.stream_enc[i]->vpg = NULL;
			}
			if (pool->base.stream_enc[i]->apg != NULL) {
				kfree(DCN31_APG_FROM_APG(pool->base.stream_enc[i]->apg));
				pool->base.stream_enc[i]->apg = NULL;
			}
			kfree(DCN10STRENC_FROM_STRENC(pool->base.stream_enc[i]));
			pool->base.stream_enc[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.hpo_frl_stream_enc_count; i++) {
		if (pool->base.hpo_frl_stream_enc[i] != NULL) {
			if (pool->base.hpo_frl_stream_enc[i]->vpg != NULL) {
				kfree(DCN31_VPG_FROM_VPG(pool->base.hpo_frl_stream_enc[i]->vpg));
				pool->base.hpo_frl_stream_enc[i]->vpg = NULL;
			}
			if (pool->base.hpo_frl_stream_enc[i]->apg != NULL) {
				kfree(DCN31_APG_FROM_APG(pool->base.hpo_frl_stream_enc[i]->apg));
				pool->base.hpo_frl_stream_enc[i]->apg = NULL;
			}
			kfree(DCN401_HPO_FRL_STRENC_FROM_HPO_FRL_STRENC(pool->base.hpo_frl_stream_enc[i]));
			pool->base.hpo_frl_stream_enc[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.hpo_dp_stream_enc_count; i++) {
		if (pool->base.hpo_dp_stream_enc[i] != NULL) {
			if (pool->base.hpo_dp_stream_enc[i]->vpg != NULL) {
				kfree(DCN31_VPG_FROM_VPG(pool->base.hpo_dp_stream_enc[i]->vpg));
				pool->base.hpo_dp_stream_enc[i]->vpg = NULL;
			}
			if (pool->base.hpo_dp_stream_enc[i]->apg != NULL) {
				kfree(DCN31_APG_FROM_APG(pool->base.hpo_dp_stream_enc[i]->apg));
				pool->base.hpo_dp_stream_enc[i]->apg = NULL;
			}
			kfree(DCN3_1_HPO_DP_STREAM_ENC_FROM_HPO_STREAM_ENC(pool->base.hpo_dp_stream_enc[i]));
			pool->base.hpo_dp_stream_enc[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.hpo_dp_link_enc_count; i++) {
		if (pool->base.hpo_dp_link_enc[i] != NULL) {
			kfree(DCN3_1_HPO_DP_LINK_ENC_FROM_HPO_LINK_ENC(pool->base.hpo_dp_link_enc[i]));
			pool->base.hpo_dp_link_enc[i] = NULL;
		}
	}

	for (i = 0; i < (unsigned int)pool->base.res_cap->num_dsc; i++) {
		if (pool->base.dscs[i] != NULL)
			dcn60_dsc_destroy(&pool->base.dscs[i]);
	}

	if (pool->base.mpc != NULL) {
		kfree(TO_DCN20_MPC(pool->base.mpc));
		pool->base.mpc = NULL;
	}
	if (pool->base.hubbub != NULL) {
		kfree(TO_DCN20_HUBBUB(pool->base.hubbub));
		pool->base.hubbub = NULL;
	}
	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.dpps[i] != NULL)
			dcn60_dpp_destroy(&pool->base.dpps[i]);

		if (pool->base.ipps[i] != NULL)
			pool->base.ipps[i]->funcs->ipp_destroy(&pool->base.ipps[i]);

		if (pool->base.hubps[i] != NULL) {
			kfree(TO_DCN20_HUBP(pool->base.hubps[i]));
			pool->base.hubps[i] = NULL;
		}

		if (pool->base.irqs != NULL) {
			dal_irq_service_destroy(&pool->base.irqs);
		}
	}

	for (i = 0; i < (unsigned int)pool->base.res_cap->num_aux; i++) {
		if (pool->base.engines[i] != NULL)
			dce110_engine_destroy(&pool->base.engines[i]);
	}

	for (i = 0; i < (unsigned int)pool->base.res_cap->num_ddc; i++) {
		if (pool->base.hw_i2cs[i] != NULL) {
			kfree(pool->base.hw_i2cs[i]);
			pool->base.hw_i2cs[i] = NULL;
		}
		if (pool->base.sw_i2cs[i] != NULL) {
			kfree(pool->base.sw_i2cs[i]);
			pool->base.sw_i2cs[i] = NULL;
		}
	}

	for (i = 0; i < (unsigned int)pool->base.res_cap->num_opp; i++) {
		if (pool->base.opps[i] != NULL)
			pool->base.opps[i]->funcs->opp_destroy(&pool->base.opps[i]);
	}

	for (i = 0; i < (unsigned int)pool->base.res_cap->num_timing_generator; i++) {
		if (pool->base.timing_generators[i] != NULL)	{
			kfree(DCN10TG_FROM_TG(pool->base.timing_generators[i]));
			pool->base.timing_generators[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.audio_count; i++) {
		if (pool->base.audios[i])
			dce_aud_destroy(&pool->base.audios[i]);
	}

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] != NULL) {
			dcn20_clock_source_destroy(&pool->base.clock_sources[i]);
			pool->base.clock_sources[i] = NULL;
		}
	}

	for (i = 0; i < (unsigned int)pool->base.res_cap->num_mpc_3dlut; i++) {
		if (pool->base.mpc_lut[i] != NULL) {
			dc_3dlut_func_release(pool->base.mpc_lut[i]);
			pool->base.mpc_lut[i] = NULL;
		}
		if (pool->base.mpc_shaper[i] != NULL) {
			dc_transfer_func_release(pool->base.mpc_shaper[i]);
			pool->base.mpc_shaper[i] = NULL;
		}
	}

	if (pool->base.dp_clock_source != NULL) {
		dcn20_clock_source_destroy(&pool->base.dp_clock_source);
		pool->base.dp_clock_source = NULL;
	}

	for (i = 0; i < (unsigned int)pool->base.res_cap->num_timing_generator; i++) {
		if (pool->base.multiple_abms[i] != NULL)
			dce_abm_destroy(&pool->base.multiple_abms[i]);
	}

	if (pool->base.psr != NULL)
		dmub_psr_destroy(&pool->base.psr);

	if (pool->base.dccg != NULL)
		dcn_dccg_destroy(&pool->base.dccg);

	if (pool->base.oem_device != NULL) {
		struct dc *dc = pool->base.oem_device->ctx->dc;

		dc->link_srv->destroy_ddc_service(&pool->base.oem_device);
	}
}

static struct display_stream_compressor *dcn60_dsc_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn60_dsc *dsc =
		kzalloc(sizeof(struct dcn60_dsc), GFP_KERNEL);

	if (!dsc) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

#undef REG_STRUCT
#define REG_STRUCT dsc_regs
	dsc_regs_init(0),
	dsc_regs_init(1),
	dsc_regs_init(2),
	dsc_regs_init(3);

	dsc60_construct(dsc, ctx, inst, &dsc_regs[inst], &dsc_shift, &dsc_mask);

	//dsc->max_image_width = 6016;
	dsc->max_image_width = 5760;

	return &dsc->base;
}

static void dcn60_destroy_resource_pool(struct resource_pool **pool)
{
	struct dcn60_resource_pool *dcn60_pool = TO_DCN60_RES_POOL(*pool);

	dcn60_resource_destruct(dcn60_pool);
	kfree(dcn60_pool);
	*pool = NULL;
}

static struct dc_cap_funcs cap_funcs = {
	.get_dcc_compression_cap = dcn20_get_dcc_compression_cap,
};

static void dcn60_update_bw_bounding_box_fpu(struct dc *dc, struct clk_bw_params *bw_params)
{
	(void)bw_params;
	dc_assert_fp_enabled();

	if (dc->debug.using_dml2 && dc->current_state && dc->current_state->bw_ctx.dml2)
		dml2_reinit(dc, &dc->dml2_options, &dc->current_state->bw_ctx.dml2);

	if (dc->debug.using_dml2 && dc->current_state && dc->current_state->bw_ctx.dml2_dc_power_source)
		dml2_reinit(dc, &dc->dml2_dc_power_options, &dc->current_state->bw_ctx.dml2_dc_power_source);
}

static void dcn60_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params)
{
	DC_FP_START();
	dcn60_update_bw_bounding_box_fpu(dc, bw_params);
	DC_FP_END();
}

static void dcn60_build_pipe_pix_clk_params(struct pipe_ctx *pipe_ctx)
{
	const struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct link_encoder *link_enc = NULL;
	struct pixel_clk_params *pixel_clk_params = &pipe_ctx->stream_res.pix_clk_params;

	pixel_clk_params->requested_pix_clk_100hz = stream->timing.pix_clk_100hz;

	link_enc = link_enc_cfg_get_link_enc(link);
	if (link_enc)
		pixel_clk_params->encoder_object_id = link_enc->id;

	pixel_clk_params->signal_type = pipe_ctx->stream->signal;
	pixel_clk_params->controller_id = pipe_ctx->stream_res.tg->inst + 1;
	/* TODO: un-hardcode*/

	/* TODO - DP2.0 HW: calculate requested_sym_clk for UHBR rates */

	pixel_clk_params->requested_sym_clk = LINK_RATE_LOW *
		LINK_RATE_REF_FREQ_IN_KHZ;
	pixel_clk_params->flags.ENABLE_SS = 0;
	pixel_clk_params->color_depth =
		stream->timing.display_color_depth;
	pixel_clk_params->flags.DISPLAY_BLANKED = 1;
	pixel_clk_params->pixel_encoding = stream->timing.pixel_encoding;

	if (stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR422)
		pixel_clk_params->color_depth = COLOR_DEPTH_888;

	if (stream->timing.timing_3d_format == TIMING_3D_FORMAT_HW_FRAME_PACKING)
		pixel_clk_params->requested_pix_clk_100hz *= 2;
	if (dc_is_tmds_signal(stream->signal) &&
			stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR420)
		pixel_clk_params->requested_pix_clk_100hz /= 2;

	pipe_ctx->clock_source->funcs->get_pix_clk_dividers(
			pipe_ctx->clock_source,
			&pipe_ctx->stream_res.pix_clk_params,
			&pipe_ctx->pll_settings);

	pixel_clk_params->dio_se_pix_per_cycle = 1;
	if (dc_is_tmds_signal(stream->signal) &&
			stream->timing.pixel_encoding == PIXEL_ENCODING_YCBCR420) {
		pixel_clk_params->dio_se_pix_per_cycle = 2;
	} else if (dc_is_dp_signal(stream->signal)) {
		/* round up to nearest power of 2, or max at 8 pixels per cycle */
		if (pixel_clk_params->requested_pix_clk_100hz > (uint32_t)(4 * stream->ctx->dc->clk_mgr->dprefclk_khz * 10)) {
			pixel_clk_params->dio_se_pix_per_cycle = 8;
		} else if (pixel_clk_params->requested_pix_clk_100hz > (uint32_t)(2 * stream->ctx->dc->clk_mgr->dprefclk_khz * 10)) {
			pixel_clk_params->dio_se_pix_per_cycle = 4;
		} else if (pixel_clk_params->requested_pix_clk_100hz > (uint32_t)(stream->ctx->dc->clk_mgr->dprefclk_khz * 10)) {
			pixel_clk_params->dio_se_pix_per_cycle = 2;
		} else {
			pixel_clk_params->dio_se_pix_per_cycle = 1;
		}
	}
}

static int dcn60_get_power_profile(const struct dc_state *context)
{
	unsigned int uclk_mhz = context->bw_ctx.bw.dcn.clk.dramclk_khz / 1000;
	int dpm_level = 0;

	for (unsigned int i = 0; i < context->clk_mgr->bw_params->clk_table.num_entries_per_clk.num_memclk_levels; i++) {
		if (context->clk_mgr->bw_params->clk_table.entries[i].memclk_mhz == 0 ||
			uclk_mhz < context->clk_mgr->bw_params->clk_table.entries[i].memclk_mhz)
			break;
		if (uclk_mhz > context->clk_mgr->bw_params->clk_table.entries[i].memclk_mhz)
			dpm_level++;
	}

	return dpm_level;
}

static struct resource_funcs dcn60_res_pool_funcs = {
	.destroy = dcn60_destroy_resource_pool,
	.link_enc_create = dcn60_link_encoder_create,
	.link_enc_create_minimal = NULL,
	.hpo_frl_link_enc_create = dcn60_hpo_frl_link_encoder_create,
	.panel_cntl_create = dcn32_panel_cntl_create,
	.validate_bandwidth = dcn401_validate_bandwidth,
	.calculate_wm_and_dlg = NULL,
	.populate_dml_pipes = NULL,
	.acquire_free_pipe_as_secondary_dpp_pipe = dcn32_acquire_free_pipe_as_secondary_dpp_pipe,
	.acquire_free_pipe_as_secondary_opp_head = dcn32_acquire_free_pipe_as_secondary_opp_head,
	.release_pipe = dcn20_release_pipe,
	.add_stream_to_ctx = dcn30_add_stream_to_ctx,
	.add_dsc_to_stream_resource = dcn20_add_dsc_to_stream_resource,
	.remove_stream_from_ctx = dcn20_remove_stream_from_ctx,
	.populate_dml_writeback_from_context = dcn30_populate_dml_writeback_from_context,
	.set_mcif_arb_params = dcn30_set_mcif_arb_params,
	.find_first_free_match_stream_enc_for_link = dcn10_find_first_free_match_stream_enc_for_link,
	.acquire_post_bldn_3dlut = dcn32_acquire_post_bldn_3dlut,
	.release_post_bldn_3dlut = dcn32_release_post_bldn_3dlut,
	.update_bw_bounding_box = dcn60_update_bw_bounding_box,
	.patch_unknown_plane_state = dcn401_patch_unknown_plane_state,
	.update_soc_for_wm_a = dcn30_update_soc_for_wm_a,
	.add_phantom_pipes = dcn32_add_phantom_pipes,
	.build_pipe_pix_clk_params = dcn60_build_pipe_pix_clk_params,
	.get_power_profile = dcn60_get_power_profile,
	.program_mcache_pipe_config = dcn60_program_mcache_pipe_config,
	.get_default_tiling_info = dcn401_get_default_tiling_info
};

static uint32_t read_pipe_fuses(struct dc_context *ctx)
{
	(void)ctx;
	uint32_t value = 0; // CC_DC_PIPE_DIS in DCN6 only indicates DCN IP full fuse, not per pipe fuse
	return value;
}

static bool dcn60_resource_construct(
	uint8_t num_virtual_links,
	struct dc *dc,
	struct dcn60_resource_pool *pool)
{
	int i;
	unsigned int j;
	struct dc_context *ctx = dc->ctx;
	struct irq_service_init_data init_data;
	struct ddc_service_init_data ddc_init_data = {0};
	uint32_t pipe_fuses = 0;
	uint32_t num_pipes  = 4;

#undef REG_STRUCT
#define REG_STRUCT bios_regs
	bios_regs_init();

#undef REG_STRUCT
#define REG_STRUCT clk_src_regs
	clk_src_regs_init(0, A),
	clk_src_regs_init(1, B),
	clk_src_regs_init(2, C),
	clk_src_regs_init(3, D);

#undef REG_STRUCT
#define REG_STRUCT abm_regs
		abm_regs_init(0),
		abm_regs_init(1),
		abm_regs_init(2),
		abm_regs_init(3);

#undef REG_STRUCT
#define REG_STRUCT dccg_regs
	dccg_regs_init();

	ctx->dc_bios->regs = &bios_regs;

	pool->base.res_cap = &res_cap_dcn6_0;

	/* max number of pipes for ASIC before checking for pipe fuses */
	num_pipes  = pool->base.res_cap->num_timing_generator;
	pipe_fuses = read_pipe_fuses(ctx);

	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++)
		if (pipe_fuses & 1 << i)
			num_pipes--;

	if (pipe_fuses & 1)
		ASSERT(0); //Unexpected - Pipe 0 should always be fully functional!

	if (pipe_fuses & CC_DC_PIPE_DIS__DC_FULL_DIS_MASK)
		ASSERT(0); //Entire DCN is harvested!

	pool->base.funcs = &dcn60_res_pool_funcs;

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->base.underlay_pipe_index = (unsigned int)NO_UNDERLAY_PIPE;
	pool->base.timing_generator_count = num_pipes;
	pool->base.pipe_count = num_pipes;
	pool->base.mpcc_count = num_pipes;
	dc->caps.max_downscale_ratio = 600;
	dc->caps.i2c_speed_in_khz = 95;
	dc->caps.i2c_speed_in_khz_hdcp = 95; /*1.4 w/a applied by default*/
	/* TODO: Bring max cursor size back to 256 after subvp cursor corruption is fixed*/
	dc->caps.max_cursor_size = 64;
	dc->caps.cursor_not_scaled = true;
	dc->caps.min_horizontal_blanking_period = 80;
	dc->caps.dmdata_alloc_size = 2048;
	dc->caps.cursor_cache_size = dc->caps.max_cursor_size * dc->caps.max_cursor_size * 8;
	dc->caps.cache_line_size = 64;
	dc->caps.cache_num_ways = 16;

	dc->caps.max_slave_planes = 2;
	dc->caps.max_slave_yuv_planes = 2;
	dc->caps.max_slave_rgb_planes = 2;
	dc->caps.post_blend_color_processing = true;
	dc->caps.force_dp_tps4_for_cp2520 = true;
	dc->caps.hdmi_hpo = true;
	dc->caps.dp_hpo = true;
	dc->caps.dp_hdmi21_pcon_support = true;
	dc->caps.edp_dsc_support = true;
	dc->caps.extended_aux_timeout_support = true;
	dc->caps.dmcub_support = true;
	dc->caps.max_v_total = (1 << 15) - 1;

	if (ASICREV_IS_GC_12_0_1_A0(dc->ctx->asic_id.hw_internal_rev))
		dc->caps.dcc_plane_width_limit = 7680;

	/* Color pipeline capabilities */
	dc->caps.color.dpp.dcn_arch = 1;
	dc->caps.color.dpp.input_lut_shared = 0;
	dc->caps.color.dpp.icsc = 1;
	dc->caps.color.dpp.dgam_ram = 0; // must use gamma_corr
	dc->caps.color.dpp.dgam_rom_caps.srgb = 1;
	dc->caps.color.dpp.dgam_rom_caps.bt2020 = 1;
	dc->caps.color.dpp.dgam_rom_caps.gamma2_2 = 1;
	dc->caps.color.dpp.dgam_rom_caps.pq = 1;
	dc->caps.color.dpp.dgam_rom_caps.hlg = 1;
	dc->caps.color.dpp.post_csc = 1;
	dc->caps.color.dpp.gamma_corr = 1;
	dc->caps.color.dpp.dgam_rom_for_yuv = 0;
	dc->caps.color.dpp.upsp_pre_scaler = 1;

	dc->caps.color.dpp.hw_3d_lut = 0;
	dc->caps.color.dpp.ogam_ram = 0;
	// no OGAM ROM on DCN2 and later ASICs
	dc->caps.color.dpp.ogam_rom_caps.srgb = 0;
	dc->caps.color.dpp.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.dpp.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.dpp.ogam_rom_caps.pq = 0;
	dc->caps.color.dpp.ogam_rom_caps.hlg = 0;
	dc->caps.color.dpp.ocsc = 0;

	dc->caps.color.mpc.gamut_remap = 1;
	dc->caps.color.mpc.num_3dluts = (uint16_t)pool->base.res_cap->num_mpc_3dlut; //4, configurable to be before or after BLND in MPCC
	dc->caps.color.mpc.ogam_ram = 1;
	dc->caps.color.mpc.ogam_rom_caps.srgb = 0;
	dc->caps.color.mpc.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.mpc.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.mpc.ogam_rom_caps.pq = 0;
	dc->caps.color.mpc.ogam_rom_caps.hlg = 0;
	dc->caps.color.mpc.ocsc = 1;
	dc->caps.color.mpc.preblend = true;
	/* HACK: Force FRL support until BIOS is ready. */
	dc->config.force_hdmi21_frl_enc_enable = true;
	dc->config.use_spl = true;
	dc->config.prefer_easf = true;

	dc->config.dcn_sharpness_range.sdr_rgb_min = 0;
	dc->config.dcn_sharpness_range.sdr_rgb_max = 1750;
	dc->config.dcn_sharpness_range.sdr_rgb_mid = 750;
	dc->config.dcn_sharpness_range.sdr_yuv_min = 0;
	dc->config.dcn_sharpness_range.sdr_yuv_max = 3500;
	dc->config.dcn_sharpness_range.sdr_yuv_mid = 1500;
	dc->config.dcn_sharpness_range.hdr_rgb_min = 0;
	dc->config.dcn_sharpness_range.hdr_rgb_max = 2750;
	dc->config.dcn_sharpness_range.hdr_rgb_mid = 1500;

	dc->config.dcn_override_sharpness_range.sdr_rgb_min = 0;
	dc->config.dcn_override_sharpness_range.sdr_rgb_max = 3250;
	dc->config.dcn_override_sharpness_range.sdr_rgb_mid = 1250;
	dc->config.dcn_override_sharpness_range.sdr_yuv_min = 0;
	dc->config.dcn_override_sharpness_range.sdr_yuv_max = 3500;
	dc->config.dcn_override_sharpness_range.sdr_yuv_mid = 1500;
	dc->config.dcn_override_sharpness_range.hdr_rgb_min = 0;
	dc->config.dcn_override_sharpness_range.hdr_rgb_max = 2750;
	dc->config.dcn_override_sharpness_range.hdr_rgb_mid = 1500;

	dc->config.enable_cursor_offload = true;
	dc->config.dc_mode_clk_limit_support = true;
	dc->config.enable_windowed_mpo_odm = true;
	dc->config.set_pipe_unlock_order = true; /* Need to ensure DET gets freed before allocating */
	dc->config.dp_connector_no_native_i2c = true;
	/* read VBIOS LTTPR caps */
	{
		if (ctx->dc_bios->funcs->get_lttpr_caps) {
			enum bp_result bp_query_result;
			uint8_t is_vbios_lttpr_enable = 0;

			bp_query_result = ctx->dc_bios->funcs->get_lttpr_caps(ctx->dc_bios, &is_vbios_lttpr_enable);
			dc->caps.vbios_lttpr_enable = (bp_query_result == BP_RESULT_OK) && !!is_vbios_lttpr_enable;
		}

		/* interop bit is implicit */
		{
			dc->caps.vbios_lttpr_aware = true;
		}
	}
	dc->check_config = config_defaults;

	if (dc->ctx->dce_environment == DCE_ENV_PRODUCTION_DRV)
		dc->debug = debug_defaults_drv;

	// Init the vm_helper
	if (dc->vm_helper)
		vm_helper_init(dc->vm_helper, 16);

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	/* Clock Sources for Pixel Clock*/
	pool->base.clock_sources[DCN60_CLK_SRC_PLL0] =
			dcn60_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL0,
				&clk_src_regs[0], false);
	pool->base.clock_sources[DCN60_CLK_SRC_PLL1] =
			dcn60_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL1,
				&clk_src_regs[1], false);
	pool->base.clock_sources[DCN60_CLK_SRC_PLL2] =
			dcn60_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL2,
				&clk_src_regs[2], false);
	pool->base.clock_sources[DCN60_CLK_SRC_PLL3] =
			dcn60_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL3,
				&clk_src_regs[3], false);
	// pool->base.clock_sources[DCN401_CLK_SRC_PLL4] =
	//		dcn60_clock_source_create(ctx, ctx->dc_bios,
	//			CLOCK_SOURCE_COMBO_PHY_PLL4,
	//			&clk_src_regs[4], false);

	pool->base.clk_src_count = DCN60_CLK_SRC_TOTAL;

	/* todo: not reuse phy_pll registers */
	pool->base.dp_clock_source =
			dcn60_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_ID_DP_DTO,
				&clk_src_regs[0], true);

	for (i = 0; i < (int)pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}

	/* DCCG */
	pool->base.dccg = dccg60_create(ctx, &dccg_regs, &dccg_shift, &dccg_mask);
	if (pool->base.dccg == NULL) {
		dm_error("DC: failed to create dccg!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* IRQ Service */
	init_data.ctx = dc->ctx;
	pool->base.irqs = dal_irq_service_dcn60_create(&init_data);
	if (!pool->base.irqs)
		goto create_fail;

	/* HUBBUB */
	pool->base.hubbub = dcn60_hubbub_create(ctx);
	if (pool->base.hubbub == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create hubbub!\n");
		goto create_fail;
	}

	/* HUBPs, DPPs, OPPs, TGs, ABMs */
	for (i = 0, j = 0; i < (int)pool->base.res_cap->num_timing_generator; i++) {

		/* if pipe is disabled, skip instance of HW pipe,
		 * i.e, skip ASIC register instance
		 */
		if (pipe_fuses & 1 << i)
			continue;
		pool->base.hubps[j] = dcn60_hubp_create(ctx, i);
		if (pool->base.hubps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create hubps!\n");
			goto create_fail;
		}

		pool->base.dpps[j] = dcn60_dpp_create(ctx, i);
		if (pool->base.dpps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create dpps!\n");
			goto create_fail;
		}

		pool->base.opps[j] = dcn60_opp_create(ctx, i);
		if (pool->base.opps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto create_fail;
		}

		pool->base.timing_generators[j] = dcn60_timing_generator_create(
				ctx, i);
		if (pool->base.timing_generators[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto create_fail;
		}

		pool->base.multiple_abms[j] = dmub_abm_create(ctx,
				&abm_regs[i],
				&abm_shift,
				&abm_mask);
		if (pool->base.multiple_abms[j] == NULL) {
			dm_error("DC: failed to create abm for pipe %d!\n", i);
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}

		/* index for resource pool arrays for next valid pipe */
		j++;
	}

	/* PSR */
	pool->base.psr = dmub_psr_create(ctx);
	if (pool->base.psr == NULL) {
		dm_error("DC: failed to create psr obj!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* MPCCs */
	pool->base.mpc = dcn60_mpc_create(ctx,  pool->base.res_cap->num_timing_generator,
		pool->base.res_cap->num_mpc_3dlut);
	if (pool->base.mpc == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mpc!\n");
		goto create_fail;
	}

	/* DSCs */
	for (i = 0; i < pool->base.res_cap->num_dsc; i++) {
		pool->base.dscs[i] = dcn60_dsc_create(ctx, i);
		if (pool->base.dscs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create display stream compressor %d!\n", i);
			goto create_fail;
		}
	}

	/* AUX */
	for (i = 0; i < pool->base.res_cap->num_aux; i++) {
		pool->base.engines[i] = dcn60_aux_engine_create(ctx, i);
		if (pool->base.engines[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create aux engine!!\n");
			goto create_fail;
		}
	}

	/* I2C */
	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		pool->base.hw_i2cs[i] = dcn60_i2c_hw_create(ctx, i);
		if (pool->base.hw_i2cs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create hw i2c!!\n");
			goto create_fail;
		}
		pool->base.sw_i2cs[i] = NULL;
	}

	/* Audio, HWSeq, Stream Encoders including HPO and virtual, MPC 3D LUTs */
	if (!resource_construct(num_virtual_links, dc, &pool->base,
			&res_create_funcs))
		goto create_fail;

	/* HW Sequencer init functions and Plane caps */
	dcn60_hw_sequencer_init_functions(dc);

	dc->caps.max_planes =  pool->base.pipe_count;

	for (i = 0; i < (int)dc->caps.max_planes; ++i)
		dc->caps.planes[i] = plane_cap;

	dc->caps.max_odm_combine_factor = 4;

	dc->cap_funcs = cap_funcs;

	if (dc->ctx->dc_bios->fw_info.oem_i2c_present) {
		ddc_init_data.ctx = dc->ctx;
		ddc_init_data.link = NULL;
		ddc_init_data.id.id = dc->ctx->dc_bios->fw_info.oem_i2c_obj_id;
		ddc_init_data.id.enum_id = 0;
		ddc_init_data.id.type = OBJECT_TYPE_GENERIC;
		pool->base.oem_device = dc->link_srv->create_ddc_service(&ddc_init_data);
	} else {
		pool->base.oem_device = NULL;
	}

	//For now enable SDPIF_REQUEST_RATE_LIMIT on DCN4_01 when vram_info.num_chans provided
	if (dc->config.sdpif_request_limit_words_per_umc == 0)
		dc->config.sdpif_request_limit_words_per_umc = 16;

	dc->dml2_options.dcn_pipe_count = pool->base.pipe_count;
	dc->dml2_options.use_native_soc_bb_construction = true;
	dc->dml2_options.minimize_dispclk_using_odm = true;
	dc->dml2_options.map_dc_pipes_with_callbacks = true;
	dc->dml2_options.force_tdlut_enable = true;

	resource_init_common_dml2_callbacks(dc, &dc->dml2_options);
	dc->dml2_options.callbacks.can_support_mclk_switch_using_fw_based_vblank_stretch =
		&dcn30_can_support_mclk_switch_using_fw_based_vblank_stretch;

	dc->dml2_options.max_segments_per_hubp = 20;
	dc->dml2_options.det_segment_size = DCN6_0_CRB_SEGMENT_SIZE_KB;

	/* SPL */
	dc->caps.scl_caps.sharpener_support = true;

	/* init DC limited DML2 options */
	memcpy(&dc->dml2_dc_power_options, &dc->dml2_options, sizeof(struct dml2_configuration_options));
	dc->dml2_dc_power_options.use_clock_dc_limits = true;

	return true;

create_fail:

	dcn60_resource_destruct(pool);

	return false;
}

struct resource_pool *dcn60_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc)
{
	struct dcn60_resource_pool *pool =
		kzalloc(sizeof(struct dcn60_resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (dcn60_resource_construct((uint8_t)init_data->num_virtual_links, dc, pool))
		return &pool->base;

	BREAK_TO_DEBUGGER();
	kfree(pool);
	return NULL;
}
