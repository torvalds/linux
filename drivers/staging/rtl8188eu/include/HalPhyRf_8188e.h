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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#ifndef __HAL_PHY_RF_8188E_H__
#define __HAL_PHY_RF_8188E_H__

/*--------------------------Define Parameters-------------------------------*/
#define	IQK_DELAY_TIME_88E		10		/* ms */
#define	index_mapping_NUM_88E	15
#define AVG_THERMAL_NUM_88E	4
#define ODM_TARGET_CHNL_NUM_2G_5G	59

void rtl88eu_dm_txpower_track_adjust(struct odm_dm_struct *dm_odm,
				     u8 type, /* 0 = OFDM, 1 = CCK */
				     u8 *dir, /* 1 = +(incr), 2 = -(decr) */
				     u32 *out_write); /* Tx tracking CCK/OFDM BB
						       * swing index adjust
						       */

void rtl88eu_dm_txpower_tracking_callback_thermalmeter(struct adapter *adapt);

/* 1 7.	IQK */

void PHY_IQCalibrate_8188E(struct adapter *Adapter, bool ReCovery);

/*  LC calibrate */
void PHY_LCCalibrate_8188E(struct adapter *pAdapter);

/*  AP calibrate */
void PHY_DigitalPredistortion_8188E(struct adapter *pAdapter);

#endif	/*  #ifndef __HAL_PHY_RF_8188E_H__ */
