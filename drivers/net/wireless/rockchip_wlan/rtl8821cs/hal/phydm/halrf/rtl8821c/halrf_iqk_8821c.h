/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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

#ifndef __HALRF_IQK_8821C_H__
#define __HALRF_IQK_8821C_H__

#if (RTL8821C_SUPPORT == 1)
/*============================================================*/
/*Definition */
/*============================================================*/

/*--------------------------Define Parameters-------------------------------*/
#define MAC_REG_NUM_8821C 3
#define BB_REG_NUM_8821C 10
#define RF_REG_NUM_8821C 5
#define DPK_BB_REG_NUM_8821C 23
#define DPK_BACKUP_REG_NUM_8821C 3

#define LOK_delay_8821C 2
#define GS_delay_8821C 2
#define WBIQK_delay_8821C 2

#define TXIQK 0
#define RXIQK 1
#define SS_8821C 1

/*---------------------------End Define Parameters-------------------------------*/

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
void do_iqk_8821c(void *dm_void, u8 delta_thermal_index, u8 thermal_value,
		  u8 threshold);
#else
void do_iqk_8821c(void *dm_void, u8 delta_thermal_index, u8 thermal_value,
		  u8 threshold);
#endif

void phy_iq_calibrate_8821c(void *dm_void, boolean clear, boolean segment_iqk);

void phy_dp_calibrate_8821c(void *dm_void, boolean clear);

void do_imr_test_8821c(void *dm_void);

#else /* (RTL8821C_SUPPORT == 0)*/

#define phy_iq_calibrate_8821c(_pdm_void, clear, segment_iqk)
#define phy_dp_calibrate_8821c(_pDM_VOID, clear)

#endif /* RTL8821C_SUPPORT */

#endif /*#ifndef __HALRF_IQK_8821C_H__*/
