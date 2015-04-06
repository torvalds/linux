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

/******************************************************************************
 *
 *
 * Module:	rtl8192d_rf.h	(Header File)
 *
 * Note:	Collect every HAL RF type exter API or constant.
 *
 * Function:
 *
 * Export:
 *
 * Abbrev:
 *
 * History:
 * Data			Who		Remark
 *
 * 09/25/2008	MHC		Create initial version.
 *
 *
******************************************************************************/
#ifndef _RTL8192D_RF_H_
#define _RTL8192D_RF_H_
/* Check to see if the file has been included already.  */


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
void		rtl8192d_PHY_RF6052SetBandwidth(
										struct rtw_adapter *				adapter,
					enum HT_CHANNEL_WIDTH		Bandwidth);
void	rtl8192d_PHY_RF6052SetCckTxPower(
										struct rtw_adapter *	adapter,
										u8*		pPowerlevel);
void	rtl8192d_PHY_RF6052SetOFDMTxPower(
										struct rtw_adapter *	adapter,
										u8*		pPowerLevel,
										u8		Channel);
int	PHY_RF6052_Config8192D(	struct rtw_adapter *		adapter	);

bool	rtl8192d_PHY_EnableAnotherPHY(struct rtw_adapter * adapter, bool	 bMac0);

void	rtl8192d_PHY_PowerDownAnotherPHY(struct rtw_adapter * adapter, bool bMac0);


/*--------------------------Exported Function prototype---------------------*/


#endif/* End of HalRf.h */
