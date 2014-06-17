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
 ******************************************************************************/
#ifndef __RTL8723A_RF_H__
#define __RTL8723A_RF_H__

/*--------------------------Define Parameters-------------------------------*/

/*  */
/*  For RF 6052 Series */
/*  */
#define		RF6052_MAX_TX_PWR			0x3F
#define		RF6052_MAX_REG				0x3F
#define		RF6052_MAX_PATH				2
/*--------------------------Define Parameters-------------------------------*/


/*------------------------------Define structure----------------------------*/

/*------------------------------Define structure----------------------------*/


/*------------------------Export global variable----------------------------*/
/*------------------------Export global variable----------------------------*/

/*------------------------Export Marco Definition---------------------------*/

/*------------------------Export Marco Definition---------------------------*/


/*--------------------------Exported Function prototype---------------------*/

/*  */
/*  RF RL6052 Series API */
/*  */
void rtl8723a_phy_rf6052set_bw(struct rtw_adapter *Adapter,
			       enum ht_channel_width Bandwidth);
void rtl823a_phy_rf6052setccktxpower(struct rtw_adapter *Adapter,
				      u8* pPowerlevel);
void rtl8723a_PHY_RF6052SetOFDMTxPower(struct rtw_adapter *Adapter,
				       u8* pPowerLevel, u8 Channel);

/*--------------------------Exported Function prototype---------------------*/

int	PHY_RF6052_Config8723A(struct rtw_adapter *Adapter);

#endif
