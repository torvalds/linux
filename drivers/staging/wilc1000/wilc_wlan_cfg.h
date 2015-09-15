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

#endif
