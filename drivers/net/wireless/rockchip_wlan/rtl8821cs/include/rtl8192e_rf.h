/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2012 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __RTL8192E_RF_H__
#define __RTL8192E_RF_H__

void
PHY_RF6052SetBandwidth8192E(
		PADAPTER				Adapter,
		enum channel_width		Bandwidth);


int
PHY_RF6052_Config_8192E(
		PADAPTER	Adapter);

#endif/* __RTL8192E_RF_H__ */
