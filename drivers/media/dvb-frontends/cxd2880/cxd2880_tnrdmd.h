/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cxd2880_tnrdmd.h
 * Sony CXD2880 DVB-T2/T tuner + demodulator driver
 * common control interface
 *
 * Copyright (C) 2016, 2017, 2018 Sony Semiconductor Solutions Corporation
 */

#ifndef CXD2880_TNRDMD_H
#define CXD2880_TNRDMD_H

#include <linux/atomic.h>

#include "cxd2880_common.h"
#include "cxd2880_io.h"
#include "cxd2880_dtv.h"
#include "cxd2880_dvbt.h"
#include "cxd2880_dvbt2.h"

#define CXD2880_TNRDMD_MAX_CFG_MEM_COUNT 100

#define slvt_unfreeze_reg(tnr_dmd) ((void)((tnr_dmd)->io->write_reg\
((tnr_dmd)->io, CXD2880_IO_TGT_DMD, 0x01, 0x00)))

#define CXD2880_TNRDMD_INTERRUPT_TYPE_BUF_UNDERFLOW     0x0001
#define CXD2880_TNRDMD_INTERRUPT_TYPE_BUF_OVERFLOW      0x0002
#define CXD2880_TNRDMD_INTERRUPT_TYPE_BUF_ALMOST_EMPTY  0x0004
#define CXD2880_TNRDMD_INTERRUPT_TYPE_BUF_ALMOST_FULL   0x0008
#define CXD2880_TNRDMD_INTERRUPT_TYPE_BUF_RRDY	  0x0010
#define CXD2880_TNRDMD_INTERRUPT_TYPE_ILLEGAL_COMMAND      0x0020
#define CXD2880_TNRDMD_INTERRUPT_TYPE_ILLEGAL_ACCESS       0x0040
#define CXD2880_TNRDMD_INTERRUPT_TYPE_CPU_ERROR	    0x0100
#define CXD2880_TNRDMD_INTERRUPT_TYPE_LOCK		 0x0200
#define CXD2880_TNRDMD_INTERRUPT_TYPE_INV_LOCK	     0x0400
#define CXD2880_TNRDMD_INTERRUPT_TYPE_NOOFDM	       0x0800
#define CXD2880_TNRDMD_INTERRUPT_TYPE_EWS		  0x1000
#define CXD2880_TNRDMD_INTERRUPT_TYPE_EEW		  0x2000
#define CXD2880_TNRDMD_INTERRUPT_TYPE_FEC_FAIL	     0x4000

#define CXD2880_TNRDMD_INTERRUPT_LOCK_SEL_L1POST_OK	0x01
#define CXD2880_TNRDMD_INTERRUPT_LOCK_SEL_DMD_LOCK	 0x02
#define CXD2880_TNRDMD_INTERRUPT_LOCK_SEL_TS_LOCK	  0x04

enum cxd2880_tnrdmd_chip_id {
	CXD2880_TNRDMD_CHIP_ID_UNKNOWN = 0x00,
	CXD2880_TNRDMD_CHIP_ID_CXD2880_ES1_0X = 0x62,
	CXD2880_TNRDMD_CHIP_ID_CXD2880_ES1_11 = 0x6a
};

#define CXD2880_TNRDMD_CHIP_ID_VALID(chip_id) \
	(((chip_id) == CXD2880_TNRDMD_CHIP_ID_CXD2880_ES1_0X) || \
	 ((chip_id) == CXD2880_TNRDMD_CHIP_ID_CXD2880_ES1_11))

enum cxd2880_tnrdmd_state {
	CXD2880_TNRDMD_STATE_UNKNOWN,
	CXD2880_TNRDMD_STATE_SLEEP,
	CXD2880_TNRDMD_STATE_ACTIVE,
	CXD2880_TNRDMD_STATE_INVALID
};

enum cxd2880_tnrdmd_divermode {
	CXD2880_TNRDMD_DIVERMODE_SINGLE,
	CXD2880_TNRDMD_DIVERMODE_MAIN,
	CXD2880_TNRDMD_DIVERMODE_SUB
};

enum cxd2880_tnrdmd_clockmode {
	CXD2880_TNRDMD_CLOCKMODE_UNKNOWN,
	CXD2880_TNRDMD_CLOCKMODE_A,
	CXD2880_TNRDMD_CLOCKMODE_B,
	CXD2880_TNRDMD_CLOCKMODE_C
};

enum cxd2880_tnrdmd_tsout_if {
	CXD2880_TNRDMD_TSOUT_IF_TS,
	CXD2880_TNRDMD_TSOUT_IF_SPI,
	CXD2880_TNRDMD_TSOUT_IF_SDIO
};

enum cxd2880_tnrdmd_xtal_share {
	CXD2880_TNRDMD_XTAL_SHARE_NONE,
	CXD2880_TNRDMD_XTAL_SHARE_EXTREF,
	CXD2880_TNRDMD_XTAL_SHARE_MASTER,
	CXD2880_TNRDMD_XTAL_SHARE_SLAVE
};

enum cxd2880_tnrdmd_spectrum_sense {
	CXD2880_TNRDMD_SPECTRUM_NORMAL,
	CXD2880_TNRDMD_SPECTRUM_INV
};

enum cxd2880_tnrdmd_cfg_id {
	CXD2880_TNRDMD_CFG_OUTPUT_SEL_MSB,
	CXD2880_TNRDMD_CFG_TSVALID_ACTIVE_HI,
	CXD2880_TNRDMD_CFG_TSSYNC_ACTIVE_HI,
	CXD2880_TNRDMD_CFG_TSERR_ACTIVE_HI,
	CXD2880_TNRDMD_CFG_LATCH_ON_POSEDGE,
	CXD2880_TNRDMD_CFG_TSCLK_CONT,
	CXD2880_TNRDMD_CFG_TSCLK_MASK,
	CXD2880_TNRDMD_CFG_TSVALID_MASK,
	CXD2880_TNRDMD_CFG_TSERR_MASK,
	CXD2880_TNRDMD_CFG_TSERR_VALID_DIS,
	CXD2880_TNRDMD_CFG_TSPIN_CURRENT,
	CXD2880_TNRDMD_CFG_TSPIN_PULLUP_MANUAL,
	CXD2880_TNRDMD_CFG_TSPIN_PULLUP,
	CXD2880_TNRDMD_CFG_TSCLK_FREQ,
	CXD2880_TNRDMD_CFG_TSBYTECLK_MANUAL,
	CXD2880_TNRDMD_CFG_TS_PACKET_GAP,
	CXD2880_TNRDMD_CFG_TS_BACKWARDS_COMPATIBLE,
	CXD2880_TNRDMD_CFG_PWM_VALUE,
	CXD2880_TNRDMD_CFG_INTERRUPT,
	CXD2880_TNRDMD_CFG_INTERRUPT_LOCK_SEL,
	CXD2880_TNRDMD_CFG_INTERRUPT_INV_LOCK_SEL,
	CXD2880_TNRDMD_CFG_TS_BUF_ALMOST_EMPTY_THRS,
	CXD2880_TNRDMD_CFG_TS_BUF_ALMOST_FULL_THRS,
	CXD2880_TNRDMD_CFG_TS_BUF_RRDY_THRS,
	CXD2880_TNRDMD_CFG_FIXED_CLOCKMODE,
	CXD2880_TNRDMD_CFG_CABLE_INPUT,
	CXD2880_TNRDMD_CFG_DVBT2_FEF_INTERMITTENT_BASE,
	CXD2880_TNRDMD_CFG_DVBT2_FEF_INTERMITTENT_LITE,
	CXD2880_TNRDMD_CFG_BLINDTUNE_DVBT2_FIRST,
	CXD2880_TNRDMD_CFG_DVBT_BERN_PERIOD,
	CXD2880_TNRDMD_CFG_DVBT_VBER_PERIOD,
	CXD2880_TNRDMD_CFG_DVBT_PER_MES,
	CXD2880_TNRDMD_CFG_DVBT2_BBER_MES,
	CXD2880_TNRDMD_CFG_DVBT2_LBER_MES,
	CXD2880_TNRDMD_CFG_DVBT2_PER_MES,
};

enum cxd2880_tnrdmd_lock_result {
	CXD2880_TNRDMD_LOCK_RESULT_NOTDETECT,
	CXD2880_TNRDMD_LOCK_RESULT_LOCKED,
	CXD2880_TNRDMD_LOCK_RESULT_UNLOCKED
};

enum cxd2880_tnrdmd_gpio_mode {
	CXD2880_TNRDMD_GPIO_MODE_OUTPUT = 0x00,
	CXD2880_TNRDMD_GPIO_MODE_INPUT = 0x01,
	CXD2880_TNRDMD_GPIO_MODE_INT = 0x02,
	CXD2880_TNRDMD_GPIO_MODE_FEC_FAIL = 0x03,
	CXD2880_TNRDMD_GPIO_MODE_PWM = 0x04,
	CXD2880_TNRDMD_GPIO_MODE_EWS = 0x05,
	CXD2880_TNRDMD_GPIO_MODE_EEW = 0x06
};

enum cxd2880_tnrdmd_serial_ts_clk {
	CXD2880_TNRDMD_SERIAL_TS_CLK_FULL,
	CXD2880_TNRDMD_SERIAL_TS_CLK_HALF
};

struct cxd2880_tnrdmd_cfg_mem {
	enum cxd2880_io_tgt tgt;
	u8 bank;
	u8 address;
	u8 value;
	u8 bit_mask;
};

struct cxd2880_tnrdmd_pid_cfg {
	u8 is_en;
	u16 pid;
};

struct cxd2880_tnrdmd_pid_ftr_cfg {
	u8 is_negative;
	struct cxd2880_tnrdmd_pid_cfg pid_cfg[32];
};

struct cxd2880_tnrdmd_lna_thrs {
	u8 off_on;
	u8 on_off;
};

struct cxd2880_tnrdmd_lna_thrs_tbl_air {
	struct cxd2880_tnrdmd_lna_thrs thrs[24];
};

struct cxd2880_tnrdmd_lna_thrs_tbl_cable {
	struct cxd2880_tnrdmd_lna_thrs thrs[32];
};

struct cxd2880_tnrdmd_create_param {
	enum cxd2880_tnrdmd_tsout_if ts_output_if;
	u8 en_internal_ldo;
	enum cxd2880_tnrdmd_xtal_share xtal_share_type;
	u8 xosc_cap;
	u8 xosc_i;
	u8 is_cxd2881gg;
	u8 stationary_use;
};

struct cxd2880_tnrdmd_diver_create_param {
	enum cxd2880_tnrdmd_tsout_if ts_output_if;
	u8 en_internal_ldo;
	u8 xosc_cap_main;
	u8 xosc_i_main;
	u8 xosc_i_sub;
	u8 is_cxd2881gg;
	u8 stationary_use;
};

struct cxd2880_tnrdmd {
	struct cxd2880_tnrdmd *diver_sub;
	struct cxd2880_io *io;
	struct cxd2880_tnrdmd_create_param create_param;
	enum cxd2880_tnrdmd_divermode diver_mode;
	enum cxd2880_tnrdmd_clockmode fixed_clk_mode;
	u8 is_cable_input;
	u8 en_fef_intmtnt_base;
	u8 en_fef_intmtnt_lite;
	u8 blind_tune_dvbt2_first;
	int (*rf_lvl_cmpstn)(struct cxd2880_tnrdmd *tnr_dmd,
			     int *rf_lvl_db);
	struct cxd2880_tnrdmd_lna_thrs_tbl_air *lna_thrs_tbl_air;
	struct cxd2880_tnrdmd_lna_thrs_tbl_cable *lna_thrs_tbl_cable;
	u8 srl_ts_clk_mod_cnts;
	enum cxd2880_tnrdmd_serial_ts_clk srl_ts_clk_frq;
	u8 ts_byte_clk_manual_setting;
	u8 is_ts_backwards_compatible_mode;
	struct cxd2880_tnrdmd_cfg_mem cfg_mem[CXD2880_TNRDMD_MAX_CFG_MEM_COUNT];
	u8 cfg_mem_last_entry;
	struct cxd2880_tnrdmd_pid_ftr_cfg pid_ftr_cfg;
	u8 pid_ftr_cfg_en;
	void *user;
	enum cxd2880_tnrdmd_chip_id chip_id;
	enum cxd2880_tnrdmd_state state;
	enum cxd2880_tnrdmd_clockmode clk_mode;
	u32 frequency_khz;
	enum cxd2880_dtv_sys sys;
	enum cxd2880_dtv_bandwidth bandwidth;
	u8 scan_mode;
	atomic_t cancel;
};

int cxd2880_tnrdmd_create(struct cxd2880_tnrdmd *tnr_dmd,
			  struct cxd2880_io *io,
			  struct cxd2880_tnrdmd_create_param
			  *create_param);

int cxd2880_tnrdmd_diver_create(struct cxd2880_tnrdmd
				*tnr_dmd_main,
				struct cxd2880_io *io_main,
				struct cxd2880_tnrdmd *tnr_dmd_sub,
				struct cxd2880_io *io_sub,
				struct
				cxd2880_tnrdmd_diver_create_param
				*create_param);

int cxd2880_tnrdmd_init1(struct cxd2880_tnrdmd *tnr_dmd);

int cxd2880_tnrdmd_init2(struct cxd2880_tnrdmd *tnr_dmd);

int cxd2880_tnrdmd_check_internal_cpu_status(struct cxd2880_tnrdmd
					     *tnr_dmd,
					     u8 *task_completed);

int cxd2880_tnrdmd_common_tune_setting1(struct cxd2880_tnrdmd
					*tnr_dmd,
					enum cxd2880_dtv_sys sys,
					u32 frequency_khz,
					enum cxd2880_dtv_bandwidth
					bandwidth, u8 one_seg_opt,
					u8 one_seg_opt_shft_dir);

int cxd2880_tnrdmd_common_tune_setting2(struct cxd2880_tnrdmd
					*tnr_dmd,
					enum cxd2880_dtv_sys sys,
					u8 en_fef_intmtnt_ctrl);

int cxd2880_tnrdmd_sleep(struct cxd2880_tnrdmd *tnr_dmd);

int cxd2880_tnrdmd_set_cfg(struct cxd2880_tnrdmd *tnr_dmd,
			   enum cxd2880_tnrdmd_cfg_id id,
			   int value);

int cxd2880_tnrdmd_gpio_set_cfg(struct cxd2880_tnrdmd *tnr_dmd,
				u8 id,
				u8 en,
				enum cxd2880_tnrdmd_gpio_mode mode,
				u8 open_drain, u8 invert);

int cxd2880_tnrdmd_gpio_set_cfg_sub(struct cxd2880_tnrdmd *tnr_dmd,
				    u8 id,
				    u8 en,
				    enum cxd2880_tnrdmd_gpio_mode
				    mode, u8 open_drain,
				    u8 invert);

int cxd2880_tnrdmd_gpio_read(struct cxd2880_tnrdmd *tnr_dmd,
			     u8 id, u8 *value);

int cxd2880_tnrdmd_gpio_read_sub(struct cxd2880_tnrdmd *tnr_dmd,
				 u8 id, u8 *value);

int cxd2880_tnrdmd_gpio_write(struct cxd2880_tnrdmd *tnr_dmd,
			      u8 id, u8 value);

int cxd2880_tnrdmd_gpio_write_sub(struct cxd2880_tnrdmd *tnr_dmd,
				  u8 id, u8 value);

int cxd2880_tnrdmd_interrupt_read(struct cxd2880_tnrdmd *tnr_dmd,
				  u16 *value);

int cxd2880_tnrdmd_interrupt_clear(struct cxd2880_tnrdmd *tnr_dmd,
				   u16 value);

int cxd2880_tnrdmd_ts_buf_clear(struct cxd2880_tnrdmd *tnr_dmd,
				u8 clear_overflow_flag,
				u8 clear_underflow_flag,
				u8 clear_buf);

int cxd2880_tnrdmd_chip_id(struct cxd2880_tnrdmd *tnr_dmd,
			   enum cxd2880_tnrdmd_chip_id *chip_id);

int cxd2880_tnrdmd_set_and_save_reg_bits(struct cxd2880_tnrdmd
					 *tnr_dmd,
					 enum cxd2880_io_tgt tgt,
					 u8 bank, u8 address,
					 u8 value, u8 bit_mask);

int cxd2880_tnrdmd_set_scan_mode(struct cxd2880_tnrdmd *tnr_dmd,
				 enum cxd2880_dtv_sys sys,
				 u8 scan_mode_end);

int cxd2880_tnrdmd_set_pid_ftr(struct cxd2880_tnrdmd *tnr_dmd,
			       struct cxd2880_tnrdmd_pid_ftr_cfg
			       *pid_ftr_cfg);

int cxd2880_tnrdmd_set_rf_lvl_cmpstn(struct cxd2880_tnrdmd
				     *tnr_dmd,
				     int (*rf_lvl_cmpstn)
				     (struct cxd2880_tnrdmd *,
				     int *));

int cxd2880_tnrdmd_set_rf_lvl_cmpstn_sub(struct cxd2880_tnrdmd *tnr_dmd,
					 int (*rf_lvl_cmpstn)
					 (struct cxd2880_tnrdmd *,
					 int *));

int cxd2880_tnrdmd_set_lna_thrs(struct cxd2880_tnrdmd *tnr_dmd,
				struct
				cxd2880_tnrdmd_lna_thrs_tbl_air
				*tbl_air,
				struct
				cxd2880_tnrdmd_lna_thrs_tbl_cable
				*tbl_cable);

int cxd2880_tnrdmd_set_lna_thrs_sub(struct cxd2880_tnrdmd *tnr_dmd,
				    struct
				    cxd2880_tnrdmd_lna_thrs_tbl_air
				    *tbl_air,
				    struct
				    cxd2880_tnrdmd_lna_thrs_tbl_cable
				    *tbl_cable);

int cxd2880_tnrdmd_set_ts_pin_high_low(struct cxd2880_tnrdmd
				       *tnr_dmd, u8 en, u8 value);

int cxd2880_tnrdmd_set_ts_output(struct cxd2880_tnrdmd *tnr_dmd,
				 u8 en);

int slvt_freeze_reg(struct cxd2880_tnrdmd *tnr_dmd);

#endif
