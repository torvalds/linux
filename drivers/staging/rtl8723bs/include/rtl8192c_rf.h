/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef _RTL8192C_RF_H_
#define _RTL8192C_RF_H_


/*  */
/*  RF RL6052 Series API */
/*  */
void rtl8192c_RF_ChangeTxPath(struct adapter *Adapter,
			      u16 DataRate);
void rtl8192c_PHY_RF6052SetBandwidth(struct adapter *Adapter,
				     enum CHANNEL_WIDTH Bandwidth);
void rtl8192c_PHY_RF6052SetCckTxPower(struct adapter *Adapter,
				      u8 *pPowerlevel);
void rtl8192c_PHY_RF6052SetOFDMTxPower(struct adapter *Adapter,
				       u8 *pPowerLevel,
				       u8 Channel);
int PHY_RF6052_Config8192C(struct adapter *Adapter);

/*--------------------------Exported Function prototype---------------------*/


#endif/* End of HalRf.h */
