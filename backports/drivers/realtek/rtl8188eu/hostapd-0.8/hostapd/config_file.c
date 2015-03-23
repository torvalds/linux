/*
 * hostapd / Configuration file parser
 * Copyright (c) 2003-2009, Jouni Malinen <j@w1.fi>
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

#include "utils/includes.h"
#ifndef CONFIG_NATIVE_WINDOWS
#include <grp.h>
#endif /* CONFIG_NATIVE_WINDOWS */

#include "utils/common.h"
#include "utils/uuid.h"
#include "common/ieee802_11_defs.h"
#include "drivers/driver.h"
#include "eap_server/eap.h"
#include "radius/radius_client.h"
#include "ap/wpa_auth.h"
#include "ap/ap_config.h"
#include "config_file.h"


extern struct wpa_driver_ops *wpa_drivers[];


#ifndef CONFIG_NO_VLAN
static int hostapd_config_read_vlan_file(struct hostapd_bss_config *bss,
					 const char *fname)
{
	FILE *f;
	char buf[128], *pos, *pos2;
	int line = 0, vlan_id;
	struct hostapd_vlan *vlan;

	f = fopen(fname, "r");
	if (!f) {
		wpa_printf(MSG_ERROR, "VLAN file '%s' not readable.", fname);
		return -1;
	}

	while (fgets(buf, sizeof(buf), f)) {
		line++;

		if (buf[0] == '#')
			continue;
		pos = buf;
		while (*pos != '\0') {
			if (*pos == '\n') {
				*pos = '\0';
				break;
			}
			pos++;
		}
		if (buf[0] == '\0')
			continue;

		if (buf[0] == '*') {
			vlan_id = VLAN_ID_WILDCARD;
			pos = buf + 1;
		} else {
			vlan_id = strtol(buf, &pos, 10);
			if (buf == pos || vlan_id < 1 ||
			    vlan_id > MAX_VLAN_ID) {
				wpa_printf(MSG_ERROR, "Invalid VLAN ID at "
					   "line %d in '%s'", line, fname);
				fclose(f);
				return -1;
			}
		}

		while (*pos == ' ' || *pos == '\t')
			pos++;
		pos2 = pos;
		while (*pos2 != ' ' && *pos2 != '\t' && *pos2 != '\0')
			pos2++;
		*pos2 = '\0';
		if (*pos == '\0' || os_strlen(pos) > IFNAMSIZ) {
			wpa_printf(MSG_ERROR, "Invalid VLAN ifname at line %d "
				   "in '%s'", line, fname);
			fclose(f);
			return -1;
		}

		vlan = os_malloc(sizeof(*vlan));
		if (vlan == NULL) {
			wpa_printf(MSG_ERROR, "Out of memory while reading "
				   "VLAN interfaces from '%s'", fname);
			fclose(f);
			return -1;
		}

		os_memset(vlan, 0, sizeof(*vlan));
		vlan->vlan_id = vlan_id;
		os_strlcpy(vlan->ifname, pos, sizeof(vlan->ifname));
		if (bss->vlan_tail)
			bss->vlan_tail->next = vlan;
		else
			bss->vlan = vlan;
		bss->vlan_tail = vlan;
	}

	fclose(f);

	return 0;
}
#endif /* CONFIG_NO_VLAN */


static int hostapd_acl_comp(const void *a, const void *b)
{
	const struct mac_acl_entry *aa = a;
	const struct mac_acl_entry *bb = b;
	return os_memcmp(aa->addr, bb->addr, sizeof(macaddr));
}


static int hostapd_config_read_maclist(const char *fname,
				       struct mac_acl_entry **acl, int *num)
{
	FILE *f;
	char buf[128], *pos;
	int line = 0;
	u8 addr[ETH_ALEN];
	struct mac_acl_entry *newacl;
	int vlan_id;

	if (!fname)
		return 0;

	f = fopen(fname, "r");
	if (!f) {
		wpa_printf(MSG_ERROR, "MAC list file '%s' not found.", fname);
		return -1;
	}

	while (fgets(buf, sizeof(buf), f)) {
		line++;

		if (buf[0] == '#')
			continue;
		pos = buf;
		while (*pos != '\0') {
			if (*pos == '\n') {
				*pos = '\0';
				break;
			}
			pos++;
		}
		if (buf[0] == '\0')
			continue;

		if (hwaddr_aton(buf, addr)) {
			wpa_printf(MSG_ERROR, "Invalid MAC address '%s' at "
				   "line %d in '%s'", buf, line, fname);
			fclose(f);
			return -1;
		}

		vlan_id = 0;
		pos = buf;
		while (*pos != '\0' && *pos != ' ' && *pos != '\t')
			pos++;
		while (*pos == ' ' || *pos == '\t')
			pos++;
		if (*pos != '\0')
			vlan_id = atoi(pos);

		newacl = os_realloc(*acl, (*num + 1) * sizeof(**acl));
		if (newacl == NULL) {
			wpa_printf(MSG_ERROR, "MAC list reallocation failed");
			fclose(f);
			return -1;
		}

		*acl = newacl;
		os_memcpy((*acl)[*num].addr, addr, ETH_ALEN);
		(*acl)[*num].vlan_id = vlan_id;
		(*num)++;
	}

	fclose(f);

	qsort(*acl, *num, sizeof(**acl), hostapd_acl_comp);

	return 0;
}


#ifdef EAP_SERVER
static int hostapd_config_read_eap_user(const char *fname,
					struct hostapd_bss_config *conf)
{
	FILE *f;
	char buf[512], *pos, *start, *pos2;
	int line = 0, ret = 0, num_methods;
	struct hostapd_eap_user *user, *tail = NULL;

	if (!fname)
		return 0;

	f = fopen(fname, "r");
	if (!f) {
		wpa_printf(MSG_ERROR, "EAP user file '%s' not found.", fname);
		return -1;
	}

	/* Lines: "user" METHOD,METHOD2 "password" (password optional) */
	while (fgets(buf, sizeof(buf), f)) {
		line++;

		if (buf[0] == '#')
			continue;
		pos = buf;
		while (*pos != '\0') {
			if (*pos == '\n') {
				*pos = '\0';
				break;
			}
			pos++;
		}
		if (buf[0] == '\0')
			continue;

		user = NULL;

		if (buf[0] != '"' && buf[0] != '*') {
			wpa_printf(MSG_ERROR, "Invalid EAP identity (no \" in "
				   "start) on line %d in '%s'", line, fname);
			goto failed;
		}

		user = os_zalloc(sizeof(*user));
		if (user == NULL) {
			wpa_printf(MSG_ERROR, "EAP user allocation failed");
			goto failed;
		}
		user->force_version = -1;

		if (buf[0] == '*') {
			pos = buf;
		} else {
			pos = buf + 1;
			start = pos;
			while (*pos != '"' && *pos != '\0')
				pos++;
			if (*pos == '\0') {
				wpa_printf(MSG_ERROR, "Invalid EAP identity "
					   "(no \" in end) on line %d in '%s'",
					   line, fname);
				goto failed;
			}

			user->identity = os_malloc(pos - start);
			if (user->identity == NULL) {
				wpa_printf(MSG_ERROR, "Failed to allocate "
					   "memory for EAP identity");
				goto failed;
			}
			os_memcpy(user->identity, start, pos - start);
			user->identity_len = pos - start;

			if (pos[0] == '"' && pos[1] == '*') {
				user->wildcard_prefix = 1;
				pos++;
			}
		}
		pos++;
		while (*pos == ' ' || *pos == '\t')
			pos++;

		if (*pos == '\0') {
			wpa_printf(MSG_ERROR, "No EAP method on line %d in "
				   "'%s'", line, fname);
			goto failed;
		}

		start = pos;
		while (*pos != ' ' && *pos != '\t' && *pos != '\0')
			pos++;
		if (*pos == '\0') {
			pos = NULL;
		} else {
			*pos = '\0';
			pos++;
		}
		num_methods = 0;
		while (*start) {
			char *pos3 = os_strchr(start, ',');
			if (pos3) {
				*pos3++ = '\0';
			}
			user->methods[num_methods].method =
				eap_server_get_type(
					start,
					&user->methods[num_methods].vendor);
			if (user->methods[num_methods].vendor ==
			    EAP_VENDOR_IETF &&
			    user->methods[num_methods].method == EAP_TYPE_NONE)
			{
				if (os_strcmp(start, "TTLS-PAP") == 0) {
					user->ttls_auth |= EAP_TTLS_AUTH_PAP;
					goto skip_eap;
				}
				if (os_strcmp(start, "TTLS-CHAP") == 0) {
					user->ttls_auth |= EAP_TTLS_AUTH_CHAP;
					goto skip_eap;
				}
				if (os_strcmp(start, "TTLS-MSCHAP") == 0) {
					user->ttls_auth |=
						EAP_TTLS_AUTH_MSCHAP;
					goto skip_eap;
				}
				if (os_strcmp(start, "TTLS-MSCHAPV2") == 0) {
					user->ttls_auth |=
						EAP_TTLS_AUTH_MSCHAPV2;
					goto skip_eap;
				}
				wpa_printf(MSG_ERROR, "Unsupported EAP type "
					   "'%s' on line %d in '%s'",
					   start, line, fname);
				goto failed;
			}

			num_methods++;
			if (num_methods >= EAP_USER_MAX_METHODS)
				break;
		skip_eap:
			if (pos3 == NULL)
				break;
			start = pos3;
		}
		if (num_methods == 0 && user->ttls_auth == 0) {
			wpa_printf(MSG_ERROR, "No EAP types configured on "
				   "line %d in '%s'", line, fname);
			goto failed;
		}

		if (pos == NULL)
			goto done;

		while (*pos == ' ' || *pos == '\t')
			pos++;
		if (*pos == '\0')
			goto done;

		if (os_strncmp(pos, "[ver=0]", 7) == 0) {
			user->force_version = 0;
			goto done;
		}

		if (os_strncmp(pos, "[ver=1]", 7) == 0) {
			user->force_version = 1;
			goto done;
		}

		if (os_strncmp(pos, "[2]", 3) == 0) {
			user->phase2 = 1;
			goto done;
		}

		if (*pos == '"') {
			pos++;
			start = pos;
			while (*pos != '"' && *pos != '\0')
				pos++;
			if (*pos == '\0') {
				wpa_printf(MSG_ERROR, "Invalid EAP password "
					   "(no \" in end) on line %d in '%s'",
					   line, fname);
				goto failed;
			}

			user->password = os_malloc(pos - start);
			if (user->password == NULL) {
				wpa_printf(MSG_ERROR, "Failed to allocate "
					   "memory for EAP password");
				goto failed;
			}
			os_memcpy(user->password, start, pos - start);
			user->password_len = pos - start;

			pos++;
		} else if (os_strncmp(pos, "hash:", 5) == 0) {
			pos += 5;
			pos2 = pos;
			while (*pos2 != '\0' && *pos2 != ' ' &&
			       *pos2 != '\t' && *pos2 != '#')
				pos2++;
			if (pos2 - pos != 32) {
				wpa_printf(MSG_ERROR, "Invalid password hash "
					   "on line %d in '%s'", line, fname);
				goto failed;
			}
			user->password = os_malloc(16);
			if (user->password == NULL) {
				wpa_printf(MSG_ERROR, "Failed to allocate "
					   "memory for EAP password hash");
				goto failed;
			}
			if (hexstr2bin(pos, user->password, 16) < 0) {
				wpa_printf(MSG_ERROR, "Invalid hash password "
					   "on line %d in '%s'", line, fname);
				goto failed;
			}
			user->password_len = 16;
			user->password_hash = 1;
			pos = pos2;
		} else {
			pos2 = pos;
			while (*pos2 != '\0' && *pos2 != ' ' &&
			       *pos2 != '\t' && *pos2 != '#')
				pos2++;
			if ((pos2 - pos) & 1) {
				wpa_printf(MSG_ERROR, "Invalid hex password "
					   "on line %d in '%s'", line, fname);
				goto failed;
			}
			user->password = os_malloc((pos2 - pos) / 2);
			if (user->password == NULL) {
				wpa_printf(MSG_ERROR, "Failed to allocate "
					   "memory for EAP password");
				goto failed;
			}
			if (hexstr2bin(pos, user->password,
				       (pos2 - pos) / 2) < 0) {
				wpa_printf(MSG_ERROR, "Invalid hex password "
					   "on line %d in '%s'", line, fname);
				goto failed;
			}
			user->password_len = (pos2 - pos) / 2;
			pos = pos2;
		}

		while (*pos == ' ' || *pos == '\t')
			pos++;
		if (os_strncmp(pos, "[2]", 3) == 0) {
			user->phase2 = 1;
		}

	done:
		if (tail == NULL) {
			tail = conf->eap_user = user;
		} else {
			tail->next = user;
			tail = user;
		}
		continue;

	failed:
		if (user) {
			os_free(user->password);
			os_free(user->identity);
			os_free(user);
		}
		ret = -1;
		break;
	}

	fclose(f);

	return ret;
}
#endif /* EAP_SERVER */


#ifndef CONFIG_NO_RADIUS
static int
hostapd_config_read_radius_addr(struct hostapd_radius_server **server,
				int *num_server, const char *val, int def_port,
				struct hostapd_radius_server **curr_serv)
{
	struct hostapd_radius_server *nserv;
	int ret;
	static int server_index = 1;

	nserv = os_realloc(*server, (*num_server + 1) * sizeof(*nserv));
	if (nserv == NULL)
		return -1;

	*server = nserv;
	nserv = &nserv[*num_server];
	(*num_server)++;
	(*curr_serv) = nserv;

	os_memset(nserv, 0, sizeof(*nserv));
	nserv->port = def_port;
	ret = hostapd_parse_ip_addr(val, &nserv->addr);
	nserv->index = server_index++;

	return ret;
}
#endif /* CONFIG_NO_RADIUS */


static int hostapd_config_parse_key_mgmt(int line, const char *value)
{
	int val = 0, last;
	char *start, *end, *buf;

	buf = os_strdup(value);
	if (buf == NULL)
		return -1;
	start = buf;

	while (*start != '\0') {
		while (*start == ' ' || *start == '\t')
			start++;
		if (*start == '\0')
			break;
		end = start;
		while (*end != ' ' && *end != '\t' && *end != '\0')
			end++;
		last = *end == '\0';
		*end = '\0';
		if (os_strcmp(start, "WPA-PSK") == 0)
			val |= WPA_KEY_MGMT_PSK;
		else if (os_strcmp(start, "WPA-EAP") == 0)
			val |= WPA_KEY_MGMT_IEEE8021X;
#ifdef CONFIG_IEEE80211R
		else if (os_strcmp(start, "FT-PSK") == 0)
			val |= WPA_KEY_MGMT_FT_PSK;
		else if (os_strcmp(start, "FT-EAP") == 0)
			val |= WPA_KEY_MGMT_FT_IEEE8021X;
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_IEEE80211W
		else if (os_strcmp(start, "WPA-PSK-SHA256") == 0)
			val |= WPA_KEY_MGMT_PSK_SHA256;
		else if (os_strcmp(start, "WPA-EAP-SHA256") == 0)
			val |= WPA_KEY_MGMT_IEEE8021X_SHA256;
#endif /* CONFIG_IEEE80211W */
		else {
			wpa_printf(MSG_ERROR, "Line %d: invalid key_mgmt '%s'",
				   line, start);
			os_free(buf);
			return -1;
		}

		if (last)
			break;
		start = end + 1;
	}

	os_free(buf);
	if (val == 0) {
		wpa_printf(MSG_ERROR, "Line %d: no key_mgmt values "
			   "configured.", line);
		return -1;
	}

	return val;
}


static int hostapd_config_parse_cipher(int line, const char *value)
{
	int val = 0, last;
	char *start, *end, *buf;

	buf = os_strdup(value);
	if (buf == NULL)
		return -1;
	start = buf;

	while (*start != '\0') {
		while (*start == ' ' || *start == '\t')
			start++;
		if (*start == '\0')
			break;
		end = start;
		while (*end != ' ' && *end != '\t' && *end != '\0')
			end++;
		last = *end == '\0';
		*end = '\0';
		if (os_strcmp(start, "CCMP") == 0)
			val |= WPA_CIPHER_CCMP;
		else if (os_strcmp(start, "TKIP") == 0)
			val |= WPA_CIPHER_TKIP;
		else if (os_strcmp(start, "WEP104") == 0)
			val |= WPA_CIPHER_WEP104;
		else if (os_strcmp(start, "WEP40") == 0)
			val |= WPA_CIPHER_WEP40;
		else if (os_strcmp(start, "NONE") == 0)
			val |= WPA_CIPHER_NONE;
		else {
			wpa_printf(MSG_ERROR, "Line %d: invalid cipher '%s'.",
				   line, start);
			os_free(buf);
			return -1;
		}

		if (last)
			break;
		start = end + 1;
	}
	os_free(buf);

	if (val == 0) {
		wpa_printf(MSG_ERROR, "Line %d: no cipher values configured.",
			   line);
		return -1;
	}
	return val;
}


static int hostapd_config_read_wep(struct hostapd_wep_keys *wep, int keyidx,
				   char *val)
{
	size_t len = os_strlen(val);

	if (keyidx < 0 || keyidx > 3 || wep->key[keyidx] != NULL)
		return -1;

	if (val[0] == '"') {
		if (len < 2 || val[len - 1] != '"')
			return -1;
		len -= 2;
		wep->key[keyidx] = os_malloc(len);
		if (wep->key[keyidx] == NULL)
			return -1;
		os_memcpy(wep->key[keyidx], val + 1, len);
		wep->len[keyidx] = len;
	} else {
		if (len & 1)
			return -1;
		len /= 2;
		wep->key[keyidx] = os_malloc(len);
		if (wep->key[keyidx] == NULL)
			return -1;
		wep->len[keyidx] = len;
		if (hexstr2bin(val, wep->key[keyidx], len) < 0)
			return -1;
	}

	wep->keys_set++;

	return 0;
}


static int hostapd_parse_rates(int **rate_list, char *val)
{
	int *list;
	int count;
	char *pos, *end;

	os_free(*rate_list);
	*rate_list = NULL;

	pos = val;
	count = 0;
	while (*pos != '\0') {
		if (*pos == ' ')
			count++;
		pos++;
	}

	list = os_malloc(sizeof(int) * (count + 2));
	if (list == NULL)
		return -1;
	pos = val;
	count = 0;
	while (*pos != '\0') {
		end = os_strchr(pos, ' ');
		if (end)
			*end = '\0';

		list[count++] = atoi(pos);
		if (!end)
			break;
		pos = end + 1;
	}
	list[count] = -1;

	*rate_list = list;
	return 0;
}


static int hostapd_config_bss(struct hostapd_config *conf, const char *ifname)
{
	struct hostapd_bss_config *bss;

	if (*ifname == '\0')
		return -1;

	bss = os_realloc(conf->bss, (conf->num_bss + 1) *
			 sizeof(struct hostapd_bss_config));
	if (bss == NULL) {
		wpa_printf(MSG_ERROR, "Failed to allocate memory for "
			   "multi-BSS entry");
		return -1;
	}
	conf->bss = bss;

	bss = &(conf->bss[conf->num_bss]);
	os_memset(bss, 0, sizeof(*bss));
	bss->radius = os_zalloc(sizeof(*bss->radius));
	if (bss->radius == NULL) {
		wpa_printf(MSG_ERROR, "Failed to allocate memory for "
			   "multi-BSS RADIUS data");
		return -1;
	}

	conf->num_bss++;
	conf->last_bss = bss;

	hostapd_config_defaults_bss(bss);
	os_strlcpy(bss->iface, ifname, sizeof(bss->iface));
	os_memcpy(bss->ssid.vlan, bss->iface, IFNAMSIZ + 1);

	return 0;
}


/* convert floats with one decimal place to value*10 int, i.e.,
 * "1.5" will return 15 */
static int hostapd_config_read_int10(const char *value)
{
	int i, d;
	char *pos;

	i = atoi(value);
	pos = os_strchr(value, '.');
	d = 0;
	if (pos) {
		pos++;
		if (*pos >= '0' && *pos <= '9')
			d = *pos - '0';
	}

	return i * 10 + d;
}


static int valid_cw(int cw)
{
	return (cw == 1 || cw == 3 || cw == 7 || cw == 15 || cw == 31 ||
		cw == 63 || cw == 127 || cw == 255 || cw == 511 || cw == 1023);
}


enum {
	IEEE80211_TX_QUEUE_DATA0 = 0, /* used for EDCA AC_VO data */
	IEEE80211_TX_QUEUE_DATA1 = 1, /* used for EDCA AC_VI data */
	IEEE80211_TX_QUEUE_DATA2 = 2, /* used for EDCA AC_BE data */
	IEEE80211_TX_QUEUE_DATA3 = 3 /* used for EDCA AC_BK data */
};

static int hostapd_config_tx_queue(struct hostapd_config *conf, char *name,
				   char *val)
{
	int num;
	char *pos;
	struct hostapd_tx_queue_params *queue;

	/* skip 'tx_queue_' prefix */
	pos = name + 9;
	if (os_strncmp(pos, "data", 4) == 0 &&
	    pos[4] >= '0' && pos[4] <= '9' && pos[5] == '_') {
		num = pos[4] - '0';
		pos += 6;
	} else if (os_strncmp(pos, "after_beacon_", 13) == 0 ||
		   os_strncmp(pos, "beacon_", 7) == 0) {
		wpa_printf(MSG_INFO, "DEPRECATED: '%s' not used", name);
		return 0;
	} else {
		wpa_printf(MSG_ERROR, "Unknown tx_queue name '%s'", pos);
		return -1;
	}

	if (num >= NUM_TX_QUEUES) {
		/* for backwards compatibility, do not trigger failure */
		wpa_printf(MSG_INFO, "DEPRECATED: '%s' not used", name);
		return 0;
	}

	queue = &conf->tx_queue[num];

	if (os_strcmp(pos, "aifs") == 0) {
		queue->aifs = atoi(val);
		if (queue->aifs < 0 || queue->aifs > 255) {
			wpa_printf(MSG_ERROR, "Invalid AIFS value %d",
				   queue->aifs);
			return -1;
		}
	} else if (os_strcmp(pos, "cwmin") == 0) {
		queue->cwmin = atoi(val);
		if (!valid_cw(queue->cwmin)) {
			wpa_printf(MSG_ERROR, "Invalid cwMin value %d",
				   queue->cwmin);
			return -1;
		}
	} else if (os_strcmp(pos, "cwmax") == 0) {
		queue->cwmax = atoi(val);
		if (!valid_cw(queue->cwmax)) {
			wpa_printf(MSG_ERROR, "Invalid cwMax value %d",
				   queue->cwmax);
			return -1;
		}
	} else if (os_strcmp(pos, "burst") == 0) {
		queue->burst = hostapd_config_read_int10(val);
	} else {
		wpa_printf(MSG_ERROR, "Unknown tx_queue field '%s'", pos);
		return -1;
	}

	return 0;
}


static int hostapd_config_wmm_ac(struct hostapd_config *conf, char *name,
				 char *val)
{
	int num, v;
	char *pos;
	struct hostapd_wmm_ac_params *ac;

	/* skip 'wme_ac_' or 'wmm_ac_' prefix */
	pos = name + 7;
	if (os_strncmp(pos, "be_", 3) == 0) {
		num = 0;
		pos += 3;
	} else if (os_strncmp(pos, "bk_", 3) == 0) {
		num = 1;
		pos += 3;
	} else if (os_strncmp(pos, "vi_", 3) == 0) {
		num = 2;
		pos += 3;
	} else if (os_strncmp(pos, "vo_", 3) == 0) {
		num = 3;
		pos += 3;
	} else {
		wpa_printf(MSG_ERROR, "Unknown WMM name '%s'", pos);
		return -1;
	}

	ac = &conf->wmm_ac_params[num];

	if (os_strcmp(pos, "aifs") == 0) {
		v = atoi(val);
		if (v < 1 || v > 255) {
			wpa_printf(MSG_ERROR, "Invalid AIFS value %d", v);
			return -1;
		}
		ac->aifs = v;
	} else if (os_strcmp(pos, "cwmin") == 0) {
		v = atoi(val);
		if (v < 0 || v > 12) {
			wpa_printf(MSG_ERROR, "Invalid cwMin value %d", v);
			return -1;
		}
		ac->cwmin = v;
	} else if (os_strcmp(pos, "cwmax") == 0) {
		v = atoi(val);
		if (v < 0 || v > 12) {
			wpa_printf(MSG_ERROR, "Invalid cwMax value %d", v);
			return -1;
		}
		ac->cwmax = v;
	} else if (os_strcmp(pos, "txop_limit") == 0) {
		v = atoi(val);
		if (v < 0 || v > 0xffff) {
			wpa_printf(MSG_ERROR, "Invalid txop value %d", v);
			return -1;
		}
		ac->txop_limit = v;
	} else if (os_strcmp(pos, "acm") == 0) {
		v = atoi(val);
		if (v < 0 || v > 1) {
			wpa_printf(MSG_ERROR, "Invalid acm value %d", v);
			return -1;
		}
		ac->admission_control_mandatory = v;
	} else {
		wpa_printf(MSG_ERROR, "Unknown wmm_ac_ field '%s'", pos);
		return -1;
	}

	return 0;
}


#ifdef CONFIG_IEEE80211R
static int add_r0kh(struct hostapd_bss_config *bss, char *value)
{
	struct ft_remote_r0kh *r0kh;
	char *pos, *next;

	r0kh = os_zalloc(sizeof(*r0kh));
	if (r0kh == NULL)
		return -1;

	/* 02:01:02:03:04:05 a.example.com 000102030405060708090a0b0c0d0e0f */
	pos = value;
	next = os_strchr(pos, ' ');
	if (next)
		*next++ = '\0';
	if (next == NULL || hwaddr_aton(pos, r0kh->addr)) {
		wpa_printf(MSG_ERROR, "Invalid R0KH MAC address: '%s'", pos);
		os_free(r0kh);
		return -1;
	}

	pos = next;
	next = os_strchr(pos, ' ');
	if (next)
		*next++ = '\0';
	if (next == NULL || next - pos > FT_R0KH_ID_MAX_LEN) {
		wpa_printf(MSG_ERROR, "Invalid R0KH-ID: '%s'", pos);
		os_free(r0kh);
		return -1;
	}
	r0kh->id_len = next - pos - 1;
	os_memcpy(r0kh->id, pos, r0kh->id_len);

	pos = next;
	if (hexstr2bin(pos, r0kh->key, sizeof(r0kh->key))) {
		wpa_printf(MSG_ERROR, "Invalid R0KH key: '%s'", pos);
		os_free(r0kh);
		return -1;
	}

	r0kh->next = bss->r0kh_list;
	bss->r0kh_list = r0kh;

	return 0;
}


static int add_r1kh(struct hostapd_bss_config *bss, char *value)
{
	struct ft_remote_r1kh *r1kh;
	char *pos, *next;

	r1kh = os_zalloc(sizeof(*r1kh));
	if (r1kh == NULL)
		return -1;

	/* 02:01:02:03:04:05 02:01:02:03:04:05
	 * 000102030405060708090a0b0c0d0e0f */
	pos = value;
	next = os_strchr(pos, ' ');
	if (next)
		*next++ = '\0';
	if (next == NULL || hwaddr_aton(pos, r1kh->addr)) {
		wpa_printf(MSG_ERROR, "Invalid R1KH MAC address: '%s'", pos);
		os_free(r1kh);
		return -1;
	}

	pos = next;
	next = os_strchr(pos, ' ');
	if (next)
		*next++ = '\0';
	if (next == NULL || hwaddr_aton(pos, r1kh->id)) {
		wpa_printf(MSG_ERROR, "Invalid R1KH-ID: '%s'", pos);
		os_free(r1kh);
		return -1;
	}

	pos = next;
	if (hexstr2bin(pos, r1kh->key, sizeof(r1kh->key))) {
		wpa_printf(MSG_ERROR, "Invalid R1KH key: '%s'", pos);
		os_free(r1kh);
		return -1;
	}

	r1kh->next = bss->r1kh_list;
	bss->r1kh_list = r1kh;

	return 0;
}
#endif /* CONFIG_IEEE80211R */


#ifdef CONFIG_IEEE80211N
static int hostapd_config_ht_capab(struct hostapd_config *conf,
				   const char *capab)
{
	if (os_strstr(capab, "[LDPC]"))
		conf->ht_capab |= HT_CAP_INFO_LDPC_CODING_CAP;
	if (os_strstr(capab, "[HT40-]")) {
		conf->ht_capab |= HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET;
		conf->secondary_channel = -1;
	}
	if (os_strstr(capab, "[HT40+]")) {
		conf->ht_capab |= HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET;
		conf->secondary_channel = 1;
	}
	if (os_strstr(capab, "[SMPS-STATIC]")) {
		conf->ht_capab &= ~HT_CAP_INFO_SMPS_MASK;
		conf->ht_capab |= HT_CAP_INFO_SMPS_STATIC;
	}
	if (os_strstr(capab, "[SMPS-DYNAMIC]")) {
		conf->ht_capab &= ~HT_CAP_INFO_SMPS_MASK;
		conf->ht_capab |= HT_CAP_INFO_SMPS_DYNAMIC;
	}
	if (os_strstr(capab, "[GF]"))
		conf->ht_capab |= HT_CAP_INFO_GREEN_FIELD;
	if (os_strstr(capab, "[SHORT-GI-20]"))
		conf->ht_capab |= HT_CAP_INFO_SHORT_GI20MHZ;
	if (os_strstr(capab, "[SHORT-GI-40]"))
		conf->ht_capab |= HT_CAP_INFO_SHORT_GI40MHZ;
	if (os_strstr(capab, "[TX-STBC]"))
		conf->ht_capab |= HT_CAP_INFO_TX_STBC;
	if (os_strstr(capab, "[RX-STBC1]")) {
		conf->ht_capab &= ~HT_CAP_INFO_RX_STBC_MASK;
		conf->ht_capab |= HT_CAP_INFO_RX_STBC_1;
	}
	if (os_strstr(capab, "[RX-STBC12]")) {
		conf->ht_capab &= ~HT_CAP_INFO_RX_STBC_MASK;
		conf->ht_capab |= HT_CAP_INFO_RX_STBC_12;
	}
	if (os_strstr(capab, "[RX-STBC123]")) {
		conf->ht_capab &= ~HT_CAP_INFO_RX_STBC_MASK;
		conf->ht_capab |= HT_CAP_INFO_RX_STBC_123;
	}
	if (os_strstr(capab, "[DELAYED-BA]"))
		conf->ht_capab |= HT_CAP_INFO_DELAYED_BA;
	if (os_strstr(capab, "[MAX-AMSDU-7935]"))
		conf->ht_capab |= HT_CAP_INFO_MAX_AMSDU_SIZE;
	if (os_strstr(capab, "[DSSS_CCK-40]"))
		conf->ht_capab |= HT_CAP_INFO_DSSS_CCK40MHZ;
	if (os_strstr(capab, "[PSMP]"))
		conf->ht_capab |= HT_CAP_INFO_PSMP_SUPP;
	if (os_strstr(capab, "[LSIG-TXOP-PROT]"))
		conf->ht_capab |= HT_CAP_INFO_LSIG_TXOP_PROTECT_SUPPORT;

	return 0;
}
#endif /* CONFIG_IEEE80211N */


static int hostapd_config_check_bss(struct hostapd_bss_config *bss,
				    struct hostapd_config *conf)
{
	if (bss->ieee802_1x && !bss->eap_server &&
	    !bss->radius->auth_servers) {
		wpa_printf(MSG_ERROR, "Invalid IEEE 802.1X configuration (no "
			   "EAP authenticator configured).");
		return -1;
	}

	if (bss->wpa && (bss->wpa_key_mgmt & WPA_KEY_MGMT_PSK) &&
	    bss->ssid.wpa_psk == NULL && bss->ssid.wpa_passphrase == NULL &&
	    bss->ssid.wpa_psk_file == NULL) {
		wpa_printf(MSG_ERROR, "WPA-PSK enabled, but PSK or passphrase "
			   "is not configured.");
		return -1;
	}

	if (hostapd_mac_comp_empty(bss->bssid) != 0) {
		size_t i;

		for (i = 0; i < conf->num_bss; i++) {
			if ((&conf->bss[i] != bss) &&
			    (hostapd_mac_comp(conf->bss[i].bssid,
					      bss->bssid) == 0)) {
				wpa_printf(MSG_ERROR, "Duplicate BSSID " MACSTR
					   " on interface '%s' and '%s'.",
					   MAC2STR(bss->bssid),
					   conf->bss[i].iface, bss->iface);
				return -1;
			}
		}
	}

#ifdef CONFIG_IEEE80211R
	if ((bss->wpa_key_mgmt &
	     (WPA_KEY_MGMT_FT_PSK | WPA_KEY_MGMT_FT_IEEE8021X)) &&
	    (bss->nas_identifier == NULL ||
	     os_strlen(bss->nas_identifier) < 1 ||
	     os_strlen(bss->nas_identifier) > FT_R0KH_ID_MAX_LEN)) {
		wpa_printf(MSG_ERROR, "FT (IEEE 802.11r) requires "
			   "nas_identifier to be configured as a 1..48 octet "
			   "string");
		return -1;
	}
#endif /* CONFIG_IEEE80211R */

#ifdef CONFIG_IEEE80211N
	if (conf->ieee80211n &&
	    bss->ssid.security_policy == SECURITY_STATIC_WEP) {
		bss->disable_11n = 1;
		wpa_printf(MSG_ERROR, "HT (IEEE 802.11n) with WEP is not "
			   "allowed, disabling HT capabilities");
	}

	if (conf->ieee80211n && bss->wpa &&
	    !(bss->wpa_pairwise & WPA_CIPHER_CCMP) &&
	    !(bss->rsn_pairwise & WPA_CIPHER_CCMP)) {
		bss->disable_11n = 1;
		wpa_printf(MSG_ERROR, "HT (IEEE 802.11n) with WPA/WPA2 "
			   "requires CCMP to be enabled, disabling HT "
			   "capabilities");
	}
#endif /* CONFIG_IEEE80211N */

#ifdef CONFIG_WPS2
	if (bss->wps_state && bss->ignore_broadcast_ssid) {
		wpa_printf(MSG_INFO, "WPS: ignore_broadcast_ssid "
			   "configuration forced WPS to be disabled");
		bss->wps_state = 0;
	}

	if (bss->wps_state && bss->ssid.wep.keys_set && bss->wpa == 0) {
		wpa_printf(MSG_INFO, "WPS: WEP configuration forced WPS to be "
			   "disabled");
		bss->wps_state = 0;
	}
#endif /* CONFIG_WPS2 */

	return 0;
}


static int hostapd_config_check(struct hostapd_config *conf)
{
	size_t i;

	if (conf->ieee80211d && (!conf->country[0] || !conf->country[1])) {
		wpa_printf(MSG_ERROR, "Cannot enable IEEE 802.11d without "
			   "setting the country_code");
		return -1;
	}

	for (i = 0; i < conf->num_bss; i++) {
		if (hostapd_config_check_bss(&conf->bss[i], conf))
			return -1;
	}

	return 0;
}


/**
 * hostapd_config_read - Read and parse a configuration file
 * @fname: Configuration file name (including path, if needed)
 * Returns: Allocated configuration data structure
 */
struct hostapd_config * hostapd_config_read(const char *fname)
{
	struct hostapd_config *conf;
	struct hostapd_bss_config *bss;
	FILE *f;
	char buf[256], *pos;
	int line = 0;
	int errors = 0;
	int pairwise;
	size_t i;

	f = fopen(fname, "r");
	if (f == NULL) {
		wpa_printf(MSG_ERROR, "Could not open configuration file '%s' "
			   "for reading.", fname);
		return NULL;
	}

	conf = hostapd_config_defaults();
	if (conf == NULL) {
		fclose(f);
		return NULL;
	}

	/* set default driver based on configuration */
	conf->driver = wpa_drivers[0];
	if (conf->driver == NULL) {
		wpa_printf(MSG_ERROR, "No driver wrappers registered!");
		hostapd_config_free(conf);
		fclose(f);
		return NULL;
	}

	bss = conf->last_bss = conf->bss;

	while (fgets(buf, sizeof(buf), f)) {
		bss = conf->last_bss;
		line++;

		if (buf[0] == '#')
			continue;
		pos = buf;
		while (*pos != '\0') {
			if (*pos == '\n') {
				*pos = '\0';
				break;
			}
			pos++;
		}
		if (buf[0] == '\0')
			continue;

		pos = os_strchr(buf, '=');
		if (pos == NULL) {
			wpa_printf(MSG_ERROR, "Line %d: invalid line '%s'",
				   line, buf);
			errors++;
			continue;
		}
		*pos = '\0';
		pos++;

		if (os_strcmp(buf, "interface") == 0) {
			os_strlcpy(conf->bss[0].iface, pos,
				   sizeof(conf->bss[0].iface));
		} else if (os_strcmp(buf, "bridge") == 0) {
			os_strlcpy(bss->bridge, pos, sizeof(bss->bridge));
		} else if (os_strcmp(buf, "wds_bridge") == 0) {
			os_strlcpy(bss->wds_bridge, pos,
				   sizeof(bss->wds_bridge));
		} else if (os_strcmp(buf, "driver") == 0) {
			int j;
			/* clear to get error below if setting is invalid */
			conf->driver = NULL;
			for (j = 0; wpa_drivers[j]; j++) {
				if (os_strcmp(pos, wpa_drivers[j]->name) == 0)
				{
					conf->driver = wpa_drivers[j];
					break;
				}
			}
			if (conf->driver == NULL) {
				wpa_printf(MSG_ERROR, "Line %d: invalid/"
					   "unknown driver '%s'", line, pos);
				errors++;
			}
		} else if (os_strcmp(buf, "debug") == 0) {
			wpa_printf(MSG_DEBUG, "Line %d: DEPRECATED: 'debug' "
				   "configuration variable is not used "
				   "anymore", line);
		} else if (os_strcmp(buf, "logger_syslog_level") == 0) {
			bss->logger_syslog_level = atoi(pos);
		} else if (os_strcmp(buf, "logger_stdout_level") == 0) {
			bss->logger_stdout_level = atoi(pos);
		} else if (os_strcmp(buf, "logger_syslog") == 0) {
			bss->logger_syslog = atoi(pos);
		} else if (os_strcmp(buf, "logger_stdout") == 0) {
			bss->logger_stdout = atoi(pos);
		} else if (os_strcmp(buf, "dump_file") == 0) {
			bss->dump_log_name = os_strdup(pos);
		} else if (os_strcmp(buf, "ssid") == 0) {
			bss->ssid.ssid_len = os_strlen(pos);
			if (bss->ssid.ssid_len > HOSTAPD_MAX_SSID_LEN ||
			    bss->ssid.ssid_len < 1) {
				wpa_printf(MSG_ERROR, "Line %d: invalid SSID "
					   "'%s'", line, pos);
				errors++;
			} else {
				os_memcpy(bss->ssid.ssid, pos,
					  bss->ssid.ssid_len);
				bss->ssid.ssid[bss->ssid.ssid_len] = '\0';
				bss->ssid.ssid_set = 1;
			}
		} else if (os_strcmp(buf, "macaddr_acl") == 0) {
			bss->macaddr_acl = atoi(pos);
			if (bss->macaddr_acl != ACCEPT_UNLESS_DENIED &&
			    bss->macaddr_acl != DENY_UNLESS_ACCEPTED &&
			    bss->macaddr_acl != USE_EXTERNAL_RADIUS_AUTH) {
				wpa_printf(MSG_ERROR, "Line %d: unknown "
					   "macaddr_acl %d",
					   line, bss->macaddr_acl);
			}
		} else if (os_strcmp(buf, "accept_mac_file") == 0) {
			if (hostapd_config_read_maclist(pos, &bss->accept_mac,
							&bss->num_accept_mac))
			{
				wpa_printf(MSG_ERROR, "Line %d: Failed to "
					   "read accept_mac_file '%s'",
					   line, pos);
				errors++;
			}
		} else if (os_strcmp(buf, "deny_mac_file") == 0) {
			if (hostapd_config_read_maclist(pos, &bss->deny_mac,
							&bss->num_deny_mac)) {
				wpa_printf(MSG_ERROR, "Line %d: Failed to "
					   "read deny_mac_file '%s'",
					   line, pos);
				errors++;
			}
		} else if (os_strcmp(buf, "wds_sta") == 0) {
			bss->wds_sta = atoi(pos);
		} else if (os_strcmp(buf, "ap_isolate") == 0) {
			bss->isolate = atoi(pos);
		} else if (os_strcmp(buf, "ap_max_inactivity") == 0) {
			bss->ap_max_inactivity = atoi(pos);
		} else if (os_strcmp(buf, "country_code") == 0) {
			os_memcpy(conf->country, pos, 2);
			/* FIX: make this configurable */
			conf->country[2] = ' ';
		} else if (os_strcmp(buf, "ieee80211d") == 0) {
			conf->ieee80211d = atoi(pos);
		} else if (os_strcmp(buf, "ieee8021x") == 0) {
			bss->ieee802_1x = atoi(pos);
		} else if (os_strcmp(buf, "eapol_version") == 0) {
			bss->eapol_version = atoi(pos);
			if (bss->eapol_version < 1 ||
			    bss->eapol_version > 2) {
				wpa_printf(MSG_ERROR, "Line %d: invalid EAPOL "
					   "version (%d): '%s'.",
					   line, bss->eapol_version, pos);
				errors++;
			} else
				wpa_printf(MSG_DEBUG, "eapol_version=%d",
					   bss->eapol_version);
#ifdef EAP_SERVER
		} else if (os_strcmp(buf, "eap_authenticator") == 0) {
			bss->eap_server = atoi(pos);
			wpa_printf(MSG_ERROR, "Line %d: obsolete "
				   "eap_authenticator used; this has been "
				   "renamed to eap_server", line);
		} else if (os_strcmp(buf, "eap_server") == 0) {
			bss->eap_server = atoi(pos);
		} else if (os_strcmp(buf, "eap_user_file") == 0) {
			if (hostapd_config_read_eap_user(pos, bss))
				errors++;
		} else if (os_strcmp(buf, "ca_cert") == 0) {
			os_free(bss->ca_cert);
			bss->ca_cert = os_strdup(pos);
		} else if (os_strcmp(buf, "server_cert") == 0) {
			os_free(bss->server_cert);
			bss->server_cert = os_strdup(pos);
		} else if (os_strcmp(buf, "private_key") == 0) {
			os_free(bss->private_key);
			bss->private_key = os_strdup(pos);
		} else if (os_strcmp(buf, "private_key_passwd") == 0) {
			os_free(bss->private_key_passwd);
			bss->private_key_passwd = os_strdup(pos);
		} else if (os_strcmp(buf, "check_crl") == 0) {
			bss->check_crl = atoi(pos);
		} else if (os_strcmp(buf, "dh_file") == 0) {
			os_free(bss->dh_file);
			bss->dh_file = os_strdup(pos);
		} else if (os_strcmp(buf, "fragment_size") == 0) {
			bss->fragment_size = atoi(pos);
#ifdef EAP_SERVER_FAST
		} else if (os_strcmp(buf, "pac_opaque_encr_key") == 0) {
			os_free(bss->pac_opaque_encr_key);
			bss->pac_opaque_encr_key = os_malloc(16);
			if (bss->pac_opaque_encr_key == NULL) {
				wpa_printf(MSG_ERROR, "Line %d: No memory for "
					   "pac_opaque_encr_key", line);
				errors++;
			} else if (hexstr2bin(pos, bss->pac_opaque_encr_key,
					      16)) {
				wpa_printf(MSG_ERROR, "Line %d: Invalid "
					   "pac_opaque_encr_key", line);
				errors++;
			}
		} else if (os_strcmp(buf, "eap_fast_a_id") == 0) {
			size_t idlen = os_strlen(pos);
			if (idlen & 1) {
				wpa_printf(MSG_ERROR, "Line %d: Invalid "
					   "eap_fast_a_id", line);
				errors++;
			} else {
				os_free(bss->eap_fast_a_id);
				bss->eap_fast_a_id = os_malloc(idlen / 2);
				if (bss->eap_fast_a_id == NULL ||
				    hexstr2bin(pos, bss->eap_fast_a_id,
					       idlen / 2)) {
					wpa_printf(MSG_ERROR, "Line %d: "
						   "Failed to parse "
						   "eap_fast_a_id", line);
					errors++;
				} else
					bss->eap_fast_a_id_len = idlen / 2;
			}
		} else if (os_strcmp(buf, "eap_fast_a_id_info") == 0) {
			os_free(bss->eap_fast_a_id_info);
			bss->eap_fast_a_id_info = os_strdup(pos);
		} else if (os_strcmp(buf, "eap_fast_prov") == 0) {
			bss->eap_fast_prov = atoi(pos);
		} else if (os_strcmp(buf, "pac_key_lifetime") == 0) {
			bss->pac_key_lifetime = atoi(pos);
		} else if (os_strcmp(buf, "pac_key_refresh_time") == 0) {
			bss->pac_key_refresh_time = atoi(pos);
#endif /* EAP_SERVER_FAST */
#ifdef EAP_SERVER_SIM
		} else if (os_strcmp(buf, "eap_sim_db") == 0) {
			os_free(bss->eap_sim_db);
			bss->eap_sim_db = os_strdup(pos);
		} else if (os_strcmp(buf, "eap_sim_aka_result_ind") == 0) {
			bss->eap_sim_aka_result_ind = atoi(pos);
#endif /* EAP_SERVER_SIM */
#ifdef EAP_SERVER_TNC
		} else if (os_strcmp(buf, "tnc") == 0) {
			bss->tnc = atoi(pos);
#endif /* EAP_SERVER_TNC */
#ifdef EAP_SERVER_PWD
		} else if (os_strcmp(buf, "pwd_group") == 0) {
			bss->pwd_group = atoi(pos);
#endif /* EAP_SERVER_PWD */
#endif /* EAP_SERVER */
		} else if (os_strcmp(buf, "eap_message") == 0) {
			char *term;
			bss->eap_req_id_text = os_strdup(pos);
			if (bss->eap_req_id_text == NULL) {
				wpa_printf(MSG_ERROR, "Line %d: Failed to "
					   "allocate memory for "
					   "eap_req_id_text", line);
				errors++;
				continue;
			}
			bss->eap_req_id_text_len =
				os_strlen(bss->eap_req_id_text);
			term = os_strstr(bss->eap_req_id_text, "\\0");
			if (term) {
				*term++ = '\0';
				os_memmove(term, term + 1,
					   bss->eap_req_id_text_len -
					   (term - bss->eap_req_id_text) - 1);
				bss->eap_req_id_text_len--;
			}
		} else if (os_strcmp(buf, "wep_key_len_broadcast") == 0) {
			bss->default_wep_key_len = atoi(pos);
			if (bss->default_wep_key_len > 13) {
				wpa_printf(MSG_ERROR, "Line %d: invalid WEP "
					   "key len %lu (= %lu bits)", line,
					   (unsigned long)
					   bss->default_wep_key_len,
					   (unsigned long)
					   bss->default_wep_key_len * 8);
				errors++;
			}
		} else if (os_strcmp(buf, "wep_key_len_unicast") == 0) {
			bss->individual_wep_key_len = atoi(pos);
			if (bss->individual_wep_key_len < 0 ||
			    bss->individual_wep_key_len > 13) {
				wpa_printf(MSG_ERROR, "Line %d: invalid WEP "
					   "key len %d (= %d bits)", line,
					   bss->individual_wep_key_len,
					   bss->individual_wep_key_len * 8);
				errors++;
			}
		} else if (os_strcmp(buf, "wep_rekey_period") == 0) {
			bss->wep_rekeying_period = atoi(pos);
			if (bss->wep_rekeying_period < 0) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "period %d",
					   line, bss->wep_rekeying_period);
				errors++;
			}
		} else if (os_strcmp(buf, "eap_reauth_period") == 0) {
			bss->eap_reauth_period = atoi(pos);
			if (bss->eap_reauth_period < 0) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "period %d",
					   line, bss->eap_reauth_period);
				errors++;
			}
		} else if (os_strcmp(buf, "eapol_key_index_workaround") == 0) {
			bss->eapol_key_index_workaround = atoi(pos);
#ifdef CONFIG_IAPP
		} else if (os_strcmp(buf, "iapp_interface") == 0) {
			bss->ieee802_11f = 1;
			os_strlcpy(bss->iapp_iface, pos,
				   sizeof(bss->iapp_iface));
#endif /* CONFIG_IAPP */
		} else if (os_strcmp(buf, "own_ip_addr") == 0) {
			if (hostapd_parse_ip_addr(pos, &bss->own_ip_addr)) {
				wpa_printf(MSG_ERROR, "Line %d: invalid IP "
					   "address '%s'", line, pos);
				errors++;
			}
		} else if (os_strcmp(buf, "nas_identifier") == 0) {
			bss->nas_identifier = os_strdup(pos);
#ifndef CONFIG_NO_RADIUS
		} else if (os_strcmp(buf, "auth_server_addr") == 0) {
			if (hostapd_config_read_radius_addr(
				    &bss->radius->auth_servers,
				    &bss->radius->num_auth_servers, pos, 1812,
				    &bss->radius->auth_server)) {
				wpa_printf(MSG_ERROR, "Line %d: invalid IP "
					   "address '%s'", line, pos);
				errors++;
			}
		} else if (bss->radius->auth_server &&
			   os_strcmp(buf, "auth_server_port") == 0) {
			bss->radius->auth_server->port = atoi(pos);
		} else if (bss->radius->auth_server &&
			   os_strcmp(buf, "auth_server_shared_secret") == 0) {
			int len = os_strlen(pos);
			if (len == 0) {
				/* RFC 2865, Ch. 3 */
				wpa_printf(MSG_ERROR, "Line %d: empty shared "
					   "secret is not allowed.", line);
				errors++;
			}
			bss->radius->auth_server->shared_secret =
				(u8 *) os_strdup(pos);
			bss->radius->auth_server->shared_secret_len = len;
		} else if (os_strcmp(buf, "acct_server_addr") == 0) {
			if (hostapd_config_read_radius_addr(
				    &bss->radius->acct_servers,
				    &bss->radius->num_acct_servers, pos, 1813,
				    &bss->radius->acct_server)) {
				wpa_printf(MSG_ERROR, "Line %d: invalid IP "
					   "address '%s'", line, pos);
				errors++;
			}
		} else if (bss->radius->acct_server &&
			   os_strcmp(buf, "acct_server_port") == 0) {
			bss->radius->acct_server->port = atoi(pos);
		} else if (bss->radius->acct_server &&
			   os_strcmp(buf, "acct_server_shared_secret") == 0) {
			int len = os_strlen(pos);
			if (len == 0) {
				/* RFC 2865, Ch. 3 */
				wpa_printf(MSG_ERROR, "Line %d: empty shared "
					   "secret is not allowed.", line);
				errors++;
			}
			bss->radius->acct_server->shared_secret =
				(u8 *) os_strdup(pos);
			bss->radius->acct_server->shared_secret_len = len;
		} else if (os_strcmp(buf, "radius_retry_primary_interval") ==
			   0) {
			bss->radius->retry_primary_interval = atoi(pos);
		} else if (os_strcmp(buf, "radius_acct_interim_interval") == 0)
		{
			bss->acct_interim_interval = atoi(pos);
#endif /* CONFIG_NO_RADIUS */
		} else if (os_strcmp(buf, "auth_algs") == 0) {
			bss->auth_algs = atoi(pos);
			if (bss->auth_algs == 0) {
				wpa_printf(MSG_ERROR, "Line %d: no "
					   "authentication algorithms allowed",
					   line);
				errors++;
			}
		} else if (os_strcmp(buf, "max_num_sta") == 0) {
			bss->max_num_sta = atoi(pos);
			if (bss->max_num_sta < 0 ||
			    bss->max_num_sta > MAX_STA_COUNT) {
				wpa_printf(MSG_ERROR, "Line %d: Invalid "
					   "max_num_sta=%d; allowed range "
					   "0..%d", line, bss->max_num_sta,
					   MAX_STA_COUNT);
				errors++;
			}
		} else if (os_strcmp(buf, "wpa") == 0) {
			bss->wpa = atoi(pos);
		} else if (os_strcmp(buf, "wpa_group_rekey") == 0) {
			bss->wpa_group_rekey = atoi(pos);
		} else if (os_strcmp(buf, "wpa_strict_rekey") == 0) {
			bss->wpa_strict_rekey = atoi(pos);
		} else if (os_strcmp(buf, "wpa_gmk_rekey") == 0) {
			bss->wpa_gmk_rekey = atoi(pos);
		} else if (os_strcmp(buf, "wpa_ptk_rekey") == 0) {
			bss->wpa_ptk_rekey = atoi(pos);
		} else if (os_strcmp(buf, "wpa_passphrase") == 0) {
			int len = os_strlen(pos);
			if (len < 8 || len > 63) {
				wpa_printf(MSG_ERROR, "Line %d: invalid WPA "
					   "passphrase length %d (expected "
					   "8..63)", line, len);
				errors++;
			} else {
				os_free(bss->ssid.wpa_passphrase);
				bss->ssid.wpa_passphrase = os_strdup(pos);
			}
		} else if (os_strcmp(buf, "wpa_psk") == 0) {
			os_free(bss->ssid.wpa_psk);
			bss->ssid.wpa_psk =
				os_zalloc(sizeof(struct hostapd_wpa_psk));
			if (bss->ssid.wpa_psk == NULL)
				errors++;
			else if (hexstr2bin(pos, bss->ssid.wpa_psk->psk,
					    PMK_LEN) ||
				 pos[PMK_LEN * 2] != '\0') {
				wpa_printf(MSG_ERROR, "Line %d: Invalid PSK "
					   "'%s'.", line, pos);
				errors++;
			} else {
				bss->ssid.wpa_psk->group = 1;
			}
		} else if (os_strcmp(buf, "wpa_psk_file") == 0) {
			os_free(bss->ssid.wpa_psk_file);
			bss->ssid.wpa_psk_file = os_strdup(pos);
			if (!bss->ssid.wpa_psk_file) {
				wpa_printf(MSG_ERROR, "Line %d: allocation "
					   "failed", line);
				errors++;
			}
		} else if (os_strcmp(buf, "wpa_key_mgmt") == 0) {
			bss->wpa_key_mgmt =
				hostapd_config_parse_key_mgmt(line, pos);
			if (bss->wpa_key_mgmt == -1)
				errors++;
		} else if (os_strcmp(buf, "wpa_pairwise") == 0) {
			bss->wpa_pairwise =
				hostapd_config_parse_cipher(line, pos);
			if (bss->wpa_pairwise == -1 ||
			    bss->wpa_pairwise == 0)
				errors++;
			else if (bss->wpa_pairwise &
				 (WPA_CIPHER_NONE | WPA_CIPHER_WEP40 |
				  WPA_CIPHER_WEP104)) {
				wpa_printf(MSG_ERROR, "Line %d: unsupported "
					   "pairwise cipher suite '%s'",
					   bss->wpa_pairwise, pos);
				errors++;
			}
		} else if (os_strcmp(buf, "rsn_pairwise") == 0) {
			bss->rsn_pairwise =
				hostapd_config_parse_cipher(line, pos);
			if (bss->rsn_pairwise == -1 ||
			    bss->rsn_pairwise == 0)
				errors++;
			else if (bss->rsn_pairwise &
				 (WPA_CIPHER_NONE | WPA_CIPHER_WEP40 |
				  WPA_CIPHER_WEP104)) {
				wpa_printf(MSG_ERROR, "Line %d: unsupported "
					   "pairwise cipher suite '%s'",
					   bss->rsn_pairwise, pos);
				errors++;
			}
#ifdef CONFIG_RSN_PREAUTH
		} else if (os_strcmp(buf, "rsn_preauth") == 0) {
			bss->rsn_preauth = atoi(pos);
		} else if (os_strcmp(buf, "rsn_preauth_interfaces") == 0) {
			bss->rsn_preauth_interfaces = os_strdup(pos);
#endif /* CONFIG_RSN_PREAUTH */
#ifdef CONFIG_PEERKEY
		} else if (os_strcmp(buf, "peerkey") == 0) {
			bss->peerkey = atoi(pos);
#endif /* CONFIG_PEERKEY */
#ifdef CONFIG_IEEE80211R
		} else if (os_strcmp(buf, "mobility_domain") == 0) {
			if (os_strlen(pos) != 2 * MOBILITY_DOMAIN_ID_LEN ||
			    hexstr2bin(pos, bss->mobility_domain,
				       MOBILITY_DOMAIN_ID_LEN) != 0) {
				wpa_printf(MSG_DEBUG, "Line %d: Invalid "
					   "mobility_domain '%s'", line, pos);
				errors++;
				continue;
			}
		} else if (os_strcmp(buf, "r1_key_holder") == 0) {
			if (os_strlen(pos) != 2 * FT_R1KH_ID_LEN ||
			    hexstr2bin(pos, bss->r1_key_holder,
				       FT_R1KH_ID_LEN) != 0) {
				wpa_printf(MSG_DEBUG, "Line %d: Invalid "
					   "r1_key_holder '%s'", line, pos);
				errors++;
				continue;
			}
		} else if (os_strcmp(buf, "r0_key_lifetime") == 0) {
			bss->r0_key_lifetime = atoi(pos);
		} else if (os_strcmp(buf, "reassociation_deadline") == 0) {
			bss->reassociation_deadline = atoi(pos);
		} else if (os_strcmp(buf, "r0kh") == 0) {
			if (add_r0kh(bss, pos) < 0) {
				wpa_printf(MSG_DEBUG, "Line %d: Invalid "
					   "r0kh '%s'", line, pos);
				errors++;
				continue;
			}
		} else if (os_strcmp(buf, "r1kh") == 0) {
			if (add_r1kh(bss, pos) < 0) {
				wpa_printf(MSG_DEBUG, "Line %d: Invalid "
					   "r1kh '%s'", line, pos);
				errors++;
				continue;
			}
		} else if (os_strcmp(buf, "pmk_r1_push") == 0) {
			bss->pmk_r1_push = atoi(pos);
		} else if (os_strcmp(buf, "ft_over_ds") == 0) {
			bss->ft_over_ds = atoi(pos);
#endif /* CONFIG_IEEE80211R */
#ifndef CONFIG_NO_CTRL_IFACE
		} else if (os_strcmp(buf, "ctrl_interface") == 0) {
			os_free(bss->ctrl_interface);
			bss->ctrl_interface = os_strdup(pos);
		} else if (os_strcmp(buf, "ctrl_interface_group") == 0) {
#ifndef CONFIG_NATIVE_WINDOWS
			struct group *grp;
			char *endp;
			const char *group = pos;

			grp = getgrnam(group);
			if (grp) {
				bss->ctrl_interface_gid = grp->gr_gid;
				bss->ctrl_interface_gid_set = 1;
				wpa_printf(MSG_DEBUG, "ctrl_interface_group=%d"
					   " (from group name '%s')",
					   bss->ctrl_interface_gid, group);
				continue;
			}

			/* Group name not found - try to parse this as gid */
			bss->ctrl_interface_gid = strtol(group, &endp, 10);
			if (*group == '\0' || *endp != '\0') {
				wpa_printf(MSG_DEBUG, "Line %d: Invalid group "
					   "'%s'", line, group);
				errors++;
				continue;
			}
			bss->ctrl_interface_gid_set = 1;
			wpa_printf(MSG_DEBUG, "ctrl_interface_group=%d",
				   bss->ctrl_interface_gid);
#endif /* CONFIG_NATIVE_WINDOWS */
#endif /* CONFIG_NO_CTRL_IFACE */
#ifdef RADIUS_SERVER
		} else if (os_strcmp(buf, "radius_server_clients") == 0) {
			os_free(bss->radius_server_clients);
			bss->radius_server_clients = os_strdup(pos);
		} else if (os_strcmp(buf, "radius_server_auth_port") == 0) {
			bss->radius_server_auth_port = atoi(pos);
		} else if (os_strcmp(buf, "radius_server_ipv6") == 0) {
			bss->radius_server_ipv6 = atoi(pos);
#endif /* RADIUS_SERVER */
		} else if (os_strcmp(buf, "test_socket") == 0) {
			os_free(bss->test_socket);
			bss->test_socket = os_strdup(pos);
		} else if (os_strcmp(buf, "use_pae_group_addr") == 0) {
			bss->use_pae_group_addr = atoi(pos);
		} else if (os_strcmp(buf, "hw_mode") == 0) {
			if (os_strcmp(pos, "a") == 0)
				conf->hw_mode = HOSTAPD_MODE_IEEE80211A;
			else if (os_strcmp(pos, "b") == 0)
				conf->hw_mode = HOSTAPD_MODE_IEEE80211B;
			else if (os_strcmp(pos, "g") == 0)
				conf->hw_mode = HOSTAPD_MODE_IEEE80211G;
			else {
				wpa_printf(MSG_ERROR, "Line %d: unknown "
					   "hw_mode '%s'", line, pos);
				errors++;
			}
		} else if (os_strcmp(buf, "channel") == 0) {
			conf->channel = atoi(pos);
		} else if (os_strcmp(buf, "beacon_int") == 0) {
			int val = atoi(pos);
			/* MIB defines range as 1..65535, but very small values
			 * cause problems with the current implementation.
			 * Since it is unlikely that this small numbers are
			 * useful in real life scenarios, do not allow beacon
			 * period to be set below 15 TU. */
			if (val < 15 || val > 65535) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "beacon_int %d (expected "
					   "15..65535)", line, val);
				errors++;
			} else
				conf->beacon_int = val;
		} else if (os_strcmp(buf, "dtim_period") == 0) {
			bss->dtim_period = atoi(pos);
			if (bss->dtim_period < 1 || bss->dtim_period > 255) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "dtim_period %d",
					   line, bss->dtim_period);
				errors++;
			}
		} else if (os_strcmp(buf, "rts_threshold") == 0) {
			conf->rts_threshold = atoi(pos);
			if (conf->rts_threshold < 0 ||
			    conf->rts_threshold > 2347) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "rts_threshold %d",
					   line, conf->rts_threshold);
				errors++;
			}
		} else if (os_strcmp(buf, "fragm_threshold") == 0) {
			conf->fragm_threshold = atoi(pos);
			if (conf->fragm_threshold < 256 ||
			    conf->fragm_threshold > 2346) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "fragm_threshold %d",
					   line, conf->fragm_threshold);
				errors++;
			}
		} else if (os_strcmp(buf, "send_probe_response") == 0) {
			int val = atoi(pos);
			if (val != 0 && val != 1) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "send_probe_response %d (expected "
					   "0 or 1)", line, val);
			} else
				conf->send_probe_response = val;
		} else if (os_strcmp(buf, "supported_rates") == 0) {
			if (hostapd_parse_rates(&conf->supported_rates, pos)) {
				wpa_printf(MSG_ERROR, "Line %d: invalid rate "
					   "list", line);
				errors++;
			}
		} else if (os_strcmp(buf, "basic_rates") == 0) {
			if (hostapd_parse_rates(&conf->basic_rates, pos)) {
				wpa_printf(MSG_ERROR, "Line %d: invalid rate "
					   "list", line);
				errors++;
			}
		} else if (os_strcmp(buf, "preamble") == 0) {
			if (atoi(pos))
				conf->preamble = SHORT_PREAMBLE;
			else
				conf->preamble = LONG_PREAMBLE;
		} else if (os_strcmp(buf, "ignore_broadcast_ssid") == 0) {
			bss->ignore_broadcast_ssid = atoi(pos);
		} else if (os_strcmp(buf, "wep_default_key") == 0) {
			bss->ssid.wep.idx = atoi(pos);
			if (bss->ssid.wep.idx > 3) {
				wpa_printf(MSG_ERROR, "Invalid "
					   "wep_default_key index %d",
					   bss->ssid.wep.idx);
				errors++;
			}
		} else if (os_strcmp(buf, "wep_key0") == 0 ||
			   os_strcmp(buf, "wep_key1") == 0 ||
			   os_strcmp(buf, "wep_key2") == 0 ||
			   os_strcmp(buf, "wep_key3") == 0) {
			if (hostapd_config_read_wep(&bss->ssid.wep,
						    buf[7] - '0', pos)) {
				wpa_printf(MSG_ERROR, "Line %d: invalid WEP "
					   "key '%s'", line, buf);
				errors++;
			}
#ifndef CONFIG_NO_VLAN
		} else if (os_strcmp(buf, "dynamic_vlan") == 0) {
			bss->ssid.dynamic_vlan = atoi(pos);
		} else if (os_strcmp(buf, "vlan_file") == 0) {
			if (hostapd_config_read_vlan_file(bss, pos)) {
				wpa_printf(MSG_ERROR, "Line %d: failed to "
					   "read VLAN file '%s'", line, pos);
				errors++;
			}
#ifdef CONFIG_FULL_DYNAMIC_VLAN
		} else if (os_strcmp(buf, "vlan_tagged_interface") == 0) {
			bss->ssid.vlan_tagged_interface = os_strdup(pos);
#endif /* CONFIG_FULL_DYNAMIC_VLAN */
#endif /* CONFIG_NO_VLAN */
		} else if (os_strcmp(buf, "ap_table_max_size") == 0) {
			conf->ap_table_max_size = atoi(pos);
		} else if (os_strcmp(buf, "ap_table_expiration_time") == 0) {
			conf->ap_table_expiration_time = atoi(pos);
		} else if (os_strncmp(buf, "tx_queue_", 9) == 0) {
			if (hostapd_config_tx_queue(conf, buf, pos)) {
				wpa_printf(MSG_ERROR, "Line %d: invalid TX "
					   "queue item", line);
				errors++;
			}
		} else if (os_strcmp(buf, "wme_enabled") == 0 ||
			   os_strcmp(buf, "wmm_enabled") == 0) {
			bss->wmm_enabled = atoi(pos);
		} else if (os_strcmp(buf, "uapsd_advertisement_enabled") == 0) {
			bss->wmm_uapsd = atoi(pos);
		} else if (os_strncmp(buf, "wme_ac_", 7) == 0 ||
			   os_strncmp(buf, "wmm_ac_", 7) == 0) {
			if (hostapd_config_wmm_ac(conf, buf, pos)) {
				wpa_printf(MSG_ERROR, "Line %d: invalid WMM "
					   "ac item", line);
				errors++;
			}
		} else if (os_strcmp(buf, "bss") == 0) {
			if (hostapd_config_bss(conf, pos)) {
				wpa_printf(MSG_ERROR, "Line %d: invalid bss "
					   "item", line);
				errors++;
			}
		} else if (os_strcmp(buf, "bssid") == 0) {
			if (hwaddr_aton(pos, bss->bssid)) {
				wpa_printf(MSG_ERROR, "Line %d: invalid bssid "
					   "item", line);
				errors++;
			}
#ifdef CONFIG_IEEE80211W
		} else if (os_strcmp(buf, "ieee80211w") == 0) {
			bss->ieee80211w = atoi(pos);
		} else if (os_strcmp(buf, "assoc_sa_query_max_timeout") == 0) {
			bss->assoc_sa_query_max_timeout = atoi(pos);
			if (bss->assoc_sa_query_max_timeout == 0) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "assoc_sa_query_max_timeout", line);
				errors++;
			}
		} else if (os_strcmp(buf, "assoc_sa_query_retry_timeout") == 0)
		{
			bss->assoc_sa_query_retry_timeout = atoi(pos);
			if (bss->assoc_sa_query_retry_timeout == 0) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "assoc_sa_query_retry_timeout",
					   line);
				errors++;
			}
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_IEEE80211N
		} else if (os_strcmp(buf, "ieee80211n") == 0) {
			conf->ieee80211n = atoi(pos);
		} else if (os_strcmp(buf, "ht_capab") == 0) {
			if (hostapd_config_ht_capab(conf, pos) < 0) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "ht_capab", line);
				errors++;
			}
		} else if (os_strcmp(buf, "require_ht") == 0) {
			conf->require_ht = atoi(pos);
#endif /* CONFIG_IEEE80211N */
		} else if (os_strcmp(buf, "max_listen_interval") == 0) {
			bss->max_listen_interval = atoi(pos);
		} else if (os_strcmp(buf, "okc") == 0) {
			bss->okc = atoi(pos);
#ifdef CONFIG_WPS
		} else if (os_strcmp(buf, "wps_state") == 0) {
			bss->wps_state = atoi(pos);
			if (bss->wps_state < 0 || bss->wps_state > 2) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "wps_state", line);
				errors++;
			}
		} else if (os_strcmp(buf, "ap_setup_locked") == 0) {
			bss->ap_setup_locked = atoi(pos);
		} else if (os_strcmp(buf, "uuid") == 0) {
			if (uuid_str2bin(pos, bss->uuid)) {
				wpa_printf(MSG_ERROR, "Line %d: invalid UUID",
					   line);
				errors++;
			}
		} else if (os_strcmp(buf, "wps_pin_requests") == 0) {
			os_free(bss->wps_pin_requests);
			bss->wps_pin_requests = os_strdup(pos);
		} else if (os_strcmp(buf, "device_name") == 0) {
			if (os_strlen(pos) > 32) {
				wpa_printf(MSG_ERROR, "Line %d: Too long "
					   "device_name", line);
				errors++;
			}
			os_free(bss->device_name);
			bss->device_name = os_strdup(pos);
		} else if (os_strcmp(buf, "manufacturer") == 0) {
			if (os_strlen(pos) > 64) {
				wpa_printf(MSG_ERROR, "Line %d: Too long "
					   "manufacturer", line);
				errors++;
			}
			os_free(bss->manufacturer);
			bss->manufacturer = os_strdup(pos);
		} else if (os_strcmp(buf, "model_name") == 0) {
			if (os_strlen(pos) > 32) {
				wpa_printf(MSG_ERROR, "Line %d: Too long "
					   "model_name", line);
				errors++;
			}
			os_free(bss->model_name);
			bss->model_name = os_strdup(pos);
		} else if (os_strcmp(buf, "model_number") == 0) {
			if (os_strlen(pos) > 32) {
				wpa_printf(MSG_ERROR, "Line %d: Too long "
					   "model_number", line);
				errors++;
			}
			os_free(bss->model_number);
			bss->model_number = os_strdup(pos);
		} else if (os_strcmp(buf, "serial_number") == 0) {
			if (os_strlen(pos) > 32) {
				wpa_printf(MSG_ERROR, "Line %d: Too long "
					   "serial_number", line);
				errors++;
			}
			os_free(bss->serial_number);
			bss->serial_number = os_strdup(pos);
		} else if (os_strcmp(buf, "device_type") == 0) {
			if (wps_dev_type_str2bin(pos, bss->device_type))
				errors++;
		} else if (os_strcmp(buf, "config_methods") == 0) {
			os_free(bss->config_methods);
			bss->config_methods = os_strdup(pos);
		} else if (os_strcmp(buf, "os_version") == 0) {
			if (hexstr2bin(pos, bss->os_version, 4)) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "os_version", line);
				errors++;
			}
		} else if (os_strcmp(buf, "ap_pin") == 0) {
			os_free(bss->ap_pin);
			bss->ap_pin = os_strdup(pos);
		} else if (os_strcmp(buf, "skip_cred_build") == 0) {
			bss->skip_cred_build = atoi(pos);
		} else if (os_strcmp(buf, "extra_cred") == 0) {
			os_free(bss->extra_cred);
			bss->extra_cred =
				(u8 *) os_readfile(pos, &bss->extra_cred_len);
			if (bss->extra_cred == NULL) {
				wpa_printf(MSG_ERROR, "Line %d: could not "
					   "read Credentials from '%s'",
					   line, pos);
				errors++;
			}
		} else if (os_strcmp(buf, "wps_cred_processing") == 0) {
			bss->wps_cred_processing = atoi(pos);
		} else if (os_strcmp(buf, "ap_settings") == 0) {
			os_free(bss->ap_settings);
			bss->ap_settings =
				(u8 *) os_readfile(pos, &bss->ap_settings_len);
			if (bss->ap_settings == NULL) {
				wpa_printf(MSG_ERROR, "Line %d: could not "
					   "read AP Settings from '%s'",
					   line, pos);
				errors++;
			}
		} else if (os_strcmp(buf, "upnp_iface") == 0) {
			bss->upnp_iface = os_strdup(pos);
		} else if (os_strcmp(buf, "friendly_name") == 0) {
			os_free(bss->friendly_name);
			bss->friendly_name = os_strdup(pos);
		} else if (os_strcmp(buf, "manufacturer_url") == 0) {
			os_free(bss->manufacturer_url);
			bss->manufacturer_url = os_strdup(pos);
		} else if (os_strcmp(buf, "model_description") == 0) {
			os_free(bss->model_description);
			bss->model_description = os_strdup(pos);
		} else if (os_strcmp(buf, "model_url") == 0) {
			os_free(bss->model_url);
			bss->model_url = os_strdup(pos);
		} else if (os_strcmp(buf, "upc") == 0) {
			os_free(bss->upc);
			bss->upc = os_strdup(pos);
#endif /* CONFIG_WPS */
#ifdef CONFIG_P2P_MANAGER
		} else if (os_strcmp(buf, "manage_p2p") == 0) {
			int manage = atoi(pos);
			if (manage)
				bss->p2p |= P2P_MANAGE;
			else
				bss->p2p &= ~P2P_MANAGE;
		} else if (os_strcmp(buf, "allow_cross_connection") == 0) {
			if (atoi(pos))
				bss->p2p |= P2P_ALLOW_CROSS_CONNECTION;
			else
				bss->p2p &= ~P2P_ALLOW_CROSS_CONNECTION;
#endif /* CONFIG_P2P_MANAGER */
		} else if (os_strcmp(buf, "disassoc_low_ack") == 0) {
			bss->disassoc_low_ack = atoi(pos);
		} else if (os_strcmp(buf, "tdls_prohibit") == 0) {
			int val = atoi(pos);
			if (val)
				bss->tdls |= TDLS_PROHIBIT;
			else
				bss->tdls &= ~TDLS_PROHIBIT;
		} else if (os_strcmp(buf, "tdls_prohibit_chan_switch") == 0) {
			int val = atoi(pos);
			if (val)
				bss->tdls |= TDLS_PROHIBIT_CHAN_SWITCH;
			else
				bss->tdls &= ~TDLS_PROHIBIT_CHAN_SWITCH;
#ifdef CONFIG_RSN_TESTING
		} else if (os_strcmp(buf, "rsn_testing") == 0) {
			extern int rsn_testing;
			rsn_testing = atoi(pos);
#endif /* CONFIG_RSN_TESTING */
		} else {
			wpa_printf(MSG_ERROR, "Line %d: unknown configuration "
				   "item '%s'", line, buf);
			errors++;
		}
	}

	fclose(f);

	for (i = 0; i < conf->num_bss; i++) {
		bss = &conf->bss[i];

		if (bss->individual_wep_key_len == 0) {
			/* individual keys are not use; can use key idx0 for
			 * broadcast keys */
			bss->broadcast_key_idx_min = 0;
		}

		/* Select group cipher based on the enabled pairwise cipher
		 * suites */
		pairwise = 0;
		if (bss->wpa & 1)
			pairwise |= bss->wpa_pairwise;
		if (bss->wpa & 2) {
			if (bss->rsn_pairwise == 0)
				bss->rsn_pairwise = bss->wpa_pairwise;
			pairwise |= bss->rsn_pairwise;
		}
		if (pairwise & WPA_CIPHER_TKIP)
			bss->wpa_group = WPA_CIPHER_TKIP;
		else
			bss->wpa_group = WPA_CIPHER_CCMP;

		bss->radius->auth_server = bss->radius->auth_servers;
		bss->radius->acct_server = bss->radius->acct_servers;

		if (bss->wpa && bss->ieee802_1x) {
			bss->ssid.security_policy = SECURITY_WPA;
		} else if (bss->wpa) {
			bss->ssid.security_policy = SECURITY_WPA_PSK;
		} else if (bss->ieee802_1x) {
			bss->ssid.security_policy = SECURITY_IEEE_802_1X;
			bss->ssid.wep.default_len = bss->default_wep_key_len;
		} else if (bss->ssid.wep.keys_set)
			bss->ssid.security_policy = SECURITY_STATIC_WEP;
		else
			bss->ssid.security_policy = SECURITY_PLAINTEXT;
	}

	if (hostapd_config_check(conf))
		errors++;

#ifndef WPA_IGNORE_CONFIG_ERRORS
	if (errors) {
		wpa_printf(MSG_ERROR, "%d errors found in configuration file "
			   "'%s'", errors, fname);
		hostapd_config_free(conf);
		conf = NULL;
	}
#endif /* WPA_IGNORE_CONFIG_ERRORS */

	return conf;
}
