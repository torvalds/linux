/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#ifndef _RTL8192E_H
#define _RTL8192E_H

#include "r8190P_def.h"

bool rtl8192_GetHalfNmodeSupportByAPs(struct net_device *dev);
bool rtl8192_GetNmodeSupportBySecCfg(struct net_device *dev);
bool rtl8192_HalTxCheckStuck(struct net_device *dev);
bool rtl8192_HalRxCheckStuck(struct net_device *dev);
void rtl8192_interrupt_recognized(struct net_device *dev, u32 *p_inta,
				  u32 *p_intb);
void rtl92e_enable_rx(struct net_device *dev);
void rtl92e_enable_tx(struct net_device *dev);
void rtl92e_enable_irq(struct net_device *dev);
void rtl92e_disable_irq(struct net_device *dev);
void rtl92e_clear_irq(struct net_device *dev);
void rtl8192_InitializeVariables(struct net_device  *dev);
void rtl8192e_start_beacon(struct net_device *dev);
void rtl8192e_SetHwReg(struct net_device *dev, u8 variable, u8 *val);
void rtl8192_get_eeprom_size(struct net_device *dev);
bool rtl92e_start_adapter(struct net_device *dev);
void rtl8192_link_change(struct net_device *dev);
void rtl92e_set_monitor_mode(struct net_device *dev, bool bAllowAllDA,
			     bool WriteIntoReg);
void  rtl8192_tx_fill_desc(struct net_device *dev, struct tx_desc *pdesc,
			   struct cb_desc *cb_desc,
			   struct sk_buff *skb);
void  rtl8192_tx_fill_cmd_desc(struct net_device *dev,
			       struct tx_desc_cmd *entry,
			       struct cb_desc *cb_desc, struct sk_buff *skb);
bool rtl8192_rx_query_status_desc(struct net_device *dev,
				  struct rtllib_rx_stats *stats,
				  struct rx_desc *pdesc,
				  struct sk_buff *skb);
void rtl8192_halt_adapter(struct net_device *dev, bool reset);
void rtl8192_update_ratr_table(struct net_device *dev);
#endif
