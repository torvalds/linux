/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

#ifndef _ICE_CGU_REGS_H_
#define _ICE_CGU_REGS_H_

#define NAC_CGU_DWORD9 0x24
union nac_cgu_dword9 {
	struct {
		u32 time_ref_freq_sel : 3;
		u32 clk_eref1_en : 1;
		u32 clk_eref0_en : 1;
		u32 time_ref_en : 1;
		u32 time_sync_en : 1;
		u32 one_pps_out_en : 1;
		u32 clk_ref_synce_en : 1;
		u32 clk_synce1_en : 1;
		u32 clk_synce0_en : 1;
		u32 net_clk_ref1_en : 1;
		u32 net_clk_ref0_en : 1;
		u32 clk_synce1_amp : 2;
		u32 misc6 : 1;
		u32 clk_synce0_amp : 2;
		u32 one_pps_out_amp : 2;
		u32 misc24 : 12;
	};
	u32 val;
};

#define NAC_CGU_DWORD16_E825C 0x40
union nac_cgu_dword16_e825c {
	struct {
		u32 synce_remndr : 6;
		u32 synce_phlmt_en : 1;
		u32 misc13 : 17;
		u32 tspll_ck_refclkfreq : 8;
	};
	u32 val;
};

#define NAC_CGU_DWORD19 0x4c
union nac_cgu_dword19 {
	struct {
		u32 tspll_fbdiv_intgr : 8;
		u32 fdpll_ulck_thr : 5;
		u32 misc15 : 3;
		u32 tspll_ndivratio : 4;
		u32 tspll_iref_ndivratio : 3;
		u32 misc19 : 1;
		u32 japll_ndivratio : 4;
		u32 japll_iref_ndivratio : 3;
		u32 misc27 : 1;
	};
	u32 val;
};

#define NAC_CGU_DWORD22 0x58
union nac_cgu_dword22 {
	struct {
		u32 fdpll_frac_div_out_nc : 2;
		u32 fdpll_lock_int_for : 1;
		u32 synce_hdov_int_for : 1;
		u32 synce_lock_int_for : 1;
		u32 fdpll_phlead_slip_nc : 1;
		u32 fdpll_acc1_ovfl_nc : 1;
		u32 fdpll_acc2_ovfl_nc : 1;
		u32 synce_status_nc : 6;
		u32 fdpll_acc1f_ovfl : 1;
		u32 misc18 : 1;
		u32 fdpllclk_div : 4;
		u32 time1588clk_div : 4;
		u32 synceclk_div : 4;
		u32 synceclk_sel_div2 : 1;
		u32 fdpllclk_sel_div2 : 1;
		u32 time1588clk_sel_div2 : 1;
		u32 misc3 : 1;
	};
	u32 val;
};

#define NAC_CGU_DWORD23_E825C 0x5C
union nac_cgu_dword23_e825c {
	struct {
		u32 cgupll_fbdiv_intgr : 10;
		u32 ux56pll_fbdiv_intgr : 10;
		u32 misc20 : 4;
		u32 ts_pll_enable : 1;
		u32 time_sync_tspll_align_sel : 1;
		u32 ext_synce_sel : 1;
		u32 ref1588_ck_div : 4;
		u32 time_ref_sel : 1;

	};
	u32 val;
};

#define NAC_CGU_DWORD24 0x60
union nac_cgu_dword24 {
	struct {
		u32 tspll_fbdiv_frac : 22;
		u32 misc20 : 2;
		u32 ts_pll_enable : 1;
		u32 time_sync_tspll_align_sel : 1;
		u32 ext_synce_sel : 1;
		u32 ref1588_ck_div : 4;
		u32 time_ref_sel : 1;
	};
	u32 val;
};

#define TSPLL_CNTR_BIST_SETTINGS 0x344
union tspll_cntr_bist_settings {
	struct {
		u32 i_irefgen_settling_time_cntr_7_0 : 8;
		u32 i_irefgen_settling_time_ro_standby_1_0 : 2;
		u32 reserved195 : 5;
		u32 i_plllock_sel_0 : 1;
		u32 i_plllock_sel_1 : 1;
		u32 i_plllock_cnt_6_0 : 7;
		u32 i_plllock_cnt_10_7 : 4;
		u32 reserved200 : 4;
	};
	u32 val;
};

#define TSPLL_RO_BWM_LF 0x370
union tspll_ro_bwm_lf {
	struct {
		u32 bw_freqov_high_cri_7_0 : 8;
		u32 bw_freqov_high_cri_9_8 : 2;
		u32 biascaldone_cri : 1;
		u32 plllock_gain_tran_cri : 1;
		u32 plllock_true_lock_cri : 1;
		u32 pllunlock_flag_cri : 1;
		u32 afcerr_cri : 1;
		u32 afcdone_cri : 1;
		u32 feedfwrdgain_cal_cri_7_0 : 8;
		u32 m2fbdivmod_cri_7_0 : 8;
	};
	u32 val;
};

#define TSPLL_RO_LOCK_E825C 0x3f0
union tspll_ro_lock_e825c {
	struct {
		u32 bw_freqov_high_cri_7_0 : 8;
		u32 bw_freqov_high_cri_9_8 : 2;
		u32 reserved455 : 1;
		u32 plllock_gain_tran_cri : 1;
		u32 plllock_true_lock_cri : 1;
		u32 pllunlock_flag_cri : 1;
		u32 afcerr_cri : 1;
		u32 afcdone_cri : 1;
		u32 feedfwrdgain_cal_cri_7_0 : 8;
		u32 reserved462 : 8;
	};
	u32 val;
};

#define TSPLL_BW_TDC_E825C 0x31c
union tspll_bw_tdc_e825c {
	struct {
		u32 i_tdc_offset_lock_1_0 : 2;
		u32 i_bbthresh1_2_0 : 3;
		u32 i_bbthresh2_2_0 : 3;
		u32 i_tdcsel_1_0 : 2;
		u32 i_tdcovccorr_en_h : 1;
		u32 i_divretimeren : 1;
		u32 i_bw_ampmeas_window : 1;
		u32 i_bw_lowerbound_2_0 : 3;
		u32 i_bw_upperbound_2_0 : 3;
		u32 i_bw_mode_1_0 : 2;
		u32 i_ft_mode_sel_2_0 : 3;
		u32 i_bwphase_4_0 : 5;
		u32 i_plllock_sel_1_0 : 2;
		u32 i_afc_divratio : 1;
	};
	u32 val;
};

#endif /* _ICE_CGU_REGS_H_ */
