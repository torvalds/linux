/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#include "dcn31/dcn31_init.h"

#include "resource.h"
#include "include/irq_service_interface.h"
#include "dcn315_resource.h"

#include "dcn20/dcn20_resource.h"
#include "dcn30/dcn30_resource.h"
#include "dcn31/dcn31_resource.h"

#include "dcn10/dcn10_ipp.h"
#include "dcn30/dcn30_hubbub.h"
#include "dcn31/dcn31_hubbub.h"
#include "dcn30/dcn30_mpc.h"
#include "dcn31/dcn31_hubp.h"
#include "irq/dcn315/irq_service_dcn315.h"
#include "dcn30/dcn30_dpp.h"
#include "dcn31/dcn31_optc.h"
#include "dcn20/dcn20_hwseq.h"
#include "dcn30/dcn30_hwseq.h"
#include "dce110/dce110_hw_sequencer.h"
#include "dcn30/dcn30_opp.h"
#include "dcn20/dcn20_dsc.h"
#include "dcn30/dcn30_vpg.h"
#include "dcn30/dcn30_afmt.h"
#include "dcn30/dcn30_dio_stream_encoder.h"
#include "dcn31/dcn31_hpo_dp_stream_encoder.h"
#include "dcn31/dcn31_hpo_dp_link_encoder.h"
#include "dcn31/dcn31_apg.h"
#include "dcn31/dcn31_dio_link_encoder.h"
#include "dcn31/dcn31_vpg.h"
#include "dcn31/dcn31_afmt.h"
#include "dce/dce_clock_source.h"
#include "dce/dce_audio.h"
#include "dce/dce_hwseq.h"
#include "clk_mgr.h"
#include "virtual/virtual_stream_encoder.h"
#include "dce110/dce110_resource.h"
#include "dml/display_mode_vba.h"
#include "dml/dcn31/dcn31_fpu.h"
#include "dcn31/dcn31_dccg.h"
#include "dcn10/dcn10_resource.h"
#include "dcn31/dcn31_panel_cntl.h"

#include "dcn30/dcn30_dwb.h"
#include "dcn30/dcn30_mmhubbub.h"

#include "dcn/dcn_3_1_5_offset.h"
#include "dcn/dcn_3_1_5_sh_mask.h"
#include "dpcs/dpcs_4_2_2_offset.h"
#include "dpcs/dpcs_4_2_2_sh_mask.h"

#define NBIO_BASE__INST0_SEG0                      0x00000000
#define NBIO_BASE__INST0_SEG1                      0x00000014
#define NBIO_BASE__INST0_SEG2                      0x00000D20
#define NBIO_BASE__INST0_SEG3                      0x00010400
#define NBIO_BASE__INST0_SEG4                      0x0241B000
#define NBIO_BASE__INST0_SEG5                      0x04040000

#define DPCS_BASE__INST0_SEG0                      0x00000012
#define DPCS_BASE__INST0_SEG1                      0x000000C0
#define DPCS_BASE__INST0_SEG2                      0x000034C0
#define DPCS_BASE__INST0_SEG3                      0x00009000
#define DPCS_BASE__INST0_SEG4                      0x02403C00
#define DPCS_BASE__INST0_SEG5                      0

#define DCN_BASE__INST0_SEG0                       0x00000012
#define DCN_BASE__INST0_SEG1                       0x000000C0
#define DCN_BASE__INST0_SEG2                       0x000034C0
#define DCN_BASE__INST0_SEG3                       0x00009000
#define DCN_BASE__INST0_SEG4                       0x02403C00
#define DCN_BASE__INST0_SEG5                       0

#define regBIF_BX_PF2_RSMU_INDEX                                                                        0x0000
#define regBIF_BX_PF2_RSMU_INDEX_BASE_IDX                                                               1
#define regBIF_BX_PF2_RSMU_DATA                                                                         0x0001
#define regBIF_BX_PF2_RSMU_DATA_BASE_IDX                                                                1
#define regBIF_BX2_BIOS_SCRATCH_6                                                                       0x003e
#define regBIF_BX2_BIOS_SCRATCH_6_BASE_IDX                                                              1
#define BIF_BX2_BIOS_SCRATCH_6__BIOS_SCRATCH_6__SHIFT                                                         0x0
#define BIF_BX2_BIOS_SCRATCH_6__BIOS_SCRATCH_6_MASK                                                           0xFFFFFFFFL
#define regBIF_BX2_BIOS_SCRATCH_2                                                                       0x003a
#define regBIF_BX2_BIOS_SCRATCH_2_BASE_IDX                                                              1
#define BIF_BX2_BIOS_SCRATCH_2__BIOS_SCRATCH_2__SHIFT                                                         0x0
#define BIF_BX2_BIOS_SCRATCH_2__BIOS_SCRATCH_2_MASK                                                           0xFFFFFFFFL
#define regBIF_BX2_BIOS_SCRATCH_3                                                                       0x003b
#define regBIF_BX2_BIOS_SCRATCH_3_BASE_IDX                                                              1
#define BIF_BX2_BIOS_SCRATCH_3__BIOS_SCRATCH_3__SHIFT                                                         0x0
#define BIF_BX2_BIOS_SCRATCH_3__BIOS_SCRATCH_3_MASK                                                           0xFFFFFFFFL

#define regDCHUBBUB_DEBUG_CTRL_0                                              0x04d6
#define regDCHUBBUB_DEBUG_CTRL_0_BASE_IDX                                     2
#define DCHUBBUB_DEBUG_CTRL_0__DET_DEPTH__SHIFT                               0x10
#define DCHUBBUB_DEBUG_CTRL_0__DET_DEPTH_MASK                                 0x01FF0000L

#include "reg_helper.h"
#include "dce/dmub_abm.h"
#include "dce/dmub_psr.h"
#include "dce/dce_aux.h"
#include "dce/dce_i2c.h"

#include "dml/dcn30/display_mode_vba_30.h"
#include "vm_helper.h"
#include "dcn20/dcn20_vmid.h"

#include "link_enc_cfg.h"

#define DCN3_15_MAX_DET_SIZE 384
#define DCN3_15_CRB_SEGMENT_SIZE_KB 64

enum dcn31_clk_src_array_id {
	DCN31_CLK_SRC_PLL0,
	DCN31_CLK_SRC_PLL1,
	DCN31_CLK_SRC_PLL2,
	DCN31_CLK_SRC_PLL3,
	DCN31_CLK_SRC_PLL4,
	DCN30_CLK_SRC_TOTAL
};

/* begin *********************
 * macros to expend register list macro defined in HW object header file
 */

/* DCN */
/* TODO awful hack. fixup dcn20_dwb.h */
#undef BASE_INNER
#define BASE_INNER(seg) DCN_BASE__INST0_SEG ## seg

#define BASE(seg) BASE_INNER(seg)

#define SR(reg_name)\
		.reg_name = BASE(reg ## reg_name ## _BASE_IDX) +  \
					reg ## reg_name

#define SRI(reg_name, block, id)\
	.reg_name = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					reg ## block ## id ## _ ## reg_name

#define SRI2(reg_name, block, id)\
	.reg_name = BASE(reg ## reg_name ## _BASE_IDX) + \
					reg ## reg_name

#define SRIR(var_name, reg_name, block, id)\
	.var_name = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					reg ## block ## id ## _ ## reg_name

#define SRII(reg_name, block, id)\
	.reg_name[id] = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					reg ## block ## id ## _ ## reg_name

#define SRII_MPC_RMU(reg_name, block, id)\
	.RMU##_##reg_name[id] = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					reg ## block ## id ## _ ## reg_name

#define SRII_DWB(reg_name, temp_name, block, id)\
	.reg_name[id] = BASE(reg ## block ## id ## _ ## temp_name ## _BASE_IDX) + \
					reg ## block ## id ## _ ## temp_name

#define DCCG_SRII(reg_name, block, id)\
	.block ## _ ## reg_name[id] = BASE(reg ## block ## id ## _ ## reg_name ## _BASE_IDX) + \
					reg ## block ## id ## _ ## reg_name

#define VUPDATE_SRII(reg_name, block, id)\
	.reg_name[id] = BASE(reg ## reg_name ## _ ## block ## id ## _BASE_IDX) + \
					reg ## reg_name ## _ ## block ## id

/* NBIO */
#define NBIO_BASE_INNER(seg) \
	NBIO_BASE__INST0_SEG ## seg

#define NBIO_BASE(seg) \
	NBIO_BASE_INNER(seg)

#define NBIO_SR(reg_name)\
		.reg_name = NBIO_BASE(regBIF_BX2_ ## reg_name ## _BASE_IDX) + \
					regBIF_BX2_ ## reg_name

static const struct bios_registers bios_regs = {
		NBIO_SR(BIOS_SCRATCH_3),
		NBIO_SR(BIOS_SCRATCH_6)
};

#define clk_src_regs(index, pllid)\
[index] = {\
	CS_COMMON_REG_LIST_DCN3_0(index, pllid),\
}

static const struct dce110_clk_src_regs clk_src_regs[] = {
	clk_src_regs(0, A),
	clk_src_regs(1, B),
	clk_src_regs(2, C),
	clk_src_regs(3, D),
	clk_src_regs(4, E)
};

static const struct dce110_clk_src_shift cs_shift = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT)
};

static const struct dce110_clk_src_mask cs_mask = {
		CS_COMMON_MASK_SH_LIST_DCN2_0(_MASK)
};

#define abm_regs(id)\
[id] = {\
		ABM_DCN302_REG_LIST(id)\
}

static const struct dce_abm_registers abm_regs[] = {
		abm_regs(0),
		abm_regs(1),
		abm_regs(2),
		abm_regs(3),
};

static const struct dce_abm_shift abm_shift = {
		ABM_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dce_abm_mask abm_mask = {
		ABM_MASK_SH_LIST_DCN30(_MASK)
};

#define audio_regs(id)\
[id] = {\
		AUD_COMMON_REG_LIST(id)\
}

static const struct dce_audio_registers audio_regs[] = {
	audio_regs(0),
	audio_regs(1),
	audio_regs(2),
	audio_regs(3),
	audio_regs(4),
	audio_regs(5),
	audio_regs(6)
};

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

#define vpg_regs(id)\
[id] = {\
	VPG_DCN31_REG_LIST(id)\
}

static const struct dcn31_vpg_registers vpg_regs[] = {
	vpg_regs(0),
	vpg_regs(1),
	vpg_regs(2),
	vpg_regs(3),
	vpg_regs(4),
	vpg_regs(5),
	vpg_regs(6),
	vpg_regs(7),
	vpg_regs(8),
	vpg_regs(9),
};

static const struct dcn31_vpg_shift vpg_shift = {
	DCN31_VPG_MASK_SH_LIST(__SHIFT)
};

static const struct dcn31_vpg_mask vpg_mask = {
	DCN31_VPG_MASK_SH_LIST(_MASK)
};

#define afmt_regs(id)\
[id] = {\
	AFMT_DCN31_REG_LIST(id)\
}

static const struct dcn31_afmt_registers afmt_regs[] = {
	afmt_regs(0),
	afmt_regs(1),
	afmt_regs(2),
	afmt_regs(3),
	afmt_regs(4),
	afmt_regs(5)
};

static const struct dcn31_afmt_shift afmt_shift = {
	DCN31_AFMT_MASK_SH_LIST(__SHIFT)
};

static const struct dcn31_afmt_mask afmt_mask = {
	DCN31_AFMT_MASK_SH_LIST(_MASK)
};

#define apg_regs(id)\
[id] = {\
	APG_DCN31_REG_LIST(id)\
}

static const struct dcn31_apg_registers apg_regs[] = {
	apg_regs(0),
	apg_regs(1),
	apg_regs(2),
	apg_regs(3)
};

static const struct dcn31_apg_shift apg_shift = {
	DCN31_APG_MASK_SH_LIST(__SHIFT)
};

static const struct dcn31_apg_mask apg_mask = {
		DCN31_APG_MASK_SH_LIST(_MASK)
};

#define stream_enc_regs(id)\
[id] = {\
	SE_DCN3_REG_LIST(id)\
}

static const struct dcn10_stream_enc_registers stream_enc_regs[] = {
	stream_enc_regs(0),
	stream_enc_regs(1),
	stream_enc_regs(2),
	stream_enc_regs(3),
	stream_enc_regs(4)
};

static const struct dcn10_stream_encoder_shift se_shift = {
		SE_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn10_stream_encoder_mask se_mask = {
		SE_COMMON_MASK_SH_LIST_DCN30(_MASK)
};


#define aux_regs(id)\
[id] = {\
	DCN2_AUX_REG_LIST(id)\
}

static const struct dcn10_link_enc_aux_registers link_enc_aux_regs[] = {
		aux_regs(0),
		aux_regs(1),
		aux_regs(2),
		aux_regs(3),
		aux_regs(4)
};

#define hpd_regs(id)\
[id] = {\
	HPD_REG_LIST(id)\
}

static const struct dcn10_link_enc_hpd_registers link_enc_hpd_regs[] = {
		hpd_regs(0),
		hpd_regs(1),
		hpd_regs(2),
		hpd_regs(3),
		hpd_regs(4)
};

#define link_regs(id, phyid)\
[id] = {\
	LE_DCN31_REG_LIST(id), \
	UNIPHY_DCN2_REG_LIST(phyid), \
	DPCS_DCN31_REG_LIST(id), \
}

static const struct dce110_aux_registers_shift aux_shift = {
	DCN_AUX_MASK_SH_LIST(__SHIFT)
};

static const struct dce110_aux_registers_mask aux_mask = {
	DCN_AUX_MASK_SH_LIST(_MASK)
};

static const struct dcn10_link_enc_registers link_enc_regs[] = {
	link_regs(0, A),
	link_regs(1, B),
	link_regs(2, C),
	link_regs(3, D),
	link_regs(4, E)
};

static const struct dcn10_link_enc_shift le_shift = {
	LINK_ENCODER_MASK_SH_LIST_DCN31(__SHIFT), \
	DPCS_DCN31_MASK_SH_LIST(__SHIFT)
};

static const struct dcn10_link_enc_mask le_mask = {
	LINK_ENCODER_MASK_SH_LIST_DCN31(_MASK), \
	DPCS_DCN31_MASK_SH_LIST(_MASK)
};

#define hpo_dp_stream_encoder_reg_list(id)\
[id] = {\
	DCN3_1_HPO_DP_STREAM_ENC_REG_LIST(id)\
}

static const struct dcn31_hpo_dp_stream_encoder_registers hpo_dp_stream_enc_regs[] = {
	hpo_dp_stream_encoder_reg_list(0),
	hpo_dp_stream_encoder_reg_list(1),
	hpo_dp_stream_encoder_reg_list(2),
	hpo_dp_stream_encoder_reg_list(3),
};

static const struct dcn31_hpo_dp_stream_encoder_shift hpo_dp_se_shift = {
	DCN3_1_HPO_DP_STREAM_ENC_MASK_SH_LIST(__SHIFT)
};

static const struct dcn31_hpo_dp_stream_encoder_mask hpo_dp_se_mask = {
	DCN3_1_HPO_DP_STREAM_ENC_MASK_SH_LIST(_MASK)
};


#define hpo_dp_link_encoder_reg_list(id)\
[id] = {\
	DCN3_1_HPO_DP_LINK_ENC_REG_LIST(id),\
	DCN3_1_RDPCSTX_REG_LIST(0),\
	DCN3_1_RDPCSTX_REG_LIST(1),\
	DCN3_1_RDPCSTX_REG_LIST(2),\
	DCN3_1_RDPCSTX_REG_LIST(3),\
	DCN3_1_RDPCSTX_REG_LIST(4)\
}

static const struct dcn31_hpo_dp_link_encoder_registers hpo_dp_link_enc_regs[] = {
	hpo_dp_link_encoder_reg_list(0),
	hpo_dp_link_encoder_reg_list(1),
};

static const struct dcn31_hpo_dp_link_encoder_shift hpo_dp_le_shift = {
	DCN3_1_HPO_DP_LINK_ENC_MASK_SH_LIST(__SHIFT)
};

static const struct dcn31_hpo_dp_link_encoder_mask hpo_dp_le_mask = {
	DCN3_1_HPO_DP_LINK_ENC_MASK_SH_LIST(_MASK)
};

#define dpp_regs(id)\
[id] = {\
	DPP_REG_LIST_DCN30(id),\
}

static const struct dcn3_dpp_registers dpp_regs[] = {
	dpp_regs(0),
	dpp_regs(1),
	dpp_regs(2),
	dpp_regs(3)
};

static const struct dcn3_dpp_shift tf_shift = {
		DPP_REG_LIST_SH_MASK_DCN30(__SHIFT)
};

static const struct dcn3_dpp_mask tf_mask = {
		DPP_REG_LIST_SH_MASK_DCN30(_MASK)
};

#define opp_regs(id)\
[id] = {\
	OPP_REG_LIST_DCN30(id),\
}

static const struct dcn20_opp_registers opp_regs[] = {
	opp_regs(0),
	opp_regs(1),
	opp_regs(2),
	opp_regs(3)
};

static const struct dcn20_opp_shift opp_shift = {
	OPP_MASK_SH_LIST_DCN20(__SHIFT)
};

static const struct dcn20_opp_mask opp_mask = {
	OPP_MASK_SH_LIST_DCN20(_MASK)
};

#define aux_engine_regs(id)\
[id] = {\
	AUX_COMMON_REG_LIST0(id), \
	.AUXN_IMPCAL = 0, \
	.AUXP_IMPCAL = 0, \
	.AUX_RESET_MASK = DP_AUX0_AUX_CONTROL__AUX_RESET_MASK, \
}

static const struct dce110_aux_registers aux_engine_regs[] = {
		aux_engine_regs(0),
		aux_engine_regs(1),
		aux_engine_regs(2),
		aux_engine_regs(3),
		aux_engine_regs(4)
};

#define dwbc_regs_dcn3(id)\
[id] = {\
	DWBC_COMMON_REG_LIST_DCN30(id),\
}

static const struct dcn30_dwbc_registers dwbc30_regs[] = {
	dwbc_regs_dcn3(0),
};

static const struct dcn30_dwbc_shift dwbc30_shift = {
	DWBC_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn30_dwbc_mask dwbc30_mask = {
	DWBC_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

#define mcif_wb_regs_dcn3(id)\
[id] = {\
	MCIF_WB_COMMON_REG_LIST_DCN30(id),\
}

static const struct dcn30_mmhubbub_registers mcif_wb30_regs[] = {
	mcif_wb_regs_dcn3(0)
};

static const struct dcn30_mmhubbub_shift mcif_wb30_shift = {
	MCIF_WB_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn30_mmhubbub_mask mcif_wb30_mask = {
	MCIF_WB_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

#define dsc_regsDCN20(id)\
[id] = {\
	DSC_REG_LIST_DCN20(id)\
}

static const struct dcn20_dsc_registers dsc_regs[] = {
	dsc_regsDCN20(0),
	dsc_regsDCN20(1),
	dsc_regsDCN20(2)
};

static const struct dcn20_dsc_shift dsc_shift = {
	DSC_REG_LIST_SH_MASK_DCN20(__SHIFT)
};

static const struct dcn20_dsc_mask dsc_mask = {
	DSC_REG_LIST_SH_MASK_DCN20(_MASK)
};

static const struct dcn30_mpc_registers mpc_regs = {
		MPC_REG_LIST_DCN3_0(0),
		MPC_REG_LIST_DCN3_0(1),
		MPC_REG_LIST_DCN3_0(2),
		MPC_REG_LIST_DCN3_0(3),
		MPC_OUT_MUX_REG_LIST_DCN3_0(0),
		MPC_OUT_MUX_REG_LIST_DCN3_0(1),
		MPC_OUT_MUX_REG_LIST_DCN3_0(2),
		MPC_OUT_MUX_REG_LIST_DCN3_0(3),
		MPC_DWB_MUX_REG_LIST_DCN3_0(0),
};

static const struct dcn30_mpc_shift mpc_shift = {
	MPC_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dcn30_mpc_mask mpc_mask = {
	MPC_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

#define optc_regs(id)\
[id] = {OPTC_COMMON_REG_LIST_DCN3_1(id)}

static const struct dcn_optc_registers optc_regs[] = {
	optc_regs(0),
	optc_regs(1),
	optc_regs(2),
	optc_regs(3)
};

static const struct dcn_optc_shift optc_shift = {
	OPTC_COMMON_MASK_SH_LIST_DCN3_1(__SHIFT)
};

static const struct dcn_optc_mask optc_mask = {
	OPTC_COMMON_MASK_SH_LIST_DCN3_1(_MASK)
};

#define hubp_regs(id)\
[id] = {\
	HUBP_REG_LIST_DCN30(id)\
}

static const struct dcn_hubp2_registers hubp_regs[] = {
		hubp_regs(0),
		hubp_regs(1),
		hubp_regs(2),
		hubp_regs(3)
};


static const struct dcn_hubp2_shift hubp_shift = {
		HUBP_MASK_SH_LIST_DCN31(__SHIFT)
};

static const struct dcn_hubp2_mask hubp_mask = {
		HUBP_MASK_SH_LIST_DCN31(_MASK)
};
static const struct dcn_hubbub_registers hubbub_reg = {
		HUBBUB_REG_LIST_DCN31(0)
};

static const struct dcn_hubbub_shift hubbub_shift = {
		HUBBUB_MASK_SH_LIST_DCN31(__SHIFT)
};

static const struct dcn_hubbub_mask hubbub_mask = {
		HUBBUB_MASK_SH_LIST_DCN31(_MASK)
};

static const struct dccg_registers dccg_regs = {
		DCCG_REG_LIST_DCN31()
};

static const struct dccg_shift dccg_shift = {
		DCCG_MASK_SH_LIST_DCN31(__SHIFT)
};

static const struct dccg_mask dccg_mask = {
		DCCG_MASK_SH_LIST_DCN31(_MASK)
};


#define SRII2(reg_name_pre, reg_name_post, id)\
	.reg_name_pre ## _ ##  reg_name_post[id] = BASE(reg ## reg_name_pre \
			## id ## _ ## reg_name_post ## _BASE_IDX) + \
			reg ## reg_name_pre ## id ## _ ## reg_name_post


#define HWSEQ_DCN31_REG_LIST()\
	SR(DCHUBBUB_GLOBAL_TIMER_CNTL), \
	SR(DCHUBBUB_ARB_HOSTVM_CNTL), \
	SR(DIO_MEM_PWR_CTRL), \
	SR(ODM_MEM_PWR_CTRL3), \
	SR(DMU_MEM_PWR_CNTL), \
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
	SR(DOMAIN0_PG_STATUS), \
	SR(DOMAIN1_PG_STATUS), \
	SR(DOMAIN2_PG_STATUS), \
	SR(DOMAIN3_PG_STATUS), \
	SR(DOMAIN16_PG_STATUS), \
	SR(DOMAIN17_PG_STATUS), \
	SR(DOMAIN18_PG_STATUS), \
	SR(D1VGA_CONTROL), \
	SR(D2VGA_CONTROL), \
	SR(D3VGA_CONTROL), \
	SR(D4VGA_CONTROL), \
	SR(D5VGA_CONTROL), \
	SR(D6VGA_CONTROL), \
	SR(DC_IP_REQUEST_CNTL), \
	SR(AZALIA_AUDIO_DTO), \
	SR(AZALIA_CONTROLLER_CLOCK_GATING), \
	SR(HPO_TOP_HW_CONTROL)

static const struct dce_hwseq_registers hwseq_reg = {
		HWSEQ_DCN31_REG_LIST()
};

#define HWSEQ_DCN31_MASK_SH_LIST(mask_sh)\
	HWSEQ_DCN_MASK_SH_LIST(mask_sh), \
	HWS_SF(, DCHUBBUB_GLOBAL_TIMER_CNTL, DCHUBBUB_GLOBAL_TIMER_REFDIV, mask_sh), \
	HWS_SF(, DCHUBBUB_ARB_HOSTVM_CNTL, DISABLE_HOSTVM_FORCE_ALLOW_PSTATE, mask_sh), \
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
	HWS_SF(, DOMAIN0_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN1_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN2_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN3_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN16_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN17_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DOMAIN18_PG_STATUS, DOMAIN_PGFSM_PWR_STATUS, mask_sh), \
	HWS_SF(, DC_IP_REQUEST_CNTL, IP_REQUEST_EN, mask_sh), \
	HWS_SF(, AZALIA_AUDIO_DTO, AZALIA_AUDIO_DTO_MODULE, mask_sh), \
	HWS_SF(, HPO_TOP_CLOCK_CONTROL, HPO_HDMISTREAMCLK_G_GATE_DIS, mask_sh), \
	HWS_SF(, DMU_MEM_PWR_CNTL, DMCU_ERAM_MEM_PWR_FORCE, mask_sh), \
	HWS_SF(, ODM_MEM_PWR_CTRL3, ODM_MEM_UNASSIGNED_PWR_MODE, mask_sh), \
	HWS_SF(, ODM_MEM_PWR_CTRL3, ODM_MEM_VBLANK_PWR_MODE, mask_sh), \
	HWS_SF(, MMHUBBUB_MEM_PWR_CNTL, VGA_MEM_PWR_FORCE, mask_sh), \
	HWS_SF(, DIO_MEM_PWR_CTRL, I2C_LIGHT_SLEEP_FORCE, mask_sh), \
	HWS_SF(, HPO_TOP_HW_CONTROL, HPO_IO_EN, mask_sh)

static const struct dce_hwseq_shift hwseq_shift = {
		HWSEQ_DCN31_MASK_SH_LIST(__SHIFT)
};

static const struct dce_hwseq_mask hwseq_mask = {
		HWSEQ_DCN31_MASK_SH_LIST(_MASK)
};
#define vmid_regs(id)\
[id] = {\
		DCN20_VMID_REG_LIST(id)\
}

static const struct dcn_vmid_registers vmid_regs[] = {
	vmid_regs(0),
	vmid_regs(1),
	vmid_regs(2),
	vmid_regs(3),
	vmid_regs(4),
	vmid_regs(5),
	vmid_regs(6),
	vmid_regs(7),
	vmid_regs(8),
	vmid_regs(9),
	vmid_regs(10),
	vmid_regs(11),
	vmid_regs(12),
	vmid_regs(13),
	vmid_regs(14),
	vmid_regs(15)
};

static const struct dcn20_vmid_shift vmid_shifts = {
		DCN20_VMID_MASK_SH_LIST(__SHIFT)
};

static const struct dcn20_vmid_mask vmid_masks = {
		DCN20_VMID_MASK_SH_LIST(_MASK)
};

static const struct resource_caps res_cap_dcn31 = {
	.num_timing_generator = 4,
	.num_opp = 4,
	.num_video_plane = 4,
	.num_audio = 5,
	.num_stream_encoder = 5,
	.num_dig_link_enc = 5,
	.num_hpo_dp_stream_encoder = 4,
	.num_hpo_dp_link_encoder = 2,
	.num_pll = 5,
	.num_dwb = 1,
	.num_ddc = 5,
	.num_vmid = 16,
	.num_mpc_3dlut = 2,
	.num_dsc = 3,
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
	.disable_z10 = true, /*hw not support it*/
	.disable_dmcu = true,
	.force_abm_enable = false,
	.timing_trace = false,
	.clock_trace = true,
	.disable_pplib_clock_request = false,
	.pipe_split_policy = MPC_SPLIT_DYNAMIC,
	.force_single_disp_pipe_split = false,
	.disable_dcc = DCC_ENABLE,
	.vsr_support = true,
	.performance_trace = false,
	.max_downscale_src_width = 4096,/*upto true 4k*/
	.disable_pplib_wm_range = false,
	.scl_reset_length10 = true,
	.sanity_checks = false,
	.underflow_assert_delay_us = 0xFFFFFFFF,
	.dwb_fi_phase = -1, // -1 = disable,
	.dmub_command_table = true,
	.pstate_enabled = true,
	.use_max_lb = true,
	.enable_mem_low_power = {
		.bits = {
			.vga = true,
			.i2c = true,
			.dmcu = false, // This is previously known to cause hang on S3 cycles if enabled
			.dscl = true,
			.cm = true,
			.mpc = true,
			.optc = true,
			.vpg = true,
			.afmt = true,
		}
	},
	.psr_power_use_phy_fsm = 0,
};

static const struct dc_debug_options debug_defaults_diags = {
	.disable_dmcu = true,
	.force_abm_enable = false,
	.timing_trace = true,
	.clock_trace = true,
	.disable_dpp_power_gate = true,
	.disable_hubp_power_gate = true,
	.disable_clock_gate = true,
	.disable_pplib_clock_request = true,
	.disable_pplib_wm_range = true,
	.disable_stutter = false,
	.scl_reset_length10 = true,
	.dwb_fi_phase = -1, // -1 = disable
	.dmub_command_table = true,
	.enable_tri_buf = true,
	.use_max_lb = true
};

static const struct dc_panel_config panel_config_defaults = {
	.psr = {
		.disable_psr = false,
		.disallow_psrsu = false,
	},
	.ilr = {
		.optimize_edp_link_rate = true,
	},
};

static void dcn31_dpp_destroy(struct dpp **dpp)
{
	kfree(TO_DCN20_DPP(*dpp));
	*dpp = NULL;
}

static struct dpp *dcn31_dpp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn3_dpp *dpp =
		kzalloc(sizeof(struct dcn3_dpp), GFP_KERNEL);

	if (!dpp)
		return NULL;

	if (dpp3_construct(dpp, ctx, inst,
			&dpp_regs[inst], &tf_shift, &tf_mask))
		return &dpp->base;

	BREAK_TO_DEBUGGER();
	kfree(dpp);
	return NULL;
}

static struct output_pixel_processor *dcn31_opp_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_opp *opp =
		kzalloc(sizeof(struct dcn20_opp), GFP_KERNEL);

	if (!opp) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dcn20_opp_construct(opp, ctx, inst,
			&opp_regs[inst], &opp_shift, &opp_mask);
	return &opp->base;
}

static struct dce_aux *dcn31_aux_engine_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct aux_engine_dce110 *aux_engine =
		kzalloc(sizeof(struct aux_engine_dce110), GFP_KERNEL);

	if (!aux_engine)
		return NULL;

	dce110_aux_engine_construct(aux_engine, ctx, inst,
				    SW_AUX_TIMEOUT_PERIOD_MULTIPLIER * AUX_TIMEOUT_PERIOD,
				    &aux_engine_regs[inst],
					&aux_mask,
					&aux_shift,
					ctx->dc->caps.extended_aux_timeout_support);

	return &aux_engine->base;
}
#define i2c_inst_regs(id) { I2C_HW_ENGINE_COMMON_REG_LIST_DCN30(id) }

static const struct dce_i2c_registers i2c_hw_regs[] = {
		i2c_inst_regs(1),
		i2c_inst_regs(2),
		i2c_inst_regs(3),
		i2c_inst_regs(4),
		i2c_inst_regs(5),
};

static const struct dce_i2c_shift i2c_shifts = {
		I2C_COMMON_MASK_SH_LIST_DCN30(__SHIFT)
};

static const struct dce_i2c_mask i2c_masks = {
		I2C_COMMON_MASK_SH_LIST_DCN30(_MASK)
};

static struct dce_i2c_hw *dcn31_i2c_hw_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dce_i2c_hw *dce_i2c_hw =
		kzalloc(sizeof(struct dce_i2c_hw), GFP_KERNEL);

	if (!dce_i2c_hw)
		return NULL;

	dcn2_i2c_hw_construct(dce_i2c_hw, ctx, inst,
				    &i2c_hw_regs[inst], &i2c_shifts, &i2c_masks);

	return dce_i2c_hw;
}
static struct mpc *dcn31_mpc_create(
		struct dc_context *ctx,
		int num_mpcc,
		int num_rmu)
{
	struct dcn30_mpc *mpc30 = kzalloc(sizeof(struct dcn30_mpc),
					  GFP_KERNEL);

	if (!mpc30)
		return NULL;

	dcn30_mpc_construct(mpc30, ctx,
			&mpc_regs,
			&mpc_shift,
			&mpc_mask,
			num_mpcc,
			num_rmu);

	return &mpc30->base;
}

static struct hubbub *dcn31_hubbub_create(struct dc_context *ctx)
{
	int i;

	struct dcn20_hubbub *hubbub3 = kzalloc(sizeof(struct dcn20_hubbub),
					  GFP_KERNEL);

	if (!hubbub3)
		return NULL;

	hubbub31_construct(hubbub3, ctx,
			&hubbub_reg,
			&hubbub_shift,
			&hubbub_mask,
			dcn3_15_ip.det_buffer_size_kbytes,
			dcn3_15_ip.pixel_chunk_size_kbytes,
			dcn3_15_ip.config_return_buffer_size_in_kbytes);


	for (i = 0; i < res_cap_dcn31.num_vmid; i++) {
		struct dcn20_vmid *vmid = &hubbub3->vmid[i];

		vmid->ctx = ctx;

		vmid->regs = &vmid_regs[i];
		vmid->shifts = &vmid_shifts;
		vmid->masks = &vmid_masks;
	}

	return &hubbub3->base;
}

static struct timing_generator *dcn31_timing_generator_create(
		struct dc_context *ctx,
		uint32_t instance)
{
	struct optc *tgn10 =
		kzalloc(sizeof(struct optc), GFP_KERNEL);

	if (!tgn10)
		return NULL;

	tgn10->base.inst = instance;
	tgn10->base.ctx = ctx;

	tgn10->tg_regs = &optc_regs[instance];
	tgn10->tg_shift = &optc_shift;
	tgn10->tg_mask = &optc_mask;

	dcn31_timing_generator_init(tgn10);

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

static struct link_encoder *dcn31_link_encoder_create(
	struct dc_context *ctx,
	const struct encoder_init_data *enc_init_data)
{
	struct dcn20_link_encoder *enc20 =
		kzalloc(sizeof(struct dcn20_link_encoder), GFP_KERNEL);

	if (!enc20)
		return NULL;

	dcn31_link_encoder_construct(enc20,
			enc_init_data,
			&link_enc_feature,
			&link_enc_regs[enc_init_data->transmitter],
			&link_enc_aux_regs[enc_init_data->channel - 1],
			&link_enc_hpd_regs[enc_init_data->hpd_source],
			&le_shift,
			&le_mask);

	return &enc20->enc10.base;
}

/* Create a minimal link encoder object not associated with a particular
 * physical connector.
 * resource_funcs.link_enc_create_minimal
 */
static struct link_encoder *dcn31_link_enc_create_minimal(
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

static struct panel_cntl *dcn31_panel_cntl_create(const struct panel_cntl_init_data *init_data)
{
	struct dcn31_panel_cntl *panel_cntl =
		kzalloc(sizeof(struct dcn31_panel_cntl), GFP_KERNEL);

	if (!panel_cntl)
		return NULL;

	dcn31_panel_cntl_construct(panel_cntl, init_data);

	return &panel_cntl->base;
}

static void read_dce_straps(
	struct dc_context *ctx,
	struct resource_straps *straps)
{
	generic_reg_get(ctx, regDC_PINSTRAPS + BASE(regDC_PINSTRAPS_BASE_IDX),
		FN(DC_PINSTRAPS, DC_PINSTRAPS_AUDIO), &straps->dc_pinstraps_audio);

}

static struct audio *dcn31_create_audio(
		struct dc_context *ctx, unsigned int inst)
{
	return dce_audio_create(ctx, inst,
			&audio_regs[inst], &audio_shift, &audio_mask);
}

static struct vpg *dcn31_vpg_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn31_vpg *vpg31 = kzalloc(sizeof(struct dcn31_vpg), GFP_KERNEL);

	if (!vpg31)
		return NULL;

	vpg31_construct(vpg31, ctx, inst,
			&vpg_regs[inst],
			&vpg_shift,
			&vpg_mask);

	return &vpg31->base;
}

static struct afmt *dcn31_afmt_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn31_afmt *afmt31 = kzalloc(sizeof(struct dcn31_afmt), GFP_KERNEL);

	if (!afmt31)
		return NULL;

	afmt31_construct(afmt31, ctx, inst,
			&afmt_regs[inst],
			&afmt_shift,
			&afmt_mask);

	// Light sleep by default, no need to power down here

	return &afmt31->base;
}

static struct apg *dcn31_apg_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn31_apg *apg31 = kzalloc(sizeof(struct dcn31_apg), GFP_KERNEL);

	if (!apg31)
		return NULL;

	apg31_construct(apg31, ctx, inst,
			&apg_regs[inst],
			&apg_shift,
			&apg_mask);

	return &apg31->base;
}

static struct stream_encoder *dcn315_stream_encoder_create(
	enum engine_id eng_id,
	struct dc_context *ctx)
{
	struct dcn10_stream_encoder *enc1;
	struct vpg *vpg;
	struct afmt *afmt;
	int vpg_inst;
	int afmt_inst;

	/*PHYB is wired off in HW, allow front end to remapping, otherwise needs more changes*/

	/* Mapping of VPG, AFMT, DME register blocks to DIO block instance */
	if (eng_id <= ENGINE_ID_DIGF) {
		vpg_inst = eng_id;
		afmt_inst = eng_id;
	} else
		return NULL;

	enc1 = kzalloc(sizeof(struct dcn10_stream_encoder), GFP_KERNEL);
	vpg = dcn31_vpg_create(ctx, vpg_inst);
	afmt = dcn31_afmt_create(ctx, afmt_inst);

	if (!enc1 || !vpg || !afmt) {
		kfree(enc1);
		kfree(vpg);
		kfree(afmt);
		return NULL;
	}

	dcn30_dio_stream_encoder_construct(enc1, ctx, ctx->dc_bios,
					eng_id, vpg, afmt,
					&stream_enc_regs[eng_id],
					&se_shift, &se_mask);

	return &enc1->base;
}

static struct hpo_dp_stream_encoder *dcn31_hpo_dp_stream_encoder_create(
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
	vpg = dcn31_vpg_create(ctx, vpg_inst);
	apg = dcn31_apg_create(ctx, apg_inst);

	if (!hpo_dp_enc31 || !vpg || !apg) {
		kfree(hpo_dp_enc31);
		kfree(vpg);
		kfree(apg);
		return NULL;
	}

	dcn31_hpo_dp_stream_encoder_construct(hpo_dp_enc31, ctx, ctx->dc_bios,
					hpo_dp_inst, eng_id, vpg, apg,
					&hpo_dp_stream_enc_regs[hpo_dp_inst],
					&hpo_dp_se_shift, &hpo_dp_se_mask);

	return &hpo_dp_enc31->base;
}

static struct hpo_dp_link_encoder *dcn31_hpo_dp_link_encoder_create(
	uint8_t inst,
	struct dc_context *ctx)
{
	struct dcn31_hpo_dp_link_encoder *hpo_dp_enc31;

	/* allocate HPO link encoder */
	hpo_dp_enc31 = kzalloc(sizeof(struct dcn31_hpo_dp_link_encoder), GFP_KERNEL);

	hpo_dp_link_encoder31_construct(hpo_dp_enc31, ctx, inst,
					&hpo_dp_link_enc_regs[inst],
					&hpo_dp_le_shift, &hpo_dp_le_mask);

	return &hpo_dp_enc31->base;
}

static struct dce_hwseq *dcn31_hwseq_create(
	struct dc_context *ctx)
{
	struct dce_hwseq *hws = kzalloc(sizeof(struct dce_hwseq), GFP_KERNEL);

	if (hws) {
		hws->ctx = ctx;
		hws->regs = &hwseq_reg;
		hws->shifts = &hwseq_shift;
		hws->masks = &hwseq_mask;
		/* DCN3.1 FPGA Workaround
		 * Need to enable HPO DP Stream Encoder before setting OTG master enable.
		 * To do so, move calling function enable_stream_timing to only be done AFTER calling
		 * function core_link_enable_stream
		 */
		if (IS_FPGA_MAXIMUS_DC(ctx->dce_environment))
			hws->wa.dp_hpo_and_otg_sequence = true;
	}
	return hws;
}
static const struct resource_create_funcs res_create_funcs = {
	.read_dce_straps = read_dce_straps,
	.create_audio = dcn31_create_audio,
	.create_stream_encoder = dcn315_stream_encoder_create,
	.create_hpo_dp_stream_encoder = dcn31_hpo_dp_stream_encoder_create,
	.create_hpo_dp_link_encoder = dcn31_hpo_dp_link_encoder_create,
	.create_hwseq = dcn31_hwseq_create,
};

static const struct resource_create_funcs res_create_maximus_funcs = {
	.read_dce_straps = NULL,
	.create_audio = NULL,
	.create_stream_encoder = NULL,
	.create_hpo_dp_stream_encoder = dcn31_hpo_dp_stream_encoder_create,
	.create_hpo_dp_link_encoder = dcn31_hpo_dp_link_encoder_create,
	.create_hwseq = dcn31_hwseq_create,
};

static void dcn315_resource_destruct(struct dcn315_resource_pool *pool)
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
		kfree(pool->base.hubbub);
		pool->base.hubbub = NULL;
	}
	for (i = 0; i < pool->base.pipe_count; i++) {
		if (pool->base.dpps[i] != NULL)
			dcn31_dpp_destroy(&pool->base.dpps[i]);

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
}

static struct hubp *dcn31_hubp_create(
	struct dc_context *ctx,
	uint32_t inst)
{
	struct dcn20_hubp *hubp2 =
		kzalloc(sizeof(struct dcn20_hubp), GFP_KERNEL);

	if (!hubp2)
		return NULL;

	if (hubp31_construct(hubp2, ctx, inst,
			&hubp_regs[inst], &hubp_shift, &hubp_mask))
		return &hubp2->base;

	BREAK_TO_DEBUGGER();
	kfree(hubp2);
	return NULL;
}

static bool dcn31_dwbc_create(struct dc_context *ctx, struct resource_pool *pool)
{
	int i;
	uint32_t pipe_count = pool->res_cap->num_dwb;

	for (i = 0; i < pipe_count; i++) {
		struct dcn30_dwbc *dwbc30 = kzalloc(sizeof(struct dcn30_dwbc),
						    GFP_KERNEL);

		if (!dwbc30) {
			dm_error("DC: failed to create dwbc30!\n");
			return false;
		}

		dcn30_dwbc_construct(dwbc30, ctx,
				&dwbc30_regs[i],
				&dwbc30_shift,
				&dwbc30_mask,
				i);

		pool->dwbc[i] = &dwbc30->base;
	}
	return true;
}

static bool dcn31_mmhubbub_create(struct dc_context *ctx, struct resource_pool *pool)
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

		dcn30_mmhubbub_construct(mcif_wb30, ctx,
				&mcif_wb30_regs[i],
				&mcif_wb30_shift,
				&mcif_wb30_mask,
				i);

		pool->mcif_wb[i] = &mcif_wb30->base;
	}
	return true;
}

static struct display_stream_compressor *dcn31_dsc_create(
	struct dc_context *ctx, uint32_t inst)
{
	struct dcn20_dsc *dsc =
		kzalloc(sizeof(struct dcn20_dsc), GFP_KERNEL);

	if (!dsc) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dsc2_construct(dsc, ctx, inst, &dsc_regs[inst], &dsc_shift, &dsc_mask);
	return &dsc->base;
}

static void dcn315_destroy_resource_pool(struct resource_pool **pool)
{
	struct dcn315_resource_pool *dcn31_pool = TO_DCN315_RES_POOL(*pool);

	dcn315_resource_destruct(dcn31_pool);
	kfree(dcn31_pool);
	*pool = NULL;
}

static struct clock_source *dcn31_clock_source_create(
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

static bool is_dual_plane(enum surface_pixel_format format)
{
	return format >= SURFACE_PIXEL_FORMAT_VIDEO_BEGIN || format == SURFACE_PIXEL_FORMAT_GRPH_RGBE_ALPHA;
}

static int dcn315_populate_dml_pipes_from_context(
	struct dc *dc, struct dc_state *context,
	display_e2e_pipe_params_st *pipes,
	bool fast_validate)
{
	int i, pipe_cnt;
	struct resource_context *res_ctx = &context->res_ctx;
	struct pipe_ctx *pipe;
	const int max_usable_det = context->bw_ctx.dml.ip.config_return_buffer_size_in_kbytes - DCN3_15_MIN_COMPBUF_SIZE_KB;

	DC_FP_START();
	dcn20_populate_dml_pipes_from_context(dc, context, pipes, fast_validate);
	DC_FP_END();

	for (i = 0, pipe_cnt = 0; i < dc->res_pool->pipe_count; i++) {
		struct dc_crtc_timing *timing;

		if (!res_ctx->pipe_ctx[i].stream)
			continue;
		pipe = &res_ctx->pipe_ctx[i];
		timing = &pipe->stream->timing;

		/*
		 * Immediate flip can be set dynamically after enabling the plane.
		 * We need to require support for immediate flip or underflow can be
		 * intermittently experienced depending on peak b/w requirements.
		 */
		pipes[pipe_cnt].pipe.src.immediate_flip = true;

		pipes[pipe_cnt].pipe.src.unbounded_req_mode = false;
		pipes[pipe_cnt].pipe.src.gpuvm = true;
		pipes[pipe_cnt].pipe.dest.vfront_porch = timing->v_front_porch;
		pipes[pipe_cnt].pipe.src.dcc_rate = 3;
		pipes[pipe_cnt].dout.dsc_input_bpc = 0;
		DC_FP_START();
		dcn31_zero_pipe_dcc_fraction(pipes, pipe_cnt);
		DC_FP_END();

		if (pipes[pipe_cnt].dout.dsc_enable) {
			switch (timing->display_color_depth) {
			case COLOR_DEPTH_888:
				pipes[pipe_cnt].dout.dsc_input_bpc = 8;
				break;
			case COLOR_DEPTH_101010:
				pipes[pipe_cnt].dout.dsc_input_bpc = 10;
				break;
			case COLOR_DEPTH_121212:
				pipes[pipe_cnt].dout.dsc_input_bpc = 12;
				break;
			default:
				ASSERT(0);
				break;
			}
		}

		pipe_cnt++;
	}

	if (pipe_cnt)
		context->bw_ctx.dml.ip.det_buffer_size_kbytes =
				(max_usable_det / DCN3_15_CRB_SEGMENT_SIZE_KB / pipe_cnt) * DCN3_15_CRB_SEGMENT_SIZE_KB;
	if (context->bw_ctx.dml.ip.det_buffer_size_kbytes > DCN3_15_MAX_DET_SIZE)
		context->bw_ctx.dml.ip.det_buffer_size_kbytes = DCN3_15_MAX_DET_SIZE;
	ASSERT(context->bw_ctx.dml.ip.det_buffer_size_kbytes >= DCN3_15_DEFAULT_DET_SIZE);
	dc->config.enable_4to1MPC = false;
	if (pipe_cnt == 1 && pipe->plane_state && !dc->debug.disable_z9_mpc) {
		if (is_dual_plane(pipe->plane_state->format)
				&& pipe->plane_state->src_rect.width <= 1920 && pipe->plane_state->src_rect.height <= 1080) {
			dc->config.enable_4to1MPC = true;
			context->bw_ctx.dml.ip.det_buffer_size_kbytes =
					(max_usable_det / DCN3_15_CRB_SEGMENT_SIZE_KB / 4) * DCN3_15_CRB_SEGMENT_SIZE_KB;
		} else if (!is_dual_plane(pipe->plane_state->format)
				&& pipe->plane_state->src_rect.width <= 5120
				&& pipe->stream->timing.pix_clk_100hz < dcn_get_max_non_odm_pix_rate_100hz(&dc->dml.soc)) {
			/* Limit to 5k max to avoid forced pipe split when there is not enough detile for swath */
			context->bw_ctx.dml.ip.det_buffer_size_kbytes = 192;
			pipes[0].pipe.src.unbounded_req_mode = true;
		}
	}

	return pipe_cnt;
}

static void dcn315_get_panel_config_defaults(struct dc_panel_config *panel_config)
{
	*panel_config = panel_config_defaults;
}

static struct dc_cap_funcs cap_funcs = {
	.get_dcc_compression_cap = dcn20_get_dcc_compression_cap
};

static struct resource_funcs dcn315_res_pool_funcs = {
	.destroy = dcn315_destroy_resource_pool,
	.link_enc_create = dcn31_link_encoder_create,
	.link_enc_create_minimal = dcn31_link_enc_create_minimal,
	.link_encs_assign = link_enc_cfg_link_encs_assign,
	.link_enc_unassign = link_enc_cfg_link_enc_unassign,
	.panel_cntl_create = dcn31_panel_cntl_create,
	.validate_bandwidth = dcn31_validate_bandwidth,
	.calculate_wm_and_dlg = dcn31_calculate_wm_and_dlg,
	.update_soc_for_wm_a = dcn315_update_soc_for_wm_a,
	.populate_dml_pipes = dcn315_populate_dml_pipes_from_context,
	.acquire_idle_pipe_for_layer = dcn20_acquire_idle_pipe_for_layer,
	.add_stream_to_ctx = dcn30_add_stream_to_ctx,
	.add_dsc_to_stream_resource = dcn20_add_dsc_to_stream_resource,
	.remove_stream_from_ctx = dcn20_remove_stream_from_ctx,
	.populate_dml_writeback_from_context = dcn31_populate_dml_writeback_from_context,
	.set_mcif_arb_params = dcn31_set_mcif_arb_params,
	.find_first_free_match_stream_enc_for_link = dcn10_find_first_free_match_stream_enc_for_link,
	.acquire_post_bldn_3dlut = dcn30_acquire_post_bldn_3dlut,
	.release_post_bldn_3dlut = dcn30_release_post_bldn_3dlut,
	.update_bw_bounding_box = dcn315_update_bw_bounding_box,
	.patch_unknown_plane_state = dcn20_patch_unknown_plane_state,
	.get_panel_config_defaults = dcn315_get_panel_config_defaults,
};

static bool dcn315_resource_construct(
	uint8_t num_virtual_links,
	struct dc *dc,
	struct dcn315_resource_pool *pool)
{
	int i;
	struct dc_context *ctx = dc->ctx;
	struct irq_service_init_data init_data;

	ctx->dc_bios->regs = &bios_regs;

	pool->base.res_cap = &res_cap_dcn31;

	pool->base.funcs = &dcn315_res_pool_funcs;

	/*************************************************
	 *  Resource + asic cap harcoding                *
	 *************************************************/
	pool->base.underlay_pipe_index = NO_UNDERLAY_PIPE;
	pool->base.pipe_count = pool->base.res_cap->num_timing_generator;
	pool->base.mpcc_count = pool->base.res_cap->num_timing_generator;
	dc->caps.max_downscale_ratio = 600;
	dc->caps.i2c_speed_in_khz = 100;
	dc->caps.i2c_speed_in_khz_hdcp = 100;
	dc->caps.max_cursor_size = 256;
	dc->caps.min_horizontal_blanking_period = 80;
	dc->caps.dmdata_alloc_size = 2048;
	dc->caps.max_slave_planes = 2;
	dc->caps.max_slave_yuv_planes = 2;
	dc->caps.max_slave_rgb_planes = 2;
	dc->caps.post_blend_color_processing = true;
	dc->caps.force_dp_tps4_for_cp2520 = true;
	dc->caps.dp_hpo = true;
	dc->caps.dp_hdmi21_pcon_support = true;
	dc->caps.edp_dsc_support = true;
	dc->caps.extended_aux_timeout_support = true;
	dc->caps.dmcub_support = true;
	dc->caps.is_apu = true;

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
	// no OGAM ROM on DCN301
	dc->caps.color.dpp.ogam_rom_caps.srgb = 0;
	dc->caps.color.dpp.ogam_rom_caps.bt2020 = 0;
	dc->caps.color.dpp.ogam_rom_caps.gamma2_2 = 0;
	dc->caps.color.dpp.ogam_rom_caps.pq = 0;
	dc->caps.color.dpp.ogam_rom_caps.hlg = 0;
	dc->caps.color.dpp.ocsc = 0;

	dc->caps.color.mpc.gamut_remap = 1;
	dc->caps.color.mpc.num_3dluts = pool->base.res_cap->num_mpc_3dlut; //2
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
	pool->base.clock_sources[DCN31_CLK_SRC_PLL0] =
			dcn31_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL0,
				&clk_src_regs[0], false);
	pool->base.clock_sources[DCN31_CLK_SRC_PLL1] =
			dcn31_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL1,
				&clk_src_regs[1], false);
	pool->base.clock_sources[DCN31_CLK_SRC_PLL2] =
			dcn31_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL2,
				&clk_src_regs[2], false);
	pool->base.clock_sources[DCN31_CLK_SRC_PLL3] =
			dcn31_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL3,
				&clk_src_regs[3], false);
	pool->base.clock_sources[DCN31_CLK_SRC_PLL4] =
			dcn31_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_COMBO_PHY_PLL4,
				&clk_src_regs[4], false);

	pool->base.clk_src_count = DCN30_CLK_SRC_TOTAL;

	/* todo: not reuse phy_pll registers */
	pool->base.dp_clock_source =
			dcn31_clock_source_create(ctx, ctx->dc_bios,
				CLOCK_SOURCE_ID_DP_DTO,
				&clk_src_regs[0], true);

	for (i = 0; i < pool->base.clk_src_count; i++) {
		if (pool->base.clock_sources[i] == NULL) {
			dm_error("DC: failed to create clock sources!\n");
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}

	/* TODO: DCCG */
	pool->base.dccg = dccg31_create(ctx, &dccg_regs, &dccg_shift, &dccg_mask);
	if (pool->base.dccg == NULL) {
		dm_error("DC: failed to create dccg!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* TODO: IRQ */
	init_data.ctx = dc->ctx;
	pool->base.irqs = dal_irq_service_dcn315_create(&init_data);
	if (!pool->base.irqs)
		goto create_fail;

	/* HUBBUB */
	pool->base.hubbub = dcn31_hubbub_create(ctx);
	if (pool->base.hubbub == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create hubbub!\n");
		goto create_fail;
	}

	/* HUBPs, DPPs, OPPs and TGs */
	for (i = 0; i < pool->base.pipe_count; i++) {
		pool->base.hubps[i] = dcn31_hubp_create(ctx, i);
		if (pool->base.hubps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create hubps!\n");
			goto create_fail;
		}

		pool->base.dpps[i] = dcn31_dpp_create(ctx, i);
		if (pool->base.dpps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create dpps!\n");
			goto create_fail;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_opp; i++) {
		pool->base.opps[i] = dcn31_opp_create(ctx, i);
		if (pool->base.opps[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC: failed to create output pixel processor!\n");
			goto create_fail;
		}
	}

	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++) {
		pool->base.timing_generators[i] = dcn31_timing_generator_create(
				ctx, i);
		if (pool->base.timing_generators[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create tg!\n");
			goto create_fail;
		}
	}
	pool->base.timing_generator_count = i;

	/* PSR */
	pool->base.psr = dmub_psr_create(ctx);
	if (pool->base.psr == NULL) {
		dm_error("DC: failed to create psr obj!\n");
		BREAK_TO_DEBUGGER();
		goto create_fail;
	}

	/* ABM */
	for (i = 0; i < pool->base.res_cap->num_timing_generator; i++) {
		pool->base.multiple_abms[i] = dmub_abm_create(ctx,
				&abm_regs[i],
				&abm_shift,
				&abm_mask);
		if (pool->base.multiple_abms[i] == NULL) {
			dm_error("DC: failed to create abm for pipe %d!\n", i);
			BREAK_TO_DEBUGGER();
			goto create_fail;
		}
	}

	/* MPC and DSC */
	pool->base.mpc = dcn31_mpc_create(ctx, pool->base.mpcc_count, pool->base.res_cap->num_mpc_3dlut);
	if (pool->base.mpc == NULL) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mpc!\n");
		goto create_fail;
	}

	for (i = 0; i < pool->base.res_cap->num_dsc; i++) {
		pool->base.dscs[i] = dcn31_dsc_create(ctx, i);
		if (pool->base.dscs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error("DC: failed to create display stream compressor %d!\n", i);
			goto create_fail;
		}
	}

	/* DWB and MMHUBBUB */
	if (!dcn31_dwbc_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create dwbc!\n");
		goto create_fail;
	}

	if (!dcn31_mmhubbub_create(ctx, &pool->base)) {
		BREAK_TO_DEBUGGER();
		dm_error("DC: failed to create mcif_wb!\n");
		goto create_fail;
	}

	/* AUX and I2C */
	for (i = 0; i < pool->base.res_cap->num_ddc; i++) {
		pool->base.engines[i] = dcn31_aux_engine_create(ctx, i);
		if (pool->base.engines[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create aux engine!!\n");
			goto create_fail;
		}
		pool->base.hw_i2cs[i] = dcn31_i2c_hw_create(ctx, i);
		if (pool->base.hw_i2cs[i] == NULL) {
			BREAK_TO_DEBUGGER();
			dm_error(
				"DC:failed to create hw i2c!!\n");
			goto create_fail;
		}
		pool->base.sw_i2cs[i] = NULL;
	}

	/* Audio, Stream Encoders including HPO and virtual, MPC 3D LUTs */
	if (!resource_construct(num_virtual_links, dc, &pool->base,
			(!IS_FPGA_MAXIMUS_DC(dc->ctx->dce_environment) ?
			&res_create_funcs : &res_create_maximus_funcs)))
			goto create_fail;

	/* HW Sequencer and Plane caps */
	dcn31_hw_sequencer_construct(dc);

	dc->caps.max_planes =  pool->base.pipe_count;

	for (i = 0; i < dc->caps.max_planes; ++i)
		dc->caps.planes[i] = plane_cap;

	dc->cap_funcs = cap_funcs;

	dc->dcn_ip->max_num_dpp = dcn3_15_ip.max_num_dpp;

	return true;

create_fail:

	dcn315_resource_destruct(pool);

	return false;
}

struct resource_pool *dcn315_create_resource_pool(
		const struct dc_init_data *init_data,
		struct dc *dc)
{
	struct dcn315_resource_pool *pool =
		kzalloc(sizeof(struct dcn315_resource_pool), GFP_KERNEL);

	if (!pool)
		return NULL;

	if (dcn315_resource_construct(init_data->num_virtual_links, dc, pool))
		return &pool->base;

	BREAK_TO_DEBUGGER();
	kfree(pool);
	return NULL;
}
