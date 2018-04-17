/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 */

#ifndef PP_ATOMVOLTAGECTRL_H
#define PP_ATOMVOLTAGECTRL_H

#include "hwmgr.h"

#define MEM_TYPE_GDDR5  0x50
#define MEM_TYPE_GDDR4  0x40
#define MEM_TYPE_GDDR3  0x30
#define MEM_TYPE_DDR2   0x20
#define MEM_TYPE_GDDR1  0x10
#define MEM_TYPE_DDR3   0xb0
#define MEM_TYPE_MASK   0xF0


/* As returned from PowerConnectorDetectionTable. */
#define PP_ATOM_POWER_BUDGET_DISABLE_OVERDRIVE  0x80
#define PP_ATOM_POWER_BUDGET_SHOW_WARNING       0x40
#define PP_ATOM_POWER_BUDGET_SHOW_WAIVER        0x20
#define PP_ATOM_POWER_POWER_BUDGET_BEHAVIOUR    0x0F

/* New functions for Evergreen and beyond. */
#define PP_ATOMCTRL_MAX_VOLTAGE_ENTRIES 32

struct pp_atomctrl_clock_dividers {
	uint32_t pll_post_divider;
	uint32_t pll_feedback_divider;
	uint32_t pll_ref_divider;
	bool  enable_post_divider;
};

typedef struct pp_atomctrl_clock_dividers pp_atomctrl_clock_dividers;

union pp_atomctrl_tcipll_fb_divider {
	struct {
		uint32_t ul_fb_div_frac : 14;
		uint32_t ul_fb_div : 12;
		uint32_t un_used : 6;
	};
	uint32_t ul_fb_divider;
};

typedef union pp_atomctrl_tcipll_fb_divider pp_atomctrl_tcipll_fb_divider;

struct pp_atomctrl_clock_dividers_rv730 {
	uint32_t pll_post_divider;
	pp_atomctrl_tcipll_fb_divider mpll_feedback_divider;
	uint32_t pll_ref_divider;
	bool  enable_post_divider;
	bool  enable_dithen;
	uint32_t vco_mode;
};
typedef struct pp_atomctrl_clock_dividers_rv730 pp_atomctrl_clock_dividers_rv730;


struct pp_atomctrl_clock_dividers_kong {
	uint32_t    pll_post_divider;
	uint32_t    real_clock;
};
typedef struct pp_atomctrl_clock_dividers_kong pp_atomctrl_clock_dividers_kong;

struct pp_atomctrl_clock_dividers_ci {
	uint32_t    pll_post_divider;               /* post divider value */
	uint32_t    real_clock;
	pp_atomctrl_tcipll_fb_divider   ul_fb_div;         /* Output Parameter: PLL FB divider */
	uint8_t   uc_pll_ref_div;                      /* Output Parameter: PLL ref divider */
	uint8_t   uc_pll_post_div;                      /* Output Parameter: PLL post divider */
	uint8_t   uc_pll_cntl_flag;                    /*Output Flags: control flag */
};
typedef struct pp_atomctrl_clock_dividers_ci pp_atomctrl_clock_dividers_ci;

struct pp_atomctrl_clock_dividers_vi {
	uint32_t    pll_post_divider;               /* post divider value */
	uint32_t    real_clock;
	pp_atomctrl_tcipll_fb_divider   ul_fb_div;         /*Output Parameter: PLL FB divider */
	uint8_t   uc_pll_ref_div;                      /*Output Parameter: PLL ref divider */
	uint8_t   uc_pll_post_div;                     /*Output Parameter: PLL post divider */
	uint8_t   uc_pll_cntl_flag;                    /*Output Flags: control flag */
};
typedef struct pp_atomctrl_clock_dividers_vi pp_atomctrl_clock_dividers_vi;

struct pp_atomctrl_clock_dividers_ai {
	u16 usSclk_fcw_frac;
	u16  usSclk_fcw_int;
	u8   ucSclkPostDiv;
	u8   ucSclkVcoMode;
	u8   ucSclkPllRange;
	u8   ucSscEnable;
	u16  usSsc_fcw1_frac;
	u16  usSsc_fcw1_int;
	u16  usReserved;
	u16  usPcc_fcw_int;
	u16  usSsc_fcw_slew_frac;
	u16  usPcc_fcw_slew_frac;
};
typedef struct pp_atomctrl_clock_dividers_ai pp_atomctrl_clock_dividers_ai;


union pp_atomctrl_s_mpll_fb_divider {
	struct {
		uint32_t cl_kf : 12;
		uint32_t clk_frac : 12;
		uint32_t un_used : 8;
	};
	uint32_t ul_fb_divider;
};
typedef union pp_atomctrl_s_mpll_fb_divider pp_atomctrl_s_mpll_fb_divider;

enum pp_atomctrl_spread_spectrum_mode {
	pp_atomctrl_spread_spectrum_mode_down = 0,
	pp_atomctrl_spread_spectrum_mode_center
};
typedef enum pp_atomctrl_spread_spectrum_mode pp_atomctrl_spread_spectrum_mode;

struct pp_atomctrl_memory_clock_param {
	pp_atomctrl_s_mpll_fb_divider mpll_fb_divider;
	uint32_t mpll_post_divider;
	uint32_t bw_ctrl;
	uint32_t dll_speed;
	uint32_t vco_mode;
	uint32_t yclk_sel;
	uint32_t qdr;
	uint32_t half_rate;
};
typedef struct pp_atomctrl_memory_clock_param pp_atomctrl_memory_clock_param;

struct pp_atomctrl_internal_ss_info {
	uint32_t speed_spectrum_percentage;                      /* in 1/100 percentage */
	uint32_t speed_spectrum_rate;                            /* in KHz */
	pp_atomctrl_spread_spectrum_mode speed_spectrum_mode;
};
typedef struct pp_atomctrl_internal_ss_info pp_atomctrl_internal_ss_info;

#ifndef NUMBER_OF_M3ARB_PARAMS
#define NUMBER_OF_M3ARB_PARAMS 3
#endif

#ifndef NUMBER_OF_M3ARB_PARAM_SETS
#define NUMBER_OF_M3ARB_PARAM_SETS 10
#endif

struct pp_atomctrl_kong_system_info {
	uint32_t			ul_bootup_uma_clock;          /* in 10kHz unit */
	uint16_t			us_max_nb_voltage;            /* high NB voltage, calculated using current VDDNB (D24F2xDC) and VDDNB offset fuse; */
	uint16_t			us_min_nb_voltage;            /* low NB voltage, calculated using current VDDNB (D24F2xDC) and VDDNB offset fuse; */
	uint16_t			us_bootup_nb_voltage;         /* boot up NB voltage */
	uint8_t			uc_htc_tmp_lmt;               /* bit [22:16] of D24F3x64 Hardware Thermal Control (HTC) Register, may not be needed, TBD */
	uint8_t			uc_tj_offset;                /* bit [28:22] of D24F3xE4 Thermtrip Status Register,may not be needed, TBD */
	/* 0: default 1: uvd 2: fs-3d */
	uint32_t          ul_csr_m3_srb_cntl[NUMBER_OF_M3ARB_PARAM_SETS][NUMBER_OF_M3ARB_PARAMS];/* arrays with values for CSR M3 arbiter for default */
};
typedef struct pp_atomctrl_kong_system_info pp_atomctrl_kong_system_info;

struct pp_atomctrl_memory_info {
	uint8_t memory_vendor;
	uint8_t memory_type;
};
typedef struct pp_atomctrl_memory_info pp_atomctrl_memory_info;

#define MAX_AC_TIMING_ENTRIES 16

struct pp_atomctrl_memory_clock_range_table {
	uint8_t   num_entries;
	uint8_t   rsv[3];

	uint32_t mclk[MAX_AC_TIMING_ENTRIES];
};
typedef struct pp_atomctrl_memory_clock_range_table pp_atomctrl_memory_clock_range_table;

struct pp_atomctrl_voltage_table_entry {
	uint16_t value;
	uint32_t smio_low;
};

typedef struct pp_atomctrl_voltage_table_entry pp_atomctrl_voltage_table_entry;

struct pp_atomctrl_voltage_table {
	uint32_t count;
	uint32_t mask_low;
	uint32_t phase_delay;   /* Used for ATOM_GPIO_VOLTAGE_OBJECT_V3 and later */
	pp_atomctrl_voltage_table_entry entries[PP_ATOMCTRL_MAX_VOLTAGE_ENTRIES];
};

typedef struct pp_atomctrl_voltage_table pp_atomctrl_voltage_table;

#define VBIOS_MC_REGISTER_ARRAY_SIZE           32
#define VBIOS_MAX_AC_TIMING_ENTRIES            20

struct pp_atomctrl_mc_reg_entry {
	uint32_t           mclk_max;
	uint32_t mc_data[VBIOS_MC_REGISTER_ARRAY_SIZE];
};
typedef struct pp_atomctrl_mc_reg_entry pp_atomctrl_mc_reg_entry;

struct pp_atomctrl_mc_register_address {
	uint16_t s1;
	uint8_t  uc_pre_reg_data;
};

typedef struct pp_atomctrl_mc_register_address pp_atomctrl_mc_register_address;

#define MAX_SCLK_RANGE 8

struct pp_atom_ctrl_sclk_range_table_entry{
	uint8_t  ucVco_setting;
	uint8_t  ucPostdiv;
	uint16_t usFcw_pcc;
	uint16_t usFcw_trans_upper;
	uint16_t usRcw_trans_lower;
};


struct pp_atom_ctrl_sclk_range_table{
	struct pp_atom_ctrl_sclk_range_table_entry entry[MAX_SCLK_RANGE];
};

struct pp_atomctrl_mc_reg_table {
	uint8_t                         last;                    /* number of registers */
	uint8_t                         num_entries;             /* number of AC timing entries */
	pp_atomctrl_mc_reg_entry        mc_reg_table_entry[VBIOS_MAX_AC_TIMING_ENTRIES];
	pp_atomctrl_mc_register_address mc_reg_address[VBIOS_MC_REGISTER_ARRAY_SIZE];
};
typedef struct pp_atomctrl_mc_reg_table pp_atomctrl_mc_reg_table;

struct pp_atomctrl_gpio_pin_assignment {
	uint16_t                   us_gpio_pin_aindex;
	uint8_t                    uc_gpio_pin_bit_shift;
};
typedef struct pp_atomctrl_gpio_pin_assignment pp_atomctrl_gpio_pin_assignment;

struct pp_atom_ctrl__avfs_parameters {
	uint32_t  ulAVFS_meanNsigma_Acontant0;
	uint32_t  ulAVFS_meanNsigma_Acontant1;
	uint32_t  ulAVFS_meanNsigma_Acontant2;
	uint16_t usAVFS_meanNsigma_DC_tol_sigma;
	uint16_t usAVFS_meanNsigma_Platform_mean;
	uint16_t usAVFS_meanNsigma_Platform_sigma;
	uint32_t  ulGB_VDROOP_TABLE_CKSOFF_a0;
	uint32_t  ulGB_VDROOP_TABLE_CKSOFF_a1;
	uint32_t  ulGB_VDROOP_TABLE_CKSOFF_a2;
	uint32_t  ulGB_VDROOP_TABLE_CKSON_a0;
	uint32_t  ulGB_VDROOP_TABLE_CKSON_a1;
	uint32_t  ulGB_VDROOP_TABLE_CKSON_a2;
	uint32_t  ulAVFSGB_FUSE_TABLE_CKSOFF_m1;
	uint16_t  usAVFSGB_FUSE_TABLE_CKSOFF_m2;
	uint32_t  ulAVFSGB_FUSE_TABLE_CKSOFF_b;
	uint32_t  ulAVFSGB_FUSE_TABLE_CKSON_m1;
	uint16_t  usAVFSGB_FUSE_TABLE_CKSON_m2;
	uint32_t  ulAVFSGB_FUSE_TABLE_CKSON_b;
	uint16_t  usMaxVoltage_0_25mv;
	uint8_t  ucEnableGB_VDROOP_TABLE_CKSOFF;
	uint8_t  ucEnableGB_VDROOP_TABLE_CKSON;
	uint8_t  ucEnableGB_FUSE_TABLE_CKSOFF;
	uint8_t  ucEnableGB_FUSE_TABLE_CKSON;
	uint16_t usPSM_Age_ComFactor;
	uint8_t  ucEnableApplyAVFS_CKS_OFF_Voltage;
	uint8_t  ucReserved;
};

extern bool atomctrl_get_pp_assign_pin(struct pp_hwmgr *hwmgr, const uint32_t pinId, pp_atomctrl_gpio_pin_assignment *gpio_pin_assignment);
extern int atomctrl_get_voltage_evv_on_sclk(struct pp_hwmgr *hwmgr, uint8_t voltage_type, uint32_t sclk, uint16_t virtual_voltage_Id, uint16_t *voltage);
extern int atomctrl_get_voltage_evv(struct pp_hwmgr *hwmgr, uint16_t virtual_voltage_id, uint16_t *voltage);
extern uint32_t atomctrl_get_mpll_reference_clock(struct pp_hwmgr *hwmgr);
extern int atomctrl_get_memory_clock_spread_spectrum(struct pp_hwmgr *hwmgr, const uint32_t memory_clock, pp_atomctrl_internal_ss_info *ssInfo);
extern int atomctrl_get_engine_clock_spread_spectrum(struct pp_hwmgr *hwmgr, const uint32_t engine_clock, pp_atomctrl_internal_ss_info *ssInfo);
extern int atomctrl_initialize_mc_reg_table(struct pp_hwmgr *hwmgr, uint8_t module_index, pp_atomctrl_mc_reg_table *table);
extern int atomctrl_set_engine_dram_timings_rv770(struct pp_hwmgr *hwmgr, uint32_t engine_clock, uint32_t memory_clock);
extern uint32_t atomctrl_get_reference_clock(struct pp_hwmgr *hwmgr);
extern int atomctrl_get_memory_pll_dividers_si(struct pp_hwmgr *hwmgr, uint32_t clock_value, pp_atomctrl_memory_clock_param *mpll_param, bool strobe_mode);
extern int atomctrl_get_engine_pll_dividers_vi(struct pp_hwmgr *hwmgr, uint32_t clock_value, pp_atomctrl_clock_dividers_vi *dividers);
extern int atomctrl_get_dfs_pll_dividers_vi(struct pp_hwmgr *hwmgr, uint32_t clock_value, pp_atomctrl_clock_dividers_vi *dividers);
extern bool atomctrl_is_voltage_controlled_by_gpio_v3(struct pp_hwmgr *hwmgr, uint8_t voltage_type, uint8_t voltage_mode);
extern int atomctrl_get_voltage_table_v3(struct pp_hwmgr *hwmgr, uint8_t voltage_type, uint8_t voltage_mode, pp_atomctrl_voltage_table *voltage_table);
extern int atomctrl_get_memory_pll_dividers_vi(struct pp_hwmgr *hwmgr,
		uint32_t clock_value, pp_atomctrl_memory_clock_param *mpll_param);
extern int atomctrl_get_engine_pll_dividers_kong(struct pp_hwmgr *hwmgr,
						 uint32_t clock_value,
						 pp_atomctrl_clock_dividers_kong *dividers);
extern int atomctrl_read_efuse(void *device, uint16_t start_index,
		uint16_t end_index, uint32_t mask, uint32_t *efuse);
extern int atomctrl_calculate_voltage_evv_on_sclk(struct pp_hwmgr *hwmgr, uint8_t voltage_type,
		uint32_t sclk, uint16_t virtual_voltage_Id, uint16_t *voltage, uint16_t dpm_level, bool debug);
extern int atomctrl_get_engine_pll_dividers_ai(struct pp_hwmgr *hwmgr, uint32_t clock_value, pp_atomctrl_clock_dividers_ai *dividers);
extern int atomctrl_set_ac_timing_ai(struct pp_hwmgr *hwmgr, uint32_t memory_clock,
								uint8_t level);
extern int atomctrl_get_voltage_evv_on_sclk_ai(struct pp_hwmgr *hwmgr, uint8_t voltage_type,
				uint32_t sclk, uint16_t virtual_voltage_Id, uint32_t *voltage);
extern int atomctrl_get_smc_sclk_range_table(struct pp_hwmgr *hwmgr, struct pp_atom_ctrl_sclk_range_table *table);

extern int atomctrl_get_avfs_information(struct pp_hwmgr *hwmgr, struct pp_atom_ctrl__avfs_parameters *param);

extern int  atomctrl_get_svi2_info(struct pp_hwmgr *hwmgr, uint8_t voltage_type,
				uint8_t *svd_gpio_id, uint8_t *svc_gpio_id,
				uint16_t *load_line);

extern int atomctrl_get_leakage_vddc_base_on_leakage(struct pp_hwmgr *hwmgr,
					uint16_t *vddc, uint16_t *vddci,
					uint16_t virtual_voltage_id,
					uint16_t efuse_voltage_id);
extern int atomctrl_get_leakage_id_from_efuse(struct pp_hwmgr *hwmgr, uint16_t *virtual_voltage_id);
#endif

