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
	uint16_t id;
	uint16_t val;
} wilc_cfg_byte_t;

typedef struct {
	uint16_t id;
	uint16_t val;
} wilc_cfg_hword_t;

typedef struct {
	uint32_t id;
	uint32_t val;
} wilc_cfg_word_t;

typedef struct {
	uint32_t id;
	uint8_t *str;
} wilc_cfg_str_t;

#endif
