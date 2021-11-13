/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTW_EFUSE_H__
#define __RTW_EFUSE_H__

#include "osdep_service.h"

#define	EFUSE_ERROE_HANDLE		1

/* E-Fuse */
#define EFUSE_MAP_SIZE      512
#define EFUSE_MAX_SIZE      256
/* end of E-Fuse */

#define		EFUSE_MAX_MAP_LEN		512
#define		EFUSE_MAX_HW_SIZE		512
#define		EFUSE_MAX_SECTION_BASE	16

#define EXT_HEADER(header) ((header & 0x1F) == 0x0F)
#define ALL_WORDS_DISABLED(wde)	((wde & 0x0F) == 0x0F)
#define GET_HDR_OFFSET_2_0(header) ((header & 0xE0) >> 5)

#define		EFUSE_REPEAT_THRESHOLD_			3

/*--------------------------Define Parameters-------------------------------*/
#define		EFUSE_MAX_WORD_UNIT			4

/*------------------------Export global variable----------------------------*/

u8 Efuse_CalculateWordCnts(u8 word_en);
void ReadEFuseByte(struct adapter *adapter, u16 _offset, u8 *pbuf);

void efuse_WordEnableDataRead(u8 word_en, u8 *sourdata, u8 *targetdata);

void EFUSE_ShadowMapUpdate(struct adapter *adapter);

#endif
