/*
 * hostapd / VLAN netlink/ioctl api
 * Copyright (c) 2012, Michael Braun <michael-dev@fami-braun.de>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef VLAN_UTIL_H
#define VLAN_UTIL_H

struct hostapd_data;
struct hostapd_vlan;
struct full_dynamic_vlan;

int vlan_add(const char *if_name, int vid, const char *vlan_if_name);
int vlan_rem(const char *if_name);
int vlan_set_name_type(unsigned int name_type);

int ifconfig_helper(const char *if_name, int up);
int ifconfig_up(const char *if_name);
int iface_exists(const char *ifname);
int vlan_if_remove(struct hostapd_data *hapd, struct hostapd_vlan *vlan);

struct full_dynamic_vlan *
full_dynamic_vlan_init(struct hostapd_data *hapd);
void full_dynamic_vlan_deinit(struct full_dynamic_vlan *priv);
void vlan_newlink(const char *ifname, struct hostapd_data *hapd);
void vlan_dellink(const char *ifname, struct hostapd_data *hapd);

#endif /* VLAN_UTIL_H */
