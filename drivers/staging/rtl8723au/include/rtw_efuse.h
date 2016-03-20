/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
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

enum _EFUSE_DEF_TYPE {
	TYPE_EFUSE_MAX_SECTION				= 0,
	TYPE_EFUSE_REAL_CONTENT_LEN			= 1,
	TYPE_AVAILABLE_EFUSE_BYTES_BANK		= 2,
	TYPE_AVAILABLE_EFUSE_BYTES_TOTAL	= 3,
	TYPE_EFUSE_MAP_LEN					= 4,
	TYPE_EFUSE_PROTECT_BYTES_BANK		= 5,
	TYPE_EFUSE_CONTENT_LEN_BANK			= 6,
};

/* E-Fuse */
#define EFUSE_MAP_SIZE      256

#define EFUSE_MAX_SIZE      512
/* end of E-Fuse */

#define		EFUSE_MAX_MAP_LEN		256
#define		EFUSE_MAX_HW_SIZE		512
#define		EFUSE_MAX_SECTION_BASE	16

#define EXT_HEADER(header) ((header & 0x1F) == 0x0F)
#define ALL_WORDS_DISABLED(wde)	((wde & 0x0F) == 0x0F)
#define GET_HDR_OFFSET_2_0(header) ( (header & 0xE0) >> 5)

#define		EFUSE_REPEAT_THRESHOLD_			3

/*  */
/*	The following is for BT Efuse definition */
/*  */
#define		EFUSE_BT_MAX_MAP_LEN		1024
#define		EFUSE_MAX_BANK			4
#define		EFUSE_MAX_BT_BANK		(EFUSE_MAX_BANK-1)
/*  */
/*--------------------------Define Parameters-------------------------------*/
#define		EFUSE_MAX_WORD_UNIT			4

/*------------------------------Define structure----------------------------*/
struct pg_pkt_struct {
	u8 offset;
	u8 word_en;
	u8 data[8];
	u8 word_cnts;
};

/*------------------------Export global variable----------------------------*/

u16	efuse_GetMaxSize23a(struct rtw_adapter *padapter);
int	rtw_efuse_access23a(struct rtw_adapter *padapter, u8 bRead, u16 start_addr, u16 cnts, u8 *data);
int	rtw_efuse_map_read23a(struct rtw_adapter *padapter, u16 addr, u16 cnts, u8 *data);
u8	rtw_efuse_map_write(struct rtw_adapter *padapter, u16 addr, u16 cnts, u8 *data);
int	rtw_BT_efuse_map_read23a(struct rtw_adapter *padapter, u16 addr, u16 cnts, u8 *data);
u8	rtw_BT_efuse_map_write(struct rtw_adapter *padapter, u16 addr, u16 cnts, u8 *data);

u16	Efuse_GetCurrentSize23a(struct rtw_adapter *pAdapter, u8 efuseType);
u8	Efuse_CalculateWordCnts23a(u8 word_en);
void	ReadEFuseByte23a(struct rtw_adapter *Adapter, u16 _offset, u8 *pbuf);
void	EFUSE_GetEfuseDefinition23a(struct rtw_adapter *pAdapter, u8 efuseType, u8 type, void *pOut);
int	efuse_OneByteRead23a(struct rtw_adapter *pAdapter, u16 addr, u8 *data);
int	efuse_OneByteWrite23a(struct rtw_adapter *pAdapter, u16 addr, u8 data);

void	Efuse_PowerSwitch23a(struct rtw_adapter *pAdapter, u8 bWrite,
			     u8 PwrState);
int	Efuse_PgPacketRead23a(struct rtw_adapter *pAdapter, u8 offset, u8 *data);
int	Efuse_PgPacketWrite23a(struct rtw_adapter *pAdapter, u8 offset, u8 word_en, u8 *data);
void	efuse_WordEnableDataRead23a(u8 word_en, u8 *sourdata, u8 *targetdata);
u8	Efuse_WordEnableDataWrite23a(struct rtw_adapter *pAdapter, u16 efuse_addr, u8 word_en, u8 *data);

u8	EFUSE_Read1Byte23a(struct rtw_adapter *pAdapter, u16 Address);
void	EFUSE_ShadowMapUpdate23a(struct rtw_adapter *pAdapter, u8 efuseType);
void	EFUSE_ShadowRead23a(struct rtw_adapter *pAdapter, u8 Type, u16 Offset, u32 *Value);

#endif
