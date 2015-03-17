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
#ifndef __RTL8188E_RF_H__
#define __RTL8188E_RF_H__

#define		RF6052_MAX_TX_PWR			0x3F
#define		RF6052_MAX_REG				0x3F
#define		RF6052_MAX_PATH				2


int	PHY_RF6052_Config8188E(	IN	PADAPTER		Adapter	);
void		rtl8188e_RF_ChangeTxPath(	IN	PADAPTER	Adapter, 
										IN	u16		DataRate);
void		rtl8188e_PHY_RF6052SetBandwidth(	
										IN	PADAPTER				Adapter,
										IN	HT_CHANNEL_WIDTH		Bandwidth);	
VOID	rtl8188e_PHY_RF6052SetCckTxPower(
										IN	PADAPTER	Adapter,
										IN	u8*		pPowerlevel);
VOID	rtl8188e_PHY_RF6052SetOFDMTxPower(
											IN	PADAPTER	Adapter,
											IN	u8*		pPowerLevelOFDM,
											IN	u8*		pPowerLevelBW20,
											IN	u8*		pPowerLevelBW40,	
											IN	u8		Channel);

#endif//__RTL8188E_RF_H__

