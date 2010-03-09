/*
 * Copyright (C) 2007-2009 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

void set_main_if_addr(uint8_t *addr);
int main_if_was_up(void);
void interface_setup(struct net_device *dev);
int interface_open(struct net_device *dev);
int interface_release(struct net_device *dev);
struct net_device_stats *interface_stats(struct net_device *dev);
int interface_set_mac_addr(struct net_device *dev, void *addr);
int interface_change_mtu(struct net_device *dev, int new_mtu);
int interface_tx(struct sk_buff *skb, struct net_device *dev);
void interface_rx(struct sk_buff *skb, int hdr_size);
int my_skb_push(struct sk_buff *skb, unsigned int len);

extern unsigned char mainIfAddr[];
