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

#define		RF6052_MAX_TX_PWR		0x3F
#define		RF6052_MAX_REG			0x3F
#define		RF6052_MAX_PATH			2

int	PHY_RF6052_Config8188E(struct adapter *Adapter);
void rtl8188e_RF_ChangeTxPath(struct adapter *Adapter, u16 DataRate);
void rtl8188e_PHY_RF6052SetBandwidth(struct adapter *Adapter,
				     enum ht_channel_width Bandwidth);
void	rtl8188e_PHY_RF6052SetCckTxPower(struct adapter *Adapter, u8 *level);
void	rtl8188e_PHY_RF6052SetOFDMTxPower(struct adapter *Adapter, u8 *ofdm,
					  u8 *pwrbw20, u8 *pwrbw40, u8 channel);

#endif/* __RTL8188E_RF_H__ */
