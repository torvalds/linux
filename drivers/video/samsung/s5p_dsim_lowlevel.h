/* linux/drivers/video/samsung/s5p_dsim_lowlevel.h
 *
 * Header file for Samsung MIPI-DSIM lowlevel driver.
 *
 * Copyright (c) 2009 Samsung Electronics
 * InKi Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _S5P_DSIM_LOWLEVEL_H
#define _S5P_DSIM_LOWLEVEL_H

extern void s5p_dsim_func_reset(unsigned int dsim_base);
extern void s5p_dsim_sw_reset(unsigned int dsim_base);
extern void s5p_dsim_set_interrupt_mask(unsigned int dsim_base, unsigned int mode,
	unsigned char mask);
extern void s5p_dsim_set_data_lane_number(unsigned int dsim_base, unsigned char count);
extern void s5p_dsim_init_fifo_pointer(unsigned int dsim_base, unsigned char cfg);
extern void s5p_dsim_set_phy_tunning(unsigned int dsim_base, unsigned int value);
extern void s5p_dsim_set_phy_tunning(unsigned int dsim_base, unsigned int value);
extern void s5p_dsim_set_main_disp_resol(unsigned int dsim_base, unsigned short vert_resol,
	unsigned short hori_resol);
extern void s5p_dsim_set_main_disp_vporch(unsigned int dsim_base, unsigned short cmd_allow,
	unsigned short vfront, unsigned short vback);
extern void s5p_dsim_set_main_disp_hporch(unsigned int dsim_base, unsigned short front,
	unsigned short back);
extern void s5p_dsim_set_main_disp_sync_area(unsigned int dsim_base, unsigned short vert,
	unsigned short hori);
extern void s5p_dsim_set_sub_disp_resol(unsigned int dsim_base, unsigned short vert,
	unsigned short hori);
extern void s5p_dsim_init_config(unsigned int dsim_base, struct dsim_lcd_config *main_lcd_info,
	struct dsim_lcd_config *sub_lcd_info, struct dsim_config *dsim_info);
extern void s5p_dsim_display_config(unsigned int dsim_base,
	struct dsim_lcd_config *main_lcd, struct dsim_lcd_config *sub_lcd);
extern void s5p_dsim_set_data_lane_number(unsigned int dsim_base, unsigned char count);
extern void s5p_dsim_enable_lane(unsigned int dsim_base, unsigned char lane, unsigned char enable);
extern void s5p_dsim_enable_afc(unsigned int dsim_base, unsigned char enable,
	unsigned char afc_code);
extern void s5p_dsim_enable_pll_bypass(unsigned int dsim_base, unsigned char enable);
extern void s5p_dsim_set_pll_pms(unsigned int dsim_base, unsigned char p,
	unsigned short m, unsigned short s);
extern void s5p_dsim_pll_freq_band(unsigned int dsim_base, unsigned char freq_band);
extern void s5p_dsim_pll_freq(unsigned int dsim_base, unsigned char pre_divider,
	unsigned short main_divider, unsigned char scaler);
extern void s5p_dsim_pll_stable_time(unsigned int dsim_base, unsigned int lock_time);
extern void s5p_dsim_enable_pll(unsigned int dsim_base, unsigned char enable);
extern void s5p_dsim_set_byte_clock_src(unsigned int dsim_base, unsigned char src);
extern void s5p_dsim_enable_byte_clock(unsigned int dsim_base, unsigned char enable);
extern void s5p_dsim_set_esc_clk_prs(unsigned int dsim_base, unsigned char enable,
	unsigned short prs_val);
extern void s5p_dsim_enable_esc_clk_on_lane(unsigned int dsim_base,
	unsigned char lane_sel, unsigned char enable);
extern void s5p_dsim_force_dphy_stop_state(unsigned int dsim_base, unsigned char enable);
extern unsigned char s5p_dsim_is_lane_state(unsigned int dsim_base, unsigned char lane);
extern void s5p_dsim_set_stop_state_counter(unsigned int dsim_base, unsigned short cnt_val);
extern void s5p_dsim_set_bta_timeout(unsigned int dsim_base, unsigned char timeout);
extern void s5p_dsim_set_lpdr_timeout(unsigned int dsim_base, unsigned short timeout);
extern void s5p_dsim_set_data_mode(unsigned int dsim_base, unsigned char data,
	unsigned char state);
extern void s5p_dsim_enable_hs_clock(unsigned int dsim_base, unsigned char enable);
extern void s5p_dsim_toggle_hs_clock(unsigned int dsim_base);
extern void s5p_dsim_dp_dn_swap(unsigned int dsim_base, unsigned char swap_en);
extern void s5p_dsim_hs_zero_ctrl(unsigned int dsim_base, unsigned char hs_zero);
extern void s5p_dsim_prep_ctrl(unsigned int dsim_base, unsigned char prep);
extern void s5p_dsim_clear_interrupt(unsigned int dsim_base, unsigned int int_src);
extern unsigned int s5p_dsim_is_interrupt_status(unsigned int dsim_base);
extern unsigned char s5p_dsim_is_pll_stable(unsigned int dsim_base);
extern unsigned int s5p_dsim_get_fifo_state(unsigned int dsim_base);
extern void s5p_dsim_wr_tx_header(unsigned int dsim_base,
	unsigned char di, unsigned char data0, unsigned char data1);
extern void s5p_dsim_wr_tx_data(unsigned int dsim_base, unsigned int tx_data);
extern int s5p_dsim_rd_rx_data(unsigned int dsim_base);
#endif /* _S5P_DSIM_LOWLEVEL_H */
