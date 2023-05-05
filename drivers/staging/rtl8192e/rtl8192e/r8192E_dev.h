/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef _RTL8192E_H
#define _RTL8192E_H

#include "r8190P_def.h"

bool rtl92e_is_halfn_supported_by_ap(struct net_device *dev);
bool rtl92e_get_nmode_support_by_sec(struct net_device *dev);
bool rtl92e_is_tx_stuck(struct net_device *dev);
bool rtl92e_is_rx_stuck(struct net_device *dev);
void rtl92e_ack_irq(struct net_device *dev, u32 *p_inta, u32 *p_intb);
void rtl92e_enable_rx(struct net_device *dev);
void rtl92e_enable_tx(struct net_device *dev);
void rtl92e_enable_irq(struct net_device *dev);
void rtl92e_disable_irq(struct net_device *dev);
void rtl92e_init_variables(struct net_device  *dev);
void rtl92e_start_beacon(struct net_device *dev);
void rtl92e_set_reg(struct net_device *dev, u8 variable, u8 *val);
void rtl92e_get_eeprom_size(struct net_device *dev);
bool rtl92e_start_adapter(struct net_device *dev);
void rtl92e_link_change(struct net_device *dev);
void rtl92e_set_monitor_mode(struct net_device *dev, bool bAllowAllDA,
			     bool WriteIntoReg);
void  rtl92e_fill_tx_desc(struct net_device *dev, struct tx_desc *pdesc,
			  struct cb_desc *cb_desc, struct sk_buff *skb);
void  rtl92e_fill_tx_cmd_desc(struct net_device *dev, struct tx_desc_cmd *entry,
			      struct cb_desc *cb_desc, struct sk_buff *skb);
bool rtl92e_get_rx_stats(struct net_device *dev, struct rtllib_rx_stats *stats,
			 struct rx_desc *pdesc, struct sk_buff *skb);
void rtl92e_stop_adapter(struct net_device *dev, bool reset);
void rtl92e_update_ratr_table(struct net_device *dev);
#endif
