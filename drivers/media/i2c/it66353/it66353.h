// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * it66353 HDMI 3 in 1 out driver.
 *
 * Author: Kenneth.Hung@ite.com.tw
 * 	   Wangqiang Guo <kay.guo@rock-chips.com>
 * Version: IT66353_SAMPLE_1.08
 *
 */
#ifndef _IT66353_H_
#define _IT66353_H_

typedef struct {
	u8 EnRxDDCBypass;
	u8 EnableAutoEQ;
} IT6635_DEVICE_OPTION;

#ifdef __cplusplus
extern "C" {
#endif

// ------------------------------
// APIs:

char *it66353_get_lib_version(void);

void it66353_setup_edid_ram_phyaddr(u8 *edid, u8 block);
void it66353_set_internal_EDID(u8 block, u8 *edid, u8 target_port);
void it66353_get_internal_EDID(u8 block, u8 *edid, u8 target_port);
void it66353_parse_edid_for_phyaddr(u8 *edid);
bool it66353_read_one_block_edid(u8 block, u8 *edid_buffer);

#define SW_HPD_LOW 0
#define SW_HPD_AUTO 1
void it66353_force_rx_hpd(u8 hpd_state);

void it66353_set_option(IT6635_DEVICE_OPTION *Opts);
void it66353_get_option(IT6635_DEVICE_OPTION *Opts);

u8 it66353_get_RS(void);
void it66353_set_RS(u8 rs_idx0, u8 rs_idx1, u8 rs_idx2);
void it66353_set_ch_RS(u8 ch, u8 rs_index);

void it66353_dump_register_all(void);
void it66353_dump_opts(void);

u8 it66353_get_active_port(void);
bool it66353_set_active_port(u8 port);

void it66353_change_default_RS(u8 port, u8 new_rs_idx0,
u8 new_rs_idx1, u8 new_rs_idx2, u8 update_hw);

void it66353_set_rx_hpd(u8 hpd_value);
void it66353_set_tx_5v(u8 output_value);
bool it66353_toggle_hpd(u16 ms_duration);

bool it66353_auto_eq_adjust(void);

void it66353_dev_restart(void);
void it66353_vars_init(void);
void it66353_options_init(void);

/*
 * it6635 event handler:
 */
// static void it66353_dev_loop(void);

/*
 * platform dependent functions: (needs implementation)
 * u8 it66353_i2c_write(u8 addr, u8 offset, u8 length, u8 *buffer);
 * u8 it66353_i2c_read(u8 addr, u8 offset, u8 length, u8 *buffer);
 * static void it66353_i2c_read(u8 i2c_addr, u16 reg, u8 n, u8 *val);
 */
void delay1ms(u16 ms);
__tick get_tick_count(void);

#ifdef __cplusplus
}
#endif

#endif
