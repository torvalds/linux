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
#ifndef	__RTL8192D_DM_H__
#define __RTL8192D_DM_H__
//============================================================
// Description:
//
// This file is for 92CE/92CU dynamic mechanism only
//
//
//============================================================

/*------------------------Export global variable----------------------------*/
/*------------------------Export global variable----------------------------*/
/*------------------------Export Marco Definition---------------------------*/
//#define DM_MultiSTA_InitGainChangeNotify(Event) {DM_DigTable.CurMultiSTAConnectState = Event;}
//============================================================
// structure and define
//============================================================
#define Rx_index_mapping_NUM	15
#define index_mapping_NUM		13

//============================================================
// function prototype
//============================================================
void rtl8192d_init_dm_priv(IN PADAPTER Adapter);
void rtl8192d_deinit_dm_priv(IN PADAPTER Adapter);

void	rtl8192d_InitHalDm(IN PADAPTER Adapter);
void	rtl8192d_HalDmWatchDog(IN PADAPTER Adapter);

#endif	//__HAL8190PCIDM_H__

