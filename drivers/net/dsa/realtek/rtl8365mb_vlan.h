/* SPDX-License-Identifier: GPL-2.0 */
/* VLAN configuration interface for the rtl8365mb switch family
 *
 * Copyright (C) 2022 Alvin Šipraga <alsi@bang-olufsen.dk>
 *
 */

#ifndef _REALTEK_RTL8365MB_VLAN_H
#define _REALTEK_RTL8365MB_VLAN_H

#include <linux/types.h>

#include "realtek.h"

enum rtl8365mb_frame_ingress {
	RTL8365MB_FRAME_TYPE_ANY_FRAME = 0,
	RTL8365MB_FRAME_TYPE_TAGGED_ONLY,
	RTL8365MB_FRAME_TYPE_UNTAGGED_ONLY,
};

int rtl8365mb_vlan_port_get_pvid(struct realtek_priv *priv, int port,
				 u16 *pvid);
int
rtl8365mb_vlan_port_get_framefilter(struct realtek_priv *priv,
				    int port,
				    enum rtl8365mb_frame_ingress *frame_type);
int
rtl8365mb_vlan_port_set_framefilter(struct realtek_priv *priv,
				    int port,
				    enum rtl8365mb_frame_ingress frame_type);
int rtl8365mb_vlan_4k_port_add(struct dsa_switch *ds, int port,
			       const struct switchdev_obj_port_vlan *vlan,
			       struct netlink_ext_ack *extack);
int rtl8365mb_vlan_4k_port_del(struct dsa_switch *ds, int port,
			       const struct switchdev_obj_port_vlan *vlan);
int rtl8365mb_vlan_pvid_port_set(struct dsa_switch *ds, int port, u16 vid,
				 struct netlink_ext_ack *extack);
int rtl8365mb_vlan_pvid_port_clear(struct dsa_switch *ds, int port, u16 vid);
#endif /* _REALTEK_RTL8365MB_VLAN_H */
