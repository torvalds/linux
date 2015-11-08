/* ////////////////////////////////////////////////////////////////////////// */
/*  */
/* Copyright (c) Atmel Corporation.  All rights reserved. */
/*  */
/* Module Name:  wilc_wlan_cfg.h */
/*  */
/*  */
/* ///////////////////////////////////////////////////////////////////////// */

#ifndef WILC_WLAN_CFG_H
#define WILC_WLAN_CFG_H

typedef struct {
	u16 id;
	u16 val;
} wilc_cfg_byte_t;

typedef struct {
	u16 id;
	u16 val;
} wilc_cfg_hword_t;

typedef struct {
	u32 id;
	u32 val;
} wilc_cfg_word_t;

typedef struct {
	u32 id;
	u8 *str;
} wilc_cfg_str_t;

int wilc_wlan_cfg_set_wid(u8 *frame, u32 offset, u16 id, u8 *buf, int size);
int wilc_wlan_cfg_get_wid(u8 *frame, u32 offset, u16 id);
int wilc_wlan_cfg_get_wid_value(u16 wid, u8 *buffer, u32 buffer_size);
int wilc_wlan_cfg_indicate_rx(u8 *frame, int size, wilc_cfg_rsp_t *rsp);
int wilc_wlan_cfg_init(wilc_debug_func func);

#endif
