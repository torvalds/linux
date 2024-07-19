/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __COMMON_H2C_H__
#define __COMMON_H2C_H__

#define H2C_RSVDPAGE_LOC_LEN		5
#define H2C_MEDIA_STATUS_RPT_LEN		3
#define H2C_PWRMODE_LEN			7
#define H2C_PSTUNEPARAM_LEN			4
#define H2C_MACID_CFG_LEN		7
#define H2C_RSSI_SETTING_LEN		4

/*  */
/*     Structure    -------------------------------------------------- */
/*  */
struct rsvdpage_loc {
	u8 LocProbeRsp;
	u8 LocPsPoll;
	u8 LocNullData;
	u8 LocQosNull;
	u8 LocBTQosNull;
};

#endif
