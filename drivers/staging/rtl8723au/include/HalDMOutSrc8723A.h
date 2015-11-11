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
#ifndef	__RTL8723A_ODM_H__
#define __RTL8723A_ODM_H__
/*  */

#define	RSSI_CCK	0
#define	RSSI_OFDM	1
#define	RSSI_DEFAULT	2

#define IQK_MAC_REG_NUM		4
#define IQK_ADDA_REG_NUM		16
#define IQK_BB_REG_NUM			9
#define HP_THERMAL_NUM		8


/*  */
/*  structure and define */
/*  */




/*------------------------Export global variable----------------------------*/
/*------------------------Export global variable----------------------------*/
/*------------------------Export Marco Definition---------------------------*/
/* define DM_MultiSTA_InitGainChangeNotify(Event) {DM_DigTable.CurMultiSTAConnectState = Event;} */


/*  */
/*  function prototype */
/*  */

/*  */
/*  IQ calibrate */
/*  */
void rtl8723a_phy_iq_calibrate(struct rtw_adapter *pAdapter, bool bReCovery);

/*  */
/*  LC calibrate */
/*  */
void rtl8723a_phy_lc_calibrate(struct rtw_adapter *pAdapter);

/*  */
/*  AP calibrate */
/*  */
void rtl8723a_phy_ap_calibrate(struct rtw_adapter *pAdapter, char delta);

void rtl8723a_odm_check_tx_power_tracking(struct rtw_adapter *Adapter);

#endif
