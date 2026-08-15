/* SPDX-License-Identifier: GPL-2.0 */
/* Forwarding and multicast database interface for the rtl8365mb switch family
 *
 * Copyright (C) 2022 Alvin Šipraga <alsi@bang-olufsen.dk>
 */

#ifndef _REALTEK_RTL8365MB_L2_H
#define _REALTEK_RTL8365MB_L2_H

#include <linux/if_ether.h>
#include <linux/types.h>

#include "realtek.h"

int rtl8365mb_l2_get_next_uc(struct realtek_priv *priv, u16 *addr, int port,
			     struct realtek_fdb_entry *entry);
int rtl8365mb_l2_add_uc(struct realtek_priv *priv, int port,
			const unsigned char addr[static ETH_ALEN],
			u16 efid, u16 vid);
int rtl8365mb_l2_del_uc(struct realtek_priv *priv, int port,
			const unsigned char addr[static ETH_ALEN],
			u16 efid, u16 vid);
int rtl8365mb_l2_flush(struct realtek_priv *priv, int port, u16 vid);

int rtl8365mb_l2_add_mc(struct realtek_priv *priv, int port,
			const unsigned char mac_addr[static ETH_ALEN],
			u16 vid);
int rtl8365mb_l2_del_mc(struct realtek_priv *priv, int port,
			const unsigned char mac_addr[static ETH_ALEN],
			u16 vid);

#endif /* _REALTEK_RTL8365MB_L2_H */
