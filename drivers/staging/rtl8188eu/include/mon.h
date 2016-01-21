/*
 * RTL8188EU monitor interface
 *
 * Copyright (C) 2015 Jakub Sitnicki
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 */

/*
 * Monitor interface receives all transmitted and received IEEE 802.11
 * frames, both Data and Management, and passes them up to userspace
 * preserving the WLAN headers.
 */

#ifndef _MON_H_
#define _MON_H_

struct net_device;
struct recv_frame;
struct xmit_frame;

struct net_device *rtl88eu_mon_init(void);
void rtl88eu_mon_deinit(struct net_device *dev);

void rtl88eu_mon_recv_hook(struct net_device *dev, struct recv_frame *frame);
void rtl88eu_mon_xmit_hook(struct net_device *dev, struct xmit_frame *frame,
			   uint frag_len);

#endif /* _MON_H_ */
