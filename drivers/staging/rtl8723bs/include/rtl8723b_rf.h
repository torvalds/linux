/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTL8723B_RF_H__
#define __RTL8723B_RF_H__


int	PHY_RF6052_Config8723B(struct adapter *Adapter);

void
PHY_RF6052SetBandwidth8723B(struct adapter *Adapter,
	enum CHANNEL_WIDTH		Bandwidth);

#endif
