/*
 * hostapd / VLAN initialization
 * Copyright 2003, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef VLAN_INIT_H
#define VLAN_INIT_H

#ifndef CONFIG_NO_VLAN
int vlan_init(struct hostapd_data *hapd);
void vlan_deinit(struct hostapd_data *hapd);
struct hostapd_vlan * vlan_add_dynamic(struct hostapd_data *hapd,
				       struct hostapd_vlan *vlan,
				       int vlan_id);
int vlan_remove_dynamic(struct hostapd_data *hapd, int vlan_id);
int vlan_setup_encryption_dyn(struct hostapd_data *hapd,
			      struct hostapd_ssid *mssid,
			      const char *dyn_vlan);
#else /* CONFIG_NO_VLAN */
static inline int vlan_init(struct hostapd_data *hapd)
{
	return 0;
}

static inline void vlan_deinit(struct hostapd_data *hapd)
{
}

static inline struct hostapd_vlan * vlan_add_dynamic(struct hostapd_data *hapd,
						     struct hostapd_vlan *vlan,
						     int vlan_id)
{
	return NULL;
}

static inline int vlan_remove_dynamic(struct hostapd_data *hapd, int vlan_id)
{
	return -1;
}

static inline int vlan_setup_encryption_dyn(struct hostapd_data *hapd,
					    struct hostapd_ssid *mssid,
					    const char *dyn_vlan)
{
	return -1;
}
#endif /* CONFIG_NO_VLAN */

#endif /* VLAN_INIT_H */
