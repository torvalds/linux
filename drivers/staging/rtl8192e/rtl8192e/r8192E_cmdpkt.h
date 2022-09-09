/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef R819XUSB_CMDPKT_H
#define R819XUSB_CMDPKT_H

bool rtl92e_send_cmd_pkt(struct net_device *dev, u32 type, const void *data,
			 u32 len);
#endif
