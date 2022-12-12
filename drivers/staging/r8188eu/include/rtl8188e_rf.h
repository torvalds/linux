/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTL8188E_RF_H__
#define __RTL8188E_RF_H__

#define		RF6052_MAX_TX_PWR		0x3F
#define		RF6052_MAX_REG			0x3F
#define		RF6052_MAX_PATH			2

int phy_RF6052_Config_ParaFile(struct adapter *Adapter);
void rtl8188e_PHY_RF6052SetBandwidth(struct adapter *Adapter,
				     enum ht_channel_width Bandwidth);
void	rtl8188e_PHY_RF6052SetCckTxPower(struct adapter *Adapter, u8 *level);
void	rtl8188e_PHY_RF6052SetOFDMTxPower(struct adapter *Adapter, u8 *ofdm,
					  u8 *pwrbw20, u8 *pwrbw40, u8 channel);

#endif/* __RTL8188E_RF_H__ */
