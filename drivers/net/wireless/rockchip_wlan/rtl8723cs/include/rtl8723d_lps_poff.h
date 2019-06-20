/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/

/******************************************** CONST  ************************/
#define NUM_OF_REGISTER_BANK	13
#define NUM_OF_TOTAL_DWORD (NUM_OF_REGISTER_BANK * 64)
#define TOTAL_LEN_FOR_HIOE ((NUM_OF_TOTAL_DWORD + 1) * 8)
#define LPS_POFF_STATIC_FILE_LEN (TOTAL_LEN_FOR_HIOE + TXDESC_SIZE)
#define LPS_POFF_DYNAMIC_FILE_LEN	(512 + TXDESC_SIZE)
/******************************************** CONST  ************************/

/******************************************** MACRO   ************************/
/* HOIE Entry Definition */
#define SET_HOIE_ENTRY_LOW_DATA(__pHOIE, __Value) \
	SET_BITS_TO_LE_4BYTE((__pHOIE),	0, 16, __Value)
#define SET_HOIE_ENTRY_HIGH_DATA(__pHOIE, __Value) \
	SET_BITS_TO_LE_4BYTE((__pHOIE), 16, 16, __Value)
#define SET_HOIE_ENTRY_MODE_SELECT(__pHOIE, __Value) \
	SET_BITS_TO_LE_4BYTE((__pHOIE)+4, 0, 1, __Value)
#define SET_HOIE_ENTRY_ADDRESS(__pHOIE, __Value) \
	SET_BITS_TO_LE_4BYTE((__pHOIE)+4, 1, 14, __Value)
#define SET_HOIE_ENTRY_BYTE_MASK(__pHOIE, __Value) \
	SET_BITS_TO_LE_4BYTE((__pHOIE)+4, 15, 4, __Value)
#define SET_HOIE_ENTRY_IO_LOCK(__pHOIE, __Value) \
	SET_BITS_TO_LE_4BYTE((__pHOIE)+4, 19, 1, __Value)
#define SET_HOIE_ENTRY_RD_EN(__pHOIE, __Value) \
	SET_BITS_TO_LE_4BYTE((__pHOIE)+4, 20, 1, __Value)
#define SET_HOIE_ENTRY_WR_EN(__pHOIE, __Value) \
	SET_BITS_TO_LE_4BYTE((__pHOIE)+4, 21, 1, __Value)
#define SET_HOIE_ENTRY_RAW_RW(__pHOIE, __Value) \
	SET_BITS_TO_LE_4BYTE((__pHOIE)+4, 22, 1, __Value)
#define SET_HOIE_ENTRY_RAW(__pHOIE, __Value) \
	SET_BITS_TO_LE_4BYTE((__pHOIE)+4, 23, 1, __Value)
#define SET_HOIE_ENTRY_IO_DELAY(__pHOIE, __Value) \
	SET_BITS_TO_LE_4BYTE((__pHOIE)+4, 24, 8, __Value)

/*********************Function Definition*******************************************/
void rtl8723d_lps_poff_init(PADAPTER padapter);
void rtl8723d_lps_poff_deinit(PADAPTER padapter);
bool rtl8723d_lps_poff_get_txbndy_status(PADAPTER padapter);
void rtl8723d_lps_poff_h2c_ctrl(PADAPTER padapter, u8 enable);
void rtl8723d_lps_poff_set_ps_mode(PADAPTER padapter, bool bEnterLPS);
bool rtl8723d_lps_poff_get_status(PADAPTER padapter);
void rtl8723d_lps_poff_wow(PADAPTER padapter);
