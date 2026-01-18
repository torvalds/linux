/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef __DAL_DCCG_H__
#define __DAL_DCCG_H__

#include "dc_types.h"
#include "hw_shared.h"

enum phyd32clk_clock_source {
	PHYD32CLKA,
	PHYD32CLKB,
	PHYD32CLKC,
	PHYD32CLKD,
	PHYD32CLKE,
	PHYD32CLKF,
	PHYD32CLKG,
};

enum physymclk_clock_source {
	PHYSYMCLK_FORCE_SRC_SYMCLK,    // Select symclk as source of clock which is output to PHY through DCIO.
	PHYSYMCLK_FORCE_SRC_PHYD18CLK, // Select phyd18clk as the source of clock which is output to PHY through DCIO.
	PHYSYMCLK_FORCE_SRC_PHYD32CLK, // Select phyd32clk as the source of clock which is output to PHY through DCIO.
};

enum streamclk_source {
	REFCLK,                   // Selects REFCLK as source for hdmistreamclk.
	DTBCLK0,                  // Selects DTBCLK0 as source for hdmistreamclk.
	DPREFCLK,                 // Selects DPREFCLK as source for hdmistreamclk
};

enum dentist_dispclk_change_mode {
	DISPCLK_CHANGE_MODE_IMMEDIATE,
	DISPCLK_CHANGE_MODE_RAMPING,
};

struct dp_dto_params {
	int otg_inst;
	enum signal_type signal;
	enum streamclk_source clk_src;
	uint64_t pixclk_hz;
	uint64_t refclk_hz;
};

enum pixel_rate_div {
   PIXEL_RATE_DIV_BY_1 = 0,
   PIXEL_RATE_DIV_BY_2 = 1,
   PIXEL_RATE_DIV_BY_4 = 3,
   PIXEL_RATE_DIV_NA = 0xF
};

struct dcn_dccg_reg_state {
	uint32_t dc_mem_global_pwr_req_cntl;
	uint32_t dccg_audio_dtbclk_dto_modulo;
	uint32_t dccg_audio_dtbclk_dto_phase;
	uint32_t dccg_audio_dto_source;
	uint32_t dccg_audio_dto0_module;
	uint32_t dccg_audio_dto0_phase;
	uint32_t dccg_audio_dto1_module;
	uint32_t dccg_audio_dto1_phase;
	uint32_t dccg_cac_status;
	uint32_t dccg_cac_status2;
	uint32_t dccg_disp_cntl_reg;
	uint32_t dccg_ds_cntl;
	uint32_t dccg_ds_dto_incr;
	uint32_t dccg_ds_dto_modulo;
	uint32_t dccg_ds_hw_cal_interval;
	uint32_t dccg_gate_disable_cntl;
	uint32_t dccg_gate_disable_cntl2;
	uint32_t dccg_gate_disable_cntl3;
	uint32_t dccg_gate_disable_cntl4;
	uint32_t dccg_gate_disable_cntl5;
	uint32_t dccg_gate_disable_cntl6;
	uint32_t dccg_global_fgcg_rep_cntl;
	uint32_t dccg_gtc_cntl;
	uint32_t dccg_gtc_current;
	uint32_t dccg_gtc_dto_incr;
	uint32_t dccg_gtc_dto_modulo;
	uint32_t dccg_perfmon_cntl;
	uint32_t dccg_perfmon_cntl2;
	uint32_t dccg_soft_reset;
	uint32_t dccg_test_clk_sel;
	uint32_t dccg_vsync_cnt_ctrl;
	uint32_t dccg_vsync_cnt_int_ctrl;
	uint32_t dccg_vsync_otg0_latch_value;
	uint32_t dccg_vsync_otg1_latch_value;
	uint32_t dccg_vsync_otg2_latch_value;
	uint32_t dccg_vsync_otg3_latch_value;
	uint32_t dccg_vsync_otg4_latch_value;
	uint32_t dccg_vsync_otg5_latch_value;
	uint32_t dispclk_cgtt_blk_ctrl_reg;
	uint32_t dispclk_freq_change_cntl;
	uint32_t dp_dto_dbuf_en;
	uint32_t dp_dto0_modulo;
	uint32_t dp_dto0_phase;
	uint32_t dp_dto1_modulo;
	uint32_t dp_dto1_phase;
	uint32_t dp_dto2_modulo;
	uint32_t dp_dto2_phase;
	uint32_t dp_dto3_modulo;
	uint32_t dp_dto3_phase;
	uint32_t dpiaclk_540m_dto_modulo;
	uint32_t dpiaclk_540m_dto_phase;
	uint32_t dpiaclk_810m_dto_modulo;
	uint32_t dpiaclk_810m_dto_phase;
	uint32_t dpiaclk_dto_cntl;
	uint32_t dpiasymclk_cntl;
	uint32_t dppclk_cgtt_blk_ctrl_reg;
	uint32_t dppclk_ctrl;
	uint32_t dppclk_dto_ctrl;
	uint32_t dppclk0_dto_param;
	uint32_t dppclk1_dto_param;
	uint32_t dppclk2_dto_param;
	uint32_t dppclk3_dto_param;
	uint32_t dprefclk_cgtt_blk_ctrl_reg;
	uint32_t dprefclk_cntl;
	uint32_t dpstreamclk_cntl;
	uint32_t dscclk_dto_ctrl;
	uint32_t dscclk0_dto_param;
	uint32_t dscclk1_dto_param;
	uint32_t dscclk2_dto_param;
	uint32_t dscclk3_dto_param;
	uint32_t dtbclk_dto_dbuf_en;
	uint32_t dtbclk_dto0_modulo;
	uint32_t dtbclk_dto0_phase;
	uint32_t dtbclk_dto1_modulo;
	uint32_t dtbclk_dto1_phase;
	uint32_t dtbclk_dto2_modulo;
	uint32_t dtbclk_dto2_phase;
	uint32_t dtbclk_dto3_modulo;
	uint32_t dtbclk_dto3_phase;
	uint32_t dtbclk_p_cntl;
	uint32_t force_symclk_disable;
	uint32_t hdmicharclk0_clock_cntl;
	uint32_t hdmistreamclk_cntl;
	uint32_t hdmistreamclk0_dto_param;
	uint32_t microsecond_time_base_div;
	uint32_t millisecond_time_base_div;
	uint32_t otg_pixel_rate_div;
	uint32_t otg0_phypll_pixel_rate_cntl;
	uint32_t otg0_pixel_rate_cntl;
	uint32_t otg1_phypll_pixel_rate_cntl;
	uint32_t otg1_pixel_rate_cntl;
	uint32_t otg2_phypll_pixel_rate_cntl;
	uint32_t otg2_pixel_rate_cntl;
	uint32_t otg3_phypll_pixel_rate_cntl;
	uint32_t otg3_pixel_rate_cntl;
	uint32_t phyasymclk_clock_cntl;
	uint32_t phybsymclk_clock_cntl;
	uint32_t phycsymclk_clock_cntl;
	uint32_t phydsymclk_clock_cntl;
	uint32_t phyesymclk_clock_cntl;
	uint32_t phyplla_pixclk_resync_cntl;
	uint32_t phypllb_pixclk_resync_cntl;
	uint32_t phypllc_pixclk_resync_cntl;
	uint32_t phyplld_pixclk_resync_cntl;
	uint32_t phyplle_pixclk_resync_cntl;
	uint32_t refclk_cgtt_blk_ctrl_reg;
	uint32_t socclk_cgtt_blk_ctrl_reg;
	uint32_t symclk_cgtt_blk_ctrl_reg;
	uint32_t symclk_psp_cntl;
	uint32_t symclk32_le_cntl;
	uint32_t symclk32_se_cntl;
	uint32_t symclka_clock_enable;
	uint32_t symclkb_clock_enable;
	uint32_t symclkc_clock_enable;
	uint32_t symclkd_clock_enable;
	uint32_t symclke_clock_enable;
};

struct dccg {
	struct dc_context *ctx;
	const struct dccg_funcs *funcs;
	int pipe_dppclk_khz[MAX_PIPES];
	int ref_dppclk;
	bool dpp_clock_gated[MAX_PIPES];
	//int dtbclk_khz[MAX_PIPES];/* TODO needs to be removed */
	//int audio_dtbclk_khz;/* TODO needs to be removed */
	//int ref_dtbclk_khz;/* TODO needs to be removed */
};
struct dtbclk_dto_params {
	const struct dc_crtc_timing *timing;
	int otg_inst;
	int pixclk_khz;
	int req_audio_dtbclk_khz;
	int num_odm_segments;
	int ref_dtbclk_khz;
	bool is_hdmi;
};

struct dccg_funcs {
	void (*update_dpp_dto)(struct dccg *dccg,
			int dpp_inst,
			int req_dppclk);
	void (*get_dccg_ref_freq)(struct dccg *dccg,
			unsigned int xtalin_freq_inKhz,
			unsigned int *dccg_ref_freq_inKhz);
	void (*set_fifo_errdet_ovr_en)(struct dccg *dccg,
			bool en);
	void (*otg_add_pixel)(struct dccg *dccg,
			uint32_t otg_inst);
	void (*otg_drop_pixel)(struct dccg *dccg,
			uint32_t otg_inst);
	void (*dccg_init)(struct dccg *dccg);
	void (*set_dpstreamclk_root_clock_gating)(
			struct dccg *dccg,
			int dp_hpo_inst,
			bool enable);

	void (*set_dpstreamclk)(
			struct dccg *dccg,
			enum streamclk_source src,
			int otg_inst,
			int dp_hpo_inst);

	void (*enable_symclk32_se)(
			struct dccg *dccg,
			int hpo_se_inst,
			enum phyd32clk_clock_source phyd32clk);

	void (*disable_symclk32_se)(
			struct dccg *dccg,
			int hpo_se_inst);

	void (*enable_symclk32_le)(
			struct dccg *dccg,
			int hpo_le_inst,
			enum phyd32clk_clock_source phyd32clk);

	void (*disable_symclk32_le)(
			struct dccg *dccg,
			int hpo_le_inst);

	void (*set_symclk32_le_root_clock_gating)(
			struct dccg *dccg,
			int hpo_le_inst,
			bool enable);

	void (*set_physymclk)(
			struct dccg *dccg,
			int phy_inst,
			enum physymclk_clock_source clk_src,
			bool force_enable);

	void (*set_physymclk_root_clock_gating)(
			struct dccg *dccg,
			int phy_inst,
			bool enable);

	void (*set_dtbclk_dto)(
			struct dccg *dccg,
			const struct dtbclk_dto_params *params);

	void (*set_audio_dtbclk_dto)(
			struct dccg *dccg,
			const struct dtbclk_dto_params *params);

	void (*set_dispclk_change_mode)(
			struct dccg *dccg,
			enum dentist_dispclk_change_mode change_mode);

	void (*disable_dsc)(
		struct dccg *dccg,
		int inst);

	void (*enable_dsc)(
		struct dccg *dccg,
		int inst);

	void (*set_pixel_rate_div)(struct dccg *dccg,
			uint32_t otg_inst,
			enum pixel_rate_div k1,
			enum pixel_rate_div k2);

	void (*get_pixel_rate_div)(struct dccg *dccg,
			uint32_t otg_inst,
			uint32_t *div_factor1,
			uint32_t *div_factor2);

	void (*set_valid_pixel_rate)(
			struct dccg *dccg,
			int ref_dtbclk_khz,
			int otg_inst,
			int pixclk_khz);

	void (*trigger_dio_fifo_resync)(
			struct dccg *dccg);

	void (*dpp_root_clock_control)(
			struct dccg *dccg,
			unsigned int dpp_inst,
			bool clock_on);

	void (*enable_symclk_se)(
			struct dccg *dccg,
			uint32_t stream_enc_inst,
			uint32_t link_enc_inst);

	void (*disable_symclk_se)(
			struct dccg *dccg,
			uint32_t stream_enc_inst,
			uint32_t link_enc_inst);
	void (*set_dp_dto)(
			struct dccg *dccg,
			const struct dp_dto_params *params);
	void (*set_dtbclk_p_src)(
			struct dccg *dccg,
			enum streamclk_source src,
			uint32_t otg_inst);
	void (*set_dto_dscclk)(struct dccg *dccg, uint32_t dsc_inst, uint32_t num_slices_h);
	void (*set_ref_dscclk)(struct dccg *dccg, uint32_t dsc_inst);
	void (*dccg_root_gate_disable_control)(struct dccg *dccg, uint32_t pipe_idx, uint32_t disable_clock_gating);
	void (*dccg_read_reg_state)(struct dccg *dccg, struct dcn_dccg_reg_state *dccg_reg_state);
	void (*dccg_enable_global_fgcg)(struct dccg *dccg, bool enable);
};

#endif //__DAL_DCCG_H__
