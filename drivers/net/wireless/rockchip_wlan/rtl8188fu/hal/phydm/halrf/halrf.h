/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __HALRF_H__
#define __HALRF_H__

/*@============================================================*/
/*@include files*/
/*@============================================================*/
#include "halrf/halrf_psd.h"
#if (RTL8822B_SUPPORT == 1)
#include "halrf/rtl8822b/halrf_rfk_init_8822b.h"
#endif
#if (RTL8822C_SUPPORT == 1)
#include "halrf/rtl8822c/halrf_rfk_init_8822c.h"
#include "halrf/rtl8822c/halrf_iqk_8822c.h"
#include "halrf/rtl8822c/halrf_tssi_8822c.h"
#include "halrf/rtl8822c/halrf_dpk_8822c.h"
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
#if (RTL8198F_SUPPORT == 1)
#include "halrf/rtl8198f/halrf_rfk_init_8198f.h"
#endif
#if (RTL8812F_SUPPORT == 1)
#include "halrf/rtl8812f/halrf_rfk_init_8812f.h"
#endif

#endif

#if (RTL8814B_SUPPORT == 1)
#include "halrf/rtl8814b/halrf_rfk_init_8814b.h"
#include "halrf/rtl8814b/halrf_iqk_8814b.h"
#include "halrf/rtl8814b/halrf_dpk_8814b.h"
#endif

/*@============================================================*/
/*@Definition */
/*@============================================================*/
/*IQK version*/
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
#define IQK_VER_8188E "0x14"
#define IQK_VER_8192E "0x01"
#define IQK_VER_8192F "0x01"
#define IQK_VER_8723B "0x1e"
#define IQK_VER_8812A "0x02"
#define IQK_VER_8821A "0x01"
#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))
#define IQK_VER_8188E "0x01"
#define IQK_VER_8192E "0x01"
#define IQK_VER_8192F "0x01"
#define IQK_VER_8723B "0x1e"
#define IQK_VER_8812A "0x01"
#define IQK_VER_8821A "0x01"
#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP))
#define IQK_VER_8188E "0x01"
#define IQK_VER_8192E "0x01"
#define IQK_VER_8192F "0x01"
#define IQK_VER_8723B "0x1e"
#define IQK_VER_8812A "0x01"
#define IQK_VER_8821A "0x01"
#elif (DM_ODM_SUPPORT_TYPE & (ODM_IOT))
#define IQK_VER_8188E "0x01"
#define IQK_VER_8192E "0x01"
#define IQK_VER_8192F "0x01"
#define IQK_VER_8723B "0x1e"
#define IQK_VER_8812A "0x01"
#define IQK_VER_8821A "0x01"
#endif
#define IQK_VER_8814A "0x0f"
#define IQK_VER_8188F "0x01"
#define IQK_VER_8197F "0x1d"
#define IQK_VER_8703B "0x05"
#define IQK_VER_8710B "0x01"
#define IQK_VER_8723D "0x02"
#define IQK_VER_8822B "0x30"
#define IQK_VER_8822C "0x0b"
#define IQK_VER_8821C "0x23"
#define IQK_VER_8198F "0x09"
#define IQK_VER_8814B "0x0b"
#define IQK_VER_8812F "0x07"

/*LCK version*/
#define LCK_VER_8188E "0x01"
#define LCK_VER_8192E "0x01"
#define LCK_VER_8192F "0x01"
#define LCK_VER_8723B "0x01"
#define LCK_VER_8812A "0x01"
#define LCK_VER_8821A "0x01"
#define LCK_VER_8814A "0x01"
#define LCK_VER_8188F "0x01"
#define LCK_VER_8197F "0x01"
#define LCK_VER_8703B "0x01"
#define LCK_VER_8710B "0x01"
#define LCK_VER_8723D "0x01"
#define LCK_VER_8822B "0x02"
#define LCK_VER_8822C "0x00"
#define LCK_VER_8821C "0x02"
#define LCK_VER_8814B "0x00"
#define LCK_VER_8195B "0x02"

/*power tracking version*/
#define PWRTRK_VER_8188E "0x01"
#define PWRTRK_VER_8192E "0x01"
#define PWRTRK_VER_8192F "0x01"
#define PWRTRK_VER_8723B "0x01"
#define PWRTRK_VER_8812A "0x01"
#define PWRTRK_VER_8821A "0x01"
#define PWRTRK_VER_8814A "0x01"
#define PWRTRK_VER_8188F "0x01"
#define PWRTRK_VER_8197F "0x01"
#define PWRTRK_VER_8703B "0x01"
#define PWRTRK_VER_8710B "0x01"
#define PWRTRK_VER_8723D "0x01"
#define PWRTRK_VER_8822B "0x01"
#define PWRTRK_VER_8822C "0x00"
#define PWRTRK_VER_8821C "0x01"
#define PWRTRK_VER_8814B "0x00"

/*DPK version*/
#define DPK_VER_8188E "NONE"
#define DPK_VER_8192E "NONE"
#define DPK_VER_8723B "NONE"
#define DPK_VER_8812A "NONE"
#define DPK_VER_8821A "NONE"
#define DPK_VER_8814A "NONE"
#define DPK_VER_8188F "NONE"
#define DPK_VER_8197F "0x08"
#define DPK_VER_8703B "NONE"
#define DPK_VER_8710B "NONE"
#define DPK_VER_8723D "NONE"
#define DPK_VER_8822B "NONE"
#define DPK_VER_8822C "0x15"
#define DPK_VER_8821C "NONE"
#define DPK_VER_8192F "0x0d"
#define DPK_VER_8198F "0x0a"
#define DPK_VER_8814B "0x04"
#define DPK_VER_8195B "0x0a"
#define DPK_VER_8812F "0x02"

/*RFK_INIT version*/
#define RFK_INIT_VER_8822B "0x8"
#define RFK_INIT_VER_8822C "0x6"
#define RFK_INIT_VER_8195B "0x1"
#define RFK_INIT_VER_8198F "0x5"
#define RFK_INIT_VER_8814B "0x5"
#define RFK_INIT_VER_8812F "0x2"


/*DACK version*/
#define DACK_VER_8822C "0x5"
#define DACK_VER_8814B "0x3"

/*Kfree tracking version*/
#define KFREE_VER_8188E \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8192E \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8192F \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8723B \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8812A \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8821A \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8814A \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8188F \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8197F \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8703B \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8710B \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8723D \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8822B \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8822C \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8821C \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"
#define KFREE_VER_8814B \
		(dm->power_trim_data.flag & KFREE_FLAG_ON) ? "0x01" : "NONE"

/*PA Bias Calibration version*/
#define PABIASK_VER_8188E \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8192E \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8192F \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8723B \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8812A \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8821A \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8814A \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8188F \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8197F \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8703B \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8710B \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8723D \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8822B \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8822C \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8821C \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"
#define PABIASK_VER_8814B \
	(dm->power_trim_data.pa_bias_flag & PA_BIAS_FLAG_ON) ? "0x01" : "NONE"

#define HALRF_IQK_VER \
	(dm->support_ic_type == ODM_RTL8188E) ? IQK_VER_8188E : \
	(dm->support_ic_type == ODM_RTL8192E) ? IQK_VER_8192E : \
	(dm->support_ic_type == ODM_RTL8192F) ? IQK_VER_8192F : \
	(dm->support_ic_type == ODM_RTL8723B) ? IQK_VER_8723B : \
	(dm->support_ic_type == ODM_RTL8812) ? IQK_VER_8812A : \
	(dm->support_ic_type == ODM_RTL8821) ? IQK_VER_8821A : \
	(dm->support_ic_type == ODM_RTL8814A) ? IQK_VER_8814A : \
	(dm->support_ic_type == ODM_RTL8188F) ? IQK_VER_8188F : \
	(dm->support_ic_type == ODM_RTL8197F) ? IQK_VER_8197F : \
	(dm->support_ic_type == ODM_RTL8703B) ? IQK_VER_8703B : \
	(dm->support_ic_type == ODM_RTL8710B) ? IQK_VER_8710B : \
	(dm->support_ic_type == ODM_RTL8723D) ? IQK_VER_8723D : \
	(dm->support_ic_type == ODM_RTL8822B) ? IQK_VER_8822B : \
	(dm->support_ic_type == ODM_RTL8822C) ? IQK_VER_8822C : \
	(dm->support_ic_type == ODM_RTL8821C) ? IQK_VER_8821C : \
	(dm->support_ic_type == ODM_RTL8814B) ? IQK_VER_8814B : "unknown"

#define HALRF_LCK_VER \
	(dm->support_ic_type == ODM_RTL8188E) ? LCK_VER_8188E : \
	(dm->support_ic_type == ODM_RTL8192E) ? LCK_VER_8192E : \
	(dm->support_ic_type == ODM_RTL8192F) ? LCK_VER_8192F : \
	(dm->support_ic_type == ODM_RTL8723B) ? LCK_VER_8723B : \
	(dm->support_ic_type == ODM_RTL8812) ? LCK_VER_8812A : \
	(dm->support_ic_type == ODM_RTL8821) ? LCK_VER_8821A : \
	(dm->support_ic_type == ODM_RTL8814A) ? LCK_VER_8814A : \
	(dm->support_ic_type == ODM_RTL8188F) ? LCK_VER_8188F : \
	(dm->support_ic_type == ODM_RTL8197F) ? LCK_VER_8197F : \
	(dm->support_ic_type == ODM_RTL8703B) ? LCK_VER_8703B : \
	(dm->support_ic_type == ODM_RTL8710B) ? LCK_VER_8710B : \
	(dm->support_ic_type == ODM_RTL8723D) ? LCK_VER_8723D : \
	(dm->support_ic_type == ODM_RTL8822B) ? LCK_VER_8822B : \
	(dm->support_ic_type == ODM_RTL8822C) ? LCK_VER_8822C : \
	(dm->support_ic_type == ODM_RTL8821C) ? LCK_VER_8821C : \
	(dm->support_ic_type == ODM_RTL8814B) ? LCK_VER_8814B : "unknown"

#define HALRF_POWRTRACKING_VER \
	(dm->support_ic_type == ODM_RTL8188E) ? PWRTRK_VER_8188E : \
	(dm->support_ic_type == ODM_RTL8192E) ? PWRTRK_VER_8192E : \
	(dm->support_ic_type == ODM_RTL8192F) ? PWRTRK_VER_8192F : \
	(dm->support_ic_type == ODM_RTL8723B) ? PWRTRK_VER_8723B : \
	(dm->support_ic_type == ODM_RTL8812) ? PWRTRK_VER_8812A : \
	(dm->support_ic_type == ODM_RTL8821) ? PWRTRK_VER_8821A : \
	(dm->support_ic_type == ODM_RTL8814A) ? PWRTRK_VER_8814A : \
	(dm->support_ic_type == ODM_RTL8188F) ? PWRTRK_VER_8188F : \
	(dm->support_ic_type == ODM_RTL8197F) ? PWRTRK_VER_8197F : \
	(dm->support_ic_type == ODM_RTL8703B) ? PWRTRK_VER_8703B : \
	(dm->support_ic_type == ODM_RTL8710B) ? PWRTRK_VER_8710B : \
	(dm->support_ic_type == ODM_RTL8723D) ? PWRTRK_VER_8723D : \
	(dm->support_ic_type == ODM_RTL8822B) ? PWRTRK_VER_8822B : \
	(dm->support_ic_type == ODM_RTL8822C) ? PWRTRK_VER_8822C : \
	(dm->support_ic_type == ODM_RTL8821C) ? PWRTRK_VER_8821C : \
	(dm->support_ic_type == ODM_RTL8814B) ? PWRTRK_VER_8814B : "unknown"

#define HALRF_DPK_VER \
	(dm->support_ic_type == ODM_RTL8188E) ? DPK_VER_8188E : \
	(dm->support_ic_type == ODM_RTL8192E) ? DPK_VER_8192E : \
	(dm->support_ic_type == ODM_RTL8192F) ? DPK_VER_8192F : \
	(dm->support_ic_type == ODM_RTL8723B) ? DPK_VER_8723B : \
	(dm->support_ic_type == ODM_RTL8812) ? DPK_VER_8812A : \
	(dm->support_ic_type == ODM_RTL8821) ? DPK_VER_8821A : \
	(dm->support_ic_type == ODM_RTL8814A) ? DPK_VER_8814A : \
	(dm->support_ic_type == ODM_RTL8188F) ? DPK_VER_8188F : \
	(dm->support_ic_type == ODM_RTL8197F) ? DPK_VER_8197F : \
	(dm->support_ic_type == ODM_RTL8198F) ? DPK_VER_8198F : \
	(dm->support_ic_type == ODM_RTL8703B) ? DPK_VER_8703B : \
	(dm->support_ic_type == ODM_RTL8710B) ? DPK_VER_8710B : \
	(dm->support_ic_type == ODM_RTL8723D) ? DPK_VER_8723D : \
	(dm->support_ic_type == ODM_RTL8822B) ? DPK_VER_8822B : \
	(dm->support_ic_type == ODM_RTL8822C) ? DPK_VER_8822C : \
	(dm->support_ic_type == ODM_RTL8821C) ? DPK_VER_8821C : \
	(dm->support_ic_type == ODM_RTL8814B) ? DPK_VER_8814B : "unknown"

#define HALRF_KFREE_VER \
	(dm->support_ic_type == ODM_RTL8188E) ? KFREE_VER_8188E : \
	(dm->support_ic_type == ODM_RTL8192E) ? KFREE_VER_8192E : \
	(dm->support_ic_type == ODM_RTL8192F) ? KFREE_VER_8192F : \
	(dm->support_ic_type == ODM_RTL8723B) ? KFREE_VER_8723B : \
	(dm->support_ic_type == ODM_RTL8812) ? KFREE_VER_8812A : \
	(dm->support_ic_type == ODM_RTL8821) ? KFREE_VER_8821A : \
	(dm->support_ic_type == ODM_RTL8814A) ? KFREE_VER_8814A : \
	(dm->support_ic_type == ODM_RTL8188F) ? KFREE_VER_8188F : \
	(dm->support_ic_type == ODM_RTL8197F) ? KFREE_VER_8197F : \
	(dm->support_ic_type == ODM_RTL8703B) ? KFREE_VER_8703B : \
	(dm->support_ic_type == ODM_RTL8710B) ? KFREE_VER_8710B : \
	(dm->support_ic_type == ODM_RTL8723D) ? KFREE_VER_8723D : \
	(dm->support_ic_type == ODM_RTL8822B) ? KFREE_VER_8822B : \
	(dm->support_ic_type == ODM_RTL8822C) ? KFREE_VER_8822C : \
	(dm->support_ic_type == ODM_RTL8821C) ? KFREE_VER_8821C : \
	(dm->support_ic_type == ODM_RTL8814B) ? KFREE_VER_8814B : "unknown"

#define HALRF_PABIASK_VER \
	(dm->support_ic_type == ODM_RTL8188E) ? PABIASK_VER_8188E : \
	(dm->support_ic_type == ODM_RTL8192E) ? PABIASK_VER_8192E : \
	(dm->support_ic_type == ODM_RTL8192F) ? PABIASK_VER_8192F : \
	(dm->support_ic_type == ODM_RTL8723B) ? PABIASK_VER_8723B : \
	(dm->support_ic_type == ODM_RTL8812) ? PABIASK_VER_8812A : \
	(dm->support_ic_type == ODM_RTL8821) ? PABIASK_VER_8821A : \
	(dm->support_ic_type == ODM_RTL8814A) ? PABIASK_VER_8814A : \
	(dm->support_ic_type == ODM_RTL8188F) ? PABIASK_VER_8188F : \
	(dm->support_ic_type == ODM_RTL8197F) ? PABIASK_VER_8197F : \
	(dm->support_ic_type == ODM_RTL8703B) ? PABIASK_VER_8703B : \
	(dm->support_ic_type == ODM_RTL8710B) ? PABIASK_VER_8710B : \
	(dm->support_ic_type == ODM_RTL8723D) ? PABIASK_VER_8723D : \
	(dm->support_ic_type == ODM_RTL8822B) ? PABIASK_VER_8822B : \
	(dm->support_ic_type == ODM_RTL8822C) ? PABIASK_VER_8822C : \
	(dm->support_ic_type == ODM_RTL8821C) ? PABIASK_VER_8821C : \
	(dm->support_ic_type == ODM_RTL8814B) ? PABIASK_VER_8814B : "unknown"

#define HALRF_RFK_INIT_VER \
	(dm->support_ic_type == ODM_RTL8822B) ? RFK_INIT_VER_8822B : \
	(dm->support_ic_type == ODM_RTL8822C) ? RFK_INIT_VER_8822C : \
	(dm->support_ic_type == ODM_RTL8198F) ? RFK_INIT_VER_8198F : \
	(dm->support_ic_type == ODM_RTL8814B) ? RFK_INIT_VER_8814B : "unknown"

#define HALRF_DACK_VER \
	(dm->support_ic_type == ODM_RTL8822C) ? DACK_VER_8822C : "unknown"

#define IQK_THRESHOLD 8
#define DPK_THRESHOLD 4
#define HALRF_ABS(a,b) ((a>b) ? (a-b) : (b-a))
#define SN 100

#define CCK_TSSI_NUM 6
#define OFDM_2G_TSSI_NUM 5
#define OFDM_5G_TSSI_NUM 14



/*@===========================================================*/
/*AGC RX High Power mode*/
/*@===========================================================*/
#define lna_low_gain_1 0x64
#define lna_low_gain_2 0x5A
#define lna_low_gain_3 0x58

/*@============================================================*/
/*@ enumeration */
/*@============================================================*/

#define POWER_INDEX_DIFF 4
#define TSSI_TXAGC_DIFF 2

#define TSSI_CODE_NUM 84

#define TSSI_SLOPE_2G 8
#define TSSI_SLOPE_5G 5

#define TSSI_EFUSE_NUM 25
#define TSSI_EFUSE_KFREE_NUM 4


enum halrf_func_idx { /*F_XXX = PHYDM XXX function*/
	RF00_PWR_TRK = 0, /*Pow_trk, TSSI_trk*/
	RF01_IQK = 1,	  /*LOK, IQK*/
	RF02_LCK = 2,
	RF03_DPK = 3,
	RF04_TXGAPK = 4,
	RF05_DACK = 5,
	RF06_DPK_TRK = 6,
};

enum halrf_ability {
	HAL_RF_TX_PWR_TRACK = BIT(RF00_PWR_TRK),
	HAL_RF_IQK = BIT(RF01_IQK),
	HAL_RF_LCK = BIT(RF02_LCK),
	HAL_RF_DPK = BIT(RF03_DPK),
	HAL_RF_TXGAPK = BIT(RF04_TXGAPK),
	HAL_RF_DACK = BIT(RF05_DACK),
	HAL_RF_DPK_TRACK = BIT(RF06_DPK_TRK)
};

enum halrf_dbg_comp {
	DBG_RF_TX_PWR_TRACK = BIT(RF00_PWR_TRK),
	DBG_RF_IQK = BIT(RF01_IQK),
	DBG_RF_LCK = BIT(RF02_LCK),
	DBG_RF_DPK = BIT(RF03_DPK),
	DBG_RF_TXGAPK = BIT(RF04_TXGAPK),
	DBG_RF_DACK = BIT(RF05_DACK),
	DBG_RF_MP = BIT(29),
	DBG_RF_TMP = BIT(30),
	DBG_RF_INIT = BIT(31)
};

enum halrf_cmninfo_init {
	HALRF_CMNINFO_ABILITY = 0,
	HALRF_CMNINFO_DPK_EN = 1,
	HALRF_CMNINFO_EEPROM_THERMAL_VALUE,
	HALRF_CMNINFO_RFK_FORBIDDEN,
	HALRF_CMNINFO_IQK_SEGMENT,
	HALRF_CMNINFO_RATE_INDEX,
	HALRF_CMNINFO_PWT_TYPE,
	HALRF_CMNINFO_MP_PSD_POINT,
	HALRF_CMNINFO_MP_PSD_START_POINT,
	HALRF_CMNINFO_MP_PSD_STOP_POINT,
	HALRF_CMNINFO_MP_PSD_AVERAGE,
	HALRF_CMNINFO_IQK_TIMES
};

enum halrf_cmninfo_hook {
	HALRF_CMNINFO_CON_TX,
	HALRF_CMNINFO_SINGLE_TONE,
	HALRF_CMNINFO_CARRIER_SUPPRESSION,
	HALRF_CMNINFO_MP_RATE_INDEX,
	HALRF_CMNINFO_MANUAL_RF_SUPPORTABILITY
};

enum halrf_lna_set {
	HALRF_LNA_DISABLE = 0,
	HALRF_LNA_ENABLE = 1,
};

enum halrf_k_segment_time {
	SEGMENT_FREE = 0,
	SEGMENT_10MS = 10, /*10ms*/
	SEGMENT_30MS = 30, /*30ms*/
	SEGMENT_50MS = 50, /*50ms*/
};


#if (RTL8822C_SUPPORT == 1 || RTL8812F_SUPPORT == 1 || RTL8814B_SUPPORT)

#define POWER_INDEX_DIFF 4
#define TSSI_TXAGC_DIFF 2

#define TSSI_CODE_NUM 84

#define TSSI_SLOPE_2G 8
#define TSSI_SLOPE_5G 5

#define TSSI_EFUSE_NUM 25
#define TSSI_EFUSE_KFREE_NUM 4

struct _halrf_tssi_data {
	s32 cck_offset_patha;
	s32 cck_offset_pathb;
	s32 power_track_offset[PHYDM_MAX_RF_PATH];
	s16 txagc_codeword[TSSI_CODE_NUM];
	u16 tssi_codeword[TSSI_CODE_NUM];
	s8 tssi_efuse[PHYDM_MAX_RF_PATH][TSSI_EFUSE_NUM];
	s8 tssi_kfree_efuse[PHYDM_MAX_RF_PATH][TSSI_EFUSE_KFREE_NUM];
	u8 thermal[PHYDM_MAX_RF_PATH];
	u32 index[PHYDM_MAX_RF_PATH][14];
	u8 do_tssi;
	u8 get_thermal;
};
#endif

/*@============================================================*/
/*@ structure */
/*@============================================================*/

struct _hal_rf_ {
	/*hook*/
	u8 *test1;

	/*update*/
	u32 rf_supportability;

	u8 eeprom_thermal;
	u8 dpk_en; /*Enable Function DPK OFF/ON = 0/1*/
	boolean dpk_done;
	u64 dpk_progressing_time;
	u64 iqk_progressing_time;
	u32 fw_ver;

	boolean *is_con_tx;
	boolean *is_single_tone;
	boolean *is_carrier_suppresion;
	boolean is_dpk_in_progress;
	boolean is_tssi_in_progress;
	boolean aac_checked;

	u8 *mp_rate_index;
	u32 *manual_rf_supportability;
	u32 p_rate_index;
	u8 pwt_type;
	u32 rf_dbg_comp;
	struct _halrf_psd_data halrf_psd_data;
#if (RTL8822C_SUPPORT == 1 || RTL8812F_SUPPORT == 1 || RTL8814B_SUPPORT == 1)
	struct _halrf_tssi_data halrf_tssi_data;
#endif
	u8 power_track_type;
};

/*@============================================================*/
/*@ function prototype */
/*@============================================================*/

#if (RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1 ||\
	RTL8195B_SUPPORT == 1 || RTL8198F_SUPPORT == 1 ||\
	RTL8814B_SUPPORT == 1 || RTL8822C_SUPPORT == 1 ||\
	RTL8812F_SUPPORT == 1)
void halrf_iqk_info_dump(void *dm_void, u32 *_used, char *output,
			 u32 *_out_len);

void halrf_iqk_hwtx_check(void *dm_void, boolean is_check);
#endif

u8 halrf_match_iqk_version(void *dm_void);

void halrf_support_ability_debug(void *dm_void, char input[][16], u32 *_used,
				 char *output, u32 *_out_len);

void halrf_cmn_info_init(void *dm_void, enum halrf_cmninfo_init cmn_info,
			 u32 value);

void halrf_cmn_info_hook(void *dm_void, enum halrf_cmninfo_hook cmn_info,
			 void *value);

void halrf_cmn_info_set(void *dm_void, u32 cmn_info, u64 value);

u64 halrf_cmn_info_get(void *dm_void, u32 cmn_info);

void halrf_watchdog(void *dm_void);

void halrf_supportability_init(void *dm_void);

void halrf_init(void *dm_void);

void halrf_iqk_trigger(void *dm_void, boolean is_recovery);

void halrf_rf_k_connect_trigger(void *dm_void, boolean is_recovery,
				enum halrf_k_segment_time seg_time);

void halrf_segment_iqk_trigger(void *dm_void, boolean clear,
			       boolean segment_iqk);

void halrf_lck_trigger(void *dm_void);

void halrf_iqk_debug(void *dm_void, u32 *const dm_value, u32 *_used,
		     char *output, u32 *_out_len);

void phydm_get_iqk_cfir(void *dm_void, u8 idx, u8 path, boolean debug);

void halrf_iqk_xym_read(void *dm_void, u8 path, u8 xym_type);

void halrf_rf_lna_setting(void *dm_void, enum halrf_lna_set type);

void halrf_do_imr_test(void *dm_void, u8 data);

u32 halrf_psd_log2base(u32 val);

void halrf_dpk_trigger(void *dm_void);

u8 halrf_dpk_result_check(void *dm_void);

void halrf_dpk_sram_read(void *dm_void);

void halrf_dpk_enable_disable(void *dm_void);

void halrf_dpk_track(void *dm_void);

void halrf_dpk_reload(void *dm_void);

/*Global function*/

void halrf_reload_bp(void *dm_void, u32 *bp_reg, u32 *bp, u32 num);

void halrf_reload_bprf(void *dm_void, u32 *bp_reg, u32 bp[][4], u32 num,
		       u8 ss);

void halrf_bp(void *dm_void, u32 *bp_reg, u32 *bp, u32 num);

void halrf_bprf(void *dm_void, u32 *bp_reg, u32 bp[][4], u32 num, u8 ss);

void halrf_mode(void *dm_void, u32 *i_value, u32 *q_value);

boolean halrf_compare(void *dm_void, u32 value);

u32 halrf_delta(void *dm_void, u32 v1, u32 v2);

void halrf_minmax_compare(void *dm_void, u32 value, u32 *min, u32 *max);

void halrf_b_sort(void *dm_void, u32 *iv, u32 *qv);

void halrf_bubble(void *dm_void, u32 *v1, u32 *v2);

void halrf_swap(void *dm_void, u32 *v1, u32 *v2);

enum hal_status
halrf_config_rfk_with_header_file(void *dm_void, u32 config_type);

#if (RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1 ||\
	RTL8195B_SUPPORT == 1 || RTL8198F_SUPPORT == 1 ||\
	RTL8814B_SUPPORT == 1  || RTL8822C_SUPPORT == 1 ||\
	RTL8812F_SUPPORT == 1)
void halrf_iqk_dbg(void *dm_void);
#endif

void halrf_tssi_get_efuse(void *dm_void);

void halrf_do_tssi(void *dm_void);

void halrf_do_thermal(void *dm_void);

u32 halrf_set_tssi_value(void *dm_void, u32 tssi_value);

void halrf_tssi_set_de_for_tx_verify(void *dm_void, u32 tssi_de, u8 path);

void halrf_set_tssi_power(void *dm_void, s8 power);

u32 halrf_query_tssi_value(void *dm_void);

void halrf_tssi_cck(void *dm_void);

void halrf_thermal_cck(void *dm_void);

void halrf_tssi_set_de(void *dm_void);

void halrf_tssi_dck(void *dm_void, u8 direct_do);

void halrf_calculate_tssi_codeword(void *dm_void);

void halrf_set_tssi_codeword(void *dm_void);

u8 halrf_get_tssi_codeword_for_txindex(void *dm_void);

u32 halrf_tssi_get_de(void *dm_void, u8 path);

void halrf_set_dpk_track(void *dm_void, u8 enable);

void halrf_set_dpkbychannel(void *dm_void, boolean dpk_by_ch);

void halrf_set_dpkenable(void *dm_void, boolean is_dpk_enable);

boolean halrf_get_dpkbychannel(void *dm_void);

boolean halrf_get_dpkenable(void *dm_void);

void _iqk_check_if_reload(void *dm_void);


#endif /*__HALRF_H__*/
