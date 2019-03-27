/*
 * hostapd / VLAN initialization
 * Copyright 2003, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "hostapd.h"
#include "ap_config.h"
#include "ap_drv_ops.h"
#include "wpa_auth.h"
#include "vlan_init.h"
#include "vlan_util.h"


static int vlan_if_add(struct hostapd_data *hapd, struct hostapd_vlan *vlan,
		       int existsok)
{
	int ret, i;

	for (i = 0; i < NUM_WEP_KEYS; i++) {
		if (!hapd->conf->ssid.wep.key[i])
			continue;
		wpa_printf(MSG_ERROR,
			   "VLAN: Refusing to set up VLAN iface %s with WEP",
			   vlan->ifname);
		return -1;
	}

	if (!iface_exists(vlan->ifname))
		ret = hostapd_vlan_if_add(hapd, vlan->ifname);
	else if (!existsok)
		return -1;
	else
		ret = 0;

	if (ret)
		return ret;

	ifconfig_up(vlan->ifname); /* else wpa group will fail fatal */

	if (hapd->wpa_auth)
		ret = wpa_auth_ensure_group(hapd->wpa_auth, vlan->vlan_id);

	if (ret == 0)
		return ret;

	wpa_printf(MSG_ERROR, "WPA initialization for VLAN %d failed (%d)",
		   vlan->vlan_id, ret);
	if (wpa_auth_release_group(hapd->wpa_auth, vlan->vlan_id))
		wpa_printf(MSG_ERROR, "WPA deinit of %s failed", vlan->ifname);

	/* group state machine setup failed */
	if (hostapd_vlan_if_remove(hapd, vlan->ifname))
		wpa_printf(MSG_ERROR, "Removal of %s failed", vlan->ifname);

	return ret;
}


int vlan_if_remove(struct hostapd_data *hapd, struct hostapd_vlan *vlan)
{
	int ret;

	ret = wpa_auth_release_group(hapd->wpa_auth, vlan->vlan_id);
	if (ret)
		wpa_printf(MSG_ERROR,
			   "WPA deinitialization for VLAN %d failed (%d)",
			   vlan->vlan_id, ret);

	return hostapd_vlan_if_remove(hapd, vlan->ifname);
}


static int vlan_dynamic_add(struct hostapd_data *hapd,
			    struct hostapd_vlan *vlan)
{
	while (vlan) {
		if (vlan->vlan_id != VLAN_ID_WILDCARD) {
			if (vlan_if_add(hapd, vlan, 1)) {
				wpa_printf(MSG_ERROR,
					   "VLAN: Could not add VLAN %s: %s",
					   vlan->ifname, strerror(errno));
				return -1;
			}
#ifdef CONFIG_FULL_DYNAMIC_VLAN
			vlan_newlink(vlan->ifname, hapd);
#endif /* CONFIG_FULL_DYNAMIC_VLAN */
		}

		vlan = vlan->next;
	}

	return 0;
}


static void vlan_dynamic_remove(struct hostapd_data *hapd,
				struct hostapd_vlan *vlan)
{
	struct hostapd_vlan *next;

	while (vlan) {
		next = vlan->next;

#ifdef CONFIG_FULL_DYNAMIC_VLAN
		/* vlan_dellink() takes care of cleanup and interface removal */
		if (vlan->vlan_id != VLAN_ID_WILDCARD)
			vlan_dellink(vlan->ifname, hapd);
#else /* CONFIG_FULL_DYNAMIC_VLAN */
		if (vlan->vlan_id != VLAN_ID_WILDCARD &&
		    vlan_if_remove(hapd, vlan)) {
			wpa_printf(MSG_ERROR, "VLAN: Could not remove VLAN "
				   "iface: %s: %s",
				   vlan->ifname, strerror(errno));
		}
#endif /* CONFIG_FULL_DYNAMIC_VLAN */

		vlan = next;
	}
}


int vlan_init(struct hostapd_data *hapd)
{
#ifdef CONFIG_FULL_DYNAMIC_VLAN
	hapd->full_dynamic_vlan = full_dynamic_vlan_init(hapd);
#endif /* CONFIG_FULL_DYNAMIC_VLAN */

	if ((hapd->conf->ssid.dynamic_vlan != DYNAMIC_VLAN_DISABLED ||
	     hapd->conf->ssid.per_sta_vif) &&
	    !hapd->conf->vlan) {
		/* dynamic vlans enabled but no (or empty) vlan_file given */
		struct hostapd_vlan *vlan;
		int ret;

		vlan = os_zalloc(sizeof(*vlan));
		if (vlan == NULL) {
			wpa_printf(MSG_ERROR, "Out of memory while assigning "
				   "VLAN interfaces");
			return -1;
		}

		vlan->vlan_id = VLAN_ID_WILDCARD;
		ret = os_snprintf(vlan->ifname, sizeof(vlan->ifname), "%s.#",
				  hapd->conf->iface);
		if (ret >= (int) sizeof(vlan->ifname)) {
			wpa_printf(MSG_WARNING,
				   "VLAN: Interface name was truncated to %s",
				   vlan->ifname);
		} else if (ret < 0) {
			os_free(vlan);
			return ret;
		}
		vlan->next = hapd->conf->vlan;
		hapd->conf->vlan = vlan;
	}

	if (vlan_dynamic_add(hapd, hapd->conf->vlan))
		return -1;

        return 0;
}


void vlan_deinit(struct hostapd_data *hapd)
{
	vlan_dynamic_remove(hapd, hapd->conf->vlan);

#ifdef CONFIG_FULL_DYNAMIC_VLAN
	full_dynamic_vlan_deinit(hapd->full_dynamic_vlan);
	hapd->full_dynamic_vlan = NULL;
#endif /* CONFIG_FULL_DYNAMIC_VLAN */
}


struct hostapd_vlan * vlan_add_dynamic(struct hostapd_data *hapd,
				       struct hostapd_vlan *vlan,
				       int vlan_id,
				       struct vlan_description *vlan_desc)
{
	struct hostapd_vlan *n;
	char ifname[IFNAMSIZ + 1], *pos;

	if (vlan == NULL || vlan->vlan_id != VLAN_ID_WILDCARD)
		return NULL;

	wpa_printf(MSG_DEBUG, "VLAN: %s(vlan_id=%d ifname=%s)",
		   __func__, vlan_id, vlan->ifname);
	os_strlcpy(ifname, vlan->ifname, sizeof(ifname));
	pos = os_strchr(ifname, '#');
	if (pos == NULL)
		return NULL;
	*pos++ = '\0';

	n = os_zalloc(sizeof(*n));
	if (n == NULL)
		return NULL;

	n->vlan_id = vlan_id;
	if (vlan_desc)
		n->vlan_desc = *vlan_desc;
	n->dynamic_vlan = 1;

	os_snprintf(n->ifname, sizeof(n->ifname), "%s%d%s", ifname, vlan_id,
		    pos);

	n->next = hapd->conf->vlan;
	hapd->conf->vlan = n;

	/* hapd->conf->vlan needs this new VLAN here for WPA setup */
	if (vlan_if_add(hapd, n, 0)) {
		hapd->conf->vlan = n->next;
		os_free(n);
		n = NULL;
	}

	return n;
}


int vlan_remove_dynamic(struct hostapd_data *hapd, int vlan_id)
{
	struct hostapd_vlan *vlan;

	if (vlan_id <= 0)
		return 1;

	wpa_printf(MSG_DEBUG, "VLAN: %s(ifname=%s vlan_id=%d)",
		   __func__, hapd->conf->iface, vlan_id);

	vlan = hapd->conf->vlan;
	while (vlan) {
		if (vlan->vlan_id == vlan_id && vlan->dynamic_vlan > 0) {
			vlan->dynamic_vlan--;
			break;
		}
		vlan = vlan->next;
	}

	if (vlan == NULL)
		return 1;

	if (vlan->dynamic_vlan == 0) {
		vlan_if_remove(hapd, vlan);
#ifdef CONFIG_FULL_DYNAMIC_VLAN
		vlan_dellink(vlan->ifname, hapd);
#endif /* CONFIG_FULL_DYNAMIC_VLAN */
	}

	return 0;
}
