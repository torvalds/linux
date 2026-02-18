// SPDX-License-Identifier: MIT
//
// Copyright 2026 Advanced Micro Devices, Inc.

#include "dm_services.h"
#include "dc.h"

#include "dcn32/dcn32_init.h"
#include "dcn42/dcn42_init.h"

#include "resource.h"
#include "include/irq_service_interface.h"

#include "dcn42_resource.h"
#include "dcn42_resource_fpu.h"
#include "dcn20/dcn20_resource.h"
#include "dcn30/dcn30_resource.h"
#include "dcn31/dcn31_resource.h"
#include "dcn32/dcn32_resource.h"
#include "dcn35/dcn35_resource.h"
#include "dcn321/dcn321_resource.h"
#include "dcn401/dcn401_resource.h"

#include "dcn10/dcn10_ipp.h"
#include "dcn35/dcn35_hubbub.h"
#include "dcn42/dcn42_hubbub.h"
#include "dcn401/dcn401_mpc.h"
#include "dcn42/dcn42_mpc.h"
#include "dcn35/dcn35_hubp.h"
#include "dcn42/dcn42_hubp.h"
#include "irq/dcn42/irq_service_dcn42.h"
#include "dcn42/dcn42_dpp.h"
#include "dcn401/dcn401_dsc.h"
#include "dcn42/dcn42_optc.h"
#include "dcn20/dcn20_hwseq.h"
#include "dcn30/dcn30_hwseq.h"
#include "dce110/dce110_hwseq.h"
#include "dcn35/dcn35_opp.h"
#include "dcn30/dcn30_vpg.h"
#include "dcn31/dcn31_vpg.h"
#include "dcn42/dcn42_dio_stream_encoder.h"
#include "dcn42/dcn42_pg_cntl.h"
#include "dcn31/dcn31_hpo_dp_stream_encoder.h"
#include "dcn31/dcn31_hpo_dp_link_encoder.h"
#include "dcn32/dcn32_hpo_dp_link_encoder.h"
#include "dcn42/dcn42_hpo_dp_link_encoder.h"
#include "dcn31/dcn31_apg.h"
#include "dcn31/dcn31_dio_link_encoder.h"
#include "dcn401/dcn401_dio_link_encoder.h"
#include "dcn10/dcn10_link_encoder.h"
#include "dcn321/dcn321_dio_link_encoder.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_audio.h"
#include "dce/dce_hwseq.h"
#include "clk_mgr.h"
#include "virtual/virtual_stream_encoder.h"
#include "dml/display_mode_vba.h"
#include "dcn42/dcn42_dccg.h"
#include "dcn10/dcn10_resource.h"
#include "link_service.h"
#include "dcn31/dcn31_panel_cntl.h"

#include "dcn30/dcn30_dwb.h"
#include "dcn42/dcn42_mmhubbub.h"
#include "dcn42/dcn42_dio_link_encoder.h"

#include "dcn/dcn_4_2_0_offset.h"
#include "dcn/dcn_4_2_0_sh_mask.h"
#include "dpcs/dpcs_4_0_0_offset.h"
#include "dpcs/dpcs_4_0_0_sh_mask.h"

#include "reg_helper.h"
#include "dce/dmub_abm.h"
#include "dce/dmub_psr.h"
#include "dce/dmub_replay.h"
#include "dce/dce_aux.h"
#include "dce/dce_i2c.h"

#include "dml/dcn30/display_mode_vba_30.h"
#include "vm_helper.h"
#include "dcn20/dcn20_vmid.h"

#include "dc_state_priv.h"
#include "link_enc_cfg.h"

#include "dml2_0/dml2_wrapper.h"

#define regBIF_BX0_BIOS_SCRATCH_3                            0x003b
#define regBIF_BX0_BIOS_SCRATCH_3_BASE_IDX                   1
#define regBIF_BX0_BIOS_SCRATCH_6                            0x003e
#define regBIF_BX0_BIOS_SCRATCH_6_BASE_IDX                   1

#define DC_LOGGER_INIT(logger)

enum dcn401_clk_src_array_id {
	DCN401_CLK_SRC_PLL0,
	DCN401_CLK_SRC_PLL1,
	DCN401_CLK_SRC_PLL2,
	DCN401_CLK_SRC_PLL3,
	DCN401_CLK_SRC_PLL4,
	DCN401_CLK_SRC_TOTAL
};

/* begin
 * macros to expend register list macro defined in HW object header file
 */

/* DCN */
#define BASE_INNER(seg) ctx->dcn_reg_offsets[seg]

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)                                       \
	REG_STRUCT.reg_name = BASE(reg##reg_name##_BASE_IDX) + \
						  reg##reg_name
#define SR_ARR(reg_name, id)                                   \
	REG_STRUCT[id].reg_name = BASE(reg##reg_name##_BASE_IDX) + \
							  reg##reg_name
#define SR_ARR_INIT(reg_name, id, value) \
	REG_STRUCT[id].reg_name = value

#define SRI(reg_name, block, id)                                         \
	REG_STRUCT.reg_name = BASE(reg##block##id##_##reg_name##_BASE_IDX) + \
						  reg##block##id##_##reg_name

#define SRI_ARR(reg_name, block, id)                                         \
	REG_STRUCT[id].reg_name = BASE(reg##block##id##_##reg_name##_BASE_IDX) + \
							  reg##block##id##_##reg_name

/*
 * Used when a reg_name would otherwise begin with an integer
 */
#define SRI_ARR_US(reg_name, block, id)                                   \
	REG_STRUCT[id].reg_name = BASE(reg##block##id##reg_name##_BASE_IDX) + \
							  reg##block##id##reg_name
#define SR_ARR_I2C(reg_name, id) \
	REG_STRUCT[id - 1].reg_name = BASE(reg##reg_name##_BASE_IDX) + reg##reg_name

#define SRI_ARR_I2C(reg_name, block, id)                                         \
	REG_STRUCT[id - 1].reg_name = BASE(reg##block##id##_##reg_name##_BASE_IDX) + \
								  reg##block##id##_##reg_name


#define SRI_ARR_ALPHABET(reg_name, block, index, id)                            \
	REG_STRUCT[index].reg_name = BASE(reg##block##id##_##reg_name##_BASE_IDX) + \
								 reg##block##id##_##reg_name

#define SRI2(reg_name, block, id)                \
	.reg_name = BASE(reg##reg_name##_BASE_IDX) + \
				reg##reg_name
#define SRI2_ARR(reg_name, block, id)                          \
	REG_STRUCT[id].reg_name = BASE(reg##reg_name##_BASE_IDX) + \
							  reg##reg_name

#define SRIR(var_name, reg_name, block, id)                    \
	.var_name = BASE(reg##block##id##_##reg_name##_BASE_IDX) + \
				reg##block##id##_##reg_name

#define SRII(reg_name, block, id)                                            \
	REG_STRUCT.reg_name[id] = BASE(reg##block##id##_##reg_name##_BASE_IDX) + \
							  reg##block##id##_##reg_name

#define SRII_ARR_2(reg_name, block, id, inst)                                      \
	REG_STRUCT[inst].reg_name[id] = BASE(reg##block##id##_##reg_name##_BASE_IDX) + \
									reg##block##id##_##reg_name

#define SRII_MPC_RMU(reg_name, block, id)                                  \
	.RMU##_##reg_name[id] = BASE(reg##block##id##_##reg_name##_BASE_IDX) + \
							reg##block##id##_##reg_name

#define SRII_DWB(reg_name, temp_name, block, id)                              \
	REG_STRUCT.reg_name[id] = \
		BASE(reg##block##id##_##temp_name##_BASE_IDX) + \
							  reg##block##id##_##temp_name

#define DCCG_SRII(reg_name, block, id)                                                 \
	REG_STRUCT.block##_##reg_name[id] = \
		BASE(reg##block##id##_##reg_name##_BASE_IDX) + \
										reg##block##id##_##reg_name

#define SF_DWB2(reg_name, block, id, field_name, post_fix) \
	.field_name = reg_name##__##field_name##post_fix

#define VUPDATE_SRII(reg_name, block, id)                                    \
	REG_STRUCT.reg_name[id] = BASE(reg##reg_name##_##block##id##_BASE_IDX) + \
							  reg##reg_name##_##block##id

/* NBIO */
#define NBIO_BASE_INNER(seg) ctx->nbio_reg_offsets[seg]

#define NBIO_BASE(seg) \
	NBIO_BASE_INNER(seg)

#define NBIO_SR(reg_name)                                               \
	REG_STRUCT.reg_name = NBIO_BASE(regBIF_BX0_##reg_name##_BASE_IDX) + \
						  regBIF_BX0_##reg_name
#define NBIO_SR_ARR(reg_name, id)                                           \
	REG_STRUCT[id].reg_name = NBIO_BASE(regBIF_BX0_##reg_name##_BASE_IDX) + \
							  regBIF_BX0_##reg_name

#define CTX ctx
#define REG(reg_name) \
	(ctx->dcn_reg_offsets[reg##reg_name##_BASE_IDX] + reg##reg_name)

static struct bios_registers bios_regs;

#define bios_regs_init()     \
	NBIO_SR(BIOS_SCRATCH_3), \
		NBIO_SR(BIOS_SCRATCH_6)

#define clk_src_regs_init(index, pllid) \
	CS_COMMON_REG_LIST_DCN42_RI(index, pllid)

static struct dce110_clk_src_regs clk_src_regs[5];

static const struct dce110_clk_src_shift cs_shift = {
	CS_COMMON_MASK_SH_LIST_DCN3_1_4(__SHIFT)
};
static const struct dce110_clk_src_mask cs_mask = {
	CS_COMMON_MASK_SH_LIST_DCN3_1_4(_MASK)
};
#define abm_regs_init(id) \
	ABM_DCN42_REG_LIST_RI(id)

static struct dce_abm_registers abm_regs[4];

static const struct dce_abm_shift abm_shift = {
	ABM_MASK_SH_LIST_DCN42(__SHIFT)};

static const struct dce_abm_mask abm_mask = {
	ABM_MASK_SH_LIST_DCN42(_MASK)};

#define audio_regs_init(id) \
	AUD_COMMON_REG_LIST_RI(id)

static struct dce_audio_registers audio_regs[5];

static const struct dce_audio_shift audio_shift = {
		DCN42_AUD_COMMON_MASK_SH_LIST(__SHIFT)
};

static const struct dce_audio_mask audio_mask = {
		DCN42_AUD_COMMON_MASK_SH_LIST(_MASK)
};

#define vpg_regs_init(id) \
	VPG_DCN401_REG_LIST_RI(id)

static struct dcn31_vpg_registers vpg_regs[10];

static const struct dcn31_vpg_shift vpg_shift = {
	DCN31_VPG_MASK_SH_LIST(__SHIFT)};

static const struct dcn31_vpg_mask vpg_mask = {
	DCN31_VPG_MASK_SH_LIST(_MASK)};

#define apg_regs_init(id) \
	APG_DCN31_REG_LIST_RI(id)

static struct dcn31_apg_registers apg_regs[10];

static const struct dcn31_apg_shift apg_shift = {
	DCN31_APG_MASK_SH_LIST(__SHIFT)};

static const struct dcn31_apg_mask apg_mask = {
	DCN31_APG_MASK_SH_LIST(_MASK)};

#define stream_enc_regs_init(id) \
	SE_DCN42_REG_LIST_RI(id)

static struct dcn10_stream_enc_registers stream_enc_regs[5];

static const struct dcn10_stream_encoder_shift se_shift = {
	SE_COMMON_MASK_SH_LIST_DCN42(__SHIFT)};

static const struct dcn10_stream_encoder_mask se_mask = {
	SE_COMMON_MASK_SH_LIST_DCN42(_MASK)};

#define aux_regs_init(id) \
	DCN2_AUX_REG_LIST_RI(id)

static struct dcn10_link_enc_aux_registers link_enc_aux_regs[5];

#define hpd_regs_init(id) \
	HPD_REG_LIST_RI(id)

static struct dcn10_link_enc_hpd_registers link_enc_hpd_regs[5];

#define link_regs_init(id, phyid) \
	LE_DCN401_REG_LIST_RI(id)

static struct dcn10_link_enc_registers link_enc_regs[5];

static const struct dcn10_link_enc_shift le_shift = {
	LINK_ENCODER_MASK_SH_LIST_DCN42(__SHIFT)};

static const struct dcn10_link_enc_mask le_mask = {
	LINK_ENCODER_MASK_SH_LIST_DCN42(_MASK)};

#define hpo_dp_stream_encoder_reg_init(id) \
	DCN42_HPO_DP_STREAM_ENC_REG_LIST_RI(id)

static struct dcn31_hpo_dp_stream_encoder_registers hpo_dp_stream_enc_regs[4];

static const struct dcn31_hpo_dp_stream_encoder_shift hpo_dp_se_shift = {
	DCN4_2_HPO_DP_STREAM_ENC_MASK_SH_LIST(__SHIFT)};

static const struct dcn31_hpo_dp_stream_encoder_mask hpo_dp_se_mask = {
	DCN4_2_HPO_DP_STREAM_ENC_MASK_SH_LIST(_MASK)};

#define hpo_dp_link_encoder_reg_init(id) \
	DCN42_HPO_DP_LINK_ENC_REG_LIST_RI(id)

static struct dcn31_hpo_dp_link_encoder_registers hpo_dp_link_enc_regs[4];

static const struct dcn31_hpo_dp_link_encoder_shift hpo_dp_le_shift = {
	DCN3_2_HPO_DP_LINK_ENC_MASK_SH_LIST(__SHIFT)};

static const struct dcn31_hpo_dp_link_encoder_mask hpo_dp_le_mask = {
	DCN3_2_HPO_DP_LINK_ENC_MASK_SH_LIST(_MASK)};

#define dpp_regs_init(id) \
	DPP_REG_LIST_DCN42_COMMON_RI(id)

static struct dcn42_dpp_registers dpp_regs[4];

static const struct dcn42_dpp_shift tf_shift = {
	DPP_REG_LIST_SH_MASK_DCN42_COMMON(__SHIFT)};

static const struct dcn42_dpp_mask tf_mask = {
	DPP_REG_LIST_SH_MASK_DCN42_COMMON(_MASK)};

#define opp_regs_init(id) \
	OPP_REG_LIST_DCN401_RI(id)

static struct dcn20_opp_registers opp_regs[4];

static const struct dcn20_opp_shift opp_shift = {
	OPP_MASK_SH_LIST_DCN20(__SHIFT)};

static const struct dcn20_opp_mask opp_mask = {
	OPP_MASK_SH_LIST_DCN20(_MASK)};

#define aux_engine_regs_init(id)                                              \
	AUX_COMMON_REG_LIST0_RI(id), SR_ARR_INIT(AUXN_IMPCAL, id, 0),             \
		SR_ARR_INIT(AUXP_IMPCAL, id, 0),                                      \
		SR_ARR_INIT(AUX_RESET_MASK, id, DP_AUX0_AUX_CONTROL__AUX_RESET_MASK), \
		SR_ARR_INIT(AUX_RESET_MASK, id, DP_AUX0_AUX_CONTROL__AUX_RESET_MASK)

static struct dce110_aux_registers aux_engine_regs[5];

static const struct dce110_aux_registers_shift aux_shift = {
	DCN_AUX_MASK_SH_LIST(__SHIFT)};

static const struct dce110_aux_registers_mask aux_mask = {
	DCN_AUX_MASK_SH_LIST(_MASK)};

#define dwbc_regs_dcn401_init(id) \
	DWBC_COMMON_REG_LIST_DCN30_RI(id)

static struct dcn30_dwbc_registers dwbc401_regs[1];

static const struct dcn30_dwbc_shift dwbc401_shift = {
	DWBC_COMMON_MASK_SH_LIST_DCN30(__SHIFT)};

static const struct dcn30_dwbc_mask dwbc401_mask = {
	DWBC_COMMON_MASK_SH_LIST_DCN30(_MASK)};

#define mcif_wb_regs_dcn3_init(id) \
	MCIF_WB_COMMON_REG_LIST_DCN3_5_RI(id)

static struct dcn35_mmhubbub_registers mcif_wb35_regs[1];

static const struct dcn35_mmhubbub_shift mcif_wb35_shift = {
	MCIF_WB_COMMON_MASK_SH_LIST_DCN3_5(__SHIFT)};

static const struct dcn35_mmhubbub_mask mcif_wb35_mask = {
	MCIF_WB_COMMON_MASK_SH_LIST_DCN3_5(_MASK)};

#define dsc_regs_init(id) \
	DSC_REG_LIST_DCN401_RI(id)

static struct dcn401_dsc_registers dsc_regs[4];

static const struct dcn401_dsc_shift dsc_shift = {
	DSC_REG_LIST_SH_MASK_DCN401(__SHIFT)};

static const struct dcn401_dsc_mask dsc_mask = {
	DSC_REG_LIST_SH_MASK_DCN401(_MASK)};

static struct dcn42_mpc_registers mpc_regs;

#define dcn_mpc_regs_init()                \
	MPC_REG_LIST_DCN42(0),                 \
		MPC_REG_LIST_DCN42(1),             \
		MPC_REG_LIST_DCN42(2),             \
		MPC_REG_LIST_DCN42(3),             \
		MPC_OUT_MUX_REG_LIST_DCN3_0_RI(0), \
		MPC_OUT_MUX_REG_LIST_DCN3_0_RI(1), \
		MPC_OUT_MUX_REG_LIST_DCN3_0_RI(2), \
		MPC_OUT_MUX_REG_LIST_DCN3_0_RI(3), \
		MPC_DWB_MUX_REG_LIST_DCN3_0_RI(0), \
		MPC_RMCM_REG_LIST_DCN42(0),		   \
		MPC_RMCM_REG_LIST_DCN42(1)

static const struct dcn42_mpc_shift mpc_shift = {
	MPC_COMMON_MASK_SH_LIST_DCN42(__SHIFT)};

static const struct dcn42_mpc_mask mpc_mask = {
	MPC_COMMON_MASK_SH_LIST_DCN42(_MASK)};

#define optc_regs_init(id) \
	OPTC_COMMON_REG_LIST_DCN42_RI(id)

static struct dcn_optc_registers optc_regs[4];

static const struct dcn_optc_shift optc_shift = {
	OPTC_COMMON_MASK_SH_LIST_DCN42(__SHIFT)};

static const struct dcn_optc_mask optc_mask = {
	OPTC_COMMON_MASK_SH_LIST_DCN42(_MASK)};

#define hubp_regs_init(id) \
	HUBP_REG_LIST_DCN42_RI(id)

static struct dcn_hubp2_registers hubp_regs[4];

static const struct dcn_hubp2_shift hubp_shift = {
	HUBP_MASK_SH_LIST_DCN42(__SHIFT)};

static const struct dcn_hubp2_mask hubp_mask = {
	HUBP_MASK_SH_LIST_DCN42(_MASK)};

static struct dcn_hubbub_registers hubbub_reg;

#define hubbub_reg_init() \
	HUBBUB_REG_LIST_DCN42(0)

static const struct dcn_hubbub_shift hubbub_shift = {
	HUBBUB_MASK_SH_LIST_DCN4_2(__SHIFT)};

static const struct dcn_hubbub_mask hubbub_mask = {
	HUBBUB_MASK_SH_LIST_DCN4_2(_MASK)};

static struct dccg_registers dccg_regs;

#define dccg_regs_init() \
	DCCG_REG_LIST_DCN42_RI()

static const struct dccg_shift dccg_shift = {
	DCCG_MASK_SH_LIST_DCN42(__SHIFT)};

static const struct dccg_mask dccg_mask = {
	DCCG_MASK_SH_LIST_DCN42(_MASK)};

static struct pg_cntl_registers pg_cntl_regs;

#define pg_cntl_dcn42_regs_init() \
	PG_CNTL_REG_LIST_DCN42()

static const struct pg_cntl_shift pg_cntl_shift = {
		PG_CNTL_MASK_SH_LIST_DCN42(__SHIFT)
};

static const struct pg_cntl_mask pg_cntl_mask = {
		PG_CNTL_MASK_SH_LIST_DCN42(_MASK)
};
#define SRII2(reg_name_pre, reg_name_post, id)                                                       \
	.reg_name_pre##_##reg_name_post[id] = \
		BASE(reg##reg_name_pre##id##_##reg_name_post##_BASE_IDX) + \
					reg##reg_name_pre##id##_##reg_name_post

#define HWSEQ_DCN42_REG_LIST()                \
	SR(DCHUBBUB_GLOBAL_TIMER_CNTL),           \
		SR(DIO_MEM_PWR_CTRL),                 \
		SR(ODM_MEM_PWR_CTRL3),                \
		SR(MMHUBBUB_MEM_PWR_CNTL),            \
		SR(DCCG_GATE_DISABLE_CNTL),           \
		SR(DCCG_GATE_DISABLE_CNTL2),          \
		SR(DCFCLK_CNTL),                      \
		SR(DC_MEM_GLOBAL_PWR_REQ_CNTL),       \
		SRII(PIXEL_RATE_CNTL, OTG, 0),        \
		SRII(PIXEL_RATE_CNTL, OTG, 1),        \
		SRII(PIXEL_RATE_CNTL, OTG, 2),        \
		SRII(PIXEL_RATE_CNTL, OTG, 3),\
		SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 0), \
		SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 1), \
		SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 2), \
		SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 3),\
		SR(MICROSECOND_TIME_BASE_DIV),        \
		SR(MILLISECOND_TIME_BASE_DIV),        \
		SR(DISPCLK_FREQ_CHANGE_CNTL),         \
		SR(RBBMIF_TIMEOUT_DIS),               \
		SR(RBBMIF_TIMEOUT_DIS_2),             \
		SR(DCHUBBUB_CRC_CTRL),                \
		SR(DPP_TOP0_DPP_CRC_CTRL),            \
		SR(DPP_TOP0_DPP_CRC_VAL_R),           \
		SR(DPP_TOP0_DPP_CRC_VAL_G),           \
		SR(DPP_TOP0_DPP_CRC_VAL_B),           \
		SR(MPC_CRC_CTRL),                     \
		SR(MPC_CRC_RESULT_R),                 \
		SR(MPC_CRC_RESULT_G),                 \
		SR(MPC_CRC_RESULT_B),                 \
		SR(MPC_CRC_RESULT_A),                 \
		SR(DOMAIN0_PG_CONFIG),                \
		SR(DOMAIN1_PG_CONFIG),                \
		SR(DOMAIN2_PG_CONFIG),                \
		SR(DOMAIN3_PG_CONFIG),                \
		SR(DOMAIN16_PG_CONFIG),               \
		SR(DOMAIN17_PG_CONFIG),               \
		SR(DOMAIN18_PG_CONFIG),               \
		SR(DOMAIN19_PG_CONFIG), \
		SR(DOMAIN22_PG_CONFIG),               \
		SR(DOMAIN23_PG_CONFIG),               \
		SR(DOMAIN24_PG_CONFIG),               \
		SR(DOMAIN25_PG_CONFIG),               \
		SR(DOMAIN26_PG_CONFIG),               \
		SR(DOMAIN0_PG_STATUS),                \
		SR(DOMAIN1_PG_STATUS),                \
		SR(DOMAIN2_PG_STATUS),                \
		SR(DOMAIN3_PG_STATUS),                \
		SR(DOMAIN16_PG_STATUS),               \
		SR(DOMAIN17_PG_STATUS),               \
		SR(DOMAIN18_PG_STATUS),               \
		SR(DOMAIN19_PG_STATUS),               \
		SR(DOMAIN22_PG_STATUS),               \
		SR(DOMAIN23_PG_STATUS),               \
		SR(DOMAIN24_PG_STATUS),               \
		SR(DOMAIN25_PG_STATUS),               \
		SR(DOMAIN26_PG_STATUS),               \
		SR(DC_IP_REQUEST_CNTL),               \
		SR(AZALIA_AUDIO_DTO),                 \
		SR(HPO_TOP_HW_CONTROL),               \
		SR(AZALIA_CONTROLLER_CLOCK_GATING)

static struct dce_hwseq_registers hwseq_reg;

#define hwseq_reg_init() \
	HWSEQ_DCN42_REG_LIST()

#define HWSEQ_DCN42_MASK_SH_LIST(mask_sh)                                            \
	HWSEQ_DCN_MASK_SH_LIST(mask_sh),                                                 \
		HWS_SF(, DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_REFDIV, mask_sh), \
		HWS_SF(, DCHUBBUB_ARB_HOSTVM_CNTL, DISABLE_HOSTVM_FORCE_ALLOW_PSTATE, mask_sh), \
		HWS_SF(, DOMAIN0_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh),                  \
		HWS_SF(, DOMAIN0_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh),                     \
		HWS_SF(, DOMAIN1_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh),                  \
		HWS_SF(, DOMAIN1_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh),                     \
		HWS_SF(, DOMAIN2_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh),                  \
		HWS_SF(, DOMAIN2_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh),                     \
		HWS_SF(, DOMAIN3_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh),                  \
		HWS_SF(, DOMAIN3_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh),                     \
		HWS_SF(, DOMAIN16_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh),                 \
		HWS_SF(, DOMAIN16_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh),                    \
		HWS_SF(, DOMAIN17_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh),                 \
		HWS_SF(, DOMAIN17_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh),                    \
		HWS_SF(, DOMAIN18_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh),                 \
		HWS_SF(, DOMAIN18_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh),                    \
		HWS_SF(, DOMAIN19_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
		HWS_SF(, DOMAIN19_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
		HWS_SF(, DOMAIN22_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh),                 \
		HWS_SF(, DOMAIN22_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh),                    \
		HWS_SF(, DOMAIN23_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh),                 \
		HWS_SF(, DOMAIN23_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh),                    \
		HWS_SF(, DOMAIN24_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh),                 \
		HWS_SF(, DOMAIN24_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh),                    \
		HWS_SF(, DOMAIN25_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh),                 \
		HWS_SF(, DOMAIN25_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh),                    \
		HWS_SF(, DOMAIN26_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh),                 \
		HWS_SF(, DOMAIN26_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh),                    \
		HWS_SF(, DOMAIN0_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh),               \
		HWS_SF(, DOMAIN1_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh),               \
		HWS_SF(, DOMAIN2_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh),               \
		HWS_SF(, DOMAIN3_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh),               \
		HWS_SF(, DOMAIN16_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh),              \
		HWS_SF(, DOMAIN17_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh),              \
		HWS_SF(, DOMAIN18_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh),              \
		HWS_SF(, DOMAIN19_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh),              \
		HWS_SF(, DOMAIN22_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh),              \
		HWS_SF(, DOMAIN23_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh),              \
		HWS_SF(, DOMAIN24_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh),              \
		HWS_SF(, DOMAIN25_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh),              \
		HWS_SF(, DOMAIN26_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh),              \
		HWS_SF(, DC_IP_REQUEST_CNTL, IP_REQUEST_EN, mask_sh),                        \
		HWS_SF(, AZALIA_AUDIO_DTO, AZALIA_AUDIO_DTO_MODULE, mask_sh),                \
		HWS_SF(, HPO_TOP_CLOCK_CONTROL, HPO_HDMISTREAMCLK_G_GATE_DIS, mask_sh),      \
		HWS_SF(, HPO_TOP_HW_CONTROL, HPO_IO_EN, mask_sh),                            \
		HWS_SF(, ODM_MEM_PWR_CTRL3, ODM_MEM_UNASSIGNED_PWR_MODE, mask_sh),           \
		HWS_SF(, ODM_MEM_PWR_CTRL3, ODM_MEM_VBLANK_PWR_MODE, mask_sh), \
		HWS_SF(, DIO_MEM_PWR_CTRL, I2C_LIGHT_SLEEP_FORCE, mask_sh), \
		HWS_SF(, DMU_CLK_CNTL, DISPCLK_R_DMU_GATE_DIS, mask_sh),\
		HWS_SF(, DMU_CLK_CNTL, DISPCLK_G_RBBMIF_GATE_DIS, mask_sh),\
		HWS_SF(, DMU_CLK_CNTL, RBBMIF_FGCG_REP_DIS, mask_sh),\
		HWS_SF(, DMU_CLK_CNTL, DPREFCLK_ALLOW_DS_CLKSTOP, mask_sh),\
		HWS_SF(, DMU_CLK_CNTL, DISPCLK_ALLOW_DS_CLKSTOP, mask_sh),\
		HWS_SF(, DMU_CLK_CNTL, DPPCLK_ALLOW_DS_CLKSTOP, mask_sh),\
		HWS_SF(, DMU_CLK_CNTL, DTBCLK_ALLOW_DS_CLKSTOP, mask_sh),\
		HWS_SF(, DMU_CLK_CNTL, DCFCLK_ALLOW_DS_CLKSTOP, mask_sh),\
		HWS_SF(, DMU_CLK_CNTL, DPIACLK_ALLOW_DS_CLKSTOP, mask_sh),\
		HWS_SF(, DMU_CLK_CNTL, LONO_FGCG_REP_DIS, mask_sh),\
		HWS_SF(, DMU_CLK_CNTL, LONO_DISPCLK_GATE_DISABLE, mask_sh),\
		HWS_SF(, DMU_CLK_CNTL, LONO_SOCCLK_GATE_DISABLE, mask_sh),\
		HWS_SF(, DMU_CLK_CNTL, LONO_DMCUBCLK_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, SYMCLKA_FE_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, SYMCLKB_FE_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, SYMCLKC_FE_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, SYMCLKD_FE_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, SYMCLKE_FE_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, HDMICHARCLK0_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, SYMCLKA_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, SYMCLKB_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, SYMCLKC_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, SYMCLKD_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, SYMCLKE_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, PHYASYMCLK_ROOT_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, PHYBSYMCLK_ROOT_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, PHYCSYMCLK_ROOT_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, PHYDSYMCLK_ROOT_GATE_DISABLE, mask_sh), \
		HWS_SF(, DCCG_GATE_DISABLE_CNTL2, PHYESYMCLK_ROOT_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL5, DTBCLK_P0_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL5, DTBCLK_P1_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL5, DTBCLK_P2_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL5, DTBCLK_P3_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK0_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK1_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK2_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL5, DPSTREAMCLK3_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL4, DPIASYMCLK0_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL4, DPIASYMCLK1_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL4, DPIASYMCLK2_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL4, DPIASYMCLK3_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL4, DPIASYMCLK4_GATE_DISABLE, mask_sh),\
		HWS_SF(, DCCG_GATE_DISABLE_CNTL4, DPIASYMCLK5_GATE_DISABLE, mask_sh)

static const struct dce_hwseq_shift hwseq_shift = {
	HWSEQ_DCN42_MASK_SH_LIST(__SHIFT)};

static const struct dce_hwseq_mask hwseq_mask = {
	HWSEQ_DCN42_MASK_SH_LIST(_MASK)};

#define vmid_regs_init(id) \
	DCN20_VMID_REG_LIST_RI(id)

static struct dcn_vmid_registers vmid_regs[16];

static const struct dcn20_vmid_shift vmid_shifts = {
	DCN20_VMID_MASK_SH_LIST(__SHIFT)};

static const struct dcn20_vmid_mask vmid_masks = {
	DCN20_VMID_MASK_SH_LIST(_MASK)};

static const struct resource_caps res_cap_dcn42 = {
	.num_timing_generator = 4,
	.num_opp = 4,
	.num_dpp = 4,
	.num_video_plane = 4,
	.num_audio = 5,
	.num_stream_encoder = 5,
	.num_dig_link_enc = 5,
	.num_usb4_dpia = 6,
	.num_hpo_dp_stream_encoder = 4,
	.num_hpo_dp_link_encoder = 4,
	.num_pll = 5,
	.num_dwb = 1,
	.num_ddc = 5,
	.num_vmid = 16,
	.num_mpc_3dlut = 2,
	.num_dsc = 4,
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

	.max_upscale_factor = {.argb8888 = 16000, .nv12 = 16000, .fp16 = 16000},

	// 6:1 downscaling ratio: 1000/6 = 166.666
	.max_downscale_factor = {.argb8888 = 167, .nv12 = 167, .fp16 = 167},

	.min_width = 64,
	.min_height = 64};

static const struct dc_debug_options debug_defaults_drv = {
	.disable_dmcu = true,
	.force_abm_enable = false,
	.clock_trace = true,
	.disable_pplib_clock_request = false,
	.disable_dpp_power_gate = true,
	.disable_hubp_power_gate = true,
	.disable_optc_power_gate = true,
	.pipe_split_policy = MPC_SPLIT_AVOID,
	.force_single_disp_pipe_split = false,
	.disable_dcc = DCC_ENABLE,
	.vsr_support = true,
	.performance_trace = false,
	.max_downscale_src_width = 4096, /*up to 4K for APU*/
	.disable_pplib_wm_range = false,
	.scl_reset_length10 = true,
	.sanity_checks = false,
	.underflow_assert_delay_us = 0xFFFFFFFF,
	.dwb_fi_phase = -1, // -1 = disable,
	.dmub_command_table = true,
	.pstate_enabled = true,
	.enable_mem_low_power = {
		.bits = {
			.vga = false,
			.i2c = true,
			.dscl = true,
			.cm = true,
			.mpc = true,
			.optc = true,
			.vpg = true,
		}},
	.root_clock_optimization = {
		.bits = {
			.dpp = true,
			.dsc = true,/*dscclk and dsc pg*/
			.hdmistream = false,
			.hdmichar = true,
			.dpstream = true,
			.symclk32_se = true,
			.symclk32_le = true,
			.symclk_fe = true,
			.physymclk = false,
			.dpiasymclk = true,
		}
	},
	.seamless_boot_odm_combine = DML_FAIL_SOURCE_PIXEL_FORMAT,
	.enable_z9_disable_interface = true, /* Allow support for the PMFW interface for disable Z9*/
	.minimum_z8_residency_time = 1, /* Always allow when other conditions are met */
	.support_eDP1_5 = true,
	.use_max_lb = true,
	.force_disable_subvp = false,
	.exit_idle_opt_for_cursor_updates = true,
	.using_dml2 = true,
	.using_dml21 = true,
	.enable_single_display_2to1_odm_policy = true,

	// must match enable_single_display_2to1_odm_policy to support dynamic ODM transitions
	.enable_double_buffered_dsc_pg_support = true,
	.enable_dp_dig_pixel_rate_div_policy = 1,
	.allow_sw_cursor_fallback = false,
	.psp_disabled_wa = true,
	.alloc_extra_way_for_cursor = true,
	.min_prefetch_in_strobe_ns = 60000, // 60us
	.disable_unbounded_requesting = false,
	.dcc_meta_propagation_delay_us = 10,
	.disable_timeout = true,
	.min_disp_clk_khz = 50000,
	.disable_z10 = false,
	.ignore_pg = true,
	.disable_stutter_for_wm_program = true,
};

static const struct dc_check_config config_defaults = {
	.enable_legacy_fast_update = false,
};

static struct dce_aux *dcn42_aux_engine_create(
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
		aux_engine_regs_init(3),
		aux_engine_regs_init(4);

	dce110_aux_engine_construct(aux_engine, ctx, inst,
								SW_AUX_TIMEOUT_PERIOD_MULTIPLIER * AUX_TIMEOUT_PERIOD,
								&aux_engine_regs[inst],
								&aux_mask,
								&aux_shift,
								ctx->dc->caps.extended_aux_timeout_support);

	return &aux_engine->base;
}

#define i2c_inst_regs_init(id) \
	I2C_HW_ENGINE_COMMON_REG_LIST_DCN30_RI(id)

static struct dce_i2c_registers i2c_hw_regs[5];

static const struct dce_i2c_shift i2c_shifts = {
	I2C_COMMON_MASK_SH_LIST_DCN35(__SHIFT)
};
static const struct dce_i2c_mask i2c_masks = {
	I2C_COMMON_MASK_SH_LIST_DCN35(_MASK)
};

/* ========================================================== */

/*
 * DPIA index | Preferred Encoder     |    Host Router
 *   0        |      C                |       0
 *   1        |      First Available  |       0
 *   2        |      D                |       1
 *   3        |      First Available  |       1
 *   4        |      E                |       2
 *   5        |      First Available  |       2
 */
/* ========================================================== */
static const enum engine_id dpia_to_preferred_enc_id_table[] = {
		ENGINE_ID_DIGC,
		ENGINE_ID_DIGC,
		ENGINE_ID_DIGD,
		ENGINE_ID_DIGD,
		ENGINE_ID_DIGE,
		ENGINE_ID_DIGE
};

static enum engine_id dcn42_get_preferred_eng_id_dpia(unsigned int dpia_index)
{
	return dpia_to_preferred_enc_id_table[dpia_index];
}

static struct dce_i2c_hw *dcn42_i2c_hw_create(
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
	i2c_inst_regs_init(2),
	i2c_inst_regs_init(3),
	i2c_inst_regs_init(4),
	i2c_inst_regs_init(5);
	dcn2_i2c_hw_construct(dce_i2c_hw, ctx, inst,
						  &i2c_hw_regs[inst], &i2c_shifts, &i2c_masks);

	return dce_i2c_hw;
}

static struct clock_source *dcn42_clock_source_create(
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

	if (dcn401_clk_src_construct(clk_src, ctx, bios, id,
								 regs, &cs_shift, &cs_mask)) {
		clk_src->base.dp_clk_src = dp_clk_src;
		return &clk_src->base;
	}

	kfree(clk_src);
	BREAK_TO_DEBUGGER();
	return NULL;
}

static struct hubbub *dcn42_hubbub_create(struct dc_context *ctx)
{
	int i;

	struct dcn20_hubbub *hubbub3 = kzalloc(sizeof(struct dcn20_hubbub),
					  GFP_KERNEL);

	if (!hubbub3)
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

	hubbub42_construct(hubbub3, ctx,
					   &hubbub_reg,
					   &hubbub_shift,
					   &hubbub_mask,
					   DCN42_DEFAULT_DET_SIZE,
					   8,
					   DCN42_CRB_SIZE_KB);
	for (i = 0; i < res_cap_dcn42.num_vmid; i++) {
		struct dcn20_vmid *vmid = &hubbub3->vmid[i];

		vmid->ctx = ctx;

		vmid->regs = &vmid_regs[i];
		vmid->shifts = &vmid_shifts;
		vmid->masks = &vmid_masks;
	}

	return &hubbub3->base;
}

static struct hubp *dcn42_hubp_create(
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

	if (hubp42_construct(hubp2, ctx, inst,
						 &hubp_regs[inst], &hubp_shift, &hubp_mask))
		return &hubp2->base;

	BREAK_TO_DEBUGGER();
	kfree(hubp2);
	return NULL;
}
static const struct dc_panel_config dcn42_panel_config_defaults = {
	.psr = {
		.disable_psr = false,
		.disallow_psrsu = false,
		.disallow_replay = false,
	},
	.ilr = {
		.optimize_edp_link_rate = true,
	},
};

static void dcn42_dpp_destroy(struct dpp **dpp)
{
	kfree(TO_DCN42_DPP(*dpp));
	*dpp = NULL;
}

static struct dpp *dcn42_dpp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn42_dpp *dpp42 =
		kzalloc(sizeof(struct dcn42_dpp), GFP_KERNEL);

	if (!dpp42)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT dpp_regs
	dpp_regs_init(0),
		dpp_regs_init(1),
		dpp_regs_init(2),
		dpp_regs_init(3);

	if (dpp42_construct(dpp42, ctx, inst,
						&dpp_regs[inst], &tf_shift, &tf_mask))
		return &dpp42->base;

	BREAK_TO_DEBUGGER();
	kfree(dpp42);
	return NULL;
}

static struct mpc *dcn42_mpc_create(
	struct dc_context *ctx,
	int num_mpcc,
	int num_rmu)
{
	struct dcn42_mpc *mpc401 = kzalloc(sizeof(struct dcn42_mpc),
										GFP_KERNEL);

	if (!mpc401)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT mpc_regs
	dcn_mpc_regs_init();

	dcn42_mpc_construct(mpc401, ctx,
						 &mpc_regs,
						 &mpc_shift,
						 &mpc_mask,
						 num_mpcc,
						 num_rmu);

	return &mpc401->base;
}

static struct output_pixel_processor *dcn42_opp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_opp *opp4 =
		kzalloc(sizeof(struct dcn20_opp), GFP_KERNEL);

	if (!opp4) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

#undef REG_STRUCT
#define REG_STRUCT opp_regs
	opp_regs_init(0),
		opp_regs_init(1),
		opp_regs_init(2),
		opp_regs_init(3);
	dcn20_opp_construct(opp4, ctx, inst,
						&opp_regs[inst], &opp_shift, &opp_mask);
	return &opp4->base;
}

static struct timing_generator *dcn42_timing_generator_create(
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

	dcn42_timing_generator_init(tgn10);

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
	.flags.bits.IS_TPS4_CAPABLE = true};

static struct link_encoder *dcn42_link_encoder_create(
	struct dc_context *ctx,
	const struct encoder_init_data *enc_init_data)
{
	struct dcn20_link_encoder *enc20 =
		kzalloc(sizeof(struct dcn20_link_encoder), GFP_KERNEL);

	if (!enc20 || enc_init_data->hpd_source >= ARRAY_SIZE(link_enc_hpd_regs))
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT link_enc_aux_regs
	aux_regs_init(0),
		aux_regs_init(1),
		aux_regs_init(2),
		aux_regs_init(3),
		aux_regs_init(4);
#undef REG_STRUCT
#define REG_STRUCT link_enc_hpd_regs
	hpd_regs_init(0),
		hpd_regs_init(1),
		hpd_regs_init(2),
		hpd_regs_init(3),
		hpd_regs_init(4);
#undef REG_STRUCT
#define REG_STRUCT link_enc_regs
	link_regs_init(0, A),
		link_regs_init(1, B),
		link_regs_init(2, C),
		link_regs_init(3, D),
		link_regs_init(4, E);

	dcn42_link_encoder_construct(enc20,
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

static struct audio *dcn42_create_audio(
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

static struct vpg *dcn42_vpg_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn31_vpg *vpg4 = kzalloc(sizeof(struct dcn31_vpg), GFP_KERNEL);

	if (!vpg4)
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
		vpg_regs_init(8),
		vpg_regs_init(9);
	vpg31_construct(vpg4, ctx, inst,
					&vpg_regs[inst],
					&vpg_shift,
					&vpg_mask);

	return &vpg4->base;
}

static struct apg *dcn42_apg_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn31_apg *apg31 = kzalloc(sizeof(struct dcn31_apg), GFP_KERNEL);

	if (!apg31)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT apg_regs
	apg_regs_init(0),
		apg_regs_init(1),
		apg_regs_init(2),
		apg_regs_init(3),
		apg_regs_init(4),
		apg_regs_init(5),
		apg_regs_init(6),
		apg_regs_init(7),
		apg_regs_init(8),
		apg_regs_init(9);

	apg31_construct(apg31, ctx, inst,
					&apg_regs[inst],
					&apg_shift,
					&apg_mask);

	return &apg31->base;
}

static struct stream_encoder *dcn42_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dcn10_stream_encoder *enc1;
	struct vpg *vpg;
	struct apg *apg;

	uint32_t vpg_inst;
	uint32_t apg_inst;

	/* Mapping of VPG, DME register blocks to DIO block instance */
	if (eng_id <= ENGINE_ID_DIGE) {
		vpg_inst = eng_id;
		apg_inst = eng_id;
	} else
		return NULL;

	enc1 = kzalloc(sizeof(struct dcn10_stream_encoder), GFP_KERNEL);
	vpg = dcn42_vpg_create(ctx, vpg_inst);
	apg = dcn42_apg_create(ctx, apg_inst);

	if (!enc1 || !vpg || !apg) {
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
		stream_enc_regs_init(3),
		stream_enc_regs_init(4);

	dcn42_dio_stream_encoder_construct(enc1, ctx, ctx->dc_bios,
									eng_id, vpg, apg,
									&stream_enc_regs[eng_id],
									&se_shift, &se_mask);
	return &enc1->base;
}

static struct hpo_dp_stream_encoder *dcn42_hpo_dp_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dcn31_hpo_dp_stream_encoder *hpo_dp_enc31;
	struct vpg *vpg;
	struct apg *apg;
	uint32_t hpo_dp_inst;
	uint32_t vpg_inst;
	uint32_t apg_inst;

	ASSERT((eng_id >= ENGINE_ID_HPO_DP_0) && (eng_id <= ENGINE_ID_HPO_DP_3));
	hpo_dp_inst = eng_id - ENGINE_ID_HPO_DP_0;

	/* Mapping of VPG register blocks to HPO DP block instance:
	 * VPG[5] -> HPO_DP[0]
	 * VPG[6] -> HPO_DP[1]
	 * VPG[7] -> HPO_DP[2]
	 * VPG[8] -> HPO_DP[3]
	 */
	vpg_inst = hpo_dp_inst + 5;

	/* Mapping of APG register blocks to HPO DP block instance:
	 * APG[6] -> HPO_DP[0]
	 * APG[7] -> HPO_DP[1]
	 * APG[8] -> HPO_DP[2]
	 * APG[9] -> HPO_DP[3]
	 */
	apg_inst = hpo_dp_inst + 5;

	/* allocate HPO stream encoder and create VPG sub-block */
	hpo_dp_enc31 = kzalloc(sizeof(struct dcn31_hpo_dp_stream_encoder), GFP_KERNEL);
	vpg = dcn42_vpg_create(ctx, vpg_inst);
	apg = dcn42_apg_create(ctx, apg_inst);

	if (!hpo_dp_enc31 || !vpg || !apg) {
		kfree(hpo_dp_enc31);
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

	dcn31_hpo_dp_stream_encoder_construct(hpo_dp_enc31, ctx, ctx->dc_bios,
				hpo_dp_inst, eng_id, vpg, apg,
				&hpo_dp_stream_enc_regs[hpo_dp_inst],
				&hpo_dp_se_shift, &hpo_dp_se_mask);

	return &hpo_dp_enc31->base;
}

static struct hpo_dp_link_encoder *dcn42_hpo_dp_link_encoder_create(
	uint8_t inst,
	struct dc_context *ctx)
{
	struct dcn31_hpo_dp_link_encoder *hpo_dp_enc31;

	/* allocate HPO link encoder */
	hpo_dp_enc31 = kzalloc(sizeof(struct dcn31_hpo_dp_link_encoder), GFP_KERNEL);
	if (!hpo_dp_enc31)
		return NULL; /* out of memory */

#undef REG_STRUCT
#define REG_STRUCT hpo_dp_link_enc_regs
	hpo_dp_link_encoder_reg_init(0),
	hpo_dp_link_encoder_reg_init(1),
	hpo_dp_link_encoder_reg_init(2),
	hpo_dp_link_encoder_reg_init(3);

	hpo_dp_link_encoder42_construct(hpo_dp_enc31, ctx, inst,
				&hpo_dp_link_enc_regs[inst],
				&hpo_dp_le_shift, &hpo_dp_le_mask);

	return &hpo_dp_enc31->base;
}

static struct dce_hwseq *dcn42_hwseq_create(
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
	.create_audio = dcn42_create_audio,
	.create_stream_encoder = dcn42_stream_encoder_create,
	.create_hpo_dp_stream_encoder = dcn42_hpo_dp_stream_encoder_create,
	.create_hpo_dp_link_encoder = dcn42_hpo_dp_link_encoder_create,
	.create_hwseq = dcn42_hwseq_create,
};

static void dcn42_dsc_destroy(struct display_stream_compressor **dsc)
{
	kfree(container_of(*dsc, struct dcn401_dsc, base));
	*dsc = NULL;
}

static void dcn42_resource_destruct(struct dcn42_resource_pool *pool)
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

	for (i = 0; i < pool->base.res_cap->num_dsc; i++) {
		if (pool->base.dscs[i] != NULL)
			dcn42_dsc_destroy(&pool->base.dscs[i]);
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
			dcn42_dpp_destroy(&pool->base.dpps[i]);

		if (pool->base.ipps[i] != NULL)
			pool->base.ipps[i]->funcs->ipp_destroy(&pool->base.ipps[i]);

		if (pool->base.hubps[i] != NULL) {
			kfree(TO_DCN20_HUBP(pool->base.hubps[i]));
			pool->base.hubps[i] = NULL;
		}

		if (pool->base.irqs != NULL)
			dal_irq_service_destroy(&pool->base.irqs);
	}

	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		if (pool->base.engines[i] != NULL)
			dce110_engine_destroy(&pool->base.engines[i]);
		if (pool->base.hw_i2cs[i] != NULL) {
			kfree(pool->base.hw_i2cs[i]);
			pool->base.hw_i2cs[i] = NULL;
		}
		if (pool->base.sw_i2cs[i] != NULL) {
			kfree(pool->base.sw_i2cs[i]);
			pool->base.sw_i2cs[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_opp; i++) {
		if (pool->base.opps[i] != NULL)
			pool->base.opps[i]->funcs->opp_destroy(&pool->base.opps[i]);
	}

	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++) {
		if (pool->base.timing_generators[i] != NULL) {
			kfree(DCN10TG_FROM_TG(pool->base.timing_generators[i]));
			pool->base.timing_generators[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_dwb; i++) {
		if (pool->base.dwbc[i] != NULL) {
			kfree(TO_DCN30_DWBC(pool->base.dwbc[i]));
			pool->base.dwbc[i] = NULL;
		}
		if (pool->base.mcif_wb[i] != NULL) {
			kfree(TO_DCN30_MMHUBBUB(pool->base.mcif_wb[i]));
			pool->base.mcif_wb[i] = NULL;
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

	for (i = 0; i < pool->base.res_cap->num_mpc_3dlut; i++) {
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

	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++) {
		if (pool->base.multiple_abms[i] != NULL)
			dce_abm_destroy(&pool->base.multiple_abms[i]);
	}

	if (pool->base.psr != NULL)
		dmub_psr_destroy(&pool->base.psr);

	if (pool->base.pg_cntl != NULL)
		dcn_pg_cntl_destroy(&pool->base.pg_cntl);
	if (pool->base.dccg != NULL)
		dcn_dccg_destroy(&pool->base.dccg);

	if (pool->base.oem_device != NULL) {
		struct dc *dc = pool->base.oem_device->ctx->dc;

		dc->link_srv->destroy_ddc_service(&pool->base.oem_device);
	}
}

static void dcn42_build_pipe_pix_clk_params(struct pipe_ctx *pipe_ctx)
{
	const struct dc_stream_state *stream = pipe_ctx->stream;
	struct dc_link *link = stream->link;
	struct link_encoder *link_enc = pipe_ctx->link_res.dio_link_enc;
	struct pixel_clk_params *pixel_clk_params = &pipe_ctx->stream_res.pix_clk_params;

	pixel_clk_params->requested_pix_clk_100hz = stream->timing.pix_clk_100hz;

	if (pipe_ctx->dsc_padding_params.dsc_hactive_padding != 0)
		pixel_clk_params->requested_pix_clk_100hz = pipe_ctx->dsc_padding_params.dsc_pix_clk_100hz;

	if (!pipe_ctx->stream->ctx->dc->config.unify_link_enc_assignment)
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
		if (pixel_clk_params->requested_pix_clk_100hz > 4 * stream->ctx->dc->clk_mgr->dprefclk_khz * 10) {
			pixel_clk_params->dio_se_pix_per_cycle = 8;
		} else if (pixel_clk_params->requested_pix_clk_100hz > 2 * stream->ctx->dc->clk_mgr->dprefclk_khz * 10) {
			pixel_clk_params->dio_se_pix_per_cycle = 4;
		} else if (pixel_clk_params->requested_pix_clk_100hz > stream->ctx->dc->clk_mgr->dprefclk_khz * 10) {
			pixel_clk_params->dio_se_pix_per_cycle = 2;
		} else {
			pixel_clk_params->dio_se_pix_per_cycle = 1;
		}
	}
}

static bool dcn42_dwbc_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t dwb_count = pool->res_cap->num_dwb;

	for (i = 0; i < dwb_count; i++) {
		struct dcn30_dwbc *dwbc42 = kzalloc(sizeof(struct dcn30_dwbc),
											GFP_KERNEL);

		if (!dwbc42) {
			dm_error("DC: failed to create dwbc42!\n");
			return false;
		}

#undef REG_STRUCT
#define REG_STRUCT dwbc401_regs
		dwbc_regs_dcn401_init(0);

		dcn30_dwbc_construct(dwbc42, ctx,
				&dwbc401_regs[i],
				&dwbc401_shift,
				&dwbc401_mask,
				i);

		pool->dwbc[i] = &dwbc42->base;
	}
	return true;
}

static void dcn42_mmhubbub_init(struct dcn30_mmhubbub *mcif_wb30,
								struct dc_context *ctx)
{
	dcn42_mmhubbub_set_fgcg(
		mcif_wb30,
		ctx->dc->debug.enable_fine_grain_clock_gating.bits.mmhubbub);
}

static bool dcn42_mmhubbub_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t pipe_count = pool->res_cap->num_dwb;

	for (i = 0; i < pipe_count; i++) {
		struct dcn30_mmhubbub *mcif_wb30 = kzalloc(sizeof(struct dcn30_mmhubbub),
												   GFP_KERNEL);

		if (!mcif_wb30) {
			dm_error("DC: failed to create mcif_wb30!\n");
			return false;
		}

#undef REG_STRUCT
#define REG_STRUCT mcif_wb35_regs
		mcif_wb_regs_dcn3_init(0);

		dcn35_mmhubbub_construct(mcif_wb30, ctx,
					&mcif_wb35_regs[i],
					&mcif_wb35_shift,
					&mcif_wb35_mask,
					i);

		dcn42_mmhubbub_init(mcif_wb30, ctx);

		pool->mcif_wb[i] = &mcif_wb30->base;
	}
	return true;
}

static struct display_stream_compressor *dcn42_dsc_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn401_dsc *dsc =
		kzalloc(sizeof(struct dcn401_dsc), GFP_KERNEL);

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

	dsc401_construct(dsc, ctx, inst, &dsc_regs[inst], &dsc_shift, &dsc_mask);
	dsc401_set_fgcg(dsc, ctx->dc->debug.enable_fine_grain_clock_gating.bits.dsc);

	dsc->max_image_width = 5760;

	return &dsc->base;
}

static void dcn42_destroy_resource_pool(struct resource_pool **pool)
{
	struct dcn42_resource_pool *dcn42_pool = TO_DCN42_RES_POOL(*pool);

	dcn42_resource_destruct(dcn42_pool);
	kfree(dcn42_pool);
	*pool = NULL;
}

static struct dc_cap_funcs cap_funcs = {
	.get_dcc_compression_cap = dcn20_get_dcc_compression_cap};

static void dcn42_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params)
{
	DC_FP_START();
	if (dc->current_state && dc->current_state->bw_ctx.dml2)
		dml2_reinit(dc, &dc->dml2_options, &dc->current_state->bw_ctx.dml2);
	DC_FP_END();
}

enum dc_status dcn42_validate_bandwidth(struct dc *dc,
							  struct dc_state *context,
							  enum dc_validate_mode validate_mode)
{
	bool out = false;

	out = dml2_validate(dc, context, context->bw_ctx.dml2,
						validate_mode);
	DC_FP_START();
	if (validate_mode == DC_VALIDATE_MODE_AND_PROGRAMMING) {
		/*not required for mode enumeration*/
		dcn42_decide_zstate_support(dc, context);
	}
	DC_FP_END();
	return out ? DC_OK : DC_FAIL_BANDWIDTH_VALIDATE;
}
void dcn42_prepare_mcache_programming(struct dc *dc,
									  struct dc_state *context)
{
	if (dc->debug.using_dml21)
		dml2_prepare_mcache_programming(dc, context,
			context->power_source == DC_POWER_SOURCE_DC ?
				context->bw_ctx.dml2_dc_power_source : context->bw_ctx.dml2);
}
/* Create a minimal link encoder object not associated with a particular
 * physical connector.
 * resource_funcs.link_enc_create_minimal
 */
static struct link_encoder *dcn42_link_enc_create_minimal(
		struct dc_context *ctx, enum engine_id eng_id)
{
	struct dcn20_link_encoder *enc20;

	if ((eng_id - ENGINE_ID_DIGA) > ctx->dc->res_pool->res_cap->num_dig_link_enc)
		return NULL;

	enc20 = kzalloc(sizeof(struct dcn20_link_encoder), GFP_KERNEL);
	if (!enc20)
		return NULL;

	dcn31_link_encoder_construct_minimal(
			enc20,
			ctx,
			&link_enc_feature,
			&link_enc_regs[eng_id - ENGINE_ID_DIGA],
			eng_id);

	return &enc20->enc10.base;
}
static void dcn42_get_panel_config_defaults(struct dc_panel_config *panel_config)
{
	*panel_config = dcn42_panel_config_defaults;
}
static unsigned int dcn42_get_max_hw_cursor_size(const struct dc *dc,
			struct dc_state *state,
			const struct dc_stream_state *stream)
{
	return dc->caps.max_cursor_size;
}
static struct resource_funcs dcn42_res_pool_funcs = {
	.destroy = dcn42_destroy_resource_pool,
	.link_enc_create = dcn42_link_encoder_create,
	.link_enc_create_minimal = dcn42_link_enc_create_minimal,
	.link_encs_assign = link_enc_cfg_link_encs_assign,
	.link_enc_unassign = link_enc_cfg_link_enc_unassign,
	.panel_cntl_create = dcn32_panel_cntl_create,
	.validate_bandwidth = dcn42_validate_bandwidth,
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
	.update_bw_bounding_box = dcn42_update_bw_bounding_box,
	.patch_unknown_plane_state = dcn401_patch_unknown_plane_state,
	.get_panel_config_defaults = dcn42_get_panel_config_defaults,
	.get_preferred_eng_id_dpia = dcn42_get_preferred_eng_id_dpia,
	.update_soc_for_wm_a = dcn30_update_soc_for_wm_a,
	.add_phantom_pipes = dcn32_add_phantom_pipes,
	.calculate_mall_ways_from_bytes = dcn32_calculate_mall_ways_from_bytes,
	.prepare_mcache_programming = dcn42_prepare_mcache_programming,
	.build_pipe_pix_clk_params = dcn42_build_pipe_pix_clk_params,
	.get_vstartup_for_pipe = dcn401_get_vstartup_for_pipe,
	.get_max_hw_cursor_size = dcn42_get_max_hw_cursor_size,
};

static uint32_t read_pipe_fuses(struct dc_context *ctx)
{
	uint32_t value = REG_READ(CC_DC_PIPE_DIS);

	if (value == 0 && ctx->dce_environment == DCE_ENV_DIAG)
		value = 0xF;
	/* DCN401 support max 4 pipes */
	value = value & 0xf;
	return value;
}

static bool dcn42_resource_construct(
	uint8_t num_virtual_links,
	struct dc *dc,
	struct dcn42_resource_pool *pool)
{
	int i, j;
	struct dc_context *ctx = dc->ctx;
	struct irq_service_init_data init_data;
	uint32_t pipe_fuses;
	uint32_t num_pipes;

#undef REG_STRUCT
#define REG_STRUCT bios_regs
	bios_regs_init();

#undef REG_STRUCT
#define REG_STRUCT clk_src_regs
	clk_src_regs_init(0, A),
	clk_src_regs_init(1, B),
	clk_src_regs_init(2, C),
	clk_src_regs_init(3, D),
	clk_src_regs_init(4, E);

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

	pool->base.res_cap = &res_cap_dcn42;

	/* max number of pipes for ASIC before checking for pipe fuses */
	num_pipes = pool->base.res_cap->num_dpp;
	pipe_fuses = read_pipe_fuses(ctx);

	for (i = 0; i < pool->base.res_cap->num_dpp; i++)
		if (pipe_fuses & 1 << i)
			num_pipes--;

	if (pipe_fuses & 1)
		ASSERT(0); // Unexpected - Pipe 0 should always be fully functional!

	if (pipe_fuses & CC_DC_PIPE_DIS__DC_FULL_DIS_MASK)
		ASSERT(0); // Entire DCN is harvested!

	pool->base.funcs = &dcn42_res_pool_funcs;

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;
	pool->base.timing_generator_count = pool->base.res_cap->num_timing_generator;
	pool->base.pipe_count = num_pipes;
	pool->base.mpcc_count = num_pipes;
	dc->caps.ips_v2_support = true;
	dc->caps.max_downscale_ratio = 600;
	dc->caps.i2c_speed_in_khz = 100;
	dc->caps.i2c_speed_in_khz_hdcp = 100; /*1.4 w/a applied by default*/
	/* TODO: Bring max cursor size back to 256 after subvp cursor corruption is fixed*/
	dc->caps.max_cursor_size = 64;
	dc->caps.max_buffered_cursor_size = 64;
	dc->caps.cursor_not_scaled = true;
	dc->caps.min_horizontal_blanking_period = 80;
	dc->caps.dmdata_alloc_size = 2048;
	dc->caps.mall_size_per_mem_channel = 4;
	/* total size = mall per channel * num channels * 1024 * 1024 */
	dc->caps.mall_size_total = dc->caps.mall_size_per_mem_channel *
		dc->ctx->dc_bios->vram_info.num_chans * 1048576;
	dc->caps.cursor_cache_size = dc->caps.max_cursor_size * dc->caps.max_cursor_size * 8;
	dc->caps.cache_line_size = 64;
	dc->caps.cache_num_ways = 16;

	/* Calculate the available MALL space */
	dc->caps.max_cab_allocation_bytes =
		dcn32_calc_num_avail_chans_for_mall(dc, dc->ctx->dc_bios->vram_info.num_chans) *
				dc->caps.mall_size_per_mem_channel * 1024 * 1024;
	dc->caps.mall_size_total = dc->caps.max_cab_allocation_bytes;

	dc->caps.subvp_fw_processing_delay_us = 15;
	dc->caps.subvp_drr_max_vblank_margin_us = 40;
	dc->caps.subvp_prefetch_end_to_mall_start_us = 15;
	dc->caps.subvp_swath_height_margin_lines = 16;
	dc->caps.subvp_pstate_allow_width_us = 20;
	dc->caps.subvp_vertical_int_margin_us = 30;
	dc->caps.subvp_drr_vblank_start_margin_us = 100; // 100us margin

	dc->caps.max_slave_planes = 2;
	dc->caps.max_slave_yuv_planes = 2;
	dc->caps.max_slave_rgb_planes = 2;
	dc->caps.post_blend_color_processing = true;
	dc->caps.force_dp_tps4_for_cp2520 = true;
	if (dc->config.forceHBR2CP2520)
		dc->caps.force_dp_tps4_for_cp2520 = false;
	dc->caps.dp_hdmi21_pcon_support = true;
	dc->caps.dp_hpo = true;
	dc->caps.edp_dsc_support = true;
	dc->caps.extended_aux_timeout_support = true;
	dc->caps.dmcub_support = true;
	dc->caps.is_apu = true;
	dc->caps.seamless_odm = true;
	dc->caps.zstate_support = true;
	dc->caps.ips_support = true;
	dc->caps.max_v_total = (1 << 15) - 1;
	dc->caps.vtotal_limited_by_fp2 = true;

	dc->caps.seamless_odm = true;
	dc->caps.zstate_support = true;
	dc->caps.ips_support = true;
	dc->caps.max_v_total = (1 << 15) - 1;
	dc->caps.vtotal_limited_by_fp2 = true;

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
	//configurable to be before or after BLND in MPCC
	dc->caps.color.mpc.num_3dluts = pool->base.res_cap->num_mpc_3dlut;
	dc->caps.color.mpc.num_rmcm_3dluts = 2;
	dc->caps.color.mpc.ogam_ram = 1;
	dc->caps.color.mpc.ogam_rom_caps.srgb = 0;
	dc->caps.color.mpc.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.mpc.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.mpc.ogam_rom_caps.pq = 0;
	dc->caps.color.mpc.ogam_rom_caps.hlg = 0;
	dc->caps.color.mpc.ocsc = 1;
	dc->caps.color.mpc.preblend = true;
	dc->caps.color.mpc.mcm_3d_lut_caps.dma_3d_lut = 1;
	dc->caps.color.mpc.mcm_3d_lut_caps.lut_dim_caps.dim_9 = 1;
	dc->caps.color.mpc.mcm_3d_lut_caps.lut_dim_caps.dim_17 = 1;
	dc->caps.color.mpc.mcm_3d_lut_caps.mem_layout_support.linear_1d = 1;
	dc->caps.color.mpc.mcm_3d_lut_caps.mem_layout_support.swizzle_3d_bgr = 1;
	dc->caps.color.mpc.mcm_3d_lut_caps.mem_layout_support.swizzle_3d_rgb = 1;
	dc->caps.color.mpc.mcm_3d_lut_caps.mem_format_support.unorm_12msb = 1;
	dc->caps.color.mpc.mcm_3d_lut_caps.mem_format_support.unorm_12lsb = 1;
	dc->caps.color.mpc.mcm_3d_lut_caps.mem_format_support.float_fp1_5_10 = 1;
	dc->caps.color.mpc.mcm_3d_lut_caps.mem_pixel_order_support.order_rgba = 1;
	dc->caps.color.mpc.mcm_3d_lut_caps.mem_pixel_order_support.order_bgra = 1;
	dc->caps.color.mpc.rmcm_3d_lut_caps.dma_3d_lut = 1;
	dc->caps.color.mpc.rmcm_3d_lut_caps.lut_dim_caps.dim_17 = 1;
	dc->caps.color.mpc.rmcm_3d_lut_caps.lut_dim_caps.dim_33 = 1;
	dc->caps.color.mpc.rmcm_3d_lut_caps.mem_layout_support.linear_1d = 1;
	dc->caps.color.mpc.rmcm_3d_lut_caps.mem_layout_support.swizzle_3d_bgr = 1;
	dc->caps.color.mpc.rmcm_3d_lut_caps.mem_layout_support.swizzle_3d_rgb = 1;
	dc->caps.color.mpc.rmcm_3d_lut_caps.mem_format_support.unorm_12msb = 1;
	dc->caps.color.mpc.rmcm_3d_lut_caps.mem_format_support.unorm_12lsb = 1;
	dc->caps.color.mpc.rmcm_3d_lut_caps.mem_format_support.float_fp1_5_10 = 1;
	dc->caps.color.mpc.rmcm_3d_lut_caps.mem_pixel_order_support.order_rgba = 1;
	dc->caps.color.mpc.rmcm_3d_lut_caps.mem_pixel_order_support.order_bgra = 1;

	dc->caps.num_of_host_routers = 3;
	dc->caps.num_of_dpias_per_host_router = 2;

	/* max_disp_clock_khz_at_vmin is slightly lower than the STA value in order
	 * to provide some margin.
	 * It's expected for furture ASIC to have equal or higher value, in order to
	 * have determinstic power improvement from generate to genration.
	 * (i.e., we should not expect new ASIC generation with lower vmin rate)
	 */
	dc->caps.max_disp_clock_khz_at_vmin = 650000;
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

	dc->config.use_pipe_ctx_sync_logic = true;
	dc->config.dc_mode_clk_limit_support = true;
	dc->config.enable_windowed_mpo_odm = true;
	/* Use psp mailbox to enable assr */
	dc->config.use_assr_psp_message = true;
	/* dcn42 and afterward always support external panel replay */
	dc->config.frame_update_cmd_version2 = true;

	/* read VBIOS LTTPR caps */
	{
		if (ctx->dc_bios->funcs->get_lttpr_caps) {
			enum bp_result bp_query_result;
			uint8_t is_vbios_lttpr_enable = 0;

			bp_query_result = ctx->dc_bios->funcs->get_lttpr_caps(ctx->dc_bios, &is_vbios_lttpr_enable);
			dc->caps.vbios_lttpr_enable = (bp_query_result == BP_RESULT_OK) && !!is_vbios_lttpr_enable;
		}

		dc->caps.vbios_lttpr_aware = true;
	}
	dc->check_config = config_defaults;

	if (dc->ctx->dce_environment == DCE_ENV_PRODUCTION_DRV)
		dc->debug = debug_defaults_drv;

	/*HW default is to have all the FGCG enabled, SW no need to program them*/
	dc->debug.enable_fine_grain_clock_gating.u32All = 0xFFFF;
	// Init the vm_helper
	if (dc->vm_helper)
		vm_helper_init(dc->vm_helper, 16);

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	/* Clock Sources for Pixel Clock*/
	pool->base.clock_sources[DCN401_CLK_SRC_PLL0] =
		dcn42_clock_source_create(ctx, ctx->dc_bios,
								  CLOCK_SOURCE_COMBO_PHY_PLL0,
								  &clk_src_regs[0], false);
	pool->base.clock_sources[DCN401_CLK_SRC_PLL1] =
		dcn42_clock_source_create(ctx, ctx->dc_bios,
								  CLOCK_SOURCE_COMBO_PHY_PLL1,
								  &clk_src_regs[1], false);
	pool->base.clock_sources[DCN401_CLK_SRC_PLL2] =
		dcn42_clock_source_create(ctx, ctx->dc_bios,
								  CLOCK_SOURCE_COMBO_PHY_PLL2,
								  &clk_src_regs[2], false);
	pool->base.clock_sources[DCN401_CLK_SRC_PLL3] =
		dcn42_clock_source_create(ctx, ctx->dc_bios,
								  CLOCK_SOURCE_COMBO_PHY_PLL3,
								  &clk_src_regs[3], false);
	pool->base.clock_sources[DCN401_CLK_SRC_PLL4] =
		dcn42_clock_source_create(ctx, ctx->dc_bios,
								  CLOCK_SOURCE_COMBO_PHY_PLL4,
								  &clk_src_regs[4], false);

	pool->base.clk_src_count = DCN401_CLK_SRC_TOTAL;

	/* todo: not reuse phy_pll registers */
	pool->base.dp_clock_source =
		dcn42_clock_source_create(ctx, ctx->dc_bios,
								  CLOCK_SOURCE_ID_DP_DTO,
								  &clk_src_regs[0], true);

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}

	/* DCCG */
	pool->base.dccg = dccg42_create(ctx, &dccg_regs, &dccg_shift, &dccg_mask);
	if (pool->base.dccg == NULL) {
		dm_error("DC: failed to create dccg!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

#undef REG_STRUCT
#define REG_STRUCT pg_cntl_regs
	pg_cntl_dcn42_regs_init();

	pool->base.pg_cntl = pg_cntl42_create(ctx, &pg_cntl_regs, &pg_cntl_shift, &pg_cntl_mask);
	if (pool->base.pg_cntl == NULL) {
		dm_error("DC: failed to create power gate control!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}
	/* IRQ Service */
	init_data.ctx = dc->ctx;
	pool->base.irqs = dal_irq_service_dcn42_create(&init_data);
	if (!pool->base.irqs)
		goto create_fail;

	/* HUBBUB */
	pool->base.hubbub = dcn42_hubbub_create(ctx);
	if (pool->base.hubbub == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create hubbub!\n");
		goto create_fail;
	}

	/* HUBPs, DPPs, OPPs, TGs, ABMs */
	for (i = 0, j = 0; i < pool->base.res_cap->num_timing_generator; i++) {
		/* if pipe is disabled, skip instance of HW pipe,
		 * i.e, skip ASIC register instance
		 */
		if (pipe_fuses & 1 << i)
			continue;

		pool->base.hubps[j] = dcn42_hubp_create(ctx, i);
		if (pool->base.hubps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create hubps!\n");
			goto create_fail;
		}

		pool->base.dpps[j] = dcn42_dpp_create(ctx, i);
		if (pool->base.dpps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create dpps!\n");
			goto create_fail;
		}

		pool->base.opps[j] = dcn42_opp_create(ctx, i);
		if (pool->base.opps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto create_fail;
		}

		pool->base.timing_generators[j] = dcn42_timing_generator_create(
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

	/* Replay */
	pool->base.replay = dmub_replay_create(ctx);
	if (pool->base.replay == NULL) {
		dm_error("DC: failed to create replay obj!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* MPCCs */
	pool->base.mpc = dcn42_mpc_create(ctx, pool->base.res_cap->num_timing_generator,
			pool->base.res_cap->num_mpc_3dlut);
	if (pool->base.mpc == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mpc!\n");
		goto create_fail;
	}

	/* DSCs */
	for (i = 0; i < pool->base.res_cap->num_dsc; i++) {
		pool->base.dscs[i] = dcn42_dsc_create(ctx, i);
		if (pool->base.dscs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create display stream compressor %d!\n", i);
			goto create_fail;
		}
	}

	/* DWB */
	if (!dcn42_dwbc_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create dwbc!\n");
		goto create_fail;
	}

	/* MMHUBBUB */
	if (!dcn42_mmhubbub_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mcif_wb!\n");
		goto create_fail;
	}

	/* AUX and I2C */
	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		pool->base.engines[i] = dcn42_aux_engine_create(ctx, i);

		if (pool->base.engines[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create aux engine!!\n");
			goto create_fail;
		}
		pool->base.hw_i2cs[i] = dcn42_i2c_hw_create(ctx, i);
		if (pool->base.hw_i2cs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create hw i2c!!\n");
			goto create_fail;
		}
		pool->base.sw_i2cs[i] = NULL;
	}
	/* DCN4.2 has 6 DPIA */
	pool->base.usb4_dpia_count = dc->caps.num_of_host_routers * dc->caps.num_of_dpias_per_host_router;
	if (dc->debug.dpia_debug.bits.disable_dpia)
		pool->base.usb4_dpia_count = 0;

	/* Audio, HWSeq, Stream Encoders including HPO and virtual, MPC 3D LUTs */
	if (!resource_construct(num_virtual_links, dc, &pool->base,
							&res_create_funcs))
		goto create_fail;

	/* HW Sequencer init functions and Plane caps */
	dcn42_hw_sequencer_init_functions(dc);

	dc->caps.max_planes = pool->base.pipe_count;

	for (i = 0; i < dc->caps.max_planes; ++i)
		dc->caps.planes[i] = plane_cap;

	dc->caps.max_odm_combine_factor = 4;

	dc->cap_funcs = cap_funcs;
	dc->dcn_ip->max_num_dpp = pool->base.pipe_count;

	// For now enable SDPIF_REQUEST_RATE_LIMIT on DCN4_01 when vram_info.num_chans provided
	if (dc->config.sdpif_request_limit_words_per_umc == 0)
		dc->config.sdpif_request_limit_words_per_umc = 16;

	dc->dml2_options.dcn_pipe_count = pool->base.pipe_count;
	 /*this will use real soc clock table*/
	dc->dml2_options.use_native_soc_bb_construction = true;
	dc->dml2_options.minimize_dispclk_using_odm = false;
	if (dc->config.EnableMinDispClkODM)
		dc->dml2_options.minimize_dispclk_using_odm = true;
	dc->dml2_options.enable_windowed_mpo_odm = dc->config.enable_windowed_mpo_odm;
	dc->dml2_options.map_dc_pipes_with_callbacks = true;
	dc->dml2_options.force_tdlut_enable = true;

	resource_init_common_dml2_callbacks(dc, &dc->dml2_options);
	dc->dml2_options.callbacks.can_support_mclk_switch_using_fw_based_vblank_stretch =
			&dcn30_can_support_mclk_switch_using_fw_based_vblank_stretch;
	dc->dml2_options.svp_pstate.callbacks.release_dsc = &dcn20_release_dsc;
	dc->dml2_options.svp_pstate.callbacks.calculate_mall_ways_from_bytes =
		pool->base.funcs->calculate_mall_ways_from_bytes;

	dc->dml2_options.svp_pstate.subvp_fw_processing_delay_us = dc->caps.subvp_fw_processing_delay_us;
	dc->dml2_options.svp_pstate.subvp_prefetch_end_to_mall_start_us = dc->caps.subvp_prefetch_end_to_mall_start_us;
	dc->dml2_options.svp_pstate.subvp_pstate_allow_width_us = dc->caps.subvp_pstate_allow_width_us;
	dc->dml2_options.svp_pstate.subvp_swath_height_margin_lines = dc->caps.subvp_swath_height_margin_lines;

	dc->dml2_options.svp_pstate.force_disable_subvp = dc->debug.force_disable_subvp;
	dc->dml2_options.svp_pstate.force_enable_subvp = dc->debug.force_subvp_mclk_switch;

	dc->dml2_options.mall_cfg.cache_line_size_bytes = dc->caps.cache_line_size;
	dc->dml2_options.mall_cfg.cache_num_ways = dc->caps.cache_num_ways;
	dc->dml2_options.mall_cfg.max_cab_allocation_bytes =
				dc->caps.max_cab_allocation_bytes;
	dc->dml2_options.mall_cfg.mblk_height_4bpe_pixels = DCN3_2_MBLK_HEIGHT_4BPE;
	dc->dml2_options.mall_cfg.mblk_height_8bpe_pixels = DCN3_2_MBLK_HEIGHT_8BPE;
	dc->dml2_options.mall_cfg.mblk_size_bytes = DCN3_2_MALL_MBLK_SIZE_BYTES;
	dc->dml2_options.mall_cfg.mblk_width_pixels = DCN3_2_MBLK_WIDTH;

	dc->dml2_options.max_segments_per_hubp = 24;
	dc->dml2_options.det_segment_size = DCN42_CRB_SEGMENT_SIZE_KB;

	/* SPL */
	dc->caps.scl_caps.sharpener_support = true;

	/* init DC limited DML2 options */
	memcpy(&dc->dml2_dc_power_options, &dc->dml2_options, sizeof(struct dml2_configuration_options));
	dc->dml2_dc_power_options.use_clock_dc_limits = true;

	return true;

create_fail:

	dcn42_resource_destruct(pool);

	return false;
}
struct resource_pool *dcn42_create_resource_pool(
	const struct dc_init_data *init_data,
	struct dc *dc)
{
	struct dcn42_resource_pool *pool =
		kzalloc(sizeof(struct dcn401_resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (dcn42_resource_construct(init_data->num_virtual_links, dc, pool))
		return &pool->base;

	BREAK_TO_DEBUGGER();
	kfree(pool);
	return NULL;
}
