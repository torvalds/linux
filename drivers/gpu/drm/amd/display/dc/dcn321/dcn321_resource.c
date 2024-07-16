// SPDX-License-Identifier: MIT
/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include "dm_services.h"
#include "dc.h"

#include "dcn32/dcn32_init.h"

#include "resource.h"
#include "include/irq_service_interface.h"
#include "dcn32/dcn32_resource.h"
#include "dcn321_resource.h"

#include "dcn20/dcn20_resource.h"
#include "dcn30/dcn30_resource.h"

#include "dml/dcn321/dcn321_fpu.h"

#include "dcn10/dcn10_ipp.h"
#include "dcn30/dcn30_hubbub.h"
#include "dcn31/dcn31_hubbub.h"
#include "dcn32/dcn32_hubbub.h"
#include "dcn32/dcn32_mpc.h"
#include "dcn32/dcn32_hubp.h"
#include "irq/dcn32/irq_service_dcn32.h"
#include "dcn32/dcn32_dpp.h"
#include "dcn32/dcn32_optc.h"
#include "dcn20/dcn20_hwseq.h"
#include "dcn30/dcn30_hwseq.h"
#include "dce110/dce110_hw_sequencer.h"
#include "dcn30/dcn30_opp.h"
#include "dcn20/dcn20_dsc.h"
#include "dcn30/dcn30_vpg.h"
#include "dcn30/dcn30_afmt.h"
#include "dcn30/dcn30_dio_stream_encoder.h"
#include "dcn32/dcn32_dio_stream_encoder.h"
#include "dcn31/dcn31_hpo_dp_stream_encoder.h"
#include "dcn31/dcn31_hpo_dp_link_encoder.h"
#include "dcn32/dcn32_hpo_dp_link_encoder.h"
#include "dc_link_dp.h"
#include "dcn31/dcn31_apg.h"
#include "dcn31/dcn31_dio_link_encoder.h"
#include "dcn32/dcn32_dio_link_encoder.h"
#include "dcn321_dio_link_encoder.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_audio.h"
#include "dce/dce_hwseq.h"
#include "clk_mgr.h"
#include "virtual/virtual_stream_encoder.h"
#include "dml/display_mode_vba.h"
#include "dcn32/dcn32_dccg.h"
#include "dcn10/dcn10_resource.h"
#include "dc_link_ddc.h"
#include "dcn31/dcn31_panel_cntl.h"

#include "dcn30/dcn30_dwb.h"
#include "dcn32/dcn32_mmhubbub.h"

#include "dcn/dcn_3_2_1_offset.h"
#include "dcn/dcn_3_2_1_sh_mask.h"
#include "nbio/nbio_4_3_0_offset.h"

#include "reg_helper.h"
#include "dce/dmub_abm.h"
#include "dce/dmub_psr.h"
#include "dce/dce_aux.h"
#include "dce/dce_i2c.h"

#include "dml/dcn30/display_mode_vba_30.h"
#include "vm_helper.h"
#include "dcn20/dcn20_vmid.h"

#define DC_LOGGER_INIT(logger)

enum dcn321_clk_src_array_id {
	DCN321_CLK_SRC_PLL0,
	DCN321_CLK_SRC_PLL1,
	DCN321_CLK_SRC_PLL2,
	DCN321_CLK_SRC_PLL3,
	DCN321_CLK_SRC_PLL4,
	DCN321_CLK_SRC_TOTAL
};

/* begin *********************
 * macros to expend register list macro defined in HW object header file
 */

/* DCN */
/* TODO awful hack. fixup dcn20_dwb.h */
#undef BASE_INNER
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

#define SR_ARR_I2C(reg_name, id) \
	REG_STRUCT[id-1].reg_name = BASE(reg##reg_name##_BASE_IDX) + reg##reg_name

#define SRI_ARR_I2C(reg_name, block, id)\
	REG_STRUCT[id-1].reg_name = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
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

#define VUPDATE_SRII(reg_name, block, id)\
	REG_STRUCT.reg_name[id] = BASE(reg ## reg_name ## _ ## block ## id ## _BASE_IDX) + \
		reg ## reg_name ## _ ## block ## id

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
		( \
		NBIO_SR(BIOS_SCRATCH_3),\
		NBIO_SR(BIOS_SCRATCH_6)\
		)

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
		ABM_DCN32_REG_LIST_RI(id)

static struct dce_abm_registers abm_regs[4];

static const struct dce_abm_shift abm_shift = {
		ABM_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dce_abm_mask abm_mask = {
		ABM_MASK_SH_LIST_DCN32(_MASK)
};

#define audio_regs_init(id)\
		AUD_COMMON_REG_LIST_RI(id)

static struct dce_audio_registers audio_regs[5];

#define DCE120_AUD_COMMON_MASK_SH_LIST(mask_sh)\
		SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_INDEX, AZALIA_ENDPOINT_REG_INDEX, mask_sh),\
		SF(AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_DATA, AZALIA_ENDPOINT_REG_DATA, mask_sh),\
		AUD_COMMON_MASK_SH_LIST_BASE(mask_sh)

static const struct dce_audio_shift audio_shift = {
		DCE120_AUD_COMMON_MASK_SH_LIST(__SHIFT)
};

static const struct dce_audio_mask audio_mask = {
		DCE120_AUD_COMMON_MASK_SH_LIST(_MASK)
};

#define vpg_regs_init(id)\
	VPG_DCN3_REG_LIST_RI(id)

static struct dcn30_vpg_registers vpg_regs[10];

static const struct dcn30_vpg_shift vpg_shift = {
	DCN3_VPG_MASK_SH_LIST(__SHIFT)
};

static const struct dcn30_vpg_mask vpg_mask = {
	DCN3_VPG_MASK_SH_LIST(_MASK)
};

#define afmt_regs_init(id)\
	AFMT_DCN3_REG_LIST_RI(id)

static struct dcn30_afmt_registers afmt_regs[6];

static const struct dcn30_afmt_shift afmt_shift = {
	DCN3_AFMT_MASK_SH_LIST(__SHIFT)
};

static const struct dcn30_afmt_mask afmt_mask = {
	DCN3_AFMT_MASK_SH_LIST(_MASK)
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
	SE_DCN32_REG_LIST_RI(id)

static struct dcn10_stream_enc_registers stream_enc_regs[5];

static const struct dcn10_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dcn10_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCN32(_MASK)
};


#define aux_regs_init(id)\
	DCN2_AUX_REG_LIST_RI(id)

static struct dcn10_link_enc_aux_registers link_enc_aux_regs[5];

#define hpd_regs_init(id)\
	HPD_REG_LIST_RI(id)

static struct dcn10_link_enc_hpd_registers link_enc_hpd_regs[5];

#define link_regs_init(id, phyid)\
	( \
	LE_DCN31_REG_LIST_RI(id), \
	UNIPHY_DCN2_REG_LIST_RI(id, phyid)\
	)
	/*DPCS_DCN31_REG_LIST(id),*/ \

static struct dcn10_link_enc_registers link_enc_regs[5];

static const struct dcn10_link_enc_shift le_shift = {
	LINK_ENCODER_MASK_SH_LIST_DCN31(__SHIFT), \
//	DPCS_DCN31_MASK_SH_LIST(__SHIFT)
};

static const struct dcn10_link_enc_mask le_mask = {
	LINK_ENCODER_MASK_SH_LIST_DCN31(_MASK), \
//	DPCS_DCN31_MASK_SH_LIST(_MASK)
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

static struct dcn31_hpo_dp_link_encoder_registers hpo_dp_link_enc_regs[2];

static const struct dcn31_hpo_dp_link_encoder_shift hpo_dp_le_shift = {
	DCN3_2_HPO_DP_LINK_ENC_MASK_SH_LIST(__SHIFT)
};

static const struct dcn31_hpo_dp_link_encoder_mask hpo_dp_le_mask = {
	DCN3_2_HPO_DP_LINK_ENC_MASK_SH_LIST(_MASK)
};

#define dpp_regs_init(id)\
	DPP_REG_LIST_DCN30_COMMON_RI(id)

static struct dcn3_dpp_registers dpp_regs[4];

static const struct dcn3_dpp_shift tf_shift = {
		DPP_REG_LIST_SH_MASK_DCN30_COMMON(__SHIFT)
};

static const struct dcn3_dpp_mask tf_mask = {
		DPP_REG_LIST_SH_MASK_DCN30_COMMON(_MASK)
};


#define opp_regs_init(id)\
	OPP_REG_LIST_DCN30_RI(id)

static struct dcn20_opp_registers opp_regs[4];

static const struct dcn20_opp_shift opp_shift = {
	OPP_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn20_opp_mask opp_mask = {
	OPP_MASK_SH_LIST_DCN20(_MASK)
};

#define aux_engine_regs_init(id) \
	( \
	AUX_COMMON_REG_LIST0_RI(id), SR_ARR_INIT(AUXN_IMPCAL, id, 0), \
	SR_ARR_INIT(AUXP_IMPCAL, id, 0), \
	SR_ARR_INIT(AUX_RESET_MASK, id, DP_AUX0_AUX_CONTROL__AUX_RESET_MASK), \
	SR_ARR_INIT(AUX_RESET_MASK, id, DP_AUX0_AUX_CONTROL__AUX_RESET_MASK)\
	)

static struct dce110_aux_registers aux_engine_regs[5];

static const struct dce110_aux_registers_shift aux_shift = {
	DCN_AUX_MASK_SH_LIST(__SHIFT)
};

static const struct dce110_aux_registers_mask aux_mask = {
	DCN_AUX_MASK_SH_LIST(_MASK)
};

#define dwbc_regs_dcn3_init(id)\
	DWBC_COMMON_REG_LIST_DCN30_RI(id)

static struct dcn30_dwbc_registers dwbc30_regs[1];

static const struct dcn30_dwbc_shift dwbc30_shift = {
	DWBC_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn30_dwbc_mask dwbc30_mask = {
	DWBC_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

#define mcif_wb_regs_dcn3_init(id)\
	MCIF_WB_COMMON_REG_LIST_DCN32_RI(id)

static struct dcn30_mmhubbub_registers mcif_wb30_regs[1];

static const struct dcn30_mmhubbub_shift mcif_wb30_shift = {
	MCIF_WB_COMMON_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dcn30_mmhubbub_mask mcif_wb30_mask = {
	MCIF_WB_COMMON_MASK_SH_LIST_DCN32(_MASK)
};

#define dsc_regsDCN20_init(id)\
	DSC_REG_LIST_DCN20_RI(id)

static struct dcn20_dsc_registers dsc_regs[4];

static const struct dcn20_dsc_shift dsc_shift = {
	DSC_REG_LIST_SH_MASK_DCN20(__SHIFT)
};

static const struct dcn20_dsc_mask dsc_mask = {
	DSC_REG_LIST_SH_MASK_DCN20(_MASK)
};

static struct dcn30_mpc_registers mpc_regs;
#define dcn_mpc_regs_init()\
	MPC_REG_LIST_DCN3_2_RI(0),\
	MPC_REG_LIST_DCN3_2_RI(1),\
	MPC_REG_LIST_DCN3_2_RI(2),\
	MPC_REG_LIST_DCN3_2_RI(3),\
	MPC_OUT_MUX_REG_LIST_DCN3_0_RI(0),\
	MPC_OUT_MUX_REG_LIST_DCN3_0_RI(1),\
	MPC_OUT_MUX_REG_LIST_DCN3_0_RI(2),\
	MPC_OUT_MUX_REG_LIST_DCN3_0_RI(3),\
	MPC_DWB_MUX_REG_LIST_DCN3_0_RI(0)

static const struct dcn30_mpc_shift mpc_shift = {
	MPC_COMMON_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dcn30_mpc_mask mpc_mask = {
	MPC_COMMON_MASK_SH_LIST_DCN32(_MASK)
};

#define optc_regs_init(id)\
	OPTC_COMMON_REG_LIST_DCN3_2_RI(id)

static struct dcn_optc_registers optc_regs[4];

static const struct dcn_optc_shift optc_shift = {
	OPTC_COMMON_MASK_SH_LIST_DCN3_2(__SHIFT)
};

static const struct dcn_optc_mask optc_mask = {
	OPTC_COMMON_MASK_SH_LIST_DCN3_2(_MASK)
};

#define hubp_regs_init(id) \
	HUBP_REG_LIST_DCN32_RI(id)

static struct dcn_hubp2_registers hubp_regs[4];

static const struct dcn_hubp2_shift hubp_shift = {
		HUBP_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dcn_hubp2_mask hubp_mask = {
		HUBP_MASK_SH_LIST_DCN32(_MASK)
};

static struct dcn_hubbub_registers hubbub_reg;
#define hubbub_reg_init()\
		HUBBUB_REG_LIST_DCN32_RI(0)

static const struct dcn_hubbub_shift hubbub_shift = {
		HUBBUB_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dcn_hubbub_mask hubbub_mask = {
		HUBBUB_MASK_SH_LIST_DCN32(_MASK)
};

static struct dccg_registers dccg_regs;

#define dccg_regs_init()\
	DCCG_REG_LIST_DCN32_RI()

static const struct dccg_shift dccg_shift = {
		DCCG_MASK_SH_LIST_DCN32(__SHIFT)
};

static const struct dccg_mask dccg_mask = {
		DCCG_MASK_SH_LIST_DCN32(_MASK)
};


#define SRII2(reg_name_pre, reg_name_post, id)\
	.reg_name_pre ## _ ##  reg_name_post[id] = BASE(reg ## reg_name_pre \
			## id ## _ ## reg_name_post ## _BASE_IDX) + \
			reg ## reg_name_pre ## id ## _ ## reg_name_post


#define HWSEQ_DCN32_REG_LIST()\
	SR(DCHUBBUB_GLOBAL_TIMER_CNTL), \
	SR(DIO_MEM_PWR_CTRL), \
	SR(ODM_MEM_PWR_CTRL3), \
	SR(MMHUBBUB_MEM_PWR_CNTL), \
	SR(DCCG_GATE_DISABLE_CNTL), \
	SR(DCCG_GATE_DISABLE_CNTL2), \
	SR(DCFCLK_CNTL),\
	SR(DC_MEM_GLOBAL_PWR_REQ_CNTL), \
	SRII(PIXEL_RATE_CNTL, OTG, 0), \
	SRII(PIXEL_RATE_CNTL, OTG, 1),\
	SRII(PIXEL_RATE_CNTL, OTG, 2),\
	SRII(PIXEL_RATE_CNTL, OTG, 3),\
	SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 0),\
	SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 1),\
	SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 2),\
	SRII(PHYPLL_PIXEL_RATE_CNTL, OTG, 3),\
	SR(MICROSECOND_TIME_BASE_DIV), \
	SR(MILLISECOND_TIME_BASE_DIV), \
	SR(DISPCLK_FREQ_CHANGE_CNTL), \
	SR(RBBMIF_TIMEOUT_DIS), \
	SR(RBBMIF_TIMEOUT_DIS_2), \
	SR(DCHUBBUB_CRC_CTRL), \
	SR(DPP_TOP0_DPP_CRC_CTRL), \
	SR(DPP_TOP0_DPP_CRC_VAL_B_A), \
	SR(DPP_TOP0_DPP_CRC_VAL_R_G), \
	SR(MPC_CRC_CTRL), \
	SR(MPC_CRC_RESULT_GB), \
	SR(MPC_CRC_RESULT_C), \
	SR(MPC_CRC_RESULT_AR), \
	SR(DOMAIN0_PG_CONFIG), \
	SR(DOMAIN1_PG_CONFIG), \
	SR(DOMAIN2_PG_CONFIG), \
	SR(DOMAIN3_PG_CONFIG), \
	SR(DOMAIN16_PG_CONFIG), \
	SR(DOMAIN17_PG_CONFIG), \
	SR(DOMAIN18_PG_CONFIG), \
	SR(DOMAIN19_PG_CONFIG), \
	SR(DOMAIN0_PG_STATUS), \
	SR(DOMAIN1_PG_STATUS), \
	SR(DOMAIN2_PG_STATUS), \
	SR(DOMAIN3_PG_STATUS), \
	SR(DOMAIN16_PG_STATUS), \
	SR(DOMAIN17_PG_STATUS), \
	SR(DOMAIN18_PG_STATUS), \
	SR(DOMAIN19_PG_STATUS), \
	SR(D1VGA_CONTROL), \
	SR(D2VGA_CONTROL), \
	SR(D3VGA_CONTROL), \
	SR(D4VGA_CONTROL), \
	SR(D5VGA_CONTROL), \
	SR(D6VGA_CONTROL), \
	SR(DC_IP_REQUEST_CNTL), \
	SR(AZALIA_AUDIO_DTO), \
	SR(AZALIA_CONTROLLER_CLOCK_GATING)

static struct dce_hwseq_registers hwseq_reg;

#define hwseq_reg_init()\
	HWSEQ_DCN32_REG_LIST()

#define HWSEQ_DCN32_MASK_SH_LIST(mask_sh)\
	HWSEQ_DCN_MASK_SH_LIST(mask_sh), \
	HWS_SF(, DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_REFDIV, mask_sh), \
	HWS_SF(, DOMAIN0_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN0_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN1_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN1_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN2_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN2_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN3_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN3_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN16_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN16_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN17_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN17_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN18_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN18_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN19_PG_CONFIG, DOMAIN_POWER_FORCEON, mask_sh), \
	HWS_SF(, DOMAIN19_PG_CONFIG, DOMAIN_POWER_GATE, mask_sh), \
	HWS_SF(, DOMAIN0_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN1_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN2_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN3_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN16_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN17_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN18_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN19_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DC_IP_REQUEST_CNTL, IP_REQUEST_EN, mask_sh), \
	HWS_SF(, AZALIA_AUDIO_DTO, AZALIA_AUDIO_DTO_MODULE, mask_sh), \
	HWS_SF(, HPO_TOP_CLOCK_CONTROL, HPO_HDMISTREAMCLK_G_GATE_DIS, mask_sh), \
	HWS_SF(, ODM_MEM_PWR_CTRL3, ODM_MEM_UNASSIGNED_PWR_MODE, mask_sh), \
	HWS_SF(, ODM_MEM_PWR_CTRL3, ODM_MEM_VBLANK_PWR_MODE, mask_sh), \
	HWS_SF(, MMHUBBUB_MEM_PWR_CNTL, VGA_MEM_PWR_FORCE, mask_sh)

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCN32_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCN32_MASK_SH_LIST(_MASK)
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

static const struct resource_caps res_cap_dcn321 = {
	.num_timing_generator = 4,
	.num_opp = 4,
	.num_video_plane = 4,
	.num_audio = 5,
	.num_stream_encoder = 5,
	.num_hpo_dp_stream_encoder = 4,
	.num_hpo_dp_link_encoder = 2,
	.num_pll = 5,
	.num_dwb = 1,
	.num_ddc = 5,
	.num_vmid = 16,
	.num_mpc_3dlut = 4,
	.num_dsc = 4,
};

static const struct dc_plane_cap plane_cap = {
	.type = DC_PLANE_TYPE_DCN_UNIVERSAL,
	.blends_with_above = true,
	.blends_with_below = true,
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
	.disable_dmcu = true,
	.force_abm_enable = false,
	.timing_trace = false,
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
	.force_disable_subvp = false,
	.exit_idle_opt_for_cursor_updates = true,
	.enable_single_display_2to1_odm_policy = true,

	/*must match enable_single_display_2to1_odm_policy to support dynamic ODM transitions*/
	.enable_double_buffered_dsc_pg_support = true,
	.enable_dp_dig_pixel_rate_div_policy = 1,
	.allow_sw_cursor_fallback = false,
	.alloc_extra_way_for_cursor = true,
	.min_prefetch_in_strobe_ns = 60000, // 60us
};

static const struct dc_debug_options debug_defaults_diags = {
	.disable_dmcu = true,
	.force_abm_enable = false,
	.timing_trace = true,
	.clock_trace = true,
	.disable_dpp_power_gate = true,
	.disable_hubp_power_gate = true,
	.disable_dsc_power_gate = true,
	.disable_clock_gate = true,
	.disable_pplib_clock_request = true,
	.disable_pplib_wm_range = true,
	.disable_stutter = false,
	.scl_reset_length10 = true,
	.dwb_fi_phase = -1, // -1 = disable
	.dmub_command_table = true,
	.enable_tri_buf = true,
	.use_max_lb = true,
	.force_disable_subvp = true
};


static struct dce_aux *dcn321_aux_engine_create(
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
#define i2c_inst_regs_init(id)\
	I2C_HW_ENGINE_COMMON_REG_LIST_DCN30_RI(id)

static struct dce_i2c_registers i2c_hw_regs[5];

static const struct dce_i2c_shift i2c_shifts = {
		I2C_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dce_i2c_mask i2c_masks = {
		I2C_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

static struct dce_i2c_hw *dcn321_i2c_hw_create(
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

static struct clock_source *dcn321_clock_source_create(
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

	if (dcn31_clk_src_construct(clk_src, ctx, bios, id,
			regs, &cs_shift, &cs_mask)) {
		clk_src->base.dp_clk_src = dp_clk_src;
		return &clk_src->base;
	}

	BREAK_TO_DEBUGGER();
	return NULL;
}

static struct hubbub *dcn321_hubbub_create(struct dc_context *ctx)
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

	hubbub32_construct(hubbub2, ctx,
			&hubbub_reg,
			&hubbub_shift,
			&hubbub_mask,
			ctx->dc->dml.ip.det_buffer_size_kbytes,
			ctx->dc->dml.ip.pixel_chunk_size_kbytes,
			ctx->dc->dml.ip.config_return_buffer_size_in_kbytes);


	for (i = 0; i < res_cap_dcn321.num_vmid; i++) {
		struct dcn20_vmid *vmid = &hubbub2->vmid[i];

		vmid->ctx = ctx;

		vmid->regs = &vmid_regs[i];
		vmid->shifts = &vmid_shifts;
		vmid->masks = &vmid_masks;
	}

	return &hubbub2->base;
}

static struct hubp *dcn321_hubp_create(
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

	if (hubp32_construct(hubp2, ctx, inst,
			&hubp_regs[inst], &hubp_shift, &hubp_mask))
		return &hubp2->base;

	BREAK_TO_DEBUGGER();
	kfree(hubp2);
	return NULL;
}

static void dcn321_dpp_destroy(struct dpp **dpp)
{
	kfree(TO_DCN30_DPP(*dpp));
	*dpp = NULL;
}

static struct dpp *dcn321_dpp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn3_dpp *dpp3 =
		kzalloc(sizeof(struct dcn3_dpp), GFP_KERNEL);

	if (!dpp3)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT dpp_regs
	dpp_regs_init(0),
	dpp_regs_init(1),
	dpp_regs_init(2),
	dpp_regs_init(3);

	if (dpp32_construct(dpp3, ctx, inst,
			&dpp_regs[inst], &tf_shift, &tf_mask))
		return &dpp3->base;

	BREAK_TO_DEBUGGER();
	kfree(dpp3);
	return NULL;
}

static struct mpc *dcn321_mpc_create(
		struct dc_context *ctx,
		int num_mpcc,
		int num_rmu)
{
	struct dcn30_mpc *mpc30 = kzalloc(sizeof(struct dcn30_mpc),
					  GFP_KERNEL);

	if (!mpc30)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT mpc_regs
	dcn_mpc_regs_init();

	dcn32_mpc_construct(mpc30, ctx,
			&mpc_regs,
			&mpc_shift,
			&mpc_mask,
			num_mpcc,
			num_rmu);

	return &mpc30->base;
}

static struct output_pixel_processor *dcn321_opp_create(
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

	dcn20_opp_construct(opp2, ctx, inst,
			&opp_regs[inst], &opp_shift, &opp_mask);
	return &opp2->base;
}


static struct timing_generator *dcn321_timing_generator_create(
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

	dcn32_timing_generator_init(tgn10);

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

static struct link_encoder *dcn321_link_encoder_create(
	struct dc_context *ctx,
	const struct encoder_init_data *enc_init_data)
{
	struct dcn20_link_encoder *enc20 =
		kzalloc(sizeof(struct dcn20_link_encoder), GFP_KERNEL);

	if (!enc20)
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

	dcn321_link_encoder_construct(enc20,
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
	generic_reg_get(ctx, ctx->dcn_reg_offsets[regDC_PINSTRAPS_BASE_IDX] + regDC_PINSTRAPS,
		FN(DC_PINSTRAPS, DC_PINSTRAPS_AUDIO), &straps->dc_pinstraps_audio);

}

static struct audio *dcn321_create_audio(
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

static struct vpg *dcn321_vpg_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn30_vpg *vpg3 = kzalloc(sizeof(struct dcn30_vpg), GFP_KERNEL);

	if (!vpg3)
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

	vpg3_construct(vpg3, ctx, inst,
			&vpg_regs[inst],
			&vpg_shift,
			&vpg_mask);

	return &vpg3->base;
}

static struct afmt *dcn321_afmt_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn30_afmt *afmt3 = kzalloc(sizeof(struct dcn30_afmt), GFP_KERNEL);

	if (!afmt3)
		return NULL;

#undef REG_STRUCT
#define REG_STRUCT afmt_regs
	afmt_regs_init(0),
	afmt_regs_init(1),
	afmt_regs_init(2),
	afmt_regs_init(3),
	afmt_regs_init(4),
	afmt_regs_init(5);

	afmt3_construct(afmt3, ctx, inst,
			&afmt_regs[inst],
			&afmt_shift,
			&afmt_mask);

	return &afmt3->base;
}

static struct apg *dcn321_apg_create(
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
	apg_regs_init(3);

	apg31_construct(apg31, ctx, inst,
			&apg_regs[inst],
			&apg_shift,
			&apg_mask);

	return &apg31->base;
}

static struct stream_encoder *dcn321_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dcn10_stream_encoder *enc1;
	struct vpg *vpg;
	struct afmt *afmt;
	int vpg_inst;
	int afmt_inst;

	/* Mapping of VPG, AFMT, DME register blocks to DIO block instance */
	if (eng_id <= ENGINE_ID_DIGF) {
		vpg_inst = eng_id;
		afmt_inst = eng_id;
	} else
		return NULL;

	enc1 = kzalloc(sizeof(struct dcn10_stream_encoder), GFP_KERNEL);
	vpg = dcn321_vpg_create(ctx, vpg_inst);
	afmt = dcn321_afmt_create(ctx, afmt_inst);

	if (!enc1 || !vpg || !afmt) {
		kfree(enc1);
		kfree(vpg);
		kfree(afmt);
		return NULL;
	}

#undef REG_STRUCT
#define REG_STRUCT stream_enc_regs
	stream_enc_regs_init(0),
	stream_enc_regs_init(1),
	stream_enc_regs_init(2),
	stream_enc_regs_init(3),
	stream_enc_regs_init(4);

	dcn32_dio_stream_encoder_construct(enc1, ctx, ctx->dc_bios,
					eng_id, vpg, afmt,
					&stream_enc_regs[eng_id],
					&se_shift, &se_mask);

	return &enc1->base;
}

static struct hpo_dp_stream_encoder *dcn321_hpo_dp_stream_encoder_create(
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
	 * VPG[6] -> HPO_DP[0]
	 * VPG[7] -> HPO_DP[1]
	 * VPG[8] -> HPO_DP[2]
	 * VPG[9] -> HPO_DP[3]
	 */
	vpg_inst = hpo_dp_inst + 6;

	/* Mapping of APG register blocks to HPO DP block instance:
	 * APG[0] -> HPO_DP[0]
	 * APG[1] -> HPO_DP[1]
	 * APG[2] -> HPO_DP[2]
	 * APG[3] -> HPO_DP[3]
	 */
	apg_inst = hpo_dp_inst;

	/* allocate HPO stream encoder and create VPG sub-block */
	hpo_dp_enc31 = kzalloc(sizeof(struct dcn31_hpo_dp_stream_encoder), GFP_KERNEL);
	vpg = dcn321_vpg_create(ctx, vpg_inst);
	apg = dcn321_apg_create(ctx, apg_inst);

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

static struct hpo_dp_link_encoder *dcn321_hpo_dp_link_encoder_create(
	uint8_t inst,
	struct dc_context *ctx)
{
	struct dcn31_hpo_dp_link_encoder *hpo_dp_enc31;

	/* allocate HPO link encoder */
	hpo_dp_enc31 = kzalloc(sizeof(struct dcn31_hpo_dp_link_encoder), GFP_KERNEL);

#undef REG_STRUCT
#define REG_STRUCT hpo_dp_link_enc_regs
	hpo_dp_link_encoder_reg_init(0),
	hpo_dp_link_encoder_reg_init(1);

	hpo_dp_link_encoder32_construct(hpo_dp_enc31, ctx, inst,
					&hpo_dp_link_enc_regs[inst],
					&hpo_dp_le_shift, &hpo_dp_le_mask);

	return &hpo_dp_enc31->base;
}

static struct dce_hwseq *dcn321_hwseq_create(
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
	.create_audio = dcn321_create_audio,
	.create_stream_encoder = dcn321_stream_encoder_create,
	.create_hpo_dp_stream_encoder = dcn321_hpo_dp_stream_encoder_create,
	.create_hpo_dp_link_encoder = dcn321_hpo_dp_link_encoder_create,
	.create_hwseq = dcn321_hwseq_create,
};

static const struct resource_create_funcs res_create_maximus_funcs = {
	.read_dce_straps = NULL,
	.create_audio = NULL,
	.create_stream_encoder = NULL,
	.create_hpo_dp_stream_encoder = dcn321_hpo_dp_stream_encoder_create,
	.create_hpo_dp_link_encoder = dcn321_hpo_dp_link_encoder_create,
	.create_hwseq = dcn321_hwseq_create,
};

static void dcn321_resource_destruct(struct dcn321_resource_pool *pool)
{
	unsigned int i;

	for (i = 0; i < pool->base.stream_enc_count; i++) {
		if (pool->base.stream_enc[i] != NULL) {
			if (pool->base.stream_enc[i]->vpg != NULL) {
				kfree(DCN30_VPG_FROM_VPG(pool->base.stream_enc[i]->vpg));
				pool->base.stream_enc[i]->vpg = NULL;
			}
			if (pool->base.stream_enc[i]->afmt != NULL) {
				kfree(DCN30_AFMT_FROM_AFMT(pool->base.stream_enc[i]->afmt));
				pool->base.stream_enc[i]->afmt = NULL;
			}
			kfree(DCN10STRENC_FROM_STRENC(pool->base.stream_enc[i]));
			pool->base.stream_enc[i] = NULL;
		}
	}

	for (i = 0; i < pool->base.hpo_dp_stream_enc_count; i++) {
		if (pool->base.hpo_dp_stream_enc[i] != NULL) {
			if (pool->base.hpo_dp_stream_enc[i]->vpg != NULL) {
				kfree(DCN30_VPG_FROM_VPG(pool->base.hpo_dp_stream_enc[i]->vpg));
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
			dcn20_dsc_destroy(&pool->base.dscs[i]);
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
			dcn321_dpp_destroy(&pool->base.dpps[i]);

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
		if (pool->base.timing_generators[i] != NULL)	{
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

	if (pool->base.dccg != NULL)
		dcn_dccg_destroy(&pool->base.dccg);

	if (pool->base.oem_device != NULL)
		dal_ddc_service_destroy(&pool->base.oem_device);
}


static bool dcn321_dwbc_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t dwb_count = pool->res_cap->num_dwb;

	for (i = 0; i < dwb_count; i++) {
		struct dcn30_dwbc *dwbc30 = kzalloc(sizeof(struct dcn30_dwbc),
						    GFP_KERNEL);

		if (!dwbc30) {
			dm_error("DC: failed to create dwbc30!\n");
			return false;
		}

#undef REG_STRUCT
#define REG_STRUCT dwbc30_regs
		dwbc_regs_dcn3_init(0);

		dcn30_dwbc_construct(dwbc30, ctx,
				&dwbc30_regs[i],
				&dwbc30_shift,
				&dwbc30_mask,
				i);

		pool->dwbc[i] = &dwbc30->base;
	}
	return true;
}

static bool dcn321_mmhubbub_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t dwb_count = pool->res_cap->num_dwb;

	for (i = 0; i < dwb_count; i++) {
		struct dcn30_mmhubbub *mcif_wb30 = kzalloc(sizeof(struct dcn30_mmhubbub),
						    GFP_KERNEL);

		if (!mcif_wb30) {
			dm_error("DC: failed to create mcif_wb30!\n");
			return false;
		}

#undef REG_STRUCT
#define REG_STRUCT mcif_wb30_regs
		mcif_wb_regs_dcn3_init(0);

		dcn32_mmhubbub_construct(mcif_wb30, ctx,
				&mcif_wb30_regs[i],
				&mcif_wb30_shift,
				&mcif_wb30_mask,
				i);

		pool->mcif_wb[i] = &mcif_wb30->base;
	}
	return true;
}

static struct display_stream_compressor *dcn321_dsc_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_dsc *dsc =
		kzalloc(sizeof(struct dcn20_dsc), GFP_KERNEL);

	if (!dsc) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

#undef REG_STRUCT
#define REG_STRUCT dsc_regs
	dsc_regsDCN20_init(0),
	dsc_regsDCN20_init(1),
	dsc_regsDCN20_init(2),
	dsc_regsDCN20_init(3);

	dsc2_construct(dsc, ctx, inst, &dsc_regs[inst], &dsc_shift, &dsc_mask);

	dsc->max_image_width = 6016;

	return &dsc->base;
}

static void dcn321_destroy_resource_pool(struct resource_pool **pool)
{
	struct dcn321_resource_pool *dcn321_pool = TO_DCN321_RES_POOL(*pool);

	dcn321_resource_destruct(dcn321_pool);
	kfree(dcn321_pool);
	*pool = NULL;
}

static struct dc_cap_funcs cap_funcs = {
	.get_dcc_compression_cap = dcn20_get_dcc_compression_cap
};

static void dcn321_update_bw_bounding_box(struct dc *dc, struct clk_bw_params *bw_params)
{
	DC_FP_START();
	dcn321_update_bw_bounding_box_fpu(dc, bw_params);
	DC_FP_END();
}

static struct resource_funcs dcn321_res_pool_funcs = {
	.destroy = dcn321_destroy_resource_pool,
	.link_enc_create = dcn321_link_encoder_create,
	.link_enc_create_minimal = NULL,
	.panel_cntl_create = dcn32_panel_cntl_create,
	.validate_bandwidth = dcn32_validate_bandwidth,
	.calculate_wm_and_dlg = dcn32_calculate_wm_and_dlg,
	.populate_dml_pipes = dcn32_populate_dml_pipes_from_context,
	.acquire_idle_pipe_for_head_pipe_in_layer = dcn32_acquire_idle_pipe_for_head_pipe_in_layer,
	.add_stream_to_ctx = dcn30_add_stream_to_ctx,
	.add_dsc_to_stream_resource = dcn20_add_dsc_to_stream_resource,
	.remove_stream_from_ctx = dcn20_remove_stream_from_ctx,
	.populate_dml_writeback_from_context = dcn30_populate_dml_writeback_from_context,
	.set_mcif_arb_params = dcn30_set_mcif_arb_params,
	.find_first_free_match_stream_enc_for_link = dcn10_find_first_free_match_stream_enc_for_link,
	.acquire_post_bldn_3dlut = dcn32_acquire_post_bldn_3dlut,
	.release_post_bldn_3dlut = dcn32_release_post_bldn_3dlut,
	.update_bw_bounding_box = dcn321_update_bw_bounding_box,
	.patch_unknown_plane_state = dcn20_patch_unknown_plane_state,
	.update_soc_for_wm_a = dcn30_update_soc_for_wm_a,
	.add_phantom_pipes = dcn32_add_phantom_pipes,
	.remove_phantom_pipes = dcn32_remove_phantom_pipes,
	.retain_phantom_pipes = dcn32_retain_phantom_pipes,
};

static uint32_t read_pipe_fuses(struct dc_context *ctx)
{
	uint32_t value = REG_READ(CC_DC_PIPE_DIS);
	/* DCN321 support max 4 pipes */
	value = value & 0xf;
	return value;
}


static bool dcn321_resource_construct(
	uint8_t num_virtual_links,
	struct dc *dc,
	struct dcn321_resource_pool *pool)
{
	int i, j;
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

	pool->base.res_cap = &res_cap_dcn321;
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

	/* within dml lib, initial value is hard coded, if ASIC pipe is fused, the
	 * value will be changed, update max_num_dpp and max_num_otg for dml.
	 */
	dcn3_21_ip.max_num_dpp = num_pipes;
	dcn3_21_ip.max_num_otg = num_pipes;

	pool->base.funcs = &dcn321_res_pool_funcs;

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;
	pool->base.timing_generator_count = num_pipes;
	pool->base.pipe_count = num_pipes;
	pool->base.mpcc_count = num_pipes;
	dc->caps.max_downscale_ratio = 600;
	dc->caps.i2c_speed_in_khz = 100;
	dc->caps.i2c_speed_in_khz_hdcp = 100; /*1.4 w/a applied by default*/
	/* TODO: Bring max cursor size back to 256 after subvp cursor corruption is fixed*/
	dc->caps.max_cursor_size = 64;
	dc->caps.min_horizontal_blanking_period = 80;
	dc->caps.dmdata_alloc_size = 2048;
	dc->caps.mall_size_per_mem_channel = 4;
	dc->caps.mall_size_total = 0;
	dc->caps.cursor_cache_size = dc->caps.max_cursor_size * dc->caps.max_cursor_size * 8;
	dc->caps.cache_line_size = 64;
	dc->caps.cache_num_ways = 16;

	/* Calculate the available MALL space */
	dc->caps.max_cab_allocation_bytes = dcn32_calc_num_avail_chans_for_mall(
		dc, dc->ctx->dc_bios->vram_info.num_chans) *
		dc->caps.mall_size_per_mem_channel * 1024 * 1024;
	dc->caps.mall_size_total = dc->caps.max_cab_allocation_bytes;

	dc->caps.max_cab_allocation_bytes = 33554432; // 32MB = 1024 * 1024 * 32
	dc->caps.subvp_fw_processing_delay_us = 15;
	dc->caps.subvp_prefetch_end_to_mall_start_us = 15;
	dc->caps.subvp_swath_height_margin_lines = 16;
	dc->caps.subvp_pstate_allow_width_us = 20;
	dc->caps.subvp_vertical_int_margin_us = 30;
	dc->caps.max_slave_planes = 1;
	dc->caps.max_slave_yuv_planes = 1;
	dc->caps.max_slave_rgb_planes = 1;
	dc->caps.post_blend_color_processing = true;
	dc->caps.force_dp_tps4_for_cp2520 = true;
	dc->caps.dp_hpo = true;
	dc->caps.dp_hdmi21_pcon_support = true;
	dc->caps.edp_dsc_support = true;
	dc->caps.extended_aux_timeout_support = true;
	dc->caps.dmcub_support = true;

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

	dc->caps.color.dpp.hw_3d_lut = 1;
	dc->caps.color.dpp.ogam_ram = 1;
	// no OGAM ROM on DCN2 and later ASICs
	dc->caps.color.dpp.ogam_rom_caps.srgb = 0;
	dc->caps.color.dpp.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.dpp.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.dpp.ogam_rom_caps.pq = 0;
	dc->caps.color.dpp.ogam_rom_caps.hlg = 0;
	dc->caps.color.dpp.ocsc = 0;

	dc->caps.color.mpc.gamut_remap = 1;
	dc->caps.color.mpc.num_3dluts = pool->base.res_cap->num_mpc_3dlut; //4, configurable to be before or after BLND in MPCC
	dc->caps.color.mpc.ogam_ram = 1;
	dc->caps.color.mpc.ogam_rom_caps.srgb = 0;
	dc->caps.color.mpc.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.mpc.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.mpc.ogam_rom_caps.pq = 0;
	dc->caps.color.mpc.ogam_rom_caps.hlg = 0;
	dc->caps.color.mpc.ocsc = 1;

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

	if (dc->ctx->dce_environment == DCE_ENV_PRODUCTION_DRV)
		dc->debug = debug_defaults_drv;
	else if (dc->ctx->dce_environment == DCE_ENV_FPGA_MAXIMUS) {
		dc->debug = debug_defaults_diags;
	} else
		dc->debug = debug_defaults_diags;
	// Init the vm_helper
	if (dc->vm_helper)
		vm_helper_init(dc->vm_helper, 16);

	/*************************************************
	 *  Create resources                             *
	 *************************************************/

	/* Clock Sources for Pixel Clock*/
	pool->base.clock_sources[DCN321_CLK_SRC_PLL0] =
			dcn321_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL0,
				&clk_src_regs[0], false);
	pool->base.clock_sources[DCN321_CLK_SRC_PLL1] =
			dcn321_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL1,
				&clk_src_regs[1], false);
	pool->base.clock_sources[DCN321_CLK_SRC_PLL2] =
			dcn321_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL2,
				&clk_src_regs[2], false);
	pool->base.clock_sources[DCN321_CLK_SRC_PLL3] =
			dcn321_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL3,
				&clk_src_regs[3], false);
	pool->base.clock_sources[DCN321_CLK_SRC_PLL4] =
			dcn321_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL4,
				&clk_src_regs[4], false);

	pool->base.clk_src_count = DCN321_CLK_SRC_TOTAL;

	/* todo: not reuse phy_pll registers */
	pool->base.dp_clock_source =
			dcn321_clock_source_create(ctx, ctx->dc_bios,
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
	pool->base.dccg = dccg32_create(ctx, &dccg_regs, &dccg_shift, &dccg_mask);
	if (pool->base.dccg == NULL) {
		dm_error("DC: failed to create dccg!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* DML */
	if (!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment))
		dml_init_instance(&dc->dml, &dcn3_21_soc, &dcn3_21_ip, DML_PROJECT_DCN32);

	/* IRQ Service */
	init_data.ctx = dc->ctx;
	pool->base.irqs = dal_irq_service_dcn32_create(&init_data);
	if (!pool->base.irqs)
		goto create_fail;

	/* HUBBUB */
	pool->base.hubbub = dcn321_hubbub_create(ctx);
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

		pool->base.hubps[j] = dcn321_hubp_create(ctx, i);
		if (pool->base.hubps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create hubps!\n");
			goto create_fail;
		}

		pool->base.dpps[j] = dcn321_dpp_create(ctx, i);
		if (pool->base.dpps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create dpps!\n");
			goto create_fail;
		}

		pool->base.opps[j] = dcn321_opp_create(ctx, i);
		if (pool->base.opps[j] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto create_fail;
		}

		pool->base.timing_generators[j] = dcn321_timing_generator_create(
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
	pool->base.mpc = dcn321_mpc_create(ctx,  pool->base.res_cap->num_timing_generator, pool->base.res_cap->num_mpc_3dlut);
	if (pool->base.mpc == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mpc!\n");
		goto create_fail;
	}

	/* DSCs */
	for (i = 0; i < pool->base.res_cap->num_dsc; i++) {
		pool->base.dscs[i] = dcn321_dsc_create(ctx, i);
		if (pool->base.dscs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create display stream compressor %d!\n", i);
			goto create_fail;
		}
	}

	/* DWB */
	if (!dcn321_dwbc_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create dwbc!\n");
		goto create_fail;
	}

	/* MMHUBBUB */
	if (!dcn321_mmhubbub_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mcif_wb!\n");
		goto create_fail;
	}

	/* AUX and I2C */
	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		pool->base.engines[i] = dcn321_aux_engine_create(ctx, i);
		if (pool->base.engines[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create aux engine!!\n");
			goto create_fail;
		}
		pool->base.hw_i2cs[i] = dcn321_i2c_hw_create(ctx, i);
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
			(!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment) ?
			&res_create_funcs : &res_create_maximus_funcs)))
			goto create_fail;

	/* HW Sequencer init functions and Plane caps */
	dcn32_hw_sequencer_init_functions(dc);

	dc->caps.max_planes =  pool->base.pipe_count;

	for (i = 0; i < dc->caps.max_planes; ++i)
		dc->caps.planes[i] = plane_cap;

	dc->cap_funcs = cap_funcs;

	if (dc->ctx->dc_bios->fw_info.oem_i2c_present) {
		ddc_init_data.ctx = dc->ctx;
		ddc_init_data.link = NULL;
		ddc_init_data.id.id = dc->ctx->dc_bios->fw_info.oem_i2c_obj_id;
		ddc_init_data.id.enum_id = 0;
		ddc_init_data.id.type = OBJECT_TYPE_GENERIC;
		pool->base.oem_device = dal_ddc_service_create(&ddc_init_data);
	} else {
		pool->base.oem_device = NULL;
	}

	return true;

create_fail:

	dcn321_resource_destruct(pool);

	return false;
}

struct resource_pool *dcn321_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc)
{
	struct dcn321_resource_pool *pool =
		kzalloc(sizeof(struct dcn321_resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (dcn321_resource_construct(init_data->num_virtual_links, dc, pool))
		return &pool->base;

	BREAK_TO_DEBUGGER();
	kfree(pool);
	return NULL;
}
