/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTW_EFUSE_H__
#define __RTW_EFUSE_H__

#define		EFUSE_MAX_WORD_UNIT			4

void ReadEFuseByte(struct adapter *adapter, u16 _offset, u8 *pbuf);

void EFUSE_ShadowMapUpdate(struct adapter *adapter);

#endif
