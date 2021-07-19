/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTW_EFUSE_H__
#define __RTW_EFUSE_H__

#include <osdep_service.h>

#define	EFUSE_ERROE_HANDLE		1

#define	PG_STATE_HEADER			0x01
#define	PG_STATE_WORD_0		0x02
#define	PG_STATE_WORD_1		0x04
#define	PG_STATE_WORD_2		0x08
#define	PG_STATE_WORD_3		0x10
#define	PG_STATE_DATA			0x20

#define	PG_SWBYTE_H			0x01
#define	PG_SWBYTE_L			0x02

#define	PGPKT_DATA_SIZE		8

#define	EFUSE_WIFI				0
#define	EFUSE_BT				1

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

/*	The following is for BT Efuse definition */
#define		EFUSE_BT_MAX_MAP_LEN		1024
#define		EFUSE_MAX_BANK			4
#define		EFUSE_MAX_BT_BANK		(EFUSE_MAX_BANK - 1)
/*--------------------------Define Parameters-------------------------------*/
#define		EFUSE_MAX_WORD_UNIT			4

/*------------------------------Define structure----------------------------*/
struct pgpkt {
	u8 offset;
	u8 word_en;
	u8 data[8];
	u8 word_cnts;
};

u8 Efuse_CalculateWordCnts(u8 word_en);
u8 efuse_OneByteRead(struct adapter *adapter, u16 addr, u8 *data);
u8 efuse_OneByteWrite(struct adapter *adapter, u16 addr, u8 data);

void efuse_ReadEFuse(struct adapter *Adapter, u8 efuseType, u16 _offset,
		u16 _size_byte, u8 *pbuf);
int Efuse_PgPacketRead(struct adapter *adapt, u8 offset, u8 *data);
bool Efuse_PgPacketWrite(struct adapter *adapter, u8 offset, u8 word, u8 *data);
void efuse_WordEnableDataRead(u8 word_en, u8 *sourdata, u8 *targetdata);
u8 Efuse_WordEnableDataWrite(struct adapter *adapter, u16 efuse_addr,
			     u8 word_en, u8 *data);

void EFUSE_ShadowMapUpdate(struct adapter *adapter, u8 efusetype);
#endif
