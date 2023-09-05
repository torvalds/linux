// SPDX-License-Identifier: GPL-2.0-or-later
/* Microchip Sparx5 Switch SerDes driver
 *
 * Copyright (c) 2020 Microchip Technology Inc. and its subsidiaries.
 *
 * The Sparx5 Chip Register Model can be browsed at this location:
 * https://github.com/microchip-ung/sparx-5_reginfo
 * and the datasheet is available here:
 * https://ww1.microchip.com/downloads/en/DeviceDoc/SparX-5_Family_L2L3_Enterprise_10G_Ethernet_Switches_Datasheet_00003822B.pdf
 */
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>

#include "sparx5_serdes.h"

#define SPX5_CMU_MAX          14

#define SPX5_SERDES_10G_START 13
#define SPX5_SERDES_25G_START 25
#define SPX5_SERDES_6G10G_CNT SPX5_SERDES_25G_START

/* Optimal power settings from GUC */
#define SPX5_SERDES_QUIET_MODE_VAL 0x01ef4e0c

enum sparx5_10g28cmu_mode {
	SPX5_SD10G28_CMU_MAIN = 0,
	SPX5_SD10G28_CMU_AUX1 = 1,
	SPX5_SD10G28_CMU_AUX2 = 3,
	SPX5_SD10G28_CMU_NONE = 4,
	SPX5_SD10G28_CMU_MAX,
};

enum sparx5_sd25g28_mode_preset_type {
	SPX5_SD25G28_MODE_PRESET_25000,
	SPX5_SD25G28_MODE_PRESET_10000,
	SPX5_SD25G28_MODE_PRESET_5000,
	SPX5_SD25G28_MODE_PRESET_SD_2G5,
	SPX5_SD25G28_MODE_PRESET_1000BASEX,
};

enum sparx5_sd10g28_mode_preset_type {
	SPX5_SD10G28_MODE_PRESET_10000,
	SPX5_SD10G28_MODE_PRESET_SFI_5000_6G,
	SPX5_SD10G28_MODE_PRESET_SFI_5000_10G,
	SPX5_SD10G28_MODE_PRESET_QSGMII,
	SPX5_SD10G28_MODE_PRESET_SD_2G5,
	SPX5_SD10G28_MODE_PRESET_1000BASEX,
};

struct sparx5_serdes_io_resource {
	enum sparx5_serdes_target id;
	phys_addr_t offset;
};

struct sparx5_sd25g28_mode_preset {
	u8 bitwidth;
	u8 tx_pre_div;
	u8 fifo_ck_div;
	u8 pre_divsel;
	u8 vco_div_mode;
	u8 sel_div;
	u8 ck_bitwidth;
	u8 subrate;
	u8 com_txcal_en;
	u8 com_tx_reserve_msb;
	u8 com_tx_reserve_lsb;
	u8 cfg_itx_ipcml_base;
	u8 tx_reserve_lsb;
	u8 tx_reserve_msb;
	u8 bw;
	u8 rxterm;
	u8 dfe_tap;
	u8 dfe_enable;
	bool txmargin;
	u8 cfg_ctle_rstn;
	u8 r_dfe_rstn;
	u8 cfg_pi_bw_3_0;
	u8 tx_tap_dly;
	u8 tx_tap_adv;
};

struct sparx5_sd25g28_media_preset {
	u8 cfg_eq_c_force_3_0;
	u8 cfg_vga_ctrl_byp_4_0;
	u8 cfg_eq_r_force_3_0;
	u8 cfg_en_adv;
	u8 cfg_en_main;
	u8 cfg_en_dly;
	u8 cfg_tap_adv_3_0;
	u8 cfg_tap_main;
	u8 cfg_tap_dly_4_0;
	u8 cfg_alos_thr_2_0;
};

struct sparx5_sd25g28_args {
	u8 if_width; /* UDL if-width: 10/16/20/32/64 */
	bool skip_cmu_cfg:1; /* Enable/disable CMU cfg */
	enum sparx5_10g28cmu_mode cmu_sel; /* Device/Mode serdes uses */
	bool no_pwrcycle:1; /* Omit initial power-cycle */
	bool txinvert:1; /* Enable inversion of output data */
	bool rxinvert:1; /* Enable inversion of input data */
	u16 txswing; /* Set output level */
	u8 rate; /* Rate of network interface */
	u8 pi_bw_gen1;
	u8 duty_cycle; /* Set output level to  half/full */
	bool mute:1; /* Mute Output Buffer */
	bool reg_rst:1;
	u8 com_pll_reserve;
};

struct sparx5_sd25g28_params {
	u8 reg_rst;
	u8 cfg_jc_byp;
	u8 cfg_common_reserve_7_0;
	u8 r_reg_manual;
	u8 r_d_width_ctrl_from_hwt;
	u8 r_d_width_ctrl_2_0;
	u8 r_txfifo_ck_div_pmad_2_0;
	u8 r_rxfifo_ck_div_pmad_2_0;
	u8 cfg_pll_lol_set;
	u8 cfg_vco_div_mode_1_0;
	u8 cfg_pre_divsel_1_0;
	u8 cfg_sel_div_3_0;
	u8 cfg_vco_start_code_3_0;
	u8 cfg_pma_tx_ck_bitwidth_2_0;
	u8 cfg_tx_prediv_1_0;
	u8 cfg_rxdiv_sel_2_0;
	u8 cfg_tx_subrate_2_0;
	u8 cfg_rx_subrate_2_0;
	u8 r_multi_lane_mode;
	u8 cfg_cdrck_en;
	u8 cfg_dfeck_en;
	u8 cfg_dfe_pd;
	u8 cfg_dfedmx_pd;
	u8 cfg_dfetap_en_5_1;
	u8 cfg_dmux_pd;
	u8 cfg_dmux_clk_pd;
	u8 cfg_erramp_pd;
	u8 cfg_pi_dfe_en;
	u8 cfg_pi_en;
	u8 cfg_pd_ctle;
	u8 cfg_summer_en;
	u8 cfg_pmad_ck_pd;
	u8 cfg_pd_clk;
	u8 cfg_pd_cml;
	u8 cfg_pd_driver;
	u8 cfg_rx_reg_pu;
	u8 cfg_pd_rms_det;
	u8 cfg_dcdr_pd;
	u8 cfg_ecdr_pd;
	u8 cfg_pd_sq;
	u8 cfg_itx_ipdriver_base_2_0;
	u8 cfg_tap_dly_4_0;
	u8 cfg_tap_main;
	u8 cfg_en_main;
	u8 cfg_tap_adv_3_0;
	u8 cfg_en_adv;
	u8 cfg_en_dly;
	u8 cfg_iscan_en;
	u8 l1_pcs_en_fast_iscan;
	u8 l0_cfg_bw_1_0;
	u8 l0_cfg_txcal_en;
	u8 cfg_en_dummy;
	u8 cfg_pll_reserve_3_0;
	u8 l0_cfg_tx_reserve_15_8;
	u8 l0_cfg_tx_reserve_7_0;
	u8 cfg_tx_reserve_15_8;
	u8 cfg_tx_reserve_7_0;
	u8 cfg_bw_1_0;
	u8 cfg_txcal_man_en;
	u8 cfg_phase_man_4_0;
	u8 cfg_quad_man_1_0;
	u8 cfg_txcal_shift_code_5_0;
	u8 cfg_txcal_valid_sel_3_0;
	u8 cfg_txcal_en;
	u8 cfg_cdr_kf_2_0;
	u8 cfg_cdr_m_7_0;
	u8 cfg_pi_bw_3_0;
	u8 cfg_pi_steps_1_0;
	u8 cfg_dis_2ndorder;
	u8 cfg_ctle_rstn;
	u8 r_dfe_rstn;
	u8 cfg_alos_thr_2_0;
	u8 cfg_itx_ipcml_base_1_0;
	u8 cfg_rx_reserve_7_0;
	u8 cfg_rx_reserve_15_8;
	u8 cfg_rxterm_2_0;
	u8 cfg_fom_selm;
	u8 cfg_rx_sp_ctle_1_0;
	u8 cfg_isel_ctle_1_0;
	u8 cfg_vga_ctrl_byp_4_0;
	u8 cfg_vga_byp;
	u8 cfg_agc_adpt_byp;
	u8 cfg_eqr_byp;
	u8 cfg_eqr_force_3_0;
	u8 cfg_eqc_force_3_0;
	u8 cfg_sum_setcm_en;
	u8 cfg_init_pos_iscan_6_0;
	u8 cfg_init_pos_ipi_6_0;
	u8 cfg_dfedig_m_2_0;
	u8 cfg_en_dfedig;
	u8 cfg_pi_DFE_en;
	u8 cfg_tx2rx_lp_en;
	u8 cfg_txlb_en;
	u8 cfg_rx2tx_lp_en;
	u8 cfg_rxlb_en;
	u8 r_tx_pol_inv;
	u8 r_rx_pol_inv;
};

struct sparx5_sd10g28_media_preset {
	u8 cfg_en_adv;
	u8 cfg_en_main;
	u8 cfg_en_dly;
	u8 cfg_tap_adv_3_0;
	u8 cfg_tap_main;
	u8 cfg_tap_dly_4_0;
	u8 cfg_vga_ctrl_3_0;
	u8 cfg_vga_cp_2_0;
	u8 cfg_eq_res_3_0;
	u8 cfg_eq_r_byp;
	u8 cfg_eq_c_force_3_0;
	u8 cfg_alos_thr_3_0;
};

struct sparx5_sd10g28_mode_preset {
	u8 bwidth; /* interface width: 10/16/20/32/64 */
	enum sparx5_10g28cmu_mode cmu_sel; /* Device/Mode serdes uses */
	u8 rate; /* Rate of network interface */
	u8 dfe_tap;
	u8 dfe_enable;
	u8 pi_bw_gen1;
	u8 duty_cycle; /* Set output level to  half/full */
};

struct sparx5_sd10g28_args {
	bool skip_cmu_cfg:1; /* Enable/disable CMU cfg */
	bool no_pwrcycle:1; /* Omit initial power-cycle */
	bool txinvert:1; /* Enable inversion of output data */
	bool rxinvert:1; /* Enable inversion of input data */
	bool txmargin:1; /* Set output level to  half/full */
	u16 txswing; /* Set output level */
	bool mute:1; /* Mute Output Buffer */
	bool is_6g:1;
	bool reg_rst:1;
};

struct sparx5_sd10g28_params {
	u8 cmu_sel;
	u8 is_6g;
	u8 skip_cmu_cfg;
	u8 cfg_lane_reserve_7_0;
	u8 cfg_ssc_rtl_clk_sel;
	u8 cfg_lane_reserve_15_8;
	u8 cfg_txrate_1_0;
	u8 cfg_rxrate_1_0;
	u8 r_d_width_ctrl_2_0;
	u8 cfg_pma_tx_ck_bitwidth_2_0;
	u8 cfg_rxdiv_sel_2_0;
	u8 r_pcs2pma_phymode_4_0;
	u8 cfg_lane_id_2_0;
	u8 cfg_cdrck_en;
	u8 cfg_dfeck_en;
	u8 cfg_dfe_pd;
	u8 cfg_dfetap_en_5_1;
	u8 cfg_erramp_pd;
	u8 cfg_pi_DFE_en;
	u8 cfg_pi_en;
	u8 cfg_pd_ctle;
	u8 cfg_summer_en;
	u8 cfg_pd_rx_cktree;
	u8 cfg_pd_clk;
	u8 cfg_pd_cml;
	u8 cfg_pd_driver;
	u8 cfg_rx_reg_pu;
	u8 cfg_d_cdr_pd;
	u8 cfg_pd_sq;
	u8 cfg_rxdet_en;
	u8 cfg_rxdet_str;
	u8 r_multi_lane_mode;
	u8 cfg_en_adv;
	u8 cfg_en_main;
	u8 cfg_en_dly;
	u8 cfg_tap_adv_3_0;
	u8 cfg_tap_main;
	u8 cfg_tap_dly_4_0;
	u8 cfg_vga_ctrl_3_0;
	u8 cfg_vga_cp_2_0;
	u8 cfg_eq_res_3_0;
	u8 cfg_eq_r_byp;
	u8 cfg_eq_c_force_3_0;
	u8 cfg_en_dfedig;
	u8 cfg_sum_setcm_en;
	u8 cfg_en_preemph;
	u8 cfg_itx_ippreemp_base_1_0;
	u8 cfg_itx_ipdriver_base_2_0;
	u8 cfg_ibias_tune_reserve_5_0;
	u8 cfg_txswing_half;
	u8 cfg_dis_2nd_order;
	u8 cfg_rx_ssc_lh;
	u8 cfg_pi_floop_steps_1_0;
	u8 cfg_pi_ext_dac_23_16;
	u8 cfg_pi_ext_dac_15_8;
	u8 cfg_iscan_ext_dac_7_0;
	u8 cfg_cdr_kf_gen1_2_0;
	u8 cfg_cdr_kf_gen2_2_0;
	u8 cfg_cdr_kf_gen3_2_0;
	u8 cfg_cdr_kf_gen4_2_0;
	u8 r_cdr_m_gen1_7_0;
	u8 cfg_pi_bw_gen1_3_0;
	u8 cfg_pi_bw_gen2;
	u8 cfg_pi_bw_gen3;
	u8 cfg_pi_bw_gen4;
	u8 cfg_pi_ext_dac_7_0;
	u8 cfg_pi_steps;
	u8 cfg_mp_max_3_0;
	u8 cfg_rstn_dfedig;
	u8 cfg_alos_thr_3_0;
	u8 cfg_predrv_slewrate_1_0;
	u8 cfg_itx_ipcml_base_1_0;
	u8 cfg_ip_pre_base_1_0;
	u8 r_cdr_m_gen2_7_0;
	u8 r_cdr_m_gen3_7_0;
	u8 r_cdr_m_gen4_7_0;
	u8 r_en_auto_cdr_rstn;
	u8 cfg_oscal_afe;
	u8 cfg_pd_osdac_afe;
	u8 cfg_resetb_oscal_afe[2];
	u8 cfg_center_spreading;
	u8 cfg_m_cnt_maxval_4_0;
	u8 cfg_ncnt_maxval_7_0;
	u8 cfg_ncnt_maxval_10_8;
	u8 cfg_ssc_en;
	u8 cfg_tx2rx_lp_en;
	u8 cfg_txlb_en;
	u8 cfg_rx2tx_lp_en;
	u8 cfg_rxlb_en;
	u8 r_tx_pol_inv;
	u8 r_rx_pol_inv;
	u8 fx_100;
};

static struct sparx5_sd25g28_media_preset media_presets_25g[] = {
	{ /* ETH_MEDIA_DEFAULT */
		.cfg_en_adv               = 0,
		.cfg_en_main              = 1,
		.cfg_en_dly               = 0,
		.cfg_tap_adv_3_0          = 0,
		.cfg_tap_main             = 1,
		.cfg_tap_dly_4_0          = 0,
		.cfg_eq_c_force_3_0       = 0xf,
		.cfg_vga_ctrl_byp_4_0     = 4,
		.cfg_eq_r_force_3_0       = 12,
		.cfg_alos_thr_2_0         = 7,
	},
	{ /* ETH_MEDIA_SR */
		.cfg_en_adv               = 1,
		.cfg_en_main              = 1,
		.cfg_en_dly               = 1,
		.cfg_tap_adv_3_0          = 0,
		.cfg_tap_main             = 1,
		.cfg_tap_dly_4_0          = 0x10,
		.cfg_eq_c_force_3_0       = 0xf,
		.cfg_vga_ctrl_byp_4_0     = 8,
		.cfg_eq_r_force_3_0       = 4,
		.cfg_alos_thr_2_0         = 0,
	},
	{ /* ETH_MEDIA_DAC */
		.cfg_en_adv               = 0,
		.cfg_en_main              = 1,
		.cfg_en_dly               = 0,
		.cfg_tap_adv_3_0          = 0,
		.cfg_tap_main             = 1,
		.cfg_tap_dly_4_0          = 0,
		.cfg_eq_c_force_3_0       = 0xf,
		.cfg_vga_ctrl_byp_4_0     = 8,
		.cfg_eq_r_force_3_0       = 0xc,
		.cfg_alos_thr_2_0         = 0,
	},
};

static struct sparx5_sd25g28_mode_preset mode_presets_25g[] = {
	{ /* SPX5_SD25G28_MODE_PRESET_25000 */
		.bitwidth           = 40,
		.tx_pre_div         = 0,
		.fifo_ck_div        = 0,
		.pre_divsel         = 1,
		.vco_div_mode       = 0,
		.sel_div            = 15,
		.ck_bitwidth        = 3,
		.subrate            = 0,
		.com_txcal_en       = 0,
		.com_tx_reserve_msb = (0x26 << 1),
		.com_tx_reserve_lsb = 0xf0,
		.cfg_itx_ipcml_base = 0,
		.tx_reserve_msb     = 0xcc,
		.tx_reserve_lsb     = 0xfe,
		.bw                 = 3,
		.rxterm             = 0,
		.dfe_enable         = 1,
		.dfe_tap            = 0x1f,
		.txmargin           = 1,
		.cfg_ctle_rstn      = 1,
		.r_dfe_rstn         = 1,
		.cfg_pi_bw_3_0      = 0,
		.tx_tap_dly         = 8,
		.tx_tap_adv         = 0xc,
	},
	{ /* SPX5_SD25G28_MODE_PRESET_10000 */
		.bitwidth           = 64,
		.tx_pre_div         = 0,
		.fifo_ck_div        = 2,
		.pre_divsel         = 0,
		.vco_div_mode       = 1,
		.sel_div            = 9,
		.ck_bitwidth        = 0,
		.subrate            = 0,
		.com_txcal_en       = 1,
		.com_tx_reserve_msb = (0x20 << 1),
		.com_tx_reserve_lsb = 0x40,
		.cfg_itx_ipcml_base = 0,
		.tx_reserve_msb     = 0x4c,
		.tx_reserve_lsb     = 0x44,
		.bw                 = 3,
		.cfg_pi_bw_3_0      = 0,
		.rxterm             = 3,
		.dfe_enable         = 1,
		.dfe_tap            = 0x1f,
		.txmargin           = 0,
		.cfg_ctle_rstn      = 1,
		.r_dfe_rstn         = 1,
		.tx_tap_dly         = 0,
		.tx_tap_adv         = 0,
	},
	{ /* SPX5_SD25G28_MODE_PRESET_5000 */
		.bitwidth           = 64,
		.tx_pre_div         = 0,
		.fifo_ck_div        = 2,
		.pre_divsel         = 0,
		.vco_div_mode       = 2,
		.sel_div            = 9,
		.ck_bitwidth        = 0,
		.subrate            = 0,
		.com_txcal_en       = 1,
		.com_tx_reserve_msb = (0x20 << 1),
		.com_tx_reserve_lsb = 0,
		.cfg_itx_ipcml_base = 0,
		.tx_reserve_msb     = 0xe,
		.tx_reserve_lsb     = 0x80,
		.bw                 = 0,
		.rxterm             = 0,
		.cfg_pi_bw_3_0      = 6,
		.dfe_enable         = 0,
		.dfe_tap            = 0,
		.tx_tap_dly         = 0,
		.tx_tap_adv         = 0,
	},
	{ /* SPX5_SD25G28_MODE_PRESET_SD_2G5 */
		.bitwidth           = 10,
		.tx_pre_div         = 0,
		.fifo_ck_div        = 0,
		.pre_divsel         = 0,
		.vco_div_mode       = 1,
		.sel_div            = 6,
		.ck_bitwidth        = 3,
		.subrate            = 2,
		.com_txcal_en       = 1,
		.com_tx_reserve_msb = (0x26 << 1),
		.com_tx_reserve_lsb = (0xf << 4),
		.cfg_itx_ipcml_base = 2,
		.tx_reserve_msb     = 0x8,
		.tx_reserve_lsb     = 0x8a,
		.bw                 = 0,
		.cfg_pi_bw_3_0      = 0,
		.rxterm             = (1 << 2),
		.dfe_enable         = 0,
		.dfe_tap            = 0,
		.tx_tap_dly         = 0,
		.tx_tap_adv         = 0,
	},
	{ /* SPX5_SD25G28_MODE_PRESET_1000BASEX */
		.bitwidth           = 10,
		.tx_pre_div         = 0,
		.fifo_ck_div        = 1,
		.pre_divsel         = 0,
		.vco_div_mode       = 1,
		.sel_div            = 8,
		.ck_bitwidth        = 3,
		.subrate            = 3,
		.com_txcal_en       = 1,
		.com_tx_reserve_msb = (0x26 << 1),
		.com_tx_reserve_lsb = 0xf0,
		.cfg_itx_ipcml_base = 0,
		.tx_reserve_msb     = 0x8,
		.tx_reserve_lsb     = 0xce,
		.bw                 = 0,
		.rxterm             = 0,
		.cfg_pi_bw_3_0      = 0,
		.dfe_enable         = 0,
		.dfe_tap            = 0,
		.tx_tap_dly         = 0,
		.tx_tap_adv         = 0,
	},
};

static struct sparx5_sd10g28_media_preset media_presets_10g[] = {
	{ /* ETH_MEDIA_DEFAULT */
		.cfg_en_adv               = 0,
		.cfg_en_main              = 1,
		.cfg_en_dly               = 0,
		.cfg_tap_adv_3_0          = 0,
		.cfg_tap_main             = 1,
		.cfg_tap_dly_4_0          = 0,
		.cfg_vga_ctrl_3_0         = 5,
		.cfg_vga_cp_2_0           = 0,
		.cfg_eq_res_3_0           = 0xa,
		.cfg_eq_r_byp             = 1,
		.cfg_eq_c_force_3_0       = 0x8,
		.cfg_alos_thr_3_0         = 0x3,
	},
	{ /* ETH_MEDIA_SR */
		.cfg_en_adv               = 1,
		.cfg_en_main              = 1,
		.cfg_en_dly               = 1,
		.cfg_tap_adv_3_0          = 0,
		.cfg_tap_main             = 1,
		.cfg_tap_dly_4_0          = 0xc,
		.cfg_vga_ctrl_3_0         = 0xa,
		.cfg_vga_cp_2_0           = 0x4,
		.cfg_eq_res_3_0           = 0xa,
		.cfg_eq_r_byp             = 1,
		.cfg_eq_c_force_3_0       = 0xF,
		.cfg_alos_thr_3_0         = 0x3,
	},
	{ /* ETH_MEDIA_DAC */
		.cfg_en_adv               = 1,
		.cfg_en_main              = 1,
		.cfg_en_dly               = 1,
		.cfg_tap_adv_3_0          = 12,
		.cfg_tap_main             = 1,
		.cfg_tap_dly_4_0          = 8,
		.cfg_vga_ctrl_3_0         = 0xa,
		.cfg_vga_cp_2_0           = 4,
		.cfg_eq_res_3_0           = 0xa,
		.cfg_eq_r_byp             = 1,
		.cfg_eq_c_force_3_0       = 0xf,
		.cfg_alos_thr_3_0         = 0x0,
	}
};

static struct sparx5_sd10g28_mode_preset mode_presets_10g[] = {
	{ /* SPX5_SD10G28_MODE_PRESET_10000 */
		.bwidth           = 64,
		.cmu_sel          = SPX5_SD10G28_CMU_MAIN,
		.rate             = 0x0,
		.dfe_enable       = 1,
		.dfe_tap          = 0x1f,
		.pi_bw_gen1       = 0x0,
		.duty_cycle       = 0x2,
	},
	{ /* SPX5_SD10G28_MODE_PRESET_SFI_5000_6G */
		.bwidth           = 16,
		.cmu_sel          = SPX5_SD10G28_CMU_MAIN,
		.rate             = 0x1,
		.dfe_enable       = 0,
		.dfe_tap          = 0,
		.pi_bw_gen1       = 0x5,
		.duty_cycle       = 0x0,
	},
	{ /* SPX5_SD10G28_MODE_PRESET_SFI_5000_10G */
		.bwidth           = 64,
		.cmu_sel          = SPX5_SD10G28_CMU_MAIN,
		.rate             = 0x1,
		.dfe_enable       = 0,
		.dfe_tap          = 0,
		.pi_bw_gen1       = 0x5,
		.duty_cycle       = 0x0,
	},
	{ /* SPX5_SD10G28_MODE_PRESET_QSGMII */
		.bwidth           = 20,
		.cmu_sel          = SPX5_SD10G28_CMU_AUX1,
		.rate             = 0x1,
		.dfe_enable       = 0,
		.dfe_tap          = 0,
		.pi_bw_gen1       = 0x5,
		.duty_cycle       = 0x0,
	},
	{ /* SPX5_SD10G28_MODE_PRESET_SD_2G5 */
		.bwidth           = 10,
		.cmu_sel          = SPX5_SD10G28_CMU_AUX2,
		.rate             = 0x2,
		.dfe_enable       = 0,
		.dfe_tap          = 0,
		.pi_bw_gen1       = 0x7,
		.duty_cycle       = 0x0,
	},
	{ /* SPX5_SD10G28_MODE_PRESET_1000BASEX */
		.bwidth           = 10,
		.cmu_sel          = SPX5_SD10G28_CMU_AUX1,
		.rate             = 0x3,
		.dfe_enable       = 0,
		.dfe_tap          = 0,
		.pi_bw_gen1       = 0x7,
		.duty_cycle       = 0x0,
	},
};

/* map from SD25G28 interface width to configuration value */
static u8 sd25g28_get_iw_setting(struct device *dev, const u8 interface_width)
{
	switch (interface_width) {
	case 10: return 0;
	case 16: return 1;
	case 32: return 3;
	case 40: return 4;
	case 64: return 5;
	default:
		dev_err(dev, "%s: Illegal value %d for interface width\n",
		       __func__, interface_width);
	}
	return 0;
}

/* map from SD10G28 interface width to configuration value */
static u8 sd10g28_get_iw_setting(struct device *dev, const u8 interface_width)
{
	switch (interface_width) {
	case 10: return 0;
	case 16: return 1;
	case 20: return 2;
	case 32: return 3;
	case 40: return 4;
	case 64: return 7;
	default:
		dev_err(dev, "%s: Illegal value %d for interface width\n", __func__,
		       interface_width);
		return 0;
	}
}

static int sparx5_sd10g25_get_mode_preset(struct sparx5_serdes_macro *macro,
					  struct sparx5_sd25g28_mode_preset *mode)
{
	switch (macro->serdesmode) {
	case SPX5_SD_MODE_SFI:
		if (macro->speed == SPEED_25000)
			*mode = mode_presets_25g[SPX5_SD25G28_MODE_PRESET_25000];
		else if (macro->speed == SPEED_10000)
			*mode = mode_presets_25g[SPX5_SD25G28_MODE_PRESET_10000];
		else if (macro->speed == SPEED_5000)
			*mode = mode_presets_25g[SPX5_SD25G28_MODE_PRESET_5000];
		break;
	case SPX5_SD_MODE_2G5:
		*mode = mode_presets_25g[SPX5_SD25G28_MODE_PRESET_SD_2G5];
		break;
	case SPX5_SD_MODE_1000BASEX:
		*mode = mode_presets_25g[SPX5_SD25G28_MODE_PRESET_1000BASEX];
		break;
	case SPX5_SD_MODE_100FX:
		 /* Not supported */
		return -EINVAL;
	default:
		*mode = mode_presets_25g[SPX5_SD25G28_MODE_PRESET_25000];
		break;
	}
	return 0;
}

static int sparx5_sd10g28_get_mode_preset(struct sparx5_serdes_macro *macro,
					  struct sparx5_sd10g28_mode_preset *mode,
					  struct sparx5_sd10g28_args *args)
{
	switch (macro->serdesmode) {
	case SPX5_SD_MODE_SFI:
		if (macro->speed == SPEED_10000) {
			*mode = mode_presets_10g[SPX5_SD10G28_MODE_PRESET_10000];
		} else if (macro->speed == SPEED_5000) {
			if (args->is_6g)
				*mode = mode_presets_10g[SPX5_SD10G28_MODE_PRESET_SFI_5000_6G];
			else
				*mode = mode_presets_10g[SPX5_SD10G28_MODE_PRESET_SFI_5000_10G];
		} else {
			dev_err(macro->priv->dev, "%s: Illegal speed: %02u, sidx: %02u, mode (%u)",
			       __func__, macro->speed, macro->sidx,
			       macro->serdesmode);
			return -EINVAL;
		}
		break;
	case SPX5_SD_MODE_QSGMII:
		*mode = mode_presets_10g[SPX5_SD10G28_MODE_PRESET_QSGMII];
		break;
	case SPX5_SD_MODE_2G5:
		*mode = mode_presets_10g[SPX5_SD10G28_MODE_PRESET_SD_2G5];
		break;
	case SPX5_SD_MODE_100FX:
	case SPX5_SD_MODE_1000BASEX:
		*mode = mode_presets_10g[SPX5_SD10G28_MODE_PRESET_1000BASEX];
		break;
	default:
		*mode = mode_presets_10g[SPX5_SD10G28_MODE_PRESET_10000];
		break;
	}
	return 0;
}

static void sparx5_sd25g28_get_params(struct sparx5_serdes_macro *macro,
				      struct sparx5_sd25g28_media_preset *media,
				      struct sparx5_sd25g28_mode_preset *mode,
				      struct sparx5_sd25g28_args *args,
				      struct sparx5_sd25g28_params *params)
{
	u8 iw = sd25g28_get_iw_setting(macro->priv->dev, mode->bitwidth);
	struct sparx5_sd25g28_params init = {
		.r_d_width_ctrl_2_0         = iw,
		.r_txfifo_ck_div_pmad_2_0   = mode->fifo_ck_div,
		.r_rxfifo_ck_div_pmad_2_0   = mode->fifo_ck_div,
		.cfg_vco_div_mode_1_0       = mode->vco_div_mode,
		.cfg_pre_divsel_1_0         = mode->pre_divsel,
		.cfg_sel_div_3_0            = mode->sel_div,
		.cfg_vco_start_code_3_0     = 0,
		.cfg_pma_tx_ck_bitwidth_2_0 = mode->ck_bitwidth,
		.cfg_tx_prediv_1_0          = mode->tx_pre_div,
		.cfg_rxdiv_sel_2_0          = mode->ck_bitwidth,
		.cfg_tx_subrate_2_0         = mode->subrate,
		.cfg_rx_subrate_2_0         = mode->subrate,
		.r_multi_lane_mode          = 0,
		.cfg_cdrck_en               = 1,
		.cfg_dfeck_en               = mode->dfe_enable,
		.cfg_dfe_pd                 = mode->dfe_enable == 1 ? 0 : 1,
		.cfg_dfedmx_pd              = 1,
		.cfg_dfetap_en_5_1          = mode->dfe_tap,
		.cfg_dmux_pd                = 0,
		.cfg_dmux_clk_pd            = 1,
		.cfg_erramp_pd              = mode->dfe_enable == 1 ? 0 : 1,
		.cfg_pi_DFE_en              = mode->dfe_enable,
		.cfg_pi_en                  = 1,
		.cfg_pd_ctle                = 0,
		.cfg_summer_en              = 1,
		.cfg_pmad_ck_pd             = 0,
		.cfg_pd_clk                 = 0,
		.cfg_pd_cml                 = 0,
		.cfg_pd_driver              = 0,
		.cfg_rx_reg_pu              = 1,
		.cfg_pd_rms_det             = 1,
		.cfg_dcdr_pd                = 0,
		.cfg_ecdr_pd                = 1,
		.cfg_pd_sq                  = 1,
		.cfg_itx_ipdriver_base_2_0  = mode->txmargin,
		.cfg_tap_dly_4_0            = media->cfg_tap_dly_4_0,
		.cfg_tap_main               = media->cfg_tap_main,
		.cfg_en_main                = media->cfg_en_main,
		.cfg_tap_adv_3_0            = media->cfg_tap_adv_3_0,
		.cfg_en_adv                 = media->cfg_en_adv,
		.cfg_en_dly                 = media->cfg_en_dly,
		.cfg_iscan_en               = 0,
		.l1_pcs_en_fast_iscan       = 0,
		.l0_cfg_bw_1_0              = 0,
		.cfg_en_dummy               = 0,
		.cfg_pll_reserve_3_0        = args->com_pll_reserve,
		.l0_cfg_txcal_en            = mode->com_txcal_en,
		.l0_cfg_tx_reserve_15_8     = mode->com_tx_reserve_msb,
		.l0_cfg_tx_reserve_7_0      = mode->com_tx_reserve_lsb,
		.cfg_tx_reserve_15_8        = mode->tx_reserve_msb,
		.cfg_tx_reserve_7_0         = mode->tx_reserve_lsb,
		.cfg_bw_1_0                 = mode->bw,
		.cfg_txcal_man_en           = 1,
		.cfg_phase_man_4_0          = 0,
		.cfg_quad_man_1_0           = 0,
		.cfg_txcal_shift_code_5_0   = 2,
		.cfg_txcal_valid_sel_3_0    = 4,
		.cfg_txcal_en               = 0,
		.cfg_cdr_kf_2_0             = 1,
		.cfg_cdr_m_7_0              = 6,
		.cfg_pi_bw_3_0              = mode->cfg_pi_bw_3_0,
		.cfg_pi_steps_1_0           = 0,
		.cfg_dis_2ndorder           = 1,
		.cfg_ctle_rstn              = mode->cfg_ctle_rstn,
		.r_dfe_rstn                 = mode->r_dfe_rstn,
		.cfg_alos_thr_2_0           = media->cfg_alos_thr_2_0,
		.cfg_itx_ipcml_base_1_0     = mode->cfg_itx_ipcml_base,
		.cfg_rx_reserve_7_0         = 0xbf,
		.cfg_rx_reserve_15_8        = 0x61,
		.cfg_rxterm_2_0             = mode->rxterm,
		.cfg_fom_selm               = 0,
		.cfg_rx_sp_ctle_1_0         = 0,
		.cfg_isel_ctle_1_0          = 0,
		.cfg_vga_ctrl_byp_4_0       = media->cfg_vga_ctrl_byp_4_0,
		.cfg_vga_byp                = 1,
		.cfg_agc_adpt_byp           = 1,
		.cfg_eqr_byp                = 1,
		.cfg_eqr_force_3_0          = media->cfg_eq_r_force_3_0,
		.cfg_eqc_force_3_0          = media->cfg_eq_c_force_3_0,
		.cfg_sum_setcm_en           = 1,
		.cfg_pi_dfe_en              = 1,
		.cfg_init_pos_iscan_6_0     = 6,
		.cfg_init_pos_ipi_6_0       = 9,
		.cfg_dfedig_m_2_0           = 6,
		.cfg_en_dfedig              = mode->dfe_enable,
		.r_d_width_ctrl_from_hwt    = 0,
		.r_reg_manual               = 1,
		.reg_rst                    = args->reg_rst,
		.cfg_jc_byp                 = 1,
		.cfg_common_reserve_7_0     = 1,
		.cfg_pll_lol_set            = 1,
		.cfg_tx2rx_lp_en            = 0,
		.cfg_txlb_en                = 0,
		.cfg_rx2tx_lp_en            = 0,
		.cfg_rxlb_en                = 0,
		.r_tx_pol_inv               = args->txinvert,
		.r_rx_pol_inv               = args->rxinvert,
	};

	*params = init;
}

static void sparx5_sd10g28_get_params(struct sparx5_serdes_macro *macro,
				      struct sparx5_sd10g28_media_preset *media,
				      struct sparx5_sd10g28_mode_preset *mode,
				      struct sparx5_sd10g28_args *args,
				      struct sparx5_sd10g28_params *params)
{
	u8 iw = sd10g28_get_iw_setting(macro->priv->dev, mode->bwidth);
	struct sparx5_sd10g28_params init = {
		.skip_cmu_cfg                = args->skip_cmu_cfg,
		.is_6g                       = args->is_6g,
		.cmu_sel                     = mode->cmu_sel,
		.cfg_lane_reserve_7_0        = (mode->cmu_sel % 2) << 6,
		.cfg_ssc_rtl_clk_sel         = (mode->cmu_sel / 2),
		.cfg_lane_reserve_15_8       = mode->duty_cycle,
		.cfg_txrate_1_0              = mode->rate,
		.cfg_rxrate_1_0              = mode->rate,
		.fx_100                      = macro->serdesmode == SPX5_SD_MODE_100FX,
		.r_d_width_ctrl_2_0          = iw,
		.cfg_pma_tx_ck_bitwidth_2_0  = iw,
		.cfg_rxdiv_sel_2_0           = iw,
		.r_pcs2pma_phymode_4_0       = 0,
		.cfg_lane_id_2_0             = 0,
		.cfg_cdrck_en                = 1,
		.cfg_dfeck_en                = mode->dfe_enable,
		.cfg_dfe_pd                  = (mode->dfe_enable == 1) ? 0 : 1,
		.cfg_dfetap_en_5_1           = mode->dfe_tap,
		.cfg_erramp_pd               = (mode->dfe_enable == 1) ? 0 : 1,
		.cfg_pi_DFE_en               = mode->dfe_enable,
		.cfg_pi_en                   = 1,
		.cfg_pd_ctle                 = 0,
		.cfg_summer_en               = 1,
		.cfg_pd_rx_cktree            = 0,
		.cfg_pd_clk                  = 0,
		.cfg_pd_cml                  = 0,
		.cfg_pd_driver               = 0,
		.cfg_rx_reg_pu               = 1,
		.cfg_d_cdr_pd                = 0,
		.cfg_pd_sq                   = mode->dfe_enable,
		.cfg_rxdet_en                = 0,
		.cfg_rxdet_str               = 0,
		.r_multi_lane_mode           = 0,
		.cfg_en_adv                  = media->cfg_en_adv,
		.cfg_en_main                 = 1,
		.cfg_en_dly                  = media->cfg_en_dly,
		.cfg_tap_adv_3_0             = media->cfg_tap_adv_3_0,
		.cfg_tap_main                = media->cfg_tap_main,
		.cfg_tap_dly_4_0             = media->cfg_tap_dly_4_0,
		.cfg_vga_ctrl_3_0            = media->cfg_vga_ctrl_3_0,
		.cfg_vga_cp_2_0              = media->cfg_vga_cp_2_0,
		.cfg_eq_res_3_0              = media->cfg_eq_res_3_0,
		.cfg_eq_r_byp                = media->cfg_eq_r_byp,
		.cfg_eq_c_force_3_0          = media->cfg_eq_c_force_3_0,
		.cfg_en_dfedig               = mode->dfe_enable,
		.cfg_sum_setcm_en            = 1,
		.cfg_en_preemph              = 0,
		.cfg_itx_ippreemp_base_1_0   = 0,
		.cfg_itx_ipdriver_base_2_0   = (args->txswing >> 6),
		.cfg_ibias_tune_reserve_5_0  = (args->txswing & 63),
		.cfg_txswing_half            = (args->txmargin),
		.cfg_dis_2nd_order           = 0x1,
		.cfg_rx_ssc_lh               = 0x0,
		.cfg_pi_floop_steps_1_0      = 0x0,
		.cfg_pi_ext_dac_23_16        = (1 << 5),
		.cfg_pi_ext_dac_15_8         = (0 << 6),
		.cfg_iscan_ext_dac_7_0       = (1 << 7) + 9,
		.cfg_cdr_kf_gen1_2_0         = 1,
		.cfg_cdr_kf_gen2_2_0         = 1,
		.cfg_cdr_kf_gen3_2_0         = 1,
		.cfg_cdr_kf_gen4_2_0         = 1,
		.r_cdr_m_gen1_7_0            = 4,
		.cfg_pi_bw_gen1_3_0          = mode->pi_bw_gen1,
		.cfg_pi_bw_gen2              = mode->pi_bw_gen1,
		.cfg_pi_bw_gen3              = mode->pi_bw_gen1,
		.cfg_pi_bw_gen4              = mode->pi_bw_gen1,
		.cfg_pi_ext_dac_7_0          = 3,
		.cfg_pi_steps                = 0,
		.cfg_mp_max_3_0              = 1,
		.cfg_rstn_dfedig             = mode->dfe_enable,
		.cfg_alos_thr_3_0            = media->cfg_alos_thr_3_0,
		.cfg_predrv_slewrate_1_0     = 3,
		.cfg_itx_ipcml_base_1_0      = 0,
		.cfg_ip_pre_base_1_0         = 0,
		.r_cdr_m_gen2_7_0            = 2,
		.r_cdr_m_gen3_7_0            = 2,
		.r_cdr_m_gen4_7_0            = 2,
		.r_en_auto_cdr_rstn          = 0,
		.cfg_oscal_afe               = 1,
		.cfg_pd_osdac_afe            = 0,
		.cfg_resetb_oscal_afe[0]     = 0,
		.cfg_resetb_oscal_afe[1]     = 1,
		.cfg_center_spreading        = 0,
		.cfg_m_cnt_maxval_4_0        = 15,
		.cfg_ncnt_maxval_7_0         = 32,
		.cfg_ncnt_maxval_10_8        = 6,
		.cfg_ssc_en                  = 1,
		.cfg_tx2rx_lp_en             = 0,
		.cfg_txlb_en                 = 0,
		.cfg_rx2tx_lp_en             = 0,
		.cfg_rxlb_en                 = 0,
		.r_tx_pol_inv                = args->txinvert,
		.r_rx_pol_inv                = args->rxinvert,
	};

	*params = init;
}

static int sparx5_cmu_apply_cfg(struct sparx5_serdes_private *priv,
				u32 cmu_idx,
				void __iomem *cmu_tgt,
				void __iomem *cmu_cfg_tgt,
				u32 spd10g)
{
	void __iomem **regs = priv->regs;
	struct device *dev = priv->dev;
	int value;

	cmu_tgt = sdx5_inst_get(priv, TARGET_SD_CMU, cmu_idx);
	cmu_cfg_tgt = sdx5_inst_get(priv, TARGET_SD_CMU_CFG, cmu_idx);

	if (cmu_idx == 1 || cmu_idx == 4 || cmu_idx == 7 ||
	    cmu_idx == 10 || cmu_idx == 13) {
		spd10g = 0;
	}

	sdx5_inst_rmw(SD_CMU_CFG_SD_CMU_CFG_EXT_CFG_RST_SET(1),
		      SD_CMU_CFG_SD_CMU_CFG_EXT_CFG_RST,
		      cmu_cfg_tgt,
		      SD_CMU_CFG_SD_CMU_CFG(cmu_idx));

	sdx5_inst_rmw(SD_CMU_CFG_SD_CMU_CFG_EXT_CFG_RST_SET(0),
		      SD_CMU_CFG_SD_CMU_CFG_EXT_CFG_RST,
		      cmu_cfg_tgt,
		      SD_CMU_CFG_SD_CMU_CFG(cmu_idx));

	sdx5_inst_rmw(SD_CMU_CFG_SD_CMU_CFG_CMU_RST_SET(1),
		      SD_CMU_CFG_SD_CMU_CFG_CMU_RST,
		      cmu_cfg_tgt,
		      SD_CMU_CFG_SD_CMU_CFG(cmu_idx));

	sdx5_inst_rmw(SD_CMU_CMU_45_R_DWIDTHCTRL_FROM_HWT_SET(0x1) |
		      SD_CMU_CMU_45_R_REFCK_SSC_EN_FROM_HWT_SET(0x1) |
		      SD_CMU_CMU_45_R_LINK_BUF_EN_FROM_HWT_SET(0x1) |
		      SD_CMU_CMU_45_R_BIAS_EN_FROM_HWT_SET(0x1) |
		      SD_CMU_CMU_45_R_EN_RATECHG_CTRL_SET(0x0),
		      SD_CMU_CMU_45_R_DWIDTHCTRL_FROM_HWT |
		      SD_CMU_CMU_45_R_REFCK_SSC_EN_FROM_HWT |
		      SD_CMU_CMU_45_R_LINK_BUF_EN_FROM_HWT |
		      SD_CMU_CMU_45_R_BIAS_EN_FROM_HWT |
		      SD_CMU_CMU_45_R_EN_RATECHG_CTRL,
		      cmu_tgt,
		      SD_CMU_CMU_45(cmu_idx));

	sdx5_inst_rmw(SD_CMU_CMU_47_R_PCS2PMA_PHYMODE_4_0_SET(0),
		      SD_CMU_CMU_47_R_PCS2PMA_PHYMODE_4_0,
		      cmu_tgt,
		      SD_CMU_CMU_47(cmu_idx));

	sdx5_inst_rmw(SD_CMU_CMU_1B_CFG_RESERVE_7_0_SET(0),
		      SD_CMU_CMU_1B_CFG_RESERVE_7_0,
		      cmu_tgt,
		      SD_CMU_CMU_1B(cmu_idx));

	sdx5_inst_rmw(SD_CMU_CMU_0D_CFG_JC_BYP_SET(0x1),
		      SD_CMU_CMU_0D_CFG_JC_BYP,
		      cmu_tgt,
		      SD_CMU_CMU_0D(cmu_idx));

	sdx5_inst_rmw(SD_CMU_CMU_1F_CFG_VTUNE_SEL_SET(1),
		      SD_CMU_CMU_1F_CFG_VTUNE_SEL,
		      cmu_tgt,
		      SD_CMU_CMU_1F(cmu_idx));

	sdx5_inst_rmw(SD_CMU_CMU_00_CFG_PLL_TP_SEL_1_0_SET(3),
		      SD_CMU_CMU_00_CFG_PLL_TP_SEL_1_0,
		      cmu_tgt,
		      SD_CMU_CMU_00(cmu_idx));

	sdx5_inst_rmw(SD_CMU_CMU_05_CFG_BIAS_TP_SEL_1_0_SET(3),
		      SD_CMU_CMU_05_CFG_BIAS_TP_SEL_1_0,
		      cmu_tgt,
		      SD_CMU_CMU_05(cmu_idx));

	sdx5_inst_rmw(SD_CMU_CMU_30_R_PLL_DLOL_EN_SET(1),
		      SD_CMU_CMU_30_R_PLL_DLOL_EN,
		      cmu_tgt,
		      SD_CMU_CMU_30(cmu_idx));

	sdx5_inst_rmw(SD_CMU_CMU_09_CFG_SW_10G_SET(spd10g),
		      SD_CMU_CMU_09_CFG_SW_10G,
		      cmu_tgt,
		      SD_CMU_CMU_09(cmu_idx));

	sdx5_inst_rmw(SD_CMU_CFG_SD_CMU_CFG_CMU_RST_SET(0),
		      SD_CMU_CFG_SD_CMU_CFG_CMU_RST,
		      cmu_cfg_tgt,
		      SD_CMU_CFG_SD_CMU_CFG(cmu_idx));

	msleep(20);

	sdx5_inst_rmw(SD_CMU_CMU_44_R_PLL_RSTN_SET(0),
		      SD_CMU_CMU_44_R_PLL_RSTN,
		      cmu_tgt,
		      SD_CMU_CMU_44(cmu_idx));

	sdx5_inst_rmw(SD_CMU_CMU_44_R_PLL_RSTN_SET(1),
		      SD_CMU_CMU_44_R_PLL_RSTN,
		      cmu_tgt,
		      SD_CMU_CMU_44(cmu_idx));

	msleep(20);

	value = readl(sdx5_addr(regs, SD_CMU_CMU_E0(cmu_idx)));
	value = SD_CMU_CMU_E0_PLL_LOL_UDL_GET(value);

	if (value) {
		dev_err(dev, "CMU PLL Loss of Lock: 0x%x\n", value);
		return -EINVAL;
	}
	sdx5_inst_rmw(SD_CMU_CMU_0D_CFG_PMA_TX_CK_PD_SET(0),
		      SD_CMU_CMU_0D_CFG_PMA_TX_CK_PD,
		      cmu_tgt,
		      SD_CMU_CMU_0D(cmu_idx));
	return 0;
}

static int sparx5_cmu_cfg(struct sparx5_serdes_private *priv, u32 cmu_idx)
{
	void __iomem *cmu_tgt, *cmu_cfg_tgt;
	u32 spd10g = 1;

	if (cmu_idx == 1 || cmu_idx == 4 || cmu_idx == 7 ||
	    cmu_idx == 10 || cmu_idx == 13) {
		spd10g = 0;
	}

	cmu_tgt = sdx5_inst_get(priv, TARGET_SD_CMU, cmu_idx);
	cmu_cfg_tgt = sdx5_inst_get(priv, TARGET_SD_CMU_CFG, cmu_idx);

	return sparx5_cmu_apply_cfg(priv, cmu_idx, cmu_tgt, cmu_cfg_tgt, spd10g);
}

/* Map of 6G/10G serdes mode and index to CMU index. */
static const int
sparx5_serdes_cmu_map[SPX5_SD10G28_CMU_MAX][SPX5_SERDES_6G10G_CNT] = {
	[SPX5_SD10G28_CMU_MAIN] = {  2,  2,  2,  2,  2,
				     2,  2,  2,  5,  5,
				     5,  5,  5,  5,  5,
				     5,  8, 11, 11, 11,
				    11, 11, 11, 11, 11 },
	[SPX5_SD10G28_CMU_AUX1] = {  0,  0,  3,  3,  3,
				     3,  3,  3,  3,  3,
				     6,  6,  6,  6,  6,
				     6,  6,  9,  9, 12,
				    12, 12, 12, 12, 12 },
	[SPX5_SD10G28_CMU_AUX2] = {  1,  1,  1,  1,  4,
				     4,  4,  4,  4,  4,
				     4,  4,  7,  7,  7,
				     7,  7, 10, 10, 10,
				    10, 13, 13, 13, 13 },
	[SPX5_SD10G28_CMU_NONE] = {  1,  1,  1,  1,  4,
				     4,  4,  4,  4,  4,
				     4,  4,  7,  7,  7,
				     7,  7, 10, 10, 10,
				    10, 13, 13, 13, 13 },
};

/* Get the index of the CMU which provides the clock for the specified serdes
 * mode and index.
 */
static int sparx5_serdes_cmu_get(enum sparx5_10g28cmu_mode mode, int sd_index)
{
	return sparx5_serdes_cmu_map[mode][sd_index];
}

static void sparx5_serdes_cmu_power_off(struct sparx5_serdes_private *priv)
{
	void __iomem *cmu_inst, *cmu_cfg_inst;
	int i;

	/* Power down each CMU */
	for (i = 0; i < SPX5_CMU_MAX; i++) {
		cmu_inst = sdx5_inst_get(priv, TARGET_SD_CMU, i);
		cmu_cfg_inst = sdx5_inst_get(priv, TARGET_SD_CMU_CFG, i);

		sdx5_inst_rmw(SD_CMU_CFG_SD_CMU_CFG_EXT_CFG_RST_SET(0),
			      SD_CMU_CFG_SD_CMU_CFG_EXT_CFG_RST, cmu_cfg_inst,
			      SD_CMU_CFG_SD_CMU_CFG(0));

		sdx5_inst_rmw(SD_CMU_CMU_05_CFG_REFCK_TERM_EN_SET(0),
			      SD_CMU_CMU_05_CFG_REFCK_TERM_EN, cmu_inst,
			      SD_CMU_CMU_05(0));

		sdx5_inst_rmw(SD_CMU_CMU_09_CFG_EN_TX_CK_DN_SET(0),
			      SD_CMU_CMU_09_CFG_EN_TX_CK_DN, cmu_inst,
			      SD_CMU_CMU_09(0));

		sdx5_inst_rmw(SD_CMU_CMU_06_CFG_VCO_PD_SET(1),
			      SD_CMU_CMU_06_CFG_VCO_PD, cmu_inst,
			      SD_CMU_CMU_06(0));

		sdx5_inst_rmw(SD_CMU_CMU_09_CFG_EN_TX_CK_UP_SET(0),
			      SD_CMU_CMU_09_CFG_EN_TX_CK_UP, cmu_inst,
			      SD_CMU_CMU_09(0));

		sdx5_inst_rmw(SD_CMU_CMU_08_CFG_CK_TREE_PD_SET(1),
			      SD_CMU_CMU_08_CFG_CK_TREE_PD, cmu_inst,
			      SD_CMU_CMU_08(0));

		sdx5_inst_rmw(SD_CMU_CMU_0D_CFG_REFCK_PD_SET(1) |
			      SD_CMU_CMU_0D_CFG_PD_DIV64_SET(1) |
			      SD_CMU_CMU_0D_CFG_PD_DIV66_SET(1),
			      SD_CMU_CMU_0D_CFG_REFCK_PD |
			      SD_CMU_CMU_0D_CFG_PD_DIV64 |
			      SD_CMU_CMU_0D_CFG_PD_DIV66, cmu_inst,
			      SD_CMU_CMU_0D(0));

		sdx5_inst_rmw(SD_CMU_CMU_06_CFG_CTRL_LOGIC_PD_SET(1),
			      SD_CMU_CMU_06_CFG_CTRL_LOGIC_PD, cmu_inst,
			      SD_CMU_CMU_06(0));
	}
}

static void sparx5_sd25g28_reset(void __iomem *regs[],
				 struct sparx5_sd25g28_params *params,
				 u32 sd_index)
{
	if (params->reg_rst == 1) {
		sdx5_rmw_addr(SD_LANE_25G_SD_LANE_CFG_EXT_CFG_RST_SET(1),
			 SD_LANE_25G_SD_LANE_CFG_EXT_CFG_RST,
			 sdx5_addr(regs, SD_LANE_25G_SD_LANE_CFG(sd_index)));

		usleep_range(1000, 2000);

		sdx5_rmw_addr(SD_LANE_25G_SD_LANE_CFG_EXT_CFG_RST_SET(0),
			 SD_LANE_25G_SD_LANE_CFG_EXT_CFG_RST,
			 sdx5_addr(regs, SD_LANE_25G_SD_LANE_CFG(sd_index)));
	}
}

static int sparx5_sd25g28_apply_params(struct sparx5_serdes_macro *macro,
				       struct sparx5_sd25g28_params *params)
{
	struct sparx5_serdes_private *priv = macro->priv;
	void __iomem **regs = priv->regs;
	struct device *dev = priv->dev;
	u32 sd_index = macro->stpidx;
	u32 value;

	sdx5_rmw(SD_LANE_25G_SD_LANE_CFG_MACRO_RST_SET(1),
		 SD_LANE_25G_SD_LANE_CFG_MACRO_RST,
		 priv,
		 SD_LANE_25G_SD_LANE_CFG(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_FF_REGISTER_TABLE_INDEX_SET(0xFF),
		 SD25G_LANE_CMU_FF_REGISTER_TABLE_INDEX,
		 priv,
		 SD25G_LANE_CMU_FF(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_1A_R_DWIDTHCTRL_FROM_HWT_SET
		 (params->r_d_width_ctrl_from_hwt) |
		 SD25G_LANE_CMU_1A_R_REG_MANUAL_SET(params->r_reg_manual),
		 SD25G_LANE_CMU_1A_R_DWIDTHCTRL_FROM_HWT |
		 SD25G_LANE_CMU_1A_R_REG_MANUAL,
		 priv,
		 SD25G_LANE_CMU_1A(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_31_CFG_COMMON_RESERVE_7_0_SET
		 (params->cfg_common_reserve_7_0),
		 SD25G_LANE_CMU_31_CFG_COMMON_RESERVE_7_0,
		 priv,
		 SD25G_LANE_CMU_31(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_09_CFG_EN_DUMMY_SET(params->cfg_en_dummy),
		 SD25G_LANE_CMU_09_CFG_EN_DUMMY,
		 priv,
		 SD25G_LANE_CMU_09(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_13_CFG_PLL_RESERVE_3_0_SET
		 (params->cfg_pll_reserve_3_0),
		 SD25G_LANE_CMU_13_CFG_PLL_RESERVE_3_0,
		 priv,
		 SD25G_LANE_CMU_13(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_40_L0_CFG_TXCAL_EN_SET(params->l0_cfg_txcal_en),
		 SD25G_LANE_CMU_40_L0_CFG_TXCAL_EN,
		 priv,
		 SD25G_LANE_CMU_40(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_46_L0_CFG_TX_RESERVE_15_8_SET
		 (params->l0_cfg_tx_reserve_15_8),
		 SD25G_LANE_CMU_46_L0_CFG_TX_RESERVE_15_8,
		 priv,
		 SD25G_LANE_CMU_46(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_45_L0_CFG_TX_RESERVE_7_0_SET
		 (params->l0_cfg_tx_reserve_7_0),
		 SD25G_LANE_CMU_45_L0_CFG_TX_RESERVE_7_0,
		 priv,
		 SD25G_LANE_CMU_45(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_0B_CFG_VCO_CAL_RESETN_SET(0),
		 SD25G_LANE_CMU_0B_CFG_VCO_CAL_RESETN,
		 priv,
		 SD25G_LANE_CMU_0B(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_0B_CFG_VCO_CAL_RESETN_SET(1),
		 SD25G_LANE_CMU_0B_CFG_VCO_CAL_RESETN,
		 priv,
		 SD25G_LANE_CMU_0B(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_19_R_CK_RESETB_SET(0),
		 SD25G_LANE_CMU_19_R_CK_RESETB,
		 priv,
		 SD25G_LANE_CMU_19(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_19_R_CK_RESETB_SET(1),
		 SD25G_LANE_CMU_19_R_CK_RESETB,
		 priv,
		 SD25G_LANE_CMU_19(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_18_R_PLL_RSTN_SET(0),
		 SD25G_LANE_CMU_18_R_PLL_RSTN,
		 priv,
		 SD25G_LANE_CMU_18(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_18_R_PLL_RSTN_SET(1),
		 SD25G_LANE_CMU_18_R_PLL_RSTN,
		 priv,
		 SD25G_LANE_CMU_18(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_1A_R_DWIDTHCTRL_2_0_SET(params->r_d_width_ctrl_2_0),
		 SD25G_LANE_CMU_1A_R_DWIDTHCTRL_2_0,
		 priv,
		 SD25G_LANE_CMU_1A(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_30_R_TXFIFO_CK_DIV_PMAD_2_0_SET
		 (params->r_txfifo_ck_div_pmad_2_0) |
		 SD25G_LANE_CMU_30_R_RXFIFO_CK_DIV_PMAD_2_0_SET
		 (params->r_rxfifo_ck_div_pmad_2_0),
		 SD25G_LANE_CMU_30_R_TXFIFO_CK_DIV_PMAD_2_0 |
		 SD25G_LANE_CMU_30_R_RXFIFO_CK_DIV_PMAD_2_0,
		 priv,
		 SD25G_LANE_CMU_30(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_0C_CFG_PLL_LOL_SET_SET(params->cfg_pll_lol_set) |
		 SD25G_LANE_CMU_0C_CFG_VCO_DIV_MODE_1_0_SET
		 (params->cfg_vco_div_mode_1_0),
		 SD25G_LANE_CMU_0C_CFG_PLL_LOL_SET |
		 SD25G_LANE_CMU_0C_CFG_VCO_DIV_MODE_1_0,
		 priv,
		 SD25G_LANE_CMU_0C(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_0D_CFG_PRE_DIVSEL_1_0_SET
		 (params->cfg_pre_divsel_1_0),
		 SD25G_LANE_CMU_0D_CFG_PRE_DIVSEL_1_0,
		 priv,
		 SD25G_LANE_CMU_0D(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_0E_CFG_SEL_DIV_3_0_SET(params->cfg_sel_div_3_0),
		 SD25G_LANE_CMU_0E_CFG_SEL_DIV_3_0,
		 priv,
		 SD25G_LANE_CMU_0E(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_FF_REGISTER_TABLE_INDEX_SET(0x00),
		 SD25G_LANE_CMU_FF_REGISTER_TABLE_INDEX,
		 priv,
		 SD25G_LANE_CMU_FF(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_0C_LN_CFG_PMA_TX_CK_BITWIDTH_2_0_SET
		 (params->cfg_pma_tx_ck_bitwidth_2_0),
		 SD25G_LANE_LANE_0C_LN_CFG_PMA_TX_CK_BITWIDTH_2_0,
		 priv,
		 SD25G_LANE_LANE_0C(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_01_LN_CFG_TX_PREDIV_1_0_SET
		 (params->cfg_tx_prediv_1_0),
		 SD25G_LANE_LANE_01_LN_CFG_TX_PREDIV_1_0,
		 priv,
		 SD25G_LANE_LANE_01(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_18_LN_CFG_RXDIV_SEL_2_0_SET
		 (params->cfg_rxdiv_sel_2_0),
		 SD25G_LANE_LANE_18_LN_CFG_RXDIV_SEL_2_0,
		 priv,
		 SD25G_LANE_LANE_18(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_2C_LN_CFG_TX_SUBRATE_2_0_SET
		 (params->cfg_tx_subrate_2_0),
		 SD25G_LANE_LANE_2C_LN_CFG_TX_SUBRATE_2_0,
		 priv,
		 SD25G_LANE_LANE_2C(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_28_LN_CFG_RX_SUBRATE_2_0_SET
		 (params->cfg_rx_subrate_2_0),
		 SD25G_LANE_LANE_28_LN_CFG_RX_SUBRATE_2_0,
		 priv,
		 SD25G_LANE_LANE_28(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_18_LN_CFG_CDRCK_EN_SET(params->cfg_cdrck_en),
		 SD25G_LANE_LANE_18_LN_CFG_CDRCK_EN,
		 priv,
		 SD25G_LANE_LANE_18(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_0F_LN_CFG_DFETAP_EN_5_1_SET
		 (params->cfg_dfetap_en_5_1),
		 SD25G_LANE_LANE_0F_LN_CFG_DFETAP_EN_5_1,
		 priv,
		 SD25G_LANE_LANE_0F(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_18_LN_CFG_ERRAMP_PD_SET(params->cfg_erramp_pd),
		 SD25G_LANE_LANE_18_LN_CFG_ERRAMP_PD,
		 priv,
		 SD25G_LANE_LANE_18(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_1D_LN_CFG_PI_DFE_EN_SET(params->cfg_pi_dfe_en),
		 SD25G_LANE_LANE_1D_LN_CFG_PI_DFE_EN,
		 priv,
		 SD25G_LANE_LANE_1D(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_19_LN_CFG_ECDR_PD_SET(params->cfg_ecdr_pd),
		 SD25G_LANE_LANE_19_LN_CFG_ECDR_PD,
		 priv,
		 SD25G_LANE_LANE_19(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_01_LN_CFG_ITX_IPDRIVER_BASE_2_0_SET
		 (params->cfg_itx_ipdriver_base_2_0),
		 SD25G_LANE_LANE_01_LN_CFG_ITX_IPDRIVER_BASE_2_0,
		 priv,
		 SD25G_LANE_LANE_01(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_03_LN_CFG_TAP_DLY_4_0_SET(params->cfg_tap_dly_4_0),
		 SD25G_LANE_LANE_03_LN_CFG_TAP_DLY_4_0,
		 priv,
		 SD25G_LANE_LANE_03(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_06_LN_CFG_TAP_ADV_3_0_SET(params->cfg_tap_adv_3_0),
		 SD25G_LANE_LANE_06_LN_CFG_TAP_ADV_3_0,
		 priv,
		 SD25G_LANE_LANE_06(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_07_LN_CFG_EN_ADV_SET(params->cfg_en_adv) |
		 SD25G_LANE_LANE_07_LN_CFG_EN_DLY_SET(params->cfg_en_dly),
		 SD25G_LANE_LANE_07_LN_CFG_EN_ADV |
		 SD25G_LANE_LANE_07_LN_CFG_EN_DLY,
		 priv,
		 SD25G_LANE_LANE_07(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_43_LN_CFG_TX_RESERVE_15_8_SET
		 (params->cfg_tx_reserve_15_8),
		 SD25G_LANE_LANE_43_LN_CFG_TX_RESERVE_15_8,
		 priv,
		 SD25G_LANE_LANE_43(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_42_LN_CFG_TX_RESERVE_7_0_SET
		 (params->cfg_tx_reserve_7_0),
		 SD25G_LANE_LANE_42_LN_CFG_TX_RESERVE_7_0,
		 priv,
		 SD25G_LANE_LANE_42(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_05_LN_CFG_BW_1_0_SET(params->cfg_bw_1_0),
		 SD25G_LANE_LANE_05_LN_CFG_BW_1_0,
		 priv,
		 SD25G_LANE_LANE_05(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_0B_LN_CFG_TXCAL_MAN_EN_SET
		 (params->cfg_txcal_man_en),
		 SD25G_LANE_LANE_0B_LN_CFG_TXCAL_MAN_EN,
		 priv,
		 SD25G_LANE_LANE_0B(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_0A_LN_CFG_TXCAL_SHIFT_CODE_5_0_SET
		 (params->cfg_txcal_shift_code_5_0),
		 SD25G_LANE_LANE_0A_LN_CFG_TXCAL_SHIFT_CODE_5_0,
		 priv,
		 SD25G_LANE_LANE_0A(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_09_LN_CFG_TXCAL_VALID_SEL_3_0_SET
		 (params->cfg_txcal_valid_sel_3_0),
		 SD25G_LANE_LANE_09_LN_CFG_TXCAL_VALID_SEL_3_0,
		 priv,
		 SD25G_LANE_LANE_09(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_1A_LN_CFG_CDR_KF_2_0_SET(params->cfg_cdr_kf_2_0),
		 SD25G_LANE_LANE_1A_LN_CFG_CDR_KF_2_0,
		 priv,
		 SD25G_LANE_LANE_1A(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_1B_LN_CFG_CDR_M_7_0_SET(params->cfg_cdr_m_7_0),
		 SD25G_LANE_LANE_1B_LN_CFG_CDR_M_7_0,
		 priv,
		 SD25G_LANE_LANE_1B(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_2B_LN_CFG_PI_BW_3_0_SET(params->cfg_pi_bw_3_0),
		 SD25G_LANE_LANE_2B_LN_CFG_PI_BW_3_0,
		 priv,
		 SD25G_LANE_LANE_2B(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_2C_LN_CFG_DIS_2NDORDER_SET
		 (params->cfg_dis_2ndorder),
		 SD25G_LANE_LANE_2C_LN_CFG_DIS_2NDORDER,
		 priv,
		 SD25G_LANE_LANE_2C(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_2E_LN_CFG_CTLE_RSTN_SET(params->cfg_ctle_rstn),
		 SD25G_LANE_LANE_2E_LN_CFG_CTLE_RSTN,
		 priv,
		 SD25G_LANE_LANE_2E(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_00_LN_CFG_ITX_IPCML_BASE_1_0_SET
		 (params->cfg_itx_ipcml_base_1_0),
		 SD25G_LANE_LANE_00_LN_CFG_ITX_IPCML_BASE_1_0,
		 priv,
		 SD25G_LANE_LANE_00(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_44_LN_CFG_RX_RESERVE_7_0_SET
		 (params->cfg_rx_reserve_7_0),
		 SD25G_LANE_LANE_44_LN_CFG_RX_RESERVE_7_0,
		 priv,
		 SD25G_LANE_LANE_44(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_45_LN_CFG_RX_RESERVE_15_8_SET
		 (params->cfg_rx_reserve_15_8),
		 SD25G_LANE_LANE_45_LN_CFG_RX_RESERVE_15_8,
		 priv,
		 SD25G_LANE_LANE_45(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_0D_LN_CFG_DFECK_EN_SET(params->cfg_dfeck_en) |
		 SD25G_LANE_LANE_0D_LN_CFG_RXTERM_2_0_SET(params->cfg_rxterm_2_0),
		 SD25G_LANE_LANE_0D_LN_CFG_DFECK_EN |
		 SD25G_LANE_LANE_0D_LN_CFG_RXTERM_2_0,
		 priv,
		 SD25G_LANE_LANE_0D(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_21_LN_CFG_VGA_CTRL_BYP_4_0_SET
		 (params->cfg_vga_ctrl_byp_4_0),
		 SD25G_LANE_LANE_21_LN_CFG_VGA_CTRL_BYP_4_0,
		 priv,
		 SD25G_LANE_LANE_21(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_22_LN_CFG_EQR_FORCE_3_0_SET
		 (params->cfg_eqr_force_3_0),
		 SD25G_LANE_LANE_22_LN_CFG_EQR_FORCE_3_0,
		 priv,
		 SD25G_LANE_LANE_22(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_1C_LN_CFG_EQC_FORCE_3_0_SET
		 (params->cfg_eqc_force_3_0) |
		 SD25G_LANE_LANE_1C_LN_CFG_DFE_PD_SET(params->cfg_dfe_pd),
		 SD25G_LANE_LANE_1C_LN_CFG_EQC_FORCE_3_0 |
		 SD25G_LANE_LANE_1C_LN_CFG_DFE_PD,
		 priv,
		 SD25G_LANE_LANE_1C(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_1E_LN_CFG_SUM_SETCM_EN_SET
		 (params->cfg_sum_setcm_en),
		 SD25G_LANE_LANE_1E_LN_CFG_SUM_SETCM_EN,
		 priv,
		 SD25G_LANE_LANE_1E(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_25_LN_CFG_INIT_POS_ISCAN_6_0_SET
		 (params->cfg_init_pos_iscan_6_0),
		 SD25G_LANE_LANE_25_LN_CFG_INIT_POS_ISCAN_6_0,
		 priv,
		 SD25G_LANE_LANE_25(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_26_LN_CFG_INIT_POS_IPI_6_0_SET
		 (params->cfg_init_pos_ipi_6_0),
		 SD25G_LANE_LANE_26_LN_CFG_INIT_POS_IPI_6_0,
		 priv,
		 SD25G_LANE_LANE_26(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_18_LN_CFG_ERRAMP_PD_SET(params->cfg_erramp_pd),
		 SD25G_LANE_LANE_18_LN_CFG_ERRAMP_PD,
		 priv,
		 SD25G_LANE_LANE_18(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_0E_LN_CFG_DFEDIG_M_2_0_SET
		 (params->cfg_dfedig_m_2_0),
		 SD25G_LANE_LANE_0E_LN_CFG_DFEDIG_M_2_0,
		 priv,
		 SD25G_LANE_LANE_0E(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_0E_LN_CFG_EN_DFEDIG_SET(params->cfg_en_dfedig),
		 SD25G_LANE_LANE_0E_LN_CFG_EN_DFEDIG,
		 priv,
		 SD25G_LANE_LANE_0E(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_40_LN_R_TX_POL_INV_SET(params->r_tx_pol_inv) |
		 SD25G_LANE_LANE_40_LN_R_RX_POL_INV_SET(params->r_rx_pol_inv),
		 SD25G_LANE_LANE_40_LN_R_TX_POL_INV |
		 SD25G_LANE_LANE_40_LN_R_RX_POL_INV,
		 priv,
		 SD25G_LANE_LANE_40(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_04_LN_CFG_RX2TX_LP_EN_SET(params->cfg_rx2tx_lp_en) |
		 SD25G_LANE_LANE_04_LN_CFG_TX2RX_LP_EN_SET(params->cfg_tx2rx_lp_en),
		 SD25G_LANE_LANE_04_LN_CFG_RX2TX_LP_EN |
		 SD25G_LANE_LANE_04_LN_CFG_TX2RX_LP_EN,
		 priv,
		 SD25G_LANE_LANE_04(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_1E_LN_CFG_RXLB_EN_SET(params->cfg_rxlb_en),
		 SD25G_LANE_LANE_1E_LN_CFG_RXLB_EN,
		 priv,
		 SD25G_LANE_LANE_1E(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_19_LN_CFG_TXLB_EN_SET(params->cfg_txlb_en),
		 SD25G_LANE_LANE_19_LN_CFG_TXLB_EN,
		 priv,
		 SD25G_LANE_LANE_19(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_2E_LN_CFG_RSTN_DFEDIG_SET(0),
		 SD25G_LANE_LANE_2E_LN_CFG_RSTN_DFEDIG,
		 priv,
		 SD25G_LANE_LANE_2E(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_2E_LN_CFG_RSTN_DFEDIG_SET(1),
		 SD25G_LANE_LANE_2E_LN_CFG_RSTN_DFEDIG,
		 priv,
		 SD25G_LANE_LANE_2E(sd_index));

	sdx5_rmw(SD_LANE_25G_SD_LANE_CFG_MACRO_RST_SET(0),
		 SD_LANE_25G_SD_LANE_CFG_MACRO_RST,
		 priv,
		 SD_LANE_25G_SD_LANE_CFG(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_1C_LN_CFG_CDR_RSTN_SET(0),
		 SD25G_LANE_LANE_1C_LN_CFG_CDR_RSTN,
		 priv,
		 SD25G_LANE_LANE_1C(sd_index));

	usleep_range(1000, 2000);

	sdx5_rmw(SD25G_LANE_LANE_1C_LN_CFG_CDR_RSTN_SET(1),
		 SD25G_LANE_LANE_1C_LN_CFG_CDR_RSTN,
		 priv,
		 SD25G_LANE_LANE_1C(sd_index));

	usleep_range(10000, 20000);

	sdx5_rmw(SD25G_LANE_CMU_FF_REGISTER_TABLE_INDEX_SET(0xff),
		 SD25G_LANE_CMU_FF_REGISTER_TABLE_INDEX,
		 priv,
		 SD25G_LANE_CMU_FF(sd_index));

	value = readl(sdx5_addr(regs, SD25G_LANE_CMU_C0(sd_index)));
	value = SD25G_LANE_CMU_C0_PLL_LOL_UDL_GET(value);

	if (value) {
		dev_err(dev, "25G PLL Loss of Lock: 0x%x\n", value);
		return -EINVAL;
	}

	value = readl(sdx5_addr(regs, SD_LANE_25G_SD_LANE_STAT(sd_index)));
	value = SD_LANE_25G_SD_LANE_STAT_PMA_RST_DONE_GET(value);

	if (value != 0x1) {
		dev_err(dev, "25G PMA Reset failed: 0x%x\n", value);
		return -EINVAL;
	}
	sdx5_rmw(SD25G_LANE_CMU_2A_R_DBG_LOL_STATUS_SET(0x1),
		 SD25G_LANE_CMU_2A_R_DBG_LOL_STATUS,
		 priv,
		 SD25G_LANE_CMU_2A(sd_index));

	sdx5_rmw(SD_LANE_25G_SD_SER_RST_SER_RST_SET(0x0),
		 SD_LANE_25G_SD_SER_RST_SER_RST,
		 priv,
		 SD_LANE_25G_SD_SER_RST(sd_index));

	sdx5_rmw(SD_LANE_25G_SD_DES_RST_DES_RST_SET(0x0),
		 SD_LANE_25G_SD_DES_RST_DES_RST,
		 priv,
		 SD_LANE_25G_SD_DES_RST(sd_index));

	sdx5_rmw(SD25G_LANE_CMU_FF_REGISTER_TABLE_INDEX_SET(0),
		 SD25G_LANE_CMU_FF_REGISTER_TABLE_INDEX,
		 priv,
		 SD25G_LANE_CMU_FF(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_2D_LN_CFG_ALOS_THR_2_0_SET
		 (params->cfg_alos_thr_2_0),
		 SD25G_LANE_LANE_2D_LN_CFG_ALOS_THR_2_0,
		 priv,
		 SD25G_LANE_LANE_2D(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_2E_LN_CFG_DIS_SQ_SET(0),
		 SD25G_LANE_LANE_2E_LN_CFG_DIS_SQ,
		 priv,
		 SD25G_LANE_LANE_2E(sd_index));

	sdx5_rmw(SD25G_LANE_LANE_2E_LN_CFG_PD_SQ_SET(0),
		 SD25G_LANE_LANE_2E_LN_CFG_PD_SQ,
		 priv,
		 SD25G_LANE_LANE_2E(sd_index));

	return 0;
}

static void sparx5_sd10g28_reset(void __iomem *regs[], u32 lane_index)
{
	/* Note: SerDes SD10G_LANE_1 is configured in 10G_LAN mode */
	sdx5_rmw_addr(SD_LANE_SD_LANE_CFG_EXT_CFG_RST_SET(1),
		      SD_LANE_SD_LANE_CFG_EXT_CFG_RST,
		      sdx5_addr(regs, SD_LANE_SD_LANE_CFG(lane_index)));

	usleep_range(1000, 2000);

	sdx5_rmw_addr(SD_LANE_SD_LANE_CFG_EXT_CFG_RST_SET(0),
		      SD_LANE_SD_LANE_CFG_EXT_CFG_RST,
		      sdx5_addr(regs, SD_LANE_SD_LANE_CFG(lane_index)));
}

static int sparx5_sd10g28_apply_params(struct sparx5_serdes_macro *macro,
				       struct sparx5_sd10g28_params *params)
{
	struct sparx5_serdes_private *priv = macro->priv;
	void __iomem **regs = priv->regs;
	struct device *dev = priv->dev;
	u32 lane_index = macro->sidx;
	u32 sd_index = macro->stpidx;
	void __iomem *sd_inst;
	u32 value, cmu_idx;
	int err;

	/* Do not configure serdes if CMU is not to be configured too */
	if (params->skip_cmu_cfg)
		return 0;

	cmu_idx = sparx5_serdes_cmu_get(params->cmu_sel, lane_index);
	err = sparx5_cmu_cfg(priv, cmu_idx);
	if (err)
		return err;

	if (params->is_6g)
		sd_inst = sdx5_inst_get(priv, TARGET_SD6G_LANE, sd_index);
	else
		sd_inst = sdx5_inst_get(priv, TARGET_SD10G_LANE, sd_index);

	sdx5_rmw(SD_LANE_SD_LANE_CFG_MACRO_RST_SET(1),
		 SD_LANE_SD_LANE_CFG_MACRO_RST,
		 priv,
		 SD_LANE_SD_LANE_CFG(lane_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_93_R_DWIDTHCTRL_FROM_HWT_SET(0x0) |
		      SD10G_LANE_LANE_93_R_REG_MANUAL_SET(0x1) |
		      SD10G_LANE_LANE_93_R_AUXCKSEL_FROM_HWT_SET(0x1) |
		      SD10G_LANE_LANE_93_R_LANE_ID_FROM_HWT_SET(0x1) |
		      SD10G_LANE_LANE_93_R_EN_RATECHG_CTRL_SET(0x0),
		      SD10G_LANE_LANE_93_R_DWIDTHCTRL_FROM_HWT |
		      SD10G_LANE_LANE_93_R_REG_MANUAL |
		      SD10G_LANE_LANE_93_R_AUXCKSEL_FROM_HWT |
		      SD10G_LANE_LANE_93_R_LANE_ID_FROM_HWT |
		      SD10G_LANE_LANE_93_R_EN_RATECHG_CTRL,
		      sd_inst,
		      SD10G_LANE_LANE_93(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_94_R_ISCAN_REG_SET(0x1) |
		      SD10G_LANE_LANE_94_R_TXEQ_REG_SET(0x1) |
		      SD10G_LANE_LANE_94_R_MISC_REG_SET(0x1) |
		      SD10G_LANE_LANE_94_R_SWING_REG_SET(0x1),
		      SD10G_LANE_LANE_94_R_ISCAN_REG |
		      SD10G_LANE_LANE_94_R_TXEQ_REG |
		      SD10G_LANE_LANE_94_R_MISC_REG |
		      SD10G_LANE_LANE_94_R_SWING_REG,
		      sd_inst,
		      SD10G_LANE_LANE_94(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_9E_R_RXEQ_REG_SET(0x1),
		      SD10G_LANE_LANE_9E_R_RXEQ_REG,
		      sd_inst,
		      SD10G_LANE_LANE_9E(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_A1_R_SSC_FROM_HWT_SET(0x0) |
		      SD10G_LANE_LANE_A1_R_CDR_FROM_HWT_SET(0x0) |
		      SD10G_LANE_LANE_A1_R_PCLK_GATING_FROM_HWT_SET(0x1),
		      SD10G_LANE_LANE_A1_R_SSC_FROM_HWT |
		      SD10G_LANE_LANE_A1_R_CDR_FROM_HWT |
		      SD10G_LANE_LANE_A1_R_PCLK_GATING_FROM_HWT,
		      sd_inst,
		      SD10G_LANE_LANE_A1(sd_index));

	sdx5_rmw(SD_LANE_SD_LANE_CFG_RX_REF_SEL_SET(params->cmu_sel) |
		 SD_LANE_SD_LANE_CFG_TX_REF_SEL_SET(params->cmu_sel),
		 SD_LANE_SD_LANE_CFG_RX_REF_SEL |
		 SD_LANE_SD_LANE_CFG_TX_REF_SEL,
		 priv,
		 SD_LANE_SD_LANE_CFG(lane_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_40_CFG_LANE_RESERVE_7_0_SET
		      (params->cfg_lane_reserve_7_0),
		      SD10G_LANE_LANE_40_CFG_LANE_RESERVE_7_0,
		      sd_inst,
		      SD10G_LANE_LANE_40(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_50_CFG_SSC_RTL_CLK_SEL_SET
		      (params->cfg_ssc_rtl_clk_sel),
		      SD10G_LANE_LANE_50_CFG_SSC_RTL_CLK_SEL,
		      sd_inst,
		      SD10G_LANE_LANE_50(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_35_CFG_TXRATE_1_0_SET
		      (params->cfg_txrate_1_0) |
		      SD10G_LANE_LANE_35_CFG_RXRATE_1_0_SET
		      (params->cfg_rxrate_1_0),
		      SD10G_LANE_LANE_35_CFG_TXRATE_1_0 |
		      SD10G_LANE_LANE_35_CFG_RXRATE_1_0,
		      sd_inst,
		      SD10G_LANE_LANE_35(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_94_R_DWIDTHCTRL_2_0_SET
		      (params->r_d_width_ctrl_2_0),
		      SD10G_LANE_LANE_94_R_DWIDTHCTRL_2_0,
		      sd_inst,
		      SD10G_LANE_LANE_94(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_01_CFG_PMA_TX_CK_BITWIDTH_2_0_SET
		      (params->cfg_pma_tx_ck_bitwidth_2_0),
		      SD10G_LANE_LANE_01_CFG_PMA_TX_CK_BITWIDTH_2_0,
		      sd_inst,
		      SD10G_LANE_LANE_01(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_30_CFG_RXDIV_SEL_2_0_SET
		      (params->cfg_rxdiv_sel_2_0),
		      SD10G_LANE_LANE_30_CFG_RXDIV_SEL_2_0,
		      sd_inst,
		      SD10G_LANE_LANE_30(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_A2_R_PCS2PMA_PHYMODE_4_0_SET
		      (params->r_pcs2pma_phymode_4_0),
		      SD10G_LANE_LANE_A2_R_PCS2PMA_PHYMODE_4_0,
		      sd_inst,
		      SD10G_LANE_LANE_A2(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_13_CFG_CDRCK_EN_SET(params->cfg_cdrck_en),
		      SD10G_LANE_LANE_13_CFG_CDRCK_EN,
		      sd_inst,
		      SD10G_LANE_LANE_13(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_23_CFG_DFECK_EN_SET
		      (params->cfg_dfeck_en) |
		      SD10G_LANE_LANE_23_CFG_DFE_PD_SET(params->cfg_dfe_pd) |
		      SD10G_LANE_LANE_23_CFG_ERRAMP_PD_SET
		      (params->cfg_erramp_pd),
		      SD10G_LANE_LANE_23_CFG_DFECK_EN |
		      SD10G_LANE_LANE_23_CFG_DFE_PD |
		      SD10G_LANE_LANE_23_CFG_ERRAMP_PD,
		      sd_inst,
		      SD10G_LANE_LANE_23(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_22_CFG_DFETAP_EN_5_1_SET
		      (params->cfg_dfetap_en_5_1),
		      SD10G_LANE_LANE_22_CFG_DFETAP_EN_5_1,
		      sd_inst,
		      SD10G_LANE_LANE_22(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_1A_CFG_PI_DFE_EN_SET
		      (params->cfg_pi_DFE_en),
		      SD10G_LANE_LANE_1A_CFG_PI_DFE_EN,
		      sd_inst,
		      SD10G_LANE_LANE_1A(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_02_CFG_EN_ADV_SET(params->cfg_en_adv) |
		      SD10G_LANE_LANE_02_CFG_EN_MAIN_SET(params->cfg_en_main) |
		      SD10G_LANE_LANE_02_CFG_EN_DLY_SET(params->cfg_en_dly) |
		      SD10G_LANE_LANE_02_CFG_TAP_ADV_3_0_SET
		      (params->cfg_tap_adv_3_0),
		      SD10G_LANE_LANE_02_CFG_EN_ADV |
		      SD10G_LANE_LANE_02_CFG_EN_MAIN |
		      SD10G_LANE_LANE_02_CFG_EN_DLY |
		      SD10G_LANE_LANE_02_CFG_TAP_ADV_3_0,
		      sd_inst,
		      SD10G_LANE_LANE_02(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_03_CFG_TAP_MAIN_SET(params->cfg_tap_main),
		      SD10G_LANE_LANE_03_CFG_TAP_MAIN,
		      sd_inst,
		      SD10G_LANE_LANE_03(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_04_CFG_TAP_DLY_4_0_SET
		      (params->cfg_tap_dly_4_0),
		      SD10G_LANE_LANE_04_CFG_TAP_DLY_4_0,
		      sd_inst,
		      SD10G_LANE_LANE_04(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_2F_CFG_VGA_CTRL_3_0_SET
		      (params->cfg_vga_ctrl_3_0),
		      SD10G_LANE_LANE_2F_CFG_VGA_CTRL_3_0,
		      sd_inst,
		      SD10G_LANE_LANE_2F(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_2F_CFG_VGA_CP_2_0_SET
		      (params->cfg_vga_cp_2_0),
		      SD10G_LANE_LANE_2F_CFG_VGA_CP_2_0,
		      sd_inst,
		      SD10G_LANE_LANE_2F(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_0B_CFG_EQ_RES_3_0_SET
		      (params->cfg_eq_res_3_0),
		      SD10G_LANE_LANE_0B_CFG_EQ_RES_3_0,
		      sd_inst,
		      SD10G_LANE_LANE_0B(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_0D_CFG_EQR_BYP_SET(params->cfg_eq_r_byp),
		      SD10G_LANE_LANE_0D_CFG_EQR_BYP,
		      sd_inst,
		      SD10G_LANE_LANE_0D(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_0E_CFG_EQC_FORCE_3_0_SET
		      (params->cfg_eq_c_force_3_0) |
		      SD10G_LANE_LANE_0E_CFG_SUM_SETCM_EN_SET
		      (params->cfg_sum_setcm_en),
		      SD10G_LANE_LANE_0E_CFG_EQC_FORCE_3_0 |
		      SD10G_LANE_LANE_0E_CFG_SUM_SETCM_EN,
		      sd_inst,
		      SD10G_LANE_LANE_0E(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_23_CFG_EN_DFEDIG_SET
		      (params->cfg_en_dfedig),
		      SD10G_LANE_LANE_23_CFG_EN_DFEDIG,
		      sd_inst,
		      SD10G_LANE_LANE_23(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_06_CFG_EN_PREEMPH_SET
		      (params->cfg_en_preemph),
		      SD10G_LANE_LANE_06_CFG_EN_PREEMPH,
		      sd_inst,
		      SD10G_LANE_LANE_06(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_33_CFG_ITX_IPPREEMP_BASE_1_0_SET
		      (params->cfg_itx_ippreemp_base_1_0) |
		      SD10G_LANE_LANE_33_CFG_ITX_IPDRIVER_BASE_2_0_SET
		      (params->cfg_itx_ipdriver_base_2_0),
		      SD10G_LANE_LANE_33_CFG_ITX_IPPREEMP_BASE_1_0 |
		      SD10G_LANE_LANE_33_CFG_ITX_IPDRIVER_BASE_2_0,
		      sd_inst,
		      SD10G_LANE_LANE_33(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_52_CFG_IBIAS_TUNE_RESERVE_5_0_SET
		      (params->cfg_ibias_tune_reserve_5_0),
		      SD10G_LANE_LANE_52_CFG_IBIAS_TUNE_RESERVE_5_0,
		      sd_inst,
		      SD10G_LANE_LANE_52(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_37_CFG_TXSWING_HALF_SET
		      (params->cfg_txswing_half),
		      SD10G_LANE_LANE_37_CFG_TXSWING_HALF,
		      sd_inst,
		      SD10G_LANE_LANE_37(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_3C_CFG_DIS_2NDORDER_SET
		      (params->cfg_dis_2nd_order),
		      SD10G_LANE_LANE_3C_CFG_DIS_2NDORDER,
		      sd_inst,
		      SD10G_LANE_LANE_3C(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_39_CFG_RX_SSC_LH_SET
		      (params->cfg_rx_ssc_lh),
		      SD10G_LANE_LANE_39_CFG_RX_SSC_LH,
		      sd_inst,
		      SD10G_LANE_LANE_39(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_1A_CFG_PI_FLOOP_STEPS_1_0_SET
		      (params->cfg_pi_floop_steps_1_0),
		      SD10G_LANE_LANE_1A_CFG_PI_FLOOP_STEPS_1_0,
		      sd_inst,
		      SD10G_LANE_LANE_1A(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_16_CFG_PI_EXT_DAC_23_16_SET
		      (params->cfg_pi_ext_dac_23_16),
		      SD10G_LANE_LANE_16_CFG_PI_EXT_DAC_23_16,
		      sd_inst,
		      SD10G_LANE_LANE_16(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_15_CFG_PI_EXT_DAC_15_8_SET
		      (params->cfg_pi_ext_dac_15_8),
		      SD10G_LANE_LANE_15_CFG_PI_EXT_DAC_15_8,
		      sd_inst,
		      SD10G_LANE_LANE_15(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_26_CFG_ISCAN_EXT_DAC_7_0_SET
		      (params->cfg_iscan_ext_dac_7_0),
		      SD10G_LANE_LANE_26_CFG_ISCAN_EXT_DAC_7_0,
		      sd_inst,
		      SD10G_LANE_LANE_26(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_42_CFG_CDR_KF_GEN1_2_0_SET
		      (params->cfg_cdr_kf_gen1_2_0),
		      SD10G_LANE_LANE_42_CFG_CDR_KF_GEN1_2_0,
		      sd_inst,
		      SD10G_LANE_LANE_42(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_0F_R_CDR_M_GEN1_7_0_SET
		      (params->r_cdr_m_gen1_7_0),
		      SD10G_LANE_LANE_0F_R_CDR_M_GEN1_7_0,
		      sd_inst,
		      SD10G_LANE_LANE_0F(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_24_CFG_PI_BW_GEN1_3_0_SET
		      (params->cfg_pi_bw_gen1_3_0),
		      SD10G_LANE_LANE_24_CFG_PI_BW_GEN1_3_0,
		      sd_inst,
		      SD10G_LANE_LANE_24(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_14_CFG_PI_EXT_DAC_7_0_SET
		      (params->cfg_pi_ext_dac_7_0),
		      SD10G_LANE_LANE_14_CFG_PI_EXT_DAC_7_0,
		      sd_inst,
		      SD10G_LANE_LANE_14(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_1A_CFG_PI_STEPS_SET(params->cfg_pi_steps),
		      SD10G_LANE_LANE_1A_CFG_PI_STEPS,
		      sd_inst,
		      SD10G_LANE_LANE_1A(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_3A_CFG_MP_MAX_3_0_SET
		      (params->cfg_mp_max_3_0),
		      SD10G_LANE_LANE_3A_CFG_MP_MAX_3_0,
		      sd_inst,
		      SD10G_LANE_LANE_3A(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_31_CFG_RSTN_DFEDIG_SET
		      (params->cfg_rstn_dfedig),
		      SD10G_LANE_LANE_31_CFG_RSTN_DFEDIG,
		      sd_inst,
		      SD10G_LANE_LANE_31(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_48_CFG_ALOS_THR_3_0_SET
		      (params->cfg_alos_thr_3_0),
		      SD10G_LANE_LANE_48_CFG_ALOS_THR_3_0,
		      sd_inst,
		      SD10G_LANE_LANE_48(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_36_CFG_PREDRV_SLEWRATE_1_0_SET
		      (params->cfg_predrv_slewrate_1_0),
		      SD10G_LANE_LANE_36_CFG_PREDRV_SLEWRATE_1_0,
		      sd_inst,
		      SD10G_LANE_LANE_36(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_32_CFG_ITX_IPCML_BASE_1_0_SET
		      (params->cfg_itx_ipcml_base_1_0),
		      SD10G_LANE_LANE_32_CFG_ITX_IPCML_BASE_1_0,
		      sd_inst,
		      SD10G_LANE_LANE_32(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_37_CFG_IP_PRE_BASE_1_0_SET
		      (params->cfg_ip_pre_base_1_0),
		      SD10G_LANE_LANE_37_CFG_IP_PRE_BASE_1_0,
		      sd_inst,
		      SD10G_LANE_LANE_37(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_41_CFG_LANE_RESERVE_15_8_SET
		      (params->cfg_lane_reserve_15_8),
		      SD10G_LANE_LANE_41_CFG_LANE_RESERVE_15_8,
		      sd_inst,
		      SD10G_LANE_LANE_41(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_9E_R_EN_AUTO_CDR_RSTN_SET
		      (params->r_en_auto_cdr_rstn),
		      SD10G_LANE_LANE_9E_R_EN_AUTO_CDR_RSTN,
		      sd_inst,
		      SD10G_LANE_LANE_9E(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_0C_CFG_OSCAL_AFE_SET
		      (params->cfg_oscal_afe) |
		      SD10G_LANE_LANE_0C_CFG_PD_OSDAC_AFE_SET
		      (params->cfg_pd_osdac_afe),
		      SD10G_LANE_LANE_0C_CFG_OSCAL_AFE |
		      SD10G_LANE_LANE_0C_CFG_PD_OSDAC_AFE,
		      sd_inst,
		      SD10G_LANE_LANE_0C(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_0B_CFG_RESETB_OSCAL_AFE_SET
		      (params->cfg_resetb_oscal_afe[0]),
		      SD10G_LANE_LANE_0B_CFG_RESETB_OSCAL_AFE,
		      sd_inst,
		      SD10G_LANE_LANE_0B(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_0B_CFG_RESETB_OSCAL_AFE_SET
		      (params->cfg_resetb_oscal_afe[1]),
		      SD10G_LANE_LANE_0B_CFG_RESETB_OSCAL_AFE,
		      sd_inst,
		      SD10G_LANE_LANE_0B(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_83_R_TX_POL_INV_SET
		      (params->r_tx_pol_inv) |
		      SD10G_LANE_LANE_83_R_RX_POL_INV_SET
		      (params->r_rx_pol_inv),
		      SD10G_LANE_LANE_83_R_TX_POL_INV |
		      SD10G_LANE_LANE_83_R_RX_POL_INV,
		      sd_inst,
		      SD10G_LANE_LANE_83(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_06_CFG_RX2TX_LP_EN_SET
		      (params->cfg_rx2tx_lp_en) |
		      SD10G_LANE_LANE_06_CFG_TX2RX_LP_EN_SET
		      (params->cfg_tx2rx_lp_en),
		      SD10G_LANE_LANE_06_CFG_RX2TX_LP_EN |
		      SD10G_LANE_LANE_06_CFG_TX2RX_LP_EN,
		      sd_inst,
		      SD10G_LANE_LANE_06(sd_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_0E_CFG_RXLB_EN_SET(params->cfg_rxlb_en) |
		      SD10G_LANE_LANE_0E_CFG_TXLB_EN_SET(params->cfg_txlb_en),
		      SD10G_LANE_LANE_0E_CFG_RXLB_EN |
		      SD10G_LANE_LANE_0E_CFG_TXLB_EN,
		      sd_inst,
		      SD10G_LANE_LANE_0E(sd_index));

	sdx5_rmw(SD_LANE_SD_LANE_CFG_MACRO_RST_SET(0),
		 SD_LANE_SD_LANE_CFG_MACRO_RST,
		 priv,
		 SD_LANE_SD_LANE_CFG(lane_index));

	sdx5_inst_rmw(SD10G_LANE_LANE_50_CFG_SSC_RESETB_SET(1),
		      SD10G_LANE_LANE_50_CFG_SSC_RESETB,
		      sd_inst,
		      SD10G_LANE_LANE_50(sd_index));

	sdx5_rmw(SD10G_LANE_LANE_50_CFG_SSC_RESETB_SET(1),
		 SD10G_LANE_LANE_50_CFG_SSC_RESETB,
		 priv,
		 SD10G_LANE_LANE_50(sd_index));

	sdx5_rmw(SD_LANE_MISC_SD_125_RST_DIS_SET(params->fx_100),
		 SD_LANE_MISC_SD_125_RST_DIS,
		 priv,
		 SD_LANE_MISC(lane_index));

	sdx5_rmw(SD_LANE_MISC_RX_ENA_SET(params->fx_100),
		 SD_LANE_MISC_RX_ENA,
		 priv,
		 SD_LANE_MISC(lane_index));

	sdx5_rmw(SD_LANE_MISC_MUX_ENA_SET(params->fx_100),
		 SD_LANE_MISC_MUX_ENA,
		 priv,
		 SD_LANE_MISC(lane_index));

	usleep_range(3000, 6000);

	value = readl(sdx5_addr(regs, SD_LANE_SD_LANE_STAT(lane_index)));
	value = SD_LANE_SD_LANE_STAT_PMA_RST_DONE_GET(value);
	if (value != 1) {
		dev_err(dev, "10G PMA Reset failed: 0x%x\n", value);
		return -EINVAL;
	}

	sdx5_rmw(SD_LANE_SD_SER_RST_SER_RST_SET(0x0),
		 SD_LANE_SD_SER_RST_SER_RST,
		 priv,
		 SD_LANE_SD_SER_RST(lane_index));

	sdx5_rmw(SD_LANE_SD_DES_RST_DES_RST_SET(0x0),
		 SD_LANE_SD_DES_RST_DES_RST,
		 priv,
		 SD_LANE_SD_DES_RST(lane_index));

	return 0;
}

static int sparx5_sd25g28_config(struct sparx5_serdes_macro *macro, bool reset)
{
	struct sparx5_sd25g28_media_preset media = media_presets_25g[macro->media];
	struct sparx5_sd25g28_mode_preset mode;
	struct sparx5_sd25g28_args args = {
		.rxinvert = 1,
		.txinvert = 0,
		.txswing = 240,
		.com_pll_reserve = 0xf,
		.reg_rst = reset,
	};
	struct sparx5_sd25g28_params params;
	int err;

	err = sparx5_sd10g25_get_mode_preset(macro, &mode);
	if (err)
		return err;
	sparx5_sd25g28_get_params(macro, &media, &mode, &args, &params);
	sparx5_sd25g28_reset(macro->priv->regs, &params, macro->stpidx);
	return sparx5_sd25g28_apply_params(macro, &params);
}

static int sparx5_sd10g28_config(struct sparx5_serdes_macro *macro, bool reset)
{
	struct sparx5_sd10g28_media_preset media = media_presets_10g[macro->media];
	struct sparx5_sd10g28_mode_preset mode;
	struct sparx5_sd10g28_params params;
	struct sparx5_sd10g28_args args = {
		.is_6g = (macro->serdestype == SPX5_SDT_6G),
		.txinvert = 0,
		.rxinvert = 1,
		.txswing = 240,
		.reg_rst = reset,
		.skip_cmu_cfg = reset,
	};
	int err;

	err = sparx5_sd10g28_get_mode_preset(macro, &mode, &args);
	if (err)
		return err;
	sparx5_sd10g28_get_params(macro, &media, &mode, &args, &params);
	sparx5_sd10g28_reset(macro->priv->regs, macro->sidx);
	return sparx5_sd10g28_apply_params(macro, &params);
}

/* Power down serdes TX driver */
static int sparx5_serdes_power_save(struct sparx5_serdes_macro *macro, u32 pwdn)
{
	struct sparx5_serdes_private *priv = macro->priv;
	void __iomem *sd_inst, *sd_lane_inst;

	if (macro->serdestype == SPX5_SDT_6G)
		sd_inst = sdx5_inst_get(priv, TARGET_SD6G_LANE, macro->stpidx);
	else if (macro->serdestype == SPX5_SDT_10G)
		sd_inst = sdx5_inst_get(priv, TARGET_SD10G_LANE, macro->stpidx);
	else
		sd_inst = sdx5_inst_get(priv, TARGET_SD25G_LANE, macro->stpidx);

	if (macro->serdestype == SPX5_SDT_25G) {
		sd_lane_inst = sdx5_inst_get(priv, TARGET_SD_LANE_25G,
					     macro->stpidx);
		/* Take serdes out of reset */
		sdx5_inst_rmw(SD_LANE_25G_SD_LANE_CFG_EXT_CFG_RST_SET(0),
			      SD_LANE_25G_SD_LANE_CFG_EXT_CFG_RST, sd_lane_inst,
			      SD_LANE_25G_SD_LANE_CFG(0));

		/* Configure optimal settings for quiet mode */
		sdx5_inst_rmw(SD_LANE_25G_QUIET_MODE_6G_QUIET_MODE_SET(SPX5_SERDES_QUIET_MODE_VAL),
			      SD_LANE_25G_QUIET_MODE_6G_QUIET_MODE,
			      sd_lane_inst, SD_LANE_25G_QUIET_MODE_6G(0));

		sdx5_inst_rmw(SD25G_LANE_LANE_04_LN_CFG_PD_DRIVER_SET(pwdn),
			      SD25G_LANE_LANE_04_LN_CFG_PD_DRIVER,
			      sd_inst,
			      SD25G_LANE_LANE_04(0));
	} else {
		/* 6G and 10G */
		sd_lane_inst = sdx5_inst_get(priv, TARGET_SD_LANE, macro->sidx);

		/* Take serdes out of reset */
		sdx5_inst_rmw(SD_LANE_SD_LANE_CFG_EXT_CFG_RST_SET(0),
			      SD_LANE_SD_LANE_CFG_EXT_CFG_RST, sd_lane_inst,
			      SD_LANE_SD_LANE_CFG(0));

		/* Configure optimal settings for quiet mode */
		sdx5_inst_rmw(SD_LANE_QUIET_MODE_6G_QUIET_MODE_SET(SPX5_SERDES_QUIET_MODE_VAL),
			      SD_LANE_QUIET_MODE_6G_QUIET_MODE, sd_lane_inst,
			      SD_LANE_QUIET_MODE_6G(0));

		sdx5_inst_rmw(SD10G_LANE_LANE_06_CFG_PD_DRIVER_SET(pwdn),
			      SD10G_LANE_LANE_06_CFG_PD_DRIVER,
			      sd_inst,
			      SD10G_LANE_LANE_06(0));
	}
	return 0;
}

static int sparx5_serdes_clock_config(struct sparx5_serdes_macro *macro)
{
	struct sparx5_serdes_private *priv = macro->priv;

	if (macro->serdesmode == SPX5_SD_MODE_100FX) {
		u32 freq = priv->coreclock == 250000000 ? 2 :
			priv->coreclock == 500000000 ? 1 : 0;

		sdx5_rmw(SD_LANE_MISC_CORE_CLK_FREQ_SET(freq),
			 SD_LANE_MISC_CORE_CLK_FREQ,
			 priv,
			 SD_LANE_MISC(macro->sidx));
	}
	return 0;
}

static int sparx5_serdes_get_serdesmode(phy_interface_t portmode, int speed)
{
	switch (portmode) {
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
		if (speed == SPEED_2500)
			return SPX5_SD_MODE_2G5;
		if (speed == SPEED_100)
			return SPX5_SD_MODE_100FX;
		return SPX5_SD_MODE_1000BASEX;
	case PHY_INTERFACE_MODE_SGMII:
		/* The same Serdes mode is used for both SGMII and 1000BaseX */
		return SPX5_SD_MODE_1000BASEX;
	case PHY_INTERFACE_MODE_QSGMII:
		return SPX5_SD_MODE_QSGMII;
	case PHY_INTERFACE_MODE_10GBASER:
		return SPX5_SD_MODE_SFI;
	default:
		return -EINVAL;
	}
}

static int sparx5_serdes_config(struct sparx5_serdes_macro *macro)
{
	struct device *dev = macro->priv->dev;
	int serdesmode;
	int err;

	serdesmode = sparx5_serdes_get_serdesmode(macro->portmode, macro->speed);
	if (serdesmode < 0) {
		dev_err(dev, "SerDes %u, interface not supported: %s\n",
			macro->sidx,
			phy_modes(macro->portmode));
		return serdesmode;
	}
	macro->serdesmode = serdesmode;

	sparx5_serdes_clock_config(macro);

	if (macro->serdestype == SPX5_SDT_25G)
		err = sparx5_sd25g28_config(macro, false);
	else
		err = sparx5_sd10g28_config(macro, false);
	if (err) {
		dev_err(dev, "SerDes %u, config error: %d\n",
			macro->sidx, err);
	}
	return err;
}

static int sparx5_serdes_power_on(struct phy *phy)
{
	struct sparx5_serdes_macro *macro = phy_get_drvdata(phy);

	return sparx5_serdes_power_save(macro, false);
}

static int sparx5_serdes_power_off(struct phy *phy)
{
	struct sparx5_serdes_macro *macro = phy_get_drvdata(phy);

	return sparx5_serdes_power_save(macro, true);
}

static int sparx5_serdes_set_mode(struct phy *phy, enum phy_mode mode, int submode)
{
	struct sparx5_serdes_macro *macro;

	if (mode != PHY_MODE_ETHERNET)
		return -EINVAL;

	switch (submode) {
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_QSGMII:
	case PHY_INTERFACE_MODE_10GBASER:
		macro = phy_get_drvdata(phy);
		macro->portmode = submode;
		sparx5_serdes_config(macro);
		return 0;
	default:
		return -EINVAL;
	}
}

static int sparx5_serdes_set_media(struct phy *phy, enum phy_media media)
{
	struct sparx5_serdes_macro *macro = phy_get_drvdata(phy);

	if (media != macro->media) {
		macro->media = media;
		if (macro->serdesmode != SPX5_SD_MODE_NONE)
			sparx5_serdes_config(macro);
	}
	return 0;
}

static int sparx5_serdes_set_speed(struct phy *phy, int speed)
{
	struct sparx5_serdes_macro *macro = phy_get_drvdata(phy);

	if (macro->sidx < SPX5_SERDES_10G_START && speed > SPEED_5000)
		return -EINVAL;
	if (macro->sidx < SPX5_SERDES_25G_START && speed > SPEED_10000)
		return -EINVAL;
	if (speed != macro->speed) {
		macro->speed = speed;
		if (macro->serdesmode != SPX5_SD_MODE_NONE)
			sparx5_serdes_config(macro);
	}
	return 0;
}

static int sparx5_serdes_reset(struct phy *phy)
{
	struct sparx5_serdes_macro *macro = phy_get_drvdata(phy);
	int err;

	if (macro->serdestype == SPX5_SDT_25G)
		err = sparx5_sd25g28_config(macro, true);
	else
		err = sparx5_sd10g28_config(macro, true);
	if (err) {
		dev_err(&phy->dev, "SerDes %u, reset error: %d\n",
			macro->sidx, err);
	}
	return err;
}

static int sparx5_serdes_validate(struct phy *phy, enum phy_mode mode,
					int submode,
					union phy_configure_opts *opts)
{
	struct sparx5_serdes_macro *macro = phy_get_drvdata(phy);

	if (mode != PHY_MODE_ETHERNET)
		return -EINVAL;

	if (macro->speed == 0)
		return -EINVAL;

	if (macro->sidx < SPX5_SERDES_10G_START && macro->speed > SPEED_5000)
		return -EINVAL;
	if (macro->sidx < SPX5_SERDES_25G_START && macro->speed > SPEED_10000)
		return -EINVAL;

	switch (submode) {
	case PHY_INTERFACE_MODE_1000BASEX:
		if (macro->speed != SPEED_100 && /* This is for 100BASE-FX */
		    macro->speed != SPEED_1000)
			return -EINVAL;
		break;
	case PHY_INTERFACE_MODE_SGMII:
	case PHY_INTERFACE_MODE_2500BASEX:
	case PHY_INTERFACE_MODE_QSGMII:
		if (macro->speed >= SPEED_5000)
			return -EINVAL;
		break;
	case PHY_INTERFACE_MODE_10GBASER:
		if (macro->speed < SPEED_5000)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct phy_ops sparx5_serdes_ops = {
	.power_on	= sparx5_serdes_power_on,
	.power_off	= sparx5_serdes_power_off,
	.set_mode	= sparx5_serdes_set_mode,
	.set_media	= sparx5_serdes_set_media,
	.set_speed	= sparx5_serdes_set_speed,
	.reset		= sparx5_serdes_reset,
	.validate	= sparx5_serdes_validate,
	.owner		= THIS_MODULE,
};

static int sparx5_phy_create(struct sparx5_serdes_private *priv,
			   int idx, struct phy **phy)
{
	struct sparx5_serdes_macro *macro;

	*phy = devm_phy_create(priv->dev, NULL, &sparx5_serdes_ops);
	if (IS_ERR(*phy))
		return PTR_ERR(*phy);

	macro = devm_kzalloc(priv->dev, sizeof(*macro), GFP_KERNEL);
	if (!macro)
		return -ENOMEM;

	macro->sidx = idx;
	macro->priv = priv;
	macro->speed = SPEED_UNKNOWN;
	if (idx < SPX5_SERDES_10G_START) {
		macro->serdestype = SPX5_SDT_6G;
		macro->stpidx = macro->sidx;
	} else if (idx < SPX5_SERDES_25G_START) {
		macro->serdestype = SPX5_SDT_10G;
		macro->stpidx = macro->sidx - SPX5_SERDES_10G_START;
	} else {
		macro->serdestype = SPX5_SDT_25G;
		macro->stpidx = macro->sidx - SPX5_SERDES_25G_START;
	}

	phy_set_drvdata(*phy, macro);

	/* Power off serdes by default */
	sparx5_serdes_power_off(*phy);

	return 0;
}

static struct sparx5_serdes_io_resource sparx5_serdes_iomap[] =  {
	{ TARGET_SD_CMU,          0x0 },      /* 0x610808000: sd_cmu_0 */
	{ TARGET_SD_CMU + 1,      0x8000 },   /* 0x610810000: sd_cmu_1 */
	{ TARGET_SD_CMU + 2,      0x10000 },  /* 0x610818000: sd_cmu_2 */
	{ TARGET_SD_CMU + 3,      0x18000 },  /* 0x610820000: sd_cmu_3 */
	{ TARGET_SD_CMU + 4,      0x20000 },  /* 0x610828000: sd_cmu_4 */
	{ TARGET_SD_CMU + 5,      0x28000 },  /* 0x610830000: sd_cmu_5 */
	{ TARGET_SD_CMU + 6,      0x30000 },  /* 0x610838000: sd_cmu_6 */
	{ TARGET_SD_CMU + 7,      0x38000 },  /* 0x610840000: sd_cmu_7 */
	{ TARGET_SD_CMU + 8,      0x40000 },  /* 0x610848000: sd_cmu_8 */
	{ TARGET_SD_CMU_CFG,      0x48000 },  /* 0x610850000: sd_cmu_cfg_0 */
	{ TARGET_SD_CMU_CFG + 1,  0x50000 },  /* 0x610858000: sd_cmu_cfg_1 */
	{ TARGET_SD_CMU_CFG + 2,  0x58000 },  /* 0x610860000: sd_cmu_cfg_2 */
	{ TARGET_SD_CMU_CFG + 3,  0x60000 },  /* 0x610868000: sd_cmu_cfg_3 */
	{ TARGET_SD_CMU_CFG + 4,  0x68000 },  /* 0x610870000: sd_cmu_cfg_4 */
	{ TARGET_SD_CMU_CFG + 5,  0x70000 },  /* 0x610878000: sd_cmu_cfg_5 */
	{ TARGET_SD_CMU_CFG + 6,  0x78000 },  /* 0x610880000: sd_cmu_cfg_6 */
	{ TARGET_SD_CMU_CFG + 7,  0x80000 },  /* 0x610888000: sd_cmu_cfg_7 */
	{ TARGET_SD_CMU_CFG + 8,  0x88000 },  /* 0x610890000: sd_cmu_cfg_8 */
	{ TARGET_SD6G_LANE,       0x90000 },  /* 0x610898000: sd6g_lane_0 */
	{ TARGET_SD6G_LANE + 1,   0x98000 },  /* 0x6108a0000: sd6g_lane_1 */
	{ TARGET_SD6G_LANE + 2,   0xa0000 },  /* 0x6108a8000: sd6g_lane_2 */
	{ TARGET_SD6G_LANE + 3,   0xa8000 },  /* 0x6108b0000: sd6g_lane_3 */
	{ TARGET_SD6G_LANE + 4,   0xb0000 },  /* 0x6108b8000: sd6g_lane_4 */
	{ TARGET_SD6G_LANE + 5,   0xb8000 },  /* 0x6108c0000: sd6g_lane_5 */
	{ TARGET_SD6G_LANE + 6,   0xc0000 },  /* 0x6108c8000: sd6g_lane_6 */
	{ TARGET_SD6G_LANE + 7,   0xc8000 },  /* 0x6108d0000: sd6g_lane_7 */
	{ TARGET_SD6G_LANE + 8,   0xd0000 },  /* 0x6108d8000: sd6g_lane_8 */
	{ TARGET_SD6G_LANE + 9,   0xd8000 },  /* 0x6108e0000: sd6g_lane_9 */
	{ TARGET_SD6G_LANE + 10,  0xe0000 },  /* 0x6108e8000: sd6g_lane_10 */
	{ TARGET_SD6G_LANE + 11,  0xe8000 },  /* 0x6108f0000: sd6g_lane_11 */
	{ TARGET_SD6G_LANE + 12,  0xf0000 },  /* 0x6108f8000: sd6g_lane_12 */
	{ TARGET_SD10G_LANE,      0xf8000 },  /* 0x610900000: sd10g_lane_0 */
	{ TARGET_SD10G_LANE + 1,  0x100000 }, /* 0x610908000: sd10g_lane_1 */
	{ TARGET_SD10G_LANE + 2,  0x108000 }, /* 0x610910000: sd10g_lane_2 */
	{ TARGET_SD10G_LANE + 3,  0x110000 }, /* 0x610918000: sd10g_lane_3 */
	{ TARGET_SD_LANE,         0x1a0000 }, /* 0x6109a8000: sd_lane_0 */
	{ TARGET_SD_LANE + 1,     0x1a8000 }, /* 0x6109b0000: sd_lane_1 */
	{ TARGET_SD_LANE + 2,     0x1b0000 }, /* 0x6109b8000: sd_lane_2 */
	{ TARGET_SD_LANE + 3,     0x1b8000 }, /* 0x6109c0000: sd_lane_3 */
	{ TARGET_SD_LANE + 4,     0x1c0000 }, /* 0x6109c8000: sd_lane_4 */
	{ TARGET_SD_LANE + 5,     0x1c8000 }, /* 0x6109d0000: sd_lane_5 */
	{ TARGET_SD_LANE + 6,     0x1d0000 }, /* 0x6109d8000: sd_lane_6 */
	{ TARGET_SD_LANE + 7,     0x1d8000 }, /* 0x6109e0000: sd_lane_7 */
	{ TARGET_SD_LANE + 8,     0x1e0000 }, /* 0x6109e8000: sd_lane_8 */
	{ TARGET_SD_LANE + 9,     0x1e8000 }, /* 0x6109f0000: sd_lane_9 */
	{ TARGET_SD_LANE + 10,    0x1f0000 }, /* 0x6109f8000: sd_lane_10 */
	{ TARGET_SD_LANE + 11,    0x1f8000 }, /* 0x610a00000: sd_lane_11 */
	{ TARGET_SD_LANE + 12,    0x200000 }, /* 0x610a08000: sd_lane_12 */
	{ TARGET_SD_LANE + 13,    0x208000 }, /* 0x610a10000: sd_lane_13 */
	{ TARGET_SD_LANE + 14,    0x210000 }, /* 0x610a18000: sd_lane_14 */
	{ TARGET_SD_LANE + 15,    0x218000 }, /* 0x610a20000: sd_lane_15 */
	{ TARGET_SD_LANE + 16,    0x220000 }, /* 0x610a28000: sd_lane_16 */
	{ TARGET_SD_CMU + 9,      0x400000 }, /* 0x610c08000: sd_cmu_9 */
	{ TARGET_SD_CMU + 10,     0x408000 }, /* 0x610c10000: sd_cmu_10 */
	{ TARGET_SD_CMU + 11,     0x410000 }, /* 0x610c18000: sd_cmu_11 */
	{ TARGET_SD_CMU + 12,     0x418000 }, /* 0x610c20000: sd_cmu_12 */
	{ TARGET_SD_CMU + 13,     0x420000 }, /* 0x610c28000: sd_cmu_13 */
	{ TARGET_SD_CMU_CFG + 9,  0x428000 }, /* 0x610c30000: sd_cmu_cfg_9 */
	{ TARGET_SD_CMU_CFG + 10, 0x430000 }, /* 0x610c38000: sd_cmu_cfg_10 */
	{ TARGET_SD_CMU_CFG + 11, 0x438000 }, /* 0x610c40000: sd_cmu_cfg_11 */
	{ TARGET_SD_CMU_CFG + 12, 0x440000 }, /* 0x610c48000: sd_cmu_cfg_12 */
	{ TARGET_SD_CMU_CFG + 13, 0x448000 }, /* 0x610c50000: sd_cmu_cfg_13 */
	{ TARGET_SD10G_LANE + 4,  0x450000 }, /* 0x610c58000: sd10g_lane_4 */
	{ TARGET_SD10G_LANE + 5,  0x458000 }, /* 0x610c60000: sd10g_lane_5 */
	{ TARGET_SD10G_LANE + 6,  0x460000 }, /* 0x610c68000: sd10g_lane_6 */
	{ TARGET_SD10G_LANE + 7,  0x468000 }, /* 0x610c70000: sd10g_lane_7 */
	{ TARGET_SD10G_LANE + 8,  0x470000 }, /* 0x610c78000: sd10g_lane_8 */
	{ TARGET_SD10G_LANE + 9,  0x478000 }, /* 0x610c80000: sd10g_lane_9 */
	{ TARGET_SD10G_LANE + 10, 0x480000 }, /* 0x610c88000: sd10g_lane_10 */
	{ TARGET_SD10G_LANE + 11, 0x488000 }, /* 0x610c90000: sd10g_lane_11 */
	{ TARGET_SD25G_LANE,      0x490000 }, /* 0x610c98000: sd25g_lane_0 */
	{ TARGET_SD25G_LANE + 1,  0x498000 }, /* 0x610ca0000: sd25g_lane_1 */
	{ TARGET_SD25G_LANE + 2,  0x4a0000 }, /* 0x610ca8000: sd25g_lane_2 */
	{ TARGET_SD25G_LANE + 3,  0x4a8000 }, /* 0x610cb0000: sd25g_lane_3 */
	{ TARGET_SD25G_LANE + 4,  0x4b0000 }, /* 0x610cb8000: sd25g_lane_4 */
	{ TARGET_SD25G_LANE + 5,  0x4b8000 }, /* 0x610cc0000: sd25g_lane_5 */
	{ TARGET_SD25G_LANE + 6,  0x4c0000 }, /* 0x610cc8000: sd25g_lane_6 */
	{ TARGET_SD25G_LANE + 7,  0x4c8000 }, /* 0x610cd0000: sd25g_lane_7 */
	{ TARGET_SD_LANE + 17,    0x550000 }, /* 0x610d58000: sd_lane_17 */
	{ TARGET_SD_LANE + 18,    0x558000 }, /* 0x610d60000: sd_lane_18 */
	{ TARGET_SD_LANE + 19,    0x560000 }, /* 0x610d68000: sd_lane_19 */
	{ TARGET_SD_LANE + 20,    0x568000 }, /* 0x610d70000: sd_lane_20 */
	{ TARGET_SD_LANE + 21,    0x570000 }, /* 0x610d78000: sd_lane_21 */
	{ TARGET_SD_LANE + 22,    0x578000 }, /* 0x610d80000: sd_lane_22 */
	{ TARGET_SD_LANE + 23,    0x580000 }, /* 0x610d88000: sd_lane_23 */
	{ TARGET_SD_LANE + 24,    0x588000 }, /* 0x610d90000: sd_lane_24 */
	{ TARGET_SD_LANE_25G,     0x590000 }, /* 0x610d98000: sd_lane_25g_25 */
	{ TARGET_SD_LANE_25G + 1, 0x598000 }, /* 0x610da0000: sd_lane_25g_26 */
	{ TARGET_SD_LANE_25G + 2, 0x5a0000 }, /* 0x610da8000: sd_lane_25g_27 */
	{ TARGET_SD_LANE_25G + 3, 0x5a8000 }, /* 0x610db0000: sd_lane_25g_28 */
	{ TARGET_SD_LANE_25G + 4, 0x5b0000 }, /* 0x610db8000: sd_lane_25g_29 */
	{ TARGET_SD_LANE_25G + 5, 0x5b8000 }, /* 0x610dc0000: sd_lane_25g_30 */
	{ TARGET_SD_LANE_25G + 6, 0x5c0000 }, /* 0x610dc8000: sd_lane_25g_31 */
	{ TARGET_SD_LANE_25G + 7, 0x5c8000 }, /* 0x610dd0000: sd_lane_25g_32 */
};

/* Client lookup function, uses serdes index */
static struct phy *sparx5_serdes_xlate(struct device *dev,
				     struct of_phandle_args *args)
{
	struct sparx5_serdes_private *priv = dev_get_drvdata(dev);
	int idx;
	unsigned int sidx;

	if (args->args_count != 1)
		return ERR_PTR(-EINVAL);

	sidx = args->args[0];

	/* Check validity: ERR_PTR(-ENODEV) if not valid */
	for (idx = 0; idx < SPX5_SERDES_MAX; idx++) {
		struct sparx5_serdes_macro *macro =
			phy_get_drvdata(priv->phys[idx]);

		if (sidx != macro->sidx)
			continue;

		return priv->phys[idx];
	}
	return ERR_PTR(-ENODEV);
}

static int sparx5_serdes_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sparx5_serdes_private *priv;
	struct phy_provider *provider;
	struct resource *iores;
	void __iomem *iomem;
	unsigned long clock;
	struct clk *clk;
	int idx;
	int err;

	if (!np && !pdev->dev.platform_data)
		return -ENODEV;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	priv->dev = &pdev->dev;

	/* Get coreclock */
	clk = devm_clk_get(priv->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(priv->dev, "Failed to get coreclock\n");
		return PTR_ERR(clk);
	}
	clock = clk_get_rate(clk);
	if (clock == 0) {
		dev_err(priv->dev, "Invalid coreclock %lu\n", clock);
		return -EINVAL;
	}
	priv->coreclock = clock;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores) {
		dev_err(priv->dev, "Invalid resource\n");
		return -EINVAL;
	}
	iomem = devm_ioremap(priv->dev, iores->start, resource_size(iores));
	if (!iomem) {
		dev_err(priv->dev, "Unable to get serdes registers: %s\n",
			iores->name);
		return -ENOMEM;
	}
	for (idx = 0; idx < ARRAY_SIZE(sparx5_serdes_iomap); idx++) {
		struct sparx5_serdes_io_resource *iomap = &sparx5_serdes_iomap[idx];

		priv->regs[iomap->id] = iomem + iomap->offset;
	}
	for (idx = 0; idx < SPX5_SERDES_MAX; idx++) {
		err = sparx5_phy_create(priv, idx, &priv->phys[idx]);
		if (err)
			return err;
	}

	/* Power down all CMUs by default */
	sparx5_serdes_cmu_power_off(priv);

	provider = devm_of_phy_provider_register(priv->dev, sparx5_serdes_xlate);

	return PTR_ERR_OR_ZERO(provider);
}

static const struct of_device_id sparx5_serdes_match[] = {
	{ .compatible = "microchip,sparx5-serdes" },
	{ }
};
MODULE_DEVICE_TABLE(of, sparx5_serdes_match);

static struct platform_driver sparx5_serdes_driver = {
	.probe = sparx5_serdes_probe,
	.driver = {
		.name = "sparx5-serdes",
		.of_match_table = sparx5_serdes_match,
	},
};

module_platform_driver(sparx5_serdes_driver);

MODULE_DESCRIPTION("Microchip Sparx5 switch serdes driver");
MODULE_AUTHOR("Steen Hegelund <steen.hegelund@microchip.com>");
MODULE_LICENSE("GPL v2");
