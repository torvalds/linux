/*
 * hostapd / Configuration file parser
 * Copyright (c) 2003-2018, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#ifndef CONFIG_NATIVE_WINDOWS
#include <grp.h>
#endif /* CONFIG_NATIVE_WINDOWS */

#include "utils/common.h"
#include "utils/uuid.h"
#include "common/ieee802_11_defs.h"
#include "crypto/sha256.h"
#include "crypto/tls.h"
#include "drivers/driver.h"
#include "eap_server/eap.h"
#include "radius/radius_client.h"
#include "ap/wpa_auth.h"
#include "ap/ap_config.h"
#include "config_file.h"


#ifndef CONFIG_NO_RADIUS
#ifdef EAP_SERVER
static struct hostapd_radius_attr *
hostapd_parse_radius_attr(const char *value);
#endif /* EAP_SERVER */
#endif /* CONFIG_NO_RADIUS */


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

		vlan = os_zalloc(sizeof(*vlan));
		if (vlan == NULL) {
			wpa_printf(MSG_ERROR, "Out of memory while reading "
				   "VLAN interfaces from '%s'", fname);
			fclose(f);
			return -1;
		}

		vlan->vlan_id = vlan_id;
		vlan->vlan_desc.untagged = vlan_id;
		vlan->vlan_desc.notempty = !!vlan_id;
		os_strlcpy(vlan->ifname, pos, sizeof(vlan->ifname));
		vlan->next = bss->vlan;
		bss->vlan = vlan;
	}

	fclose(f);

	return 0;
}
#endif /* CONFIG_NO_VLAN */


int hostapd_acl_comp(const void *a, const void *b)
{
	const struct mac_acl_entry *aa = a;
	const struct mac_acl_entry *bb = b;
	return os_memcmp(aa->addr, bb->addr, sizeof(macaddr));
}


int hostapd_add_acl_maclist(struct mac_acl_entry **acl, int *num,
			    int vlan_id, const u8 *addr)
{
	struct mac_acl_entry *newacl;

	newacl = os_realloc_array(*acl, *num + 1, sizeof(**acl));
	if (!newacl) {
		wpa_printf(MSG_ERROR, "MAC list reallocation failed");
		return -1;
	}

	*acl = newacl;
	os_memcpy((*acl)[*num].addr, addr, ETH_ALEN);
	os_memset(&(*acl)[*num].vlan_id, 0, sizeof((*acl)[*num].vlan_id));
	(*acl)[*num].vlan_id.untagged = vlan_id;
	(*acl)[*num].vlan_id.notempty = !!vlan_id;
	(*num)++;

	return 0;
}


void hostapd_remove_acl_mac(struct mac_acl_entry **acl, int *num,
			    const u8 *addr)
{
	int i = 0;

	while (i < *num) {
		if (os_memcmp((*acl)[i].addr, addr, ETH_ALEN) == 0) {
			os_remove_in_array(*acl, *num, sizeof(**acl), i);
			(*num)--;
		} else {
			i++;
		}
	}
}


static int hostapd_config_read_maclist(const char *fname,
				       struct mac_acl_entry **acl, int *num)
{
	FILE *f;
	char buf[128], *pos;
	int line = 0;
	u8 addr[ETH_ALEN];
	int vlan_id;

	f = fopen(fname, "r");
	if (!f) {
		wpa_printf(MSG_ERROR, "MAC list file '%s' not found.", fname);
		return -1;
	}

	while (fgets(buf, sizeof(buf), f)) {
		int rem = 0;

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
		pos = buf;
		if (buf[0] == '-') {
			rem = 1;
			pos++;
		}

		if (hwaddr_aton(pos, addr)) {
			wpa_printf(MSG_ERROR, "Invalid MAC address '%s' at "
				   "line %d in '%s'", pos, line, fname);
			fclose(f);
			return -1;
		}

		if (rem) {
			hostapd_remove_acl_mac(acl, num, addr);
			continue;
		}
		vlan_id = 0;
		pos = buf;
		while (*pos != '\0' && *pos != ' ' && *pos != '\t')
			pos++;
		while (*pos == ' ' || *pos == '\t')
			pos++;
		if (*pos != '\0')
			vlan_id = atoi(pos);

		if (hostapd_add_acl_maclist(acl, num, vlan_id, addr) < 0) {
			fclose(f);
			return -1;
		}
	}

	fclose(f);

	if (*acl)
		qsort(*acl, *num, sizeof(**acl), hostapd_acl_comp);

	return 0;
}


#ifdef EAP_SERVER

static int hostapd_config_eap_user_salted(struct hostapd_eap_user *user,
					  const char *hash, size_t len,
					  char **pos, int line,
					  const char *fname)
{
	char *pos2 = *pos;

	while (*pos2 != '\0' && *pos2 != ' ' && *pos2 != '\t' && *pos2 != '#')
		pos2++;

	if (pos2 - *pos < (int) (2 * (len + 1))) { /* at least 1 byte of salt */
		wpa_printf(MSG_ERROR,
			   "Invalid salted %s hash on line %d in '%s'",
			   hash, line, fname);
		return -1;
	}

	user->password = os_malloc(len);
	if (!user->password) {
		wpa_printf(MSG_ERROR,
			   "Failed to allocate memory for salted %s hash",
			   hash);
		return -1;
	}

	if (hexstr2bin(*pos, user->password, len) < 0) {
		wpa_printf(MSG_ERROR,
			   "Invalid salted password on line %d in '%s'",
			   line, fname);
		return -1;
	}
	user->password_len = len;
	*pos += 2 * len;

	user->salt_len = (pos2 - *pos) / 2;
	user->salt = os_malloc(user->salt_len);
	if (!user->salt) {
		wpa_printf(MSG_ERROR,
			   "Failed to allocate memory for salted %s hash",
			   hash);
		return -1;
	}

	if (hexstr2bin(*pos, user->salt, user->salt_len) < 0) {
		wpa_printf(MSG_ERROR,
			   "Invalid salt for password on line %d in '%s'",
			   line, fname);
		return -1;
	}

	*pos = pos2;
	return 0;
}


static int hostapd_config_read_eap_user(const char *fname,
					struct hostapd_bss_config *conf)
{
	FILE *f;
	char buf[512], *pos, *start, *pos2;
	int line = 0, ret = 0, num_methods;
	struct hostapd_eap_user *user = NULL, *tail = NULL, *new_user = NULL;

	if (os_strncmp(fname, "sqlite:", 7) == 0) {
#ifdef CONFIG_SQLITE
		os_free(conf->eap_user_sqlite);
		conf->eap_user_sqlite = os_strdup(fname + 7);
		return 0;
#else /* CONFIG_SQLITE */
		wpa_printf(MSG_ERROR,
			   "EAP user file in SQLite DB, but CONFIG_SQLITE was not enabled in the build.");
		return -1;
#endif /* CONFIG_SQLITE */
	}

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

#ifndef CONFIG_NO_RADIUS
		if (user && os_strncmp(buf, "radius_accept_attr=", 19) == 0) {
			struct hostapd_radius_attr *attr, *a;
			attr = hostapd_parse_radius_attr(buf + 19);
			if (attr == NULL) {
				wpa_printf(MSG_ERROR, "Invalid radius_auth_req_attr: %s",
					   buf + 19);
				user = NULL; /* already in the BSS list */
				goto failed;
			}
			if (user->accept_attr == NULL) {
				user->accept_attr = attr;
			} else {
				a = user->accept_attr;
				while (a->next)
					a = a->next;
				a->next = attr;
			}
			continue;
		}
#endif /* CONFIG_NO_RADIUS */

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

			user->identity = os_memdup(start, pos - start);
			if (user->identity == NULL) {
				wpa_printf(MSG_ERROR, "Failed to allocate "
					   "memory for EAP identity");
				goto failed;
			}
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
				if (os_strcmp(start, "MACACL") == 0) {
					user->macacl = 1;
					goto skip_eap;
				}
				wpa_printf(MSG_ERROR, "Unsupported EAP type "
					   "'%s' on line %d in '%s'",
					   start, line, fname);
				goto failed;
			}

			num_methods++;
			if (num_methods >= EAP_MAX_METHODS)
				break;
		skip_eap:
			if (pos3 == NULL)
				break;
			start = pos3;
		}
		if (num_methods == 0 && user->ttls_auth == 0 && !user->macacl) {
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

			user->password = os_memdup(start, pos - start);
			if (user->password == NULL) {
				wpa_printf(MSG_ERROR, "Failed to allocate "
					   "memory for EAP password");
				goto failed;
			}
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
		} else if (os_strncmp(pos, "ssha1:", 6) == 0) {
			pos += 6;
			if (hostapd_config_eap_user_salted(user, "sha1", 20,
							   &pos,
							   line, fname) < 0)
				goto failed;
		} else if (os_strncmp(pos, "ssha256:", 8) == 0) {
			pos += 8;
			if (hostapd_config_eap_user_salted(user, "sha256", 32,
							   &pos,
							   line, fname) < 0)
				goto failed;
		} else if (os_strncmp(pos, "ssha512:", 8) == 0) {
			pos += 8;
			if (hostapd_config_eap_user_salted(user, "sha512", 64,
							   &pos,
							   line, fname) < 0)
				goto failed;
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
			tail = new_user = user;
		} else {
			tail->next = user;
			tail = user;
		}
		continue;

	failed:
		if (user)
			hostapd_config_free_eap_user(user);
		ret = -1;
		break;
	}

	fclose(f);

	if (ret == 0) {
		hostapd_config_free_eap_users(conf->eap_user);
		conf->eap_user = new_user;
	} else {
		hostapd_config_free_eap_users(new_user);
	}

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

	nserv = os_realloc_array(*server, *num_server + 1, sizeof(*nserv));
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


static struct hostapd_radius_attr *
hostapd_parse_radius_attr(const char *value)
{
	const char *pos;
	char syntax;
	struct hostapd_radius_attr *attr;
	size_t len;

	attr = os_zalloc(sizeof(*attr));
	if (attr == NULL)
		return NULL;

	attr->type = atoi(value);

	pos = os_strchr(value, ':');
	if (pos == NULL) {
		attr->val = wpabuf_alloc(1);
		if (attr->val == NULL) {
			os_free(attr);
			return NULL;
		}
		wpabuf_put_u8(attr->val, 0);
		return attr;
	}

	pos++;
	if (pos[0] == '\0' || pos[1] != ':') {
		os_free(attr);
		return NULL;
	}
	syntax = *pos++;
	pos++;

	switch (syntax) {
	case 's':
		attr->val = wpabuf_alloc_copy(pos, os_strlen(pos));
		break;
	case 'x':
		len = os_strlen(pos);
		if (len & 1)
			break;
		len /= 2;
		attr->val = wpabuf_alloc(len);
		if (attr->val == NULL)
			break;
		if (hexstr2bin(pos, wpabuf_put(attr->val, len), len) < 0) {
			wpabuf_free(attr->val);
			os_free(attr);
			return NULL;
		}
		break;
	case 'd':
		attr->val = wpabuf_alloc(4);
		if (attr->val)
			wpabuf_put_be32(attr->val, atoi(pos));
		break;
	default:
		os_free(attr);
		return NULL;
	}

	if (attr->val == NULL) {
		os_free(attr);
		return NULL;
	}

	return attr;
}


static int hostapd_parse_das_client(struct hostapd_bss_config *bss, char *val)
{
	char *secret;

	secret = os_strchr(val, ' ');
	if (secret == NULL)
		return -1;

	*secret++ = '\0';

	if (hostapd_parse_ip_addr(val, &bss->radius_das_client_addr))
		return -1;

	os_free(bss->radius_das_shared_secret);
	bss->radius_das_shared_secret = (u8 *) os_strdup(secret);
	if (bss->radius_das_shared_secret == NULL)
		return -1;
	bss->radius_das_shared_secret_len = os_strlen(secret);

	return 0;
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
#ifdef CONFIG_IEEE80211R_AP
		else if (os_strcmp(start, "FT-PSK") == 0)
			val |= WPA_KEY_MGMT_FT_PSK;
		else if (os_strcmp(start, "FT-EAP") == 0)
			val |= WPA_KEY_MGMT_FT_IEEE8021X;
#ifdef CONFIG_SHA384
		else if (os_strcmp(start, "FT-EAP-SHA384") == 0)
			val |= WPA_KEY_MGMT_FT_IEEE8021X_SHA384;
#endif /* CONFIG_SHA384 */
#endif /* CONFIG_IEEE80211R_AP */
#ifdef CONFIG_IEEE80211W
		else if (os_strcmp(start, "WPA-PSK-SHA256") == 0)
			val |= WPA_KEY_MGMT_PSK_SHA256;
		else if (os_strcmp(start, "WPA-EAP-SHA256") == 0)
			val |= WPA_KEY_MGMT_IEEE8021X_SHA256;
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_SAE
		else if (os_strcmp(start, "SAE") == 0)
			val |= WPA_KEY_MGMT_SAE;
		else if (os_strcmp(start, "FT-SAE") == 0)
			val |= WPA_KEY_MGMT_FT_SAE;
#endif /* CONFIG_SAE */
#ifdef CONFIG_SUITEB
		else if (os_strcmp(start, "WPA-EAP-SUITE-B") == 0)
			val |= WPA_KEY_MGMT_IEEE8021X_SUITE_B;
#endif /* CONFIG_SUITEB */
#ifdef CONFIG_SUITEB192
		else if (os_strcmp(start, "WPA-EAP-SUITE-B-192") == 0)
			val |= WPA_KEY_MGMT_IEEE8021X_SUITE_B_192;
#endif /* CONFIG_SUITEB192 */
#ifdef CONFIG_FILS
		else if (os_strcmp(start, "FILS-SHA256") == 0)
			val |= WPA_KEY_MGMT_FILS_SHA256;
		else if (os_strcmp(start, "FILS-SHA384") == 0)
			val |= WPA_KEY_MGMT_FILS_SHA384;
#ifdef CONFIG_IEEE80211R_AP
		else if (os_strcmp(start, "FT-FILS-SHA256") == 0)
			val |= WPA_KEY_MGMT_FT_FILS_SHA256;
		else if (os_strcmp(start, "FT-FILS-SHA384") == 0)
			val |= WPA_KEY_MGMT_FT_FILS_SHA384;
#endif /* CONFIG_IEEE80211R_AP */
#endif /* CONFIG_FILS */
#ifdef CONFIG_OWE
		else if (os_strcmp(start, "OWE") == 0)
			val |= WPA_KEY_MGMT_OWE;
#endif /* CONFIG_OWE */
#ifdef CONFIG_DPP
		else if (os_strcmp(start, "DPP") == 0)
			val |= WPA_KEY_MGMT_DPP;
#endif /* CONFIG_DPP */
#ifdef CONFIG_HS20
		else if (os_strcmp(start, "OSEN") == 0)
			val |= WPA_KEY_MGMT_OSEN;
#endif /* CONFIG_HS20 */
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
	int val = wpa_parse_cipher(value);
	if (val < 0) {
		wpa_printf(MSG_ERROR, "Line %d: invalid cipher '%s'.",
			   line, value);
		return -1;
	}
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

	if (keyidx < 0 || keyidx > 3)
		return -1;

	if (len == 0) {
		int i, set = 0;

		bin_clear_free(wep->key[keyidx], wep->len[keyidx]);
		wep->key[keyidx] = NULL;
		wep->len[keyidx] = 0;
		for (i = 0; i < NUM_WEP_KEYS; i++) {
			if (wep->key[i])
				set++;
		}
		if (!set)
			wep->keys_set = 0;
		return 0;
	}

	if (wep->key[keyidx] != NULL)
		return -1;

	if (val[0] == '"') {
		if (len < 2 || val[len - 1] != '"')
			return -1;
		len -= 2;
		wep->key[keyidx] = os_memdup(val + 1, len);
		if (wep->key[keyidx] == NULL)
			return -1;
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


static int hostapd_parse_chanlist(struct hostapd_config *conf, char *val)
{
	char *pos;

	/* for backwards compatibility, translate ' ' in conf str to ',' */
	pos = val;
	while (pos) {
		pos = os_strchr(pos, ' ');
		if (pos)
			*pos++ = ',';
	}
	if (freq_range_list_parse(&conf->acs_ch_list, val))
		return -1;

	return 0;
}


static int hostapd_parse_intlist(int **int_list, char *val)
{
	int *list;
	int count;
	char *pos, *end;

	os_free(*int_list);
	*int_list = NULL;

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

	*int_list = list;
	return 0;
}


static int hostapd_config_bss(struct hostapd_config *conf, const char *ifname)
{
	struct hostapd_bss_config **all, *bss;

	if (*ifname == '\0')
		return -1;

	all = os_realloc_array(conf->bss, conf->num_bss + 1,
			       sizeof(struct hostapd_bss_config *));
	if (all == NULL) {
		wpa_printf(MSG_ERROR, "Failed to allocate memory for "
			   "multi-BSS entry");
		return -1;
	}
	conf->bss = all;

	bss = os_zalloc(sizeof(*bss));
	if (bss == NULL)
		return -1;
	bss->radius = os_zalloc(sizeof(*bss->radius));
	if (bss->radius == NULL) {
		wpa_printf(MSG_ERROR, "Failed to allocate memory for "
			   "multi-BSS RADIUS data");
		os_free(bss);
		return -1;
	}

	conf->bss[conf->num_bss++] = bss;
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
		cw == 63 || cw == 127 || cw == 255 || cw == 511 || cw == 1023 ||
		cw == 2047 || cw == 4095 || cw == 8191 || cw == 16383 ||
		cw == 32767);
}


enum {
	IEEE80211_TX_QUEUE_DATA0 = 0, /* used for EDCA AC_VO data */
	IEEE80211_TX_QUEUE_DATA1 = 1, /* used for EDCA AC_VI data */
	IEEE80211_TX_QUEUE_DATA2 = 2, /* used for EDCA AC_BE data */
	IEEE80211_TX_QUEUE_DATA3 = 3 /* used for EDCA AC_BK data */
};

static int hostapd_config_tx_queue(struct hostapd_config *conf,
				   const char *name, const char *val)
{
	int num;
	const char *pos;
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


#ifdef CONFIG_IEEE80211R_AP

static int rkh_derive_key(const char *pos, u8 *key, size_t key_len)
{
	u8 oldkey[16];
	int ret;

	if (!hexstr2bin(pos, key, key_len))
		return 0;

	/* Try to use old short key for backwards compatibility */
	if (hexstr2bin(pos, oldkey, sizeof(oldkey)))
		return -1;

	ret = hmac_sha256_kdf(oldkey, sizeof(oldkey), "FT OLDKEY", NULL, 0,
			      key, key_len);
	os_memset(oldkey, 0, sizeof(oldkey));
	return ret;
}


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
	if (rkh_derive_key(pos, r0kh->key, sizeof(r0kh->key)) < 0) {
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
	if (rkh_derive_key(pos, r1kh->key, sizeof(r1kh->key)) < 0) {
		wpa_printf(MSG_ERROR, "Invalid R1KH key: '%s'", pos);
		os_free(r1kh);
		return -1;
	}

	r1kh->next = bss->r1kh_list;
	bss->r1kh_list = r1kh;

	return 0;
}
#endif /* CONFIG_IEEE80211R_AP */


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
	if (os_strstr(capab, "[HT40+]") && os_strstr(capab, "[HT40-]")) {
		conf->ht_capab |= HT_CAP_INFO_SUPP_CHANNEL_WIDTH_SET;
		conf->ht40_plus_minus_allowed = 1;
	}
	if (!os_strstr(capab, "[HT40+]") && !os_strstr(capab, "[HT40-]"))
		conf->secondary_channel = 0;
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
	if (os_strstr(capab, "[40-INTOLERANT]"))
		conf->ht_capab |= HT_CAP_INFO_40MHZ_INTOLERANT;
	if (os_strstr(capab, "[LSIG-TXOP-PROT]"))
		conf->ht_capab |= HT_CAP_INFO_LSIG_TXOP_PROTECT_SUPPORT;

	return 0;
}
#endif /* CONFIG_IEEE80211N */


#ifdef CONFIG_IEEE80211AC
static int hostapd_config_vht_capab(struct hostapd_config *conf,
				    const char *capab)
{
	if (os_strstr(capab, "[MAX-MPDU-7991]"))
		conf->vht_capab |= VHT_CAP_MAX_MPDU_LENGTH_7991;
	if (os_strstr(capab, "[MAX-MPDU-11454]"))
		conf->vht_capab |= VHT_CAP_MAX_MPDU_LENGTH_11454;
	if (os_strstr(capab, "[VHT160]"))
		conf->vht_capab |= VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
	if (os_strstr(capab, "[VHT160-80PLUS80]"))
		conf->vht_capab |= VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ;
	if (os_strstr(capab, "[RXLDPC]"))
		conf->vht_capab |= VHT_CAP_RXLDPC;
	if (os_strstr(capab, "[SHORT-GI-80]"))
		conf->vht_capab |= VHT_CAP_SHORT_GI_80;
	if (os_strstr(capab, "[SHORT-GI-160]"))
		conf->vht_capab |= VHT_CAP_SHORT_GI_160;
	if (os_strstr(capab, "[TX-STBC-2BY1]"))
		conf->vht_capab |= VHT_CAP_TXSTBC;
	if (os_strstr(capab, "[RX-STBC-1]"))
		conf->vht_capab |= VHT_CAP_RXSTBC_1;
	if (os_strstr(capab, "[RX-STBC-12]"))
		conf->vht_capab |= VHT_CAP_RXSTBC_2;
	if (os_strstr(capab, "[RX-STBC-123]"))
		conf->vht_capab |= VHT_CAP_RXSTBC_3;
	if (os_strstr(capab, "[RX-STBC-1234]"))
		conf->vht_capab |= VHT_CAP_RXSTBC_4;
	if (os_strstr(capab, "[SU-BEAMFORMER]"))
		conf->vht_capab |= VHT_CAP_SU_BEAMFORMER_CAPABLE;
	if (os_strstr(capab, "[SU-BEAMFORMEE]"))
		conf->vht_capab |= VHT_CAP_SU_BEAMFORMEE_CAPABLE;
	if (os_strstr(capab, "[BF-ANTENNA-2]") &&
	    (conf->vht_capab & VHT_CAP_SU_BEAMFORMEE_CAPABLE))
		conf->vht_capab |= (1 << VHT_CAP_BEAMFORMEE_STS_OFFSET);
	if (os_strstr(capab, "[BF-ANTENNA-3]") &&
	    (conf->vht_capab & VHT_CAP_SU_BEAMFORMEE_CAPABLE))
		conf->vht_capab |= (2 << VHT_CAP_BEAMFORMEE_STS_OFFSET);
	if (os_strstr(capab, "[BF-ANTENNA-4]") &&
	    (conf->vht_capab & VHT_CAP_SU_BEAMFORMEE_CAPABLE))
		conf->vht_capab |= (3 << VHT_CAP_BEAMFORMEE_STS_OFFSET);
	if (os_strstr(capab, "[SOUNDING-DIMENSION-2]") &&
	    (conf->vht_capab & VHT_CAP_SU_BEAMFORMER_CAPABLE))
		conf->vht_capab |= (1 << VHT_CAP_SOUNDING_DIMENSION_OFFSET);
	if (os_strstr(capab, "[SOUNDING-DIMENSION-3]") &&
	    (conf->vht_capab & VHT_CAP_SU_BEAMFORMER_CAPABLE))
		conf->vht_capab |= (2 << VHT_CAP_SOUNDING_DIMENSION_OFFSET);
	if (os_strstr(capab, "[SOUNDING-DIMENSION-4]") &&
	    (conf->vht_capab & VHT_CAP_SU_BEAMFORMER_CAPABLE))
		conf->vht_capab |= (3 << VHT_CAP_SOUNDING_DIMENSION_OFFSET);
	if (os_strstr(capab, "[MU-BEAMFORMER]"))
		conf->vht_capab |= VHT_CAP_MU_BEAMFORMER_CAPABLE;
	if (os_strstr(capab, "[VHT-TXOP-PS]"))
		conf->vht_capab |= VHT_CAP_VHT_TXOP_PS;
	if (os_strstr(capab, "[HTC-VHT]"))
		conf->vht_capab |= VHT_CAP_HTC_VHT;
	if (os_strstr(capab, "[MAX-A-MPDU-LEN-EXP7]"))
		conf->vht_capab |= VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MAX;
	else if (os_strstr(capab, "[MAX-A-MPDU-LEN-EXP6]"))
		conf->vht_capab |= VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_6;
	else if (os_strstr(capab, "[MAX-A-MPDU-LEN-EXP5]"))
		conf->vht_capab |= VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_5;
	else if (os_strstr(capab, "[MAX-A-MPDU-LEN-EXP4]"))
		conf->vht_capab |= VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_4;
	else if (os_strstr(capab, "[MAX-A-MPDU-LEN-EXP3]"))
		conf->vht_capab |= VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_3;
	else if (os_strstr(capab, "[MAX-A-MPDU-LEN-EXP2]"))
		conf->vht_capab |= VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_2;
	else if (os_strstr(capab, "[MAX-A-MPDU-LEN-EXP1]"))
		conf->vht_capab |= VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_1;
	if (os_strstr(capab, "[VHT-LINK-ADAPT2]") &&
	    (conf->vht_capab & VHT_CAP_HTC_VHT))
		conf->vht_capab |= VHT_CAP_VHT_LINK_ADAPTATION_VHT_UNSOL_MFB;
	if (os_strstr(capab, "[VHT-LINK-ADAPT3]") &&
	    (conf->vht_capab & VHT_CAP_HTC_VHT))
		conf->vht_capab |= VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB;
	if (os_strstr(capab, "[RX-ANTENNA-PATTERN]"))
		conf->vht_capab |= VHT_CAP_RX_ANTENNA_PATTERN;
	if (os_strstr(capab, "[TX-ANTENNA-PATTERN]"))
		conf->vht_capab |= VHT_CAP_TX_ANTENNA_PATTERN;
	return 0;
}
#endif /* CONFIG_IEEE80211AC */


#ifdef CONFIG_INTERWORKING
static int parse_roaming_consortium(struct hostapd_bss_config *bss, char *pos,
				    int line)
{
	size_t len = os_strlen(pos);
	u8 oi[MAX_ROAMING_CONSORTIUM_LEN];

	struct hostapd_roaming_consortium *rc;

	if ((len & 1) || len < 2 * 3 || len / 2 > MAX_ROAMING_CONSORTIUM_LEN ||
	    hexstr2bin(pos, oi, len / 2)) {
		wpa_printf(MSG_ERROR, "Line %d: invalid roaming_consortium "
			   "'%s'", line, pos);
		return -1;
	}
	len /= 2;

	rc = os_realloc_array(bss->roaming_consortium,
			      bss->roaming_consortium_count + 1,
			      sizeof(struct hostapd_roaming_consortium));
	if (rc == NULL)
		return -1;

	os_memcpy(rc[bss->roaming_consortium_count].oi, oi, len);
	rc[bss->roaming_consortium_count].len = len;

	bss->roaming_consortium = rc;
	bss->roaming_consortium_count++;

	return 0;
}


static int parse_lang_string(struct hostapd_lang_string **array,
			     unsigned int *count, char *pos)
{
	char *sep, *str = NULL;
	size_t clen, nlen, slen;
	struct hostapd_lang_string *ls;
	int ret = -1;

	if (*pos == '"' || (*pos == 'P' && pos[1] == '"')) {
		str = wpa_config_parse_string(pos, &slen);
		if (!str)
			return -1;
		pos = str;
	}

	sep = os_strchr(pos, ':');
	if (sep == NULL)
		goto fail;
	*sep++ = '\0';

	clen = os_strlen(pos);
	if (clen < 2 || clen > sizeof(ls->lang))
		goto fail;
	nlen = os_strlen(sep);
	if (nlen > 252)
		goto fail;

	ls = os_realloc_array(*array, *count + 1,
			      sizeof(struct hostapd_lang_string));
	if (ls == NULL)
		goto fail;

	*array = ls;
	ls = &(*array)[*count];
	(*count)++;

	os_memset(ls->lang, 0, sizeof(ls->lang));
	os_memcpy(ls->lang, pos, clen);
	ls->name_len = nlen;
	os_memcpy(ls->name, sep, nlen);

	ret = 0;
fail:
	os_free(str);
	return ret;
}


static int parse_venue_name(struct hostapd_bss_config *bss, char *pos,
			    int line)
{
	if (parse_lang_string(&bss->venue_name, &bss->venue_name_count, pos)) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid venue_name '%s'",
			   line, pos);
		return -1;
	}
	return 0;
}


static int parse_venue_url(struct hostapd_bss_config *bss, char *pos,
			    int line)
{
	char *sep;
	size_t nlen;
	struct hostapd_venue_url *url;
	int ret = -1;

	sep = os_strchr(pos, ':');
	if (!sep)
		goto fail;
	*sep++ = '\0';

	nlen = os_strlen(sep);
	if (nlen > 254)
		goto fail;

	url = os_realloc_array(bss->venue_url, bss->venue_url_count + 1,
			       sizeof(struct hostapd_venue_url));
	if (!url)
		goto fail;

	bss->venue_url = url;
	url = &bss->venue_url[bss->venue_url_count++];

	url->venue_number = atoi(pos);
	url->url_len = nlen;
	os_memcpy(url->url, sep, nlen);

	ret = 0;
fail:
	if (ret)
		wpa_printf(MSG_ERROR, "Line %d: Invalid venue_url '%s'",
			   line, pos);
	return ret;
}


static int parse_3gpp_cell_net(struct hostapd_bss_config *bss, char *buf,
			       int line)
{
	size_t count;
	char *pos;
	u8 *info = NULL, *ipos;

	/* format: <MCC1,MNC1>[;<MCC2,MNC2>][;...] */

	count = 1;
	for (pos = buf; *pos; pos++) {
		if ((*pos < '0' || *pos > '9') && *pos != ';' && *pos != ',')
			goto fail;
		if (*pos == ';')
			count++;
	}
	if (1 + count * 3 > 0x7f)
		goto fail;

	info = os_zalloc(2 + 3 + count * 3);
	if (info == NULL)
		return -1;

	ipos = info;
	*ipos++ = 0; /* GUD - Version 1 */
	*ipos++ = 3 + count * 3; /* User Data Header Length (UDHL) */
	*ipos++ = 0; /* PLMN List IEI */
	/* ext(b8) | Length of PLMN List value contents(b7..1) */
	*ipos++ = 1 + count * 3;
	*ipos++ = count; /* Number of PLMNs */

	pos = buf;
	while (pos && *pos) {
		char *mcc, *mnc;
		size_t mnc_len;

		mcc = pos;
		mnc = os_strchr(pos, ',');
		if (mnc == NULL)
			goto fail;
		*mnc++ = '\0';
		pos = os_strchr(mnc, ';');
		if (pos)
			*pos++ = '\0';

		mnc_len = os_strlen(mnc);
		if (os_strlen(mcc) != 3 || (mnc_len != 2 && mnc_len != 3))
			goto fail;

		/* BC coded MCC,MNC */
		/* MCC digit 2 | MCC digit 1 */
		*ipos++ = ((mcc[1] - '0') << 4) | (mcc[0] - '0');
		/* MNC digit 3 | MCC digit 3 */
		*ipos++ = (((mnc_len == 2) ? 0xf0 : ((mnc[2] - '0') << 4))) |
			(mcc[2] - '0');
		/* MNC digit 2 | MNC digit 1 */
		*ipos++ = ((mnc[1] - '0') << 4) | (mnc[0] - '0');
	}

	os_free(bss->anqp_3gpp_cell_net);
	bss->anqp_3gpp_cell_net = info;
	bss->anqp_3gpp_cell_net_len = 2 + 3 + 3 * count;
	wpa_hexdump(MSG_MSGDUMP, "3GPP Cellular Network information",
		    bss->anqp_3gpp_cell_net, bss->anqp_3gpp_cell_net_len);

	return 0;

fail:
	wpa_printf(MSG_ERROR, "Line %d: Invalid anqp_3gpp_cell_net: %s",
		   line, buf);
	os_free(info);
	return -1;
}


static int parse_nai_realm(struct hostapd_bss_config *bss, char *buf, int line)
{
	struct hostapd_nai_realm_data *realm;
	size_t i, j, len;
	int *offsets;
	char *pos, *end, *rpos;

	offsets = os_calloc(bss->nai_realm_count * MAX_NAI_REALMS,
			    sizeof(int));
	if (offsets == NULL)
		return -1;

	for (i = 0; i < bss->nai_realm_count; i++) {
		realm = &bss->nai_realm_data[i];
		for (j = 0; j < MAX_NAI_REALMS; j++) {
			offsets[i * MAX_NAI_REALMS + j] =
				realm->realm[j] ?
				realm->realm[j] - realm->realm_buf : -1;
		}
	}

	realm = os_realloc_array(bss->nai_realm_data, bss->nai_realm_count + 1,
				 sizeof(struct hostapd_nai_realm_data));
	if (realm == NULL) {
		os_free(offsets);
		return -1;
	}
	bss->nai_realm_data = realm;

	/* patch the pointers after realloc */
	for (i = 0; i < bss->nai_realm_count; i++) {
		realm = &bss->nai_realm_data[i];
		for (j = 0; j < MAX_NAI_REALMS; j++) {
			int offs = offsets[i * MAX_NAI_REALMS + j];
			if (offs >= 0)
				realm->realm[j] = realm->realm_buf + offs;
			else
				realm->realm[j] = NULL;
		}
	}
	os_free(offsets);

	realm = &bss->nai_realm_data[bss->nai_realm_count];
	os_memset(realm, 0, sizeof(*realm));

	pos = buf;
	realm->encoding = atoi(pos);
	pos = os_strchr(pos, ',');
	if (pos == NULL)
		goto fail;
	pos++;

	end = os_strchr(pos, ',');
	if (end) {
		len = end - pos;
		*end = '\0';
	} else {
		len = os_strlen(pos);
	}

	if (len > MAX_NAI_REALMLEN) {
		wpa_printf(MSG_ERROR, "Too long a realm string (%d > max %d "
			   "characters)", (int) len, MAX_NAI_REALMLEN);
		goto fail;
	}
	os_memcpy(realm->realm_buf, pos, len);

	if (end)
		pos = end + 1;
	else
		pos = NULL;

	while (pos && *pos) {
		struct hostapd_nai_realm_eap *eap;

		if (realm->eap_method_count >= MAX_NAI_EAP_METHODS) {
			wpa_printf(MSG_ERROR, "Too many EAP methods");
			goto fail;
		}

		eap = &realm->eap_method[realm->eap_method_count];
		realm->eap_method_count++;

		end = os_strchr(pos, ',');
		if (end == NULL)
			end = pos + os_strlen(pos);

		eap->eap_method = atoi(pos);
		for (;;) {
			pos = os_strchr(pos, '[');
			if (pos == NULL || pos > end)
				break;
			pos++;
			if (eap->num_auths >= MAX_NAI_AUTH_TYPES) {
				wpa_printf(MSG_ERROR, "Too many auth params");
				goto fail;
			}
			eap->auth_id[eap->num_auths] = atoi(pos);
			pos = os_strchr(pos, ':');
			if (pos == NULL || pos > end)
				goto fail;
			pos++;
			eap->auth_val[eap->num_auths] = atoi(pos);
			pos = os_strchr(pos, ']');
			if (pos == NULL || pos > end)
				goto fail;
			pos++;
			eap->num_auths++;
		}

		if (*end != ',')
			break;

		pos = end + 1;
	}

	/* Split realm list into null terminated realms */
	rpos = realm->realm_buf;
	i = 0;
	while (*rpos) {
		if (i >= MAX_NAI_REALMS) {
			wpa_printf(MSG_ERROR, "Too many realms");
			goto fail;
		}
		realm->realm[i++] = rpos;
		rpos = os_strchr(rpos, ';');
		if (rpos == NULL)
			break;
		*rpos++ = '\0';
	}

	bss->nai_realm_count++;

	return 0;

fail:
	wpa_printf(MSG_ERROR, "Line %d: invalid nai_realm '%s'", line, buf);
	return -1;
}


static int parse_anqp_elem(struct hostapd_bss_config *bss, char *buf, int line)
{
	char *delim;
	u16 infoid;
	size_t len;
	struct wpabuf *payload;
	struct anqp_element *elem;

	delim = os_strchr(buf, ':');
	if (!delim)
		return -1;
	delim++;
	infoid = atoi(buf);
	len = os_strlen(delim);
	if (len & 1)
		return -1;
	len /= 2;
	payload = wpabuf_alloc(len);
	if (!payload)
		return -1;
	if (hexstr2bin(delim, wpabuf_put(payload, len), len) < 0) {
		wpabuf_free(payload);
		return -1;
	}

	dl_list_for_each(elem, &bss->anqp_elem, struct anqp_element, list) {
		if (elem->infoid == infoid) {
			/* Update existing entry */
			wpabuf_free(elem->payload);
			elem->payload = payload;
			return 0;
		}
	}

	/* Add a new entry */
	elem = os_zalloc(sizeof(*elem));
	if (!elem) {
		wpabuf_free(payload);
		return -1;
	}
	elem->infoid = infoid;
	elem->payload = payload;
	dl_list_add(&bss->anqp_elem, &elem->list);

	return 0;
}


static int parse_qos_map_set(struct hostapd_bss_config *bss,
			     char *buf, int line)
{
	u8 qos_map_set[16 + 2 * 21], count = 0;
	char *pos = buf;
	int val;

	for (;;) {
		if (count == sizeof(qos_map_set)) {
			wpa_printf(MSG_ERROR, "Line %d: Too many qos_map_set "
				   "parameters '%s'", line, buf);
			return -1;
		}

		val = atoi(pos);
		if (val > 255 || val < 0) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid qos_map_set "
				   "'%s'", line, buf);
			return -1;
		}

		qos_map_set[count++] = val;
		pos = os_strchr(pos, ',');
		if (!pos)
			break;
		pos++;
	}

	if (count < 16 || count & 1) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid qos_map_set '%s'",
			   line, buf);
		return -1;
	}

	os_memcpy(bss->qos_map_set, qos_map_set, count);
	bss->qos_map_set_len = count;

	return 0;
}

#endif /* CONFIG_INTERWORKING */


#ifdef CONFIG_HS20
static int hs20_parse_conn_capab(struct hostapd_bss_config *bss, char *buf,
				 int line)
{
	u8 *conn_cap;
	char *pos;

	if (bss->hs20_connection_capability_len >= 0xfff0)
		return -1;

	conn_cap = os_realloc(bss->hs20_connection_capability,
			      bss->hs20_connection_capability_len + 4);
	if (conn_cap == NULL)
		return -1;

	bss->hs20_connection_capability = conn_cap;
	conn_cap += bss->hs20_connection_capability_len;
	pos = buf;
	conn_cap[0] = atoi(pos);
	pos = os_strchr(pos, ':');
	if (pos == NULL)
		return -1;
	pos++;
	WPA_PUT_LE16(conn_cap + 1, atoi(pos));
	pos = os_strchr(pos, ':');
	if (pos == NULL)
		return -1;
	pos++;
	conn_cap[3] = atoi(pos);
	bss->hs20_connection_capability_len += 4;

	return 0;
}


static int hs20_parse_wan_metrics(struct hostapd_bss_config *bss, char *buf,
				  int line)
{
	u8 *wan_metrics;
	char *pos;

	/* <WAN Info>:<DL Speed>:<UL Speed>:<DL Load>:<UL Load>:<LMD> */

	wan_metrics = os_zalloc(13);
	if (wan_metrics == NULL)
		return -1;

	pos = buf;
	/* WAN Info */
	if (hexstr2bin(pos, wan_metrics, 1) < 0)
		goto fail;
	pos += 2;
	if (*pos != ':')
		goto fail;
	pos++;

	/* Downlink Speed */
	WPA_PUT_LE32(wan_metrics + 1, atoi(pos));
	pos = os_strchr(pos, ':');
	if (pos == NULL)
		goto fail;
	pos++;

	/* Uplink Speed */
	WPA_PUT_LE32(wan_metrics + 5, atoi(pos));
	pos = os_strchr(pos, ':');
	if (pos == NULL)
		goto fail;
	pos++;

	/* Downlink Load */
	wan_metrics[9] = atoi(pos);
	pos = os_strchr(pos, ':');
	if (pos == NULL)
		goto fail;
	pos++;

	/* Uplink Load */
	wan_metrics[10] = atoi(pos);
	pos = os_strchr(pos, ':');
	if (pos == NULL)
		goto fail;
	pos++;

	/* LMD */
	WPA_PUT_LE16(wan_metrics + 11, atoi(pos));

	os_free(bss->hs20_wan_metrics);
	bss->hs20_wan_metrics = wan_metrics;

	return 0;

fail:
	wpa_printf(MSG_ERROR, "Line %d: Invalid hs20_wan_metrics '%s'",
		   line, buf);
	os_free(wan_metrics);
	return -1;
}


static int hs20_parse_oper_friendly_name(struct hostapd_bss_config *bss,
					 char *pos, int line)
{
	if (parse_lang_string(&bss->hs20_oper_friendly_name,
			      &bss->hs20_oper_friendly_name_count, pos)) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid "
			   "hs20_oper_friendly_name '%s'", line, pos);
		return -1;
	}
	return 0;
}


static int hs20_parse_icon(struct hostapd_bss_config *bss, char *pos)
{
	struct hs20_icon *icon;
	char *end;

	icon = os_realloc_array(bss->hs20_icons, bss->hs20_icons_count + 1,
				sizeof(struct hs20_icon));
	if (icon == NULL)
		return -1;
	bss->hs20_icons = icon;
	icon = &bss->hs20_icons[bss->hs20_icons_count];
	os_memset(icon, 0, sizeof(*icon));

	icon->width = atoi(pos);
	pos = os_strchr(pos, ':');
	if (pos == NULL)
		return -1;
	pos++;

	icon->height = atoi(pos);
	pos = os_strchr(pos, ':');
	if (pos == NULL)
		return -1;
	pos++;

	end = os_strchr(pos, ':');
	if (end == NULL || end - pos > 3)
		return -1;
	os_memcpy(icon->language, pos, end - pos);
	pos = end + 1;

	end = os_strchr(pos, ':');
	if (end == NULL || end - pos > 255)
		return -1;
	os_memcpy(icon->type, pos, end - pos);
	pos = end + 1;

	end = os_strchr(pos, ':');
	if (end == NULL || end - pos > 255)
		return -1;
	os_memcpy(icon->name, pos, end - pos);
	pos = end + 1;

	if (os_strlen(pos) > 255)
		return -1;
	os_memcpy(icon->file, pos, os_strlen(pos));

	bss->hs20_icons_count++;

	return 0;
}


static int hs20_parse_osu_ssid(struct hostapd_bss_config *bss,
			       char *pos, int line)
{
	size_t slen;
	char *str;

	str = wpa_config_parse_string(pos, &slen);
	if (str == NULL || slen < 1 || slen > SSID_MAX_LEN) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid SSID '%s'", line, pos);
		os_free(str);
		return -1;
	}

	os_memcpy(bss->osu_ssid, str, slen);
	bss->osu_ssid_len = slen;
	os_free(str);

	return 0;
}


static int hs20_parse_osu_server_uri(struct hostapd_bss_config *bss,
				     char *pos, int line)
{
	struct hs20_osu_provider *p;

	p = os_realloc_array(bss->hs20_osu_providers,
			     bss->hs20_osu_providers_count + 1, sizeof(*p));
	if (p == NULL)
		return -1;

	bss->hs20_osu_providers = p;
	bss->last_osu = &bss->hs20_osu_providers[bss->hs20_osu_providers_count];
	bss->hs20_osu_providers_count++;
	os_memset(bss->last_osu, 0, sizeof(*p));
	bss->last_osu->server_uri = os_strdup(pos);

	return 0;
}


static int hs20_parse_osu_friendly_name(struct hostapd_bss_config *bss,
					char *pos, int line)
{
	if (bss->last_osu == NULL) {
		wpa_printf(MSG_ERROR, "Line %d: Unexpected OSU field", line);
		return -1;
	}

	if (parse_lang_string(&bss->last_osu->friendly_name,
			      &bss->last_osu->friendly_name_count, pos)) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid osu_friendly_name '%s'",
			   line, pos);
		return -1;
	}

	return 0;
}


static int hs20_parse_osu_nai(struct hostapd_bss_config *bss,
			      char *pos, int line)
{
	if (bss->last_osu == NULL) {
		wpa_printf(MSG_ERROR, "Line %d: Unexpected OSU field", line);
		return -1;
	}

	os_free(bss->last_osu->osu_nai);
	bss->last_osu->osu_nai = os_strdup(pos);
	if (bss->last_osu->osu_nai == NULL)
		return -1;

	return 0;
}


static int hs20_parse_osu_nai2(struct hostapd_bss_config *bss,
			       char *pos, int line)
{
	if (bss->last_osu == NULL) {
		wpa_printf(MSG_ERROR, "Line %d: Unexpected OSU field", line);
		return -1;
	}

	os_free(bss->last_osu->osu_nai2);
	bss->last_osu->osu_nai2 = os_strdup(pos);
	if (bss->last_osu->osu_nai2 == NULL)
		return -1;
	bss->hs20_osu_providers_nai_count++;

	return 0;
}


static int hs20_parse_osu_method_list(struct hostapd_bss_config *bss, char *pos,
				      int line)
{
	if (bss->last_osu == NULL) {
		wpa_printf(MSG_ERROR, "Line %d: Unexpected OSU field", line);
		return -1;
	}

	if (hostapd_parse_intlist(&bss->last_osu->method_list, pos)) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid osu_method_list", line);
		return -1;
	}

	return 0;
}


static int hs20_parse_osu_icon(struct hostapd_bss_config *bss, char *pos,
			       int line)
{
	char **n;
	struct hs20_osu_provider *p = bss->last_osu;

	if (p == NULL) {
		wpa_printf(MSG_ERROR, "Line %d: Unexpected OSU field", line);
		return -1;
	}

	n = os_realloc_array(p->icons, p->icons_count + 1, sizeof(char *));
	if (n == NULL)
		return -1;
	p->icons = n;
	p->icons[p->icons_count] = os_strdup(pos);
	if (p->icons[p->icons_count] == NULL)
		return -1;
	p->icons_count++;

	return 0;
}


static int hs20_parse_osu_service_desc(struct hostapd_bss_config *bss,
				       char *pos, int line)
{
	if (bss->last_osu == NULL) {
		wpa_printf(MSG_ERROR, "Line %d: Unexpected OSU field", line);
		return -1;
	}

	if (parse_lang_string(&bss->last_osu->service_desc,
			      &bss->last_osu->service_desc_count, pos)) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid osu_service_desc '%s'",
			   line, pos);
		return -1;
	}

	return 0;
}


static int hs20_parse_operator_icon(struct hostapd_bss_config *bss, char *pos,
				    int line)
{
	char **n;

	n = os_realloc_array(bss->hs20_operator_icon,
			     bss->hs20_operator_icon_count + 1, sizeof(char *));
	if (!n)
		return -1;
	bss->hs20_operator_icon = n;
	bss->hs20_operator_icon[bss->hs20_operator_icon_count] = os_strdup(pos);
	if (!bss->hs20_operator_icon[bss->hs20_operator_icon_count])
		return -1;
	bss->hs20_operator_icon_count++;

	return 0;
}

#endif /* CONFIG_HS20 */


#ifdef CONFIG_ACS
static int hostapd_config_parse_acs_chan_bias(struct hostapd_config *conf,
					      char *pos)
{
	struct acs_bias *bias = NULL, *tmp;
	unsigned int num = 0;
	char *end;

	while (*pos) {
		tmp = os_realloc_array(bias, num + 1, sizeof(*bias));
		if (!tmp)
			goto fail;
		bias = tmp;

		bias[num].channel = atoi(pos);
		if (bias[num].channel <= 0)
			goto fail;
		pos = os_strchr(pos, ':');
		if (!pos)
			goto fail;
		pos++;
		bias[num].bias = strtod(pos, &end);
		if (end == pos || bias[num].bias < 0.0)
			goto fail;
		pos = end;
		if (*pos != ' ' && *pos != '\0')
			goto fail;
		num++;
	}

	os_free(conf->acs_chan_bias);
	conf->acs_chan_bias = bias;
	conf->num_acs_chan_bias = num;

	return 0;
fail:
	os_free(bias);
	return -1;
}
#endif /* CONFIG_ACS */


static int parse_wpabuf_hex(int line, const char *name, struct wpabuf **buf,
			    const char *val)
{
	struct wpabuf *elems;

	if (val[0] == '\0') {
		wpabuf_free(*buf);
		*buf = NULL;
		return 0;
	}

	elems = wpabuf_parse_bin(val);
	if (!elems) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid %s '%s'",
			   line, name, val);
		return -1;
	}

	wpabuf_free(*buf);
	*buf = elems;

	return 0;
}


#ifdef CONFIG_FILS
static int parse_fils_realm(struct hostapd_bss_config *bss, const char *val)
{
	struct fils_realm *realm;
	size_t len;

	len = os_strlen(val);
	realm = os_zalloc(sizeof(*realm) + len + 1);
	if (!realm)
		return -1;

	os_memcpy(realm->realm, val, len);
	if (fils_domain_name_hash(val, realm->hash) < 0) {
		os_free(realm);
		return -1;
	}
	dl_list_add_tail(&bss->fils_realms, &realm->list);

	return 0;
}
#endif /* CONFIG_FILS */


#ifdef EAP_SERVER
static unsigned int parse_tls_flags(const char *val)
{
	unsigned int flags = 0;

	/* Disable TLS v1.3 by default for now to avoid interoperability issue.
	 * This can be enabled by default once the implementation has been fully
	 * completed and tested with other implementations. */
	flags |= TLS_CONN_DISABLE_TLSv1_3;

	if (os_strstr(val, "[ALLOW-SIGN-RSA-MD5]"))
		flags |= TLS_CONN_ALLOW_SIGN_RSA_MD5;
	if (os_strstr(val, "[DISABLE-TIME-CHECKS]"))
		flags |= TLS_CONN_DISABLE_TIME_CHECKS;
	if (os_strstr(val, "[DISABLE-TLSv1.0]"))
		flags |= TLS_CONN_DISABLE_TLSv1_0;
	if (os_strstr(val, "[DISABLE-TLSv1.1]"))
		flags |= TLS_CONN_DISABLE_TLSv1_1;
	if (os_strstr(val, "[DISABLE-TLSv1.2]"))
		flags |= TLS_CONN_DISABLE_TLSv1_2;
	if (os_strstr(val, "[DISABLE-TLSv1.3]"))
		flags |= TLS_CONN_DISABLE_TLSv1_3;
	if (os_strstr(val, "[ENABLE-TLSv1.3]"))
		flags &= ~TLS_CONN_DISABLE_TLSv1_3;
	if (os_strstr(val, "[SUITEB]"))
		flags |= TLS_CONN_SUITEB;
	if (os_strstr(val, "[SUITEB-NO-ECDH]"))
		flags |= TLS_CONN_SUITEB_NO_ECDH | TLS_CONN_SUITEB;

	return flags;
}
#endif /* EAP_SERVER */


#ifdef CONFIG_SAE
static int parse_sae_password(struct hostapd_bss_config *bss, const char *val)
{
	struct sae_password_entry *pw;
	const char *pos = val, *pos2, *end = NULL;

	pw = os_zalloc(sizeof(*pw));
	if (!pw)
		return -1;
	os_memset(pw->peer_addr, 0xff, ETH_ALEN); /* default to wildcard */

	pos2 = os_strstr(pos, "|mac=");
	if (pos2) {
		end = pos2;
		pos2 += 5;
		if (hwaddr_aton(pos2, pw->peer_addr) < 0)
			goto fail;
		pos = pos2 + ETH_ALEN * 3 - 1;
	}

	pos2 = os_strstr(pos, "|id=");
	if (pos2) {
		if (!end)
			end = pos2;
		pos2 += 4;
		pw->identifier = os_strdup(pos2);
		if (!pw->identifier)
			goto fail;
	}

	if (!end) {
		pw->password = os_strdup(val);
		if (!pw->password)
			goto fail;
	} else {
		pw->password = os_malloc(end - val + 1);
		if (!pw->password)
			goto fail;
		os_memcpy(pw->password, val, end - val);
		pw->password[end - val] = '\0';
	}

	pw->next = bss->sae_passwords;
	bss->sae_passwords = pw;

	return 0;
fail:
	str_clear_free(pw->password);
	os_free(pw->identifier);
	os_free(pw);
	return -1;
}
#endif /* CONFIG_SAE */


static int hostapd_config_fill(struct hostapd_config *conf,
			       struct hostapd_bss_config *bss,
			       const char *buf, char *pos, int line)
{
	if (os_strcmp(buf, "interface") == 0) {
		os_strlcpy(conf->bss[0]->iface, pos,
			   sizeof(conf->bss[0]->iface));
	} else if (os_strcmp(buf, "bridge") == 0) {
		os_strlcpy(bss->bridge, pos, sizeof(bss->bridge));
	} else if (os_strcmp(buf, "vlan_bridge") == 0) {
		os_strlcpy(bss->vlan_bridge, pos, sizeof(bss->vlan_bridge));
	} else if (os_strcmp(buf, "wds_bridge") == 0) {
		os_strlcpy(bss->wds_bridge, pos, sizeof(bss->wds_bridge));
	} else if (os_strcmp(buf, "driver") == 0) {
		int j;
		const struct wpa_driver_ops *driver = NULL;

		for (j = 0; wpa_drivers[j]; j++) {
			if (os_strcmp(pos, wpa_drivers[j]->name) == 0) {
				driver = wpa_drivers[j];
				break;
			}
		}
		if (!driver) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid/unknown driver '%s'",
				   line, pos);
			return 1;
		}
		conf->driver = driver;
	} else if (os_strcmp(buf, "driver_params") == 0) {
		os_free(conf->driver_params);
		conf->driver_params = os_strdup(pos);
	} else if (os_strcmp(buf, "debug") == 0) {
		wpa_printf(MSG_DEBUG, "Line %d: DEPRECATED: 'debug' configuration variable is not used anymore",
			   line);
	} else if (os_strcmp(buf, "logger_syslog_level") == 0) {
		bss->logger_syslog_level = atoi(pos);
	} else if (os_strcmp(buf, "logger_stdout_level") == 0) {
		bss->logger_stdout_level = atoi(pos);
	} else if (os_strcmp(buf, "logger_syslog") == 0) {
		bss->logger_syslog = atoi(pos);
	} else if (os_strcmp(buf, "logger_stdout") == 0) {
		bss->logger_stdout = atoi(pos);
	} else if (os_strcmp(buf, "dump_file") == 0) {
		wpa_printf(MSG_INFO, "Line %d: DEPRECATED: 'dump_file' configuration variable is not used anymore",
			   line);
	} else if (os_strcmp(buf, "ssid") == 0) {
		bss->ssid.ssid_len = os_strlen(pos);
		if (bss->ssid.ssid_len > SSID_MAX_LEN ||
		    bss->ssid.ssid_len < 1) {
			wpa_printf(MSG_ERROR, "Line %d: invalid SSID '%s'",
				   line, pos);
			return 1;
		}
		os_memcpy(bss->ssid.ssid, pos, bss->ssid.ssid_len);
		bss->ssid.ssid_set = 1;
	} else if (os_strcmp(buf, "ssid2") == 0) {
		size_t slen;
		char *str = wpa_config_parse_string(pos, &slen);
		if (str == NULL || slen < 1 || slen > SSID_MAX_LEN) {
			wpa_printf(MSG_ERROR, "Line %d: invalid SSID '%s'",
				   line, pos);
			os_free(str);
			return 1;
		}
		os_memcpy(bss->ssid.ssid, str, slen);
		bss->ssid.ssid_len = slen;
		bss->ssid.ssid_set = 1;
		os_free(str);
	} else if (os_strcmp(buf, "utf8_ssid") == 0) {
		bss->ssid.utf8_ssid = atoi(pos) > 0;
	} else if (os_strcmp(buf, "macaddr_acl") == 0) {
		enum macaddr_acl acl = atoi(pos);

		if (acl != ACCEPT_UNLESS_DENIED &&
		    acl != DENY_UNLESS_ACCEPTED &&
		    acl != USE_EXTERNAL_RADIUS_AUTH) {
			wpa_printf(MSG_ERROR, "Line %d: unknown macaddr_acl %d",
				   line, acl);
			return 1;
		}
		bss->macaddr_acl = acl;
	} else if (os_strcmp(buf, "accept_mac_file") == 0) {
		if (hostapd_config_read_maclist(pos, &bss->accept_mac,
						&bss->num_accept_mac)) {
			wpa_printf(MSG_ERROR, "Line %d: Failed to read accept_mac_file '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "deny_mac_file") == 0) {
		if (hostapd_config_read_maclist(pos, &bss->deny_mac,
						&bss->num_deny_mac)) {
			wpa_printf(MSG_ERROR, "Line %d: Failed to read deny_mac_file '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "wds_sta") == 0) {
		bss->wds_sta = atoi(pos);
	} else if (os_strcmp(buf, "start_disabled") == 0) {
		bss->start_disabled = atoi(pos);
	} else if (os_strcmp(buf, "ap_isolate") == 0) {
		bss->isolate = atoi(pos);
	} else if (os_strcmp(buf, "ap_max_inactivity") == 0) {
		bss->ap_max_inactivity = atoi(pos);
	} else if (os_strcmp(buf, "skip_inactivity_poll") == 0) {
		bss->skip_inactivity_poll = atoi(pos);
	} else if (os_strcmp(buf, "country_code") == 0) {
		os_memcpy(conf->country, pos, 2);
	} else if (os_strcmp(buf, "country3") == 0) {
		conf->country[2] = strtol(pos, NULL, 16);
	} else if (os_strcmp(buf, "ieee80211d") == 0) {
		conf->ieee80211d = atoi(pos);
	} else if (os_strcmp(buf, "ieee80211h") == 0) {
		conf->ieee80211h = atoi(pos);
	} else if (os_strcmp(buf, "ieee8021x") == 0) {
		bss->ieee802_1x = atoi(pos);
	} else if (os_strcmp(buf, "eapol_version") == 0) {
		int eapol_version = atoi(pos);

		if (eapol_version < 1 || eapol_version > 2) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid EAPOL version (%d): '%s'.",
				   line, eapol_version, pos);
			return 1;
		}
		bss->eapol_version = eapol_version;
		wpa_printf(MSG_DEBUG, "eapol_version=%d", bss->eapol_version);
#ifdef EAP_SERVER
	} else if (os_strcmp(buf, "eap_authenticator") == 0) {
		bss->eap_server = atoi(pos);
		wpa_printf(MSG_ERROR, "Line %d: obsolete eap_authenticator used; this has been renamed to eap_server", line);
	} else if (os_strcmp(buf, "eap_server") == 0) {
		bss->eap_server = atoi(pos);
	} else if (os_strcmp(buf, "eap_user_file") == 0) {
		if (hostapd_config_read_eap_user(pos, bss))
			return 1;
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
	} else if (os_strcmp(buf, "tls_session_lifetime") == 0) {
		bss->tls_session_lifetime = atoi(pos);
	} else if (os_strcmp(buf, "tls_flags") == 0) {
		bss->tls_flags = parse_tls_flags(pos);
	} else if (os_strcmp(buf, "ocsp_stapling_response") == 0) {
		os_free(bss->ocsp_stapling_response);
		bss->ocsp_stapling_response = os_strdup(pos);
	} else if (os_strcmp(buf, "ocsp_stapling_response_multi") == 0) {
		os_free(bss->ocsp_stapling_response_multi);
		bss->ocsp_stapling_response_multi = os_strdup(pos);
	} else if (os_strcmp(buf, "dh_file") == 0) {
		os_free(bss->dh_file);
		bss->dh_file = os_strdup(pos);
	} else if (os_strcmp(buf, "openssl_ciphers") == 0) {
		os_free(bss->openssl_ciphers);
		bss->openssl_ciphers = os_strdup(pos);
	} else if (os_strcmp(buf, "fragment_size") == 0) {
		bss->fragment_size = atoi(pos);
#ifdef EAP_SERVER_FAST
	} else if (os_strcmp(buf, "pac_opaque_encr_key") == 0) {
		os_free(bss->pac_opaque_encr_key);
		bss->pac_opaque_encr_key = os_malloc(16);
		if (bss->pac_opaque_encr_key == NULL) {
			wpa_printf(MSG_ERROR,
				   "Line %d: No memory for pac_opaque_encr_key",
				   line);
			return 1;
		} else if (hexstr2bin(pos, bss->pac_opaque_encr_key, 16)) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid pac_opaque_encr_key",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "eap_fast_a_id") == 0) {
		size_t idlen = os_strlen(pos);
		if (idlen & 1) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid eap_fast_a_id",
				   line);
			return 1;
		}
		os_free(bss->eap_fast_a_id);
		bss->eap_fast_a_id = os_malloc(idlen / 2);
		if (bss->eap_fast_a_id == NULL ||
		    hexstr2bin(pos, bss->eap_fast_a_id, idlen / 2)) {
			wpa_printf(MSG_ERROR, "Line %d: Failed to parse eap_fast_a_id",
				   line);
			os_free(bss->eap_fast_a_id);
			bss->eap_fast_a_id = NULL;
			return 1;
		} else {
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
	} else if (os_strcmp(buf, "eap_sim_db_timeout") == 0) {
		bss->eap_sim_db_timeout = atoi(pos);
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
#ifdef CONFIG_ERP
	} else if (os_strcmp(buf, "eap_server_erp") == 0) {
		bss->eap_server_erp = atoi(pos);
#endif /* CONFIG_ERP */
#endif /* EAP_SERVER */
	} else if (os_strcmp(buf, "eap_message") == 0) {
		char *term;
		os_free(bss->eap_req_id_text);
		bss->eap_req_id_text = os_strdup(pos);
		if (bss->eap_req_id_text == NULL) {
			wpa_printf(MSG_ERROR, "Line %d: Failed to allocate memory for eap_req_id_text",
				   line);
			return 1;
		}
		bss->eap_req_id_text_len = os_strlen(bss->eap_req_id_text);
		term = os_strstr(bss->eap_req_id_text, "\\0");
		if (term) {
			*term++ = '\0';
			os_memmove(term, term + 1,
				   bss->eap_req_id_text_len -
				   (term - bss->eap_req_id_text) - 1);
			bss->eap_req_id_text_len--;
		}
	} else if (os_strcmp(buf, "erp_send_reauth_start") == 0) {
		bss->erp_send_reauth_start = atoi(pos);
	} else if (os_strcmp(buf, "erp_domain") == 0) {
		os_free(bss->erp_domain);
		bss->erp_domain = os_strdup(pos);
	} else if (os_strcmp(buf, "wep_key_len_broadcast") == 0) {
		int val = atoi(pos);

		if (val < 0 || val > 13) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid WEP key len %d (= %d bits)",
				   line, val, val * 8);
			return 1;
		}
		bss->default_wep_key_len = val;
	} else if (os_strcmp(buf, "wep_key_len_unicast") == 0) {
		int val = atoi(pos);

		if (val < 0 || val > 13) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid WEP key len %d (= %d bits)",
				   line, val, val * 8);
			return 1;
		}
		bss->individual_wep_key_len = val;
	} else if (os_strcmp(buf, "wep_rekey_period") == 0) {
		bss->wep_rekeying_period = atoi(pos);
		if (bss->wep_rekeying_period < 0) {
			wpa_printf(MSG_ERROR, "Line %d: invalid period %d",
				   line, bss->wep_rekeying_period);
			return 1;
		}
	} else if (os_strcmp(buf, "eap_reauth_period") == 0) {
		bss->eap_reauth_period = atoi(pos);
		if (bss->eap_reauth_period < 0) {
			wpa_printf(MSG_ERROR, "Line %d: invalid period %d",
				   line, bss->eap_reauth_period);
			return 1;
		}
	} else if (os_strcmp(buf, "eapol_key_index_workaround") == 0) {
		bss->eapol_key_index_workaround = atoi(pos);
#ifdef CONFIG_IAPP
	} else if (os_strcmp(buf, "iapp_interface") == 0) {
		bss->ieee802_11f = 1;
		os_strlcpy(bss->iapp_iface, pos, sizeof(bss->iapp_iface));
#endif /* CONFIG_IAPP */
	} else if (os_strcmp(buf, "own_ip_addr") == 0) {
		if (hostapd_parse_ip_addr(pos, &bss->own_ip_addr)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid IP address '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "nas_identifier") == 0) {
		os_free(bss->nas_identifier);
		bss->nas_identifier = os_strdup(pos);
#ifndef CONFIG_NO_RADIUS
	} else if (os_strcmp(buf, "radius_client_addr") == 0) {
		if (hostapd_parse_ip_addr(pos, &bss->radius->client_addr)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid IP address '%s'",
				   line, pos);
			return 1;
		}
		bss->radius->force_client_addr = 1;
	} else if (os_strcmp(buf, "auth_server_addr") == 0) {
		if (hostapd_config_read_radius_addr(
			    &bss->radius->auth_servers,
			    &bss->radius->num_auth_servers, pos, 1812,
			    &bss->radius->auth_server)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid IP address '%s'",
				   line, pos);
			return 1;
		}
	} else if (bss->radius->auth_server &&
		   os_strcmp(buf, "auth_server_addr_replace") == 0) {
		if (hostapd_parse_ip_addr(pos,
					  &bss->radius->auth_server->addr)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid IP address '%s'",
				   line, pos);
			return 1;
		}
	} else if (bss->radius->auth_server &&
		   os_strcmp(buf, "auth_server_port") == 0) {
		bss->radius->auth_server->port = atoi(pos);
	} else if (bss->radius->auth_server &&
		   os_strcmp(buf, "auth_server_shared_secret") == 0) {
		int len = os_strlen(pos);
		if (len == 0) {
			/* RFC 2865, Ch. 3 */
			wpa_printf(MSG_ERROR, "Line %d: empty shared secret is not allowed",
				   line);
			return 1;
		}
		os_free(bss->radius->auth_server->shared_secret);
		bss->radius->auth_server->shared_secret = (u8 *) os_strdup(pos);
		bss->radius->auth_server->shared_secret_len = len;
	} else if (os_strcmp(buf, "acct_server_addr") == 0) {
		if (hostapd_config_read_radius_addr(
			    &bss->radius->acct_servers,
			    &bss->radius->num_acct_servers, pos, 1813,
			    &bss->radius->acct_server)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid IP address '%s'",
				   line, pos);
			return 1;
		}
	} else if (bss->radius->acct_server &&
		   os_strcmp(buf, "acct_server_addr_replace") == 0) {
		if (hostapd_parse_ip_addr(pos,
					  &bss->radius->acct_server->addr)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid IP address '%s'",
				   line, pos);
			return 1;
		}
	} else if (bss->radius->acct_server &&
		   os_strcmp(buf, "acct_server_port") == 0) {
		bss->radius->acct_server->port = atoi(pos);
	} else if (bss->radius->acct_server &&
		   os_strcmp(buf, "acct_server_shared_secret") == 0) {
		int len = os_strlen(pos);
		if (len == 0) {
			/* RFC 2865, Ch. 3 */
			wpa_printf(MSG_ERROR, "Line %d: empty shared secret is not allowed",
				   line);
			return 1;
		}
		os_free(bss->radius->acct_server->shared_secret);
		bss->radius->acct_server->shared_secret = (u8 *) os_strdup(pos);
		bss->radius->acct_server->shared_secret_len = len;
	} else if (os_strcmp(buf, "radius_retry_primary_interval") == 0) {
		bss->radius->retry_primary_interval = atoi(pos);
	} else if (os_strcmp(buf, "radius_acct_interim_interval") == 0) {
		bss->acct_interim_interval = atoi(pos);
	} else if (os_strcmp(buf, "radius_request_cui") == 0) {
		bss->radius_request_cui = atoi(pos);
	} else if (os_strcmp(buf, "radius_auth_req_attr") == 0) {
		struct hostapd_radius_attr *attr, *a;
		attr = hostapd_parse_radius_attr(pos);
		if (attr == NULL) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid radius_auth_req_attr",
				   line);
			return 1;
		} else if (bss->radius_auth_req_attr == NULL) {
			bss->radius_auth_req_attr = attr;
		} else {
			a = bss->radius_auth_req_attr;
			while (a->next)
				a = a->next;
			a->next = attr;
		}
	} else if (os_strcmp(buf, "radius_acct_req_attr") == 0) {
		struct hostapd_radius_attr *attr, *a;
		attr = hostapd_parse_radius_attr(pos);
		if (attr == NULL) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid radius_acct_req_attr",
				   line);
			return 1;
		} else if (bss->radius_acct_req_attr == NULL) {
			bss->radius_acct_req_attr = attr;
		} else {
			a = bss->radius_acct_req_attr;
			while (a->next)
				a = a->next;
			a->next = attr;
		}
	} else if (os_strcmp(buf, "radius_das_port") == 0) {
		bss->radius_das_port = atoi(pos);
	} else if (os_strcmp(buf, "radius_das_client") == 0) {
		if (hostapd_parse_das_client(bss, pos) < 0) {
			wpa_printf(MSG_ERROR, "Line %d: invalid DAS client",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "radius_das_time_window") == 0) {
		bss->radius_das_time_window = atoi(pos);
	} else if (os_strcmp(buf, "radius_das_require_event_timestamp") == 0) {
		bss->radius_das_require_event_timestamp = atoi(pos);
	} else if (os_strcmp(buf, "radius_das_require_message_authenticator") ==
		   0) {
		bss->radius_das_require_message_authenticator = atoi(pos);
#endif /* CONFIG_NO_RADIUS */
	} else if (os_strcmp(buf, "auth_algs") == 0) {
		bss->auth_algs = atoi(pos);
		if (bss->auth_algs == 0) {
			wpa_printf(MSG_ERROR, "Line %d: no authentication algorithms allowed",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "max_num_sta") == 0) {
		bss->max_num_sta = atoi(pos);
		if (bss->max_num_sta < 0 ||
		    bss->max_num_sta > MAX_STA_COUNT) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid max_num_sta=%d; allowed range 0..%d",
				   line, bss->max_num_sta, MAX_STA_COUNT);
			return 1;
		}
	} else if (os_strcmp(buf, "wpa") == 0) {
		bss->wpa = atoi(pos);
	} else if (os_strcmp(buf, "wpa_group_rekey") == 0) {
		bss->wpa_group_rekey = atoi(pos);
		bss->wpa_group_rekey_set = 1;
	} else if (os_strcmp(buf, "wpa_strict_rekey") == 0) {
		bss->wpa_strict_rekey = atoi(pos);
	} else if (os_strcmp(buf, "wpa_gmk_rekey") == 0) {
		bss->wpa_gmk_rekey = atoi(pos);
	} else if (os_strcmp(buf, "wpa_ptk_rekey") == 0) {
		bss->wpa_ptk_rekey = atoi(pos);
	} else if (os_strcmp(buf, "wpa_group_update_count") == 0) {
		char *endp;
		unsigned long val = strtoul(pos, &endp, 0);

		if (*endp || val < 1 || val > (u32) -1) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid wpa_group_update_count=%lu; allowed range 1..4294967295",
				   line, val);
			return 1;
		}
		bss->wpa_group_update_count = (u32) val;
	} else if (os_strcmp(buf, "wpa_pairwise_update_count") == 0) {
		char *endp;
		unsigned long val = strtoul(pos, &endp, 0);

		if (*endp || val < 1 || val > (u32) -1) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid wpa_pairwise_update_count=%lu; allowed range 1..4294967295",
				   line, val);
			return 1;
		}
		bss->wpa_pairwise_update_count = (u32) val;
	} else if (os_strcmp(buf, "wpa_disable_eapol_key_retries") == 0) {
		bss->wpa_disable_eapol_key_retries = atoi(pos);
	} else if (os_strcmp(buf, "wpa_passphrase") == 0) {
		int len = os_strlen(pos);
		if (len < 8 || len > 63) {
			wpa_printf(MSG_ERROR, "Line %d: invalid WPA passphrase length %d (expected 8..63)",
				   line, len);
			return 1;
		}
		os_free(bss->ssid.wpa_passphrase);
		bss->ssid.wpa_passphrase = os_strdup(pos);
		if (bss->ssid.wpa_passphrase) {
			hostapd_config_clear_wpa_psk(&bss->ssid.wpa_psk);
			bss->ssid.wpa_passphrase_set = 1;
		}
	} else if (os_strcmp(buf, "wpa_psk") == 0) {
		hostapd_config_clear_wpa_psk(&bss->ssid.wpa_psk);
		bss->ssid.wpa_psk = os_zalloc(sizeof(struct hostapd_wpa_psk));
		if (bss->ssid.wpa_psk == NULL)
			return 1;
		if (hexstr2bin(pos, bss->ssid.wpa_psk->psk, PMK_LEN) ||
		    pos[PMK_LEN * 2] != '\0') {
			wpa_printf(MSG_ERROR, "Line %d: Invalid PSK '%s'.",
				   line, pos);
			hostapd_config_clear_wpa_psk(&bss->ssid.wpa_psk);
			return 1;
		}
		bss->ssid.wpa_psk->group = 1;
		os_free(bss->ssid.wpa_passphrase);
		bss->ssid.wpa_passphrase = NULL;
		bss->ssid.wpa_psk_set = 1;
	} else if (os_strcmp(buf, "wpa_psk_file") == 0) {
		os_free(bss->ssid.wpa_psk_file);
		bss->ssid.wpa_psk_file = os_strdup(pos);
		if (!bss->ssid.wpa_psk_file) {
			wpa_printf(MSG_ERROR, "Line %d: allocation failed",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "wpa_key_mgmt") == 0) {
		bss->wpa_key_mgmt = hostapd_config_parse_key_mgmt(line, pos);
		if (bss->wpa_key_mgmt == -1)
			return 1;
	} else if (os_strcmp(buf, "wpa_psk_radius") == 0) {
		bss->wpa_psk_radius = atoi(pos);
		if (bss->wpa_psk_radius != PSK_RADIUS_IGNORED &&
		    bss->wpa_psk_radius != PSK_RADIUS_ACCEPTED &&
		    bss->wpa_psk_radius != PSK_RADIUS_REQUIRED) {
			wpa_printf(MSG_ERROR,
				   "Line %d: unknown wpa_psk_radius %d",
				   line, bss->wpa_psk_radius);
			return 1;
		}
	} else if (os_strcmp(buf, "wpa_pairwise") == 0) {
		bss->wpa_pairwise = hostapd_config_parse_cipher(line, pos);
		if (bss->wpa_pairwise == -1 || bss->wpa_pairwise == 0)
			return 1;
		if (bss->wpa_pairwise &
		    (WPA_CIPHER_NONE | WPA_CIPHER_WEP40 | WPA_CIPHER_WEP104)) {
			wpa_printf(MSG_ERROR, "Line %d: unsupported pairwise cipher suite '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "rsn_pairwise") == 0) {
		bss->rsn_pairwise = hostapd_config_parse_cipher(line, pos);
		if (bss->rsn_pairwise == -1 || bss->rsn_pairwise == 0)
			return 1;
		if (bss->rsn_pairwise &
		    (WPA_CIPHER_NONE | WPA_CIPHER_WEP40 | WPA_CIPHER_WEP104)) {
			wpa_printf(MSG_ERROR, "Line %d: unsupported pairwise cipher suite '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "group_cipher") == 0) {
		bss->group_cipher = hostapd_config_parse_cipher(line, pos);
		if (bss->group_cipher == -1 || bss->group_cipher == 0)
			return 1;
		if (bss->group_cipher != WPA_CIPHER_TKIP &&
		    bss->group_cipher != WPA_CIPHER_CCMP &&
		    bss->group_cipher != WPA_CIPHER_GCMP &&
		    bss->group_cipher != WPA_CIPHER_GCMP_256 &&
		    bss->group_cipher != WPA_CIPHER_CCMP_256) {
			wpa_printf(MSG_ERROR,
				   "Line %d: unsupported group cipher suite '%s'",
				   line, pos);
			return 1;
		}
#ifdef CONFIG_RSN_PREAUTH
	} else if (os_strcmp(buf, "rsn_preauth") == 0) {
		bss->rsn_preauth = atoi(pos);
	} else if (os_strcmp(buf, "rsn_preauth_interfaces") == 0) {
		os_free(bss->rsn_preauth_interfaces);
		bss->rsn_preauth_interfaces = os_strdup(pos);
#endif /* CONFIG_RSN_PREAUTH */
	} else if (os_strcmp(buf, "peerkey") == 0) {
		wpa_printf(MSG_INFO,
			   "Line %d: Obsolete peerkey parameter ignored", line);
#ifdef CONFIG_IEEE80211R_AP
	} else if (os_strcmp(buf, "mobility_domain") == 0) {
		if (os_strlen(pos) != 2 * MOBILITY_DOMAIN_ID_LEN ||
		    hexstr2bin(pos, bss->mobility_domain,
			       MOBILITY_DOMAIN_ID_LEN) != 0) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid mobility_domain '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "r1_key_holder") == 0) {
		if (os_strlen(pos) != 2 * FT_R1KH_ID_LEN ||
		    hexstr2bin(pos, bss->r1_key_holder, FT_R1KH_ID_LEN) != 0) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid r1_key_holder '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "r0_key_lifetime") == 0) {
		/* DEPRECATED: Use ft_r0_key_lifetime instead. */
		bss->r0_key_lifetime = atoi(pos) * 60;
	} else if (os_strcmp(buf, "ft_r0_key_lifetime") == 0) {
		bss->r0_key_lifetime = atoi(pos);
	} else if (os_strcmp(buf, "r1_max_key_lifetime") == 0) {
		bss->r1_max_key_lifetime = atoi(pos);
	} else if (os_strcmp(buf, "reassociation_deadline") == 0) {
		bss->reassociation_deadline = atoi(pos);
	} else if (os_strcmp(buf, "rkh_pos_timeout") == 0) {
		bss->rkh_pos_timeout = atoi(pos);
	} else if (os_strcmp(buf, "rkh_neg_timeout") == 0) {
		bss->rkh_neg_timeout = atoi(pos);
	} else if (os_strcmp(buf, "rkh_pull_timeout") == 0) {
		bss->rkh_pull_timeout = atoi(pos);
	} else if (os_strcmp(buf, "rkh_pull_retries") == 0) {
		bss->rkh_pull_retries = atoi(pos);
	} else if (os_strcmp(buf, "r0kh") == 0) {
		if (add_r0kh(bss, pos) < 0) {
			wpa_printf(MSG_DEBUG, "Line %d: Invalid r0kh '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "r1kh") == 0) {
		if (add_r1kh(bss, pos) < 0) {
			wpa_printf(MSG_DEBUG, "Line %d: Invalid r1kh '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "pmk_r1_push") == 0) {
		bss->pmk_r1_push = atoi(pos);
	} else if (os_strcmp(buf, "ft_over_ds") == 0) {
		bss->ft_over_ds = atoi(pos);
	} else if (os_strcmp(buf, "ft_psk_generate_local") == 0) {
		bss->ft_psk_generate_local = atoi(pos);
#endif /* CONFIG_IEEE80211R_AP */
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
			wpa_printf(MSG_DEBUG, "ctrl_interface_group=%d (from group name '%s')",
				   bss->ctrl_interface_gid, group);
			return 0;
		}

		/* Group name not found - try to parse this as gid */
		bss->ctrl_interface_gid = strtol(group, &endp, 10);
		if (*group == '\0' || *endp != '\0') {
			wpa_printf(MSG_DEBUG, "Line %d: Invalid group '%s'",
				   line, group);
			return 1;
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
	} else if (os_strcmp(buf, "radius_server_acct_port") == 0) {
		bss->radius_server_acct_port = atoi(pos);
	} else if (os_strcmp(buf, "radius_server_ipv6") == 0) {
		bss->radius_server_ipv6 = atoi(pos);
#endif /* RADIUS_SERVER */
	} else if (os_strcmp(buf, "use_pae_group_addr") == 0) {
		bss->use_pae_group_addr = atoi(pos);
	} else if (os_strcmp(buf, "hw_mode") == 0) {
		if (os_strcmp(pos, "a") == 0)
			conf->hw_mode = HOSTAPD_MODE_IEEE80211A;
		else if (os_strcmp(pos, "b") == 0)
			conf->hw_mode = HOSTAPD_MODE_IEEE80211B;
		else if (os_strcmp(pos, "g") == 0)
			conf->hw_mode = HOSTAPD_MODE_IEEE80211G;
		else if (os_strcmp(pos, "ad") == 0)
			conf->hw_mode = HOSTAPD_MODE_IEEE80211AD;
		else if (os_strcmp(pos, "any") == 0)
			conf->hw_mode = HOSTAPD_MODE_IEEE80211ANY;
		else {
			wpa_printf(MSG_ERROR, "Line %d: unknown hw_mode '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "wps_rf_bands") == 0) {
		if (os_strcmp(pos, "ad") == 0)
			bss->wps_rf_bands = WPS_RF_60GHZ;
		else if (os_strcmp(pos, "a") == 0)
			bss->wps_rf_bands = WPS_RF_50GHZ;
		else if (os_strcmp(pos, "g") == 0 ||
			 os_strcmp(pos, "b") == 0)
			bss->wps_rf_bands = WPS_RF_24GHZ;
		else if (os_strcmp(pos, "ag") == 0 ||
			 os_strcmp(pos, "ga") == 0)
			bss->wps_rf_bands = WPS_RF_24GHZ | WPS_RF_50GHZ;
		else {
			wpa_printf(MSG_ERROR,
				   "Line %d: unknown wps_rf_band '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "acs_exclude_dfs") == 0) {
		conf->acs_exclude_dfs = atoi(pos);
	} else if (os_strcmp(buf, "channel") == 0) {
		if (os_strcmp(pos, "acs_survey") == 0) {
#ifndef CONFIG_ACS
			wpa_printf(MSG_ERROR, "Line %d: tries to enable ACS but CONFIG_ACS disabled",
				   line);
			return 1;
#else /* CONFIG_ACS */
			conf->acs = 1;
			conf->channel = 0;
#endif /* CONFIG_ACS */
		} else {
			conf->channel = atoi(pos);
			conf->acs = conf->channel == 0;
		}
	} else if (os_strcmp(buf, "chanlist") == 0) {
		if (hostapd_parse_chanlist(conf, pos)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid channel list",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "beacon_int") == 0) {
		int val = atoi(pos);
		/* MIB defines range as 1..65535, but very small values
		 * cause problems with the current implementation.
		 * Since it is unlikely that this small numbers are
		 * useful in real life scenarios, do not allow beacon
		 * period to be set below 15 TU. */
		if (val < 15 || val > 65535) {
			wpa_printf(MSG_ERROR, "Line %d: invalid beacon_int %d (expected 15..65535)",
				   line, val);
			return 1;
		}
		conf->beacon_int = val;
#ifdef CONFIG_ACS
	} else if (os_strcmp(buf, "acs_num_scans") == 0) {
		int val = atoi(pos);
		if (val <= 0 || val > 100) {
			wpa_printf(MSG_ERROR, "Line %d: invalid acs_num_scans %d (expected 1..100)",
				   line, val);
			return 1;
		}
		conf->acs_num_scans = val;
	} else if (os_strcmp(buf, "acs_chan_bias") == 0) {
		if (hostapd_config_parse_acs_chan_bias(conf, pos)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid acs_chan_bias",
				   line);
			return -1;
		}
#endif /* CONFIG_ACS */
	} else if (os_strcmp(buf, "dtim_period") == 0) {
		int val = atoi(pos);

		if (val < 1 || val > 255) {
			wpa_printf(MSG_ERROR, "Line %d: invalid dtim_period %d",
				   line, val);
			return 1;
		}
		bss->dtim_period = val;
	} else if (os_strcmp(buf, "bss_load_update_period") == 0) {
		int val = atoi(pos);

		if (val < 0 || val > 100) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid bss_load_update_period %d",
				   line, val);
			return 1;
		}
		bss->bss_load_update_period = val;
	} else if (os_strcmp(buf, "chan_util_avg_period") == 0) {
		int val = atoi(pos);

		if (val < 0) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid chan_util_avg_period",
				   line);
			return 1;
		}
		bss->chan_util_avg_period = val;
	} else if (os_strcmp(buf, "rts_threshold") == 0) {
		conf->rts_threshold = atoi(pos);
		if (conf->rts_threshold < -1 || conf->rts_threshold > 65535) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid rts_threshold %d",
				   line, conf->rts_threshold);
			return 1;
		}
	} else if (os_strcmp(buf, "fragm_threshold") == 0) {
		conf->fragm_threshold = atoi(pos);
		if (conf->fragm_threshold == -1) {
			/* allow a value of -1 */
		} else if (conf->fragm_threshold < 256 ||
			   conf->fragm_threshold > 2346) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid fragm_threshold %d",
				   line, conf->fragm_threshold);
			return 1;
		}
	} else if (os_strcmp(buf, "send_probe_response") == 0) {
		int val = atoi(pos);
		if (val != 0 && val != 1) {
			wpa_printf(MSG_ERROR, "Line %d: invalid send_probe_response %d (expected 0 or 1)",
				   line, val);
			return 1;
		}
		conf->send_probe_response = val;
	} else if (os_strcmp(buf, "supported_rates") == 0) {
		if (hostapd_parse_intlist(&conf->supported_rates, pos)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid rate list",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "basic_rates") == 0) {
		if (hostapd_parse_intlist(&conf->basic_rates, pos)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid rate list",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "beacon_rate") == 0) {
		int val;

		if (os_strncmp(pos, "ht:", 3) == 0) {
			val = atoi(pos + 3);
			if (val < 0 || val > 31) {
				wpa_printf(MSG_ERROR,
					   "Line %d: invalid beacon_rate HT-MCS %d",
					   line, val);
				return 1;
			}
			conf->rate_type = BEACON_RATE_HT;
			conf->beacon_rate = val;
		} else if (os_strncmp(pos, "vht:", 4) == 0) {
			val = atoi(pos + 4);
			if (val < 0 || val > 9) {
				wpa_printf(MSG_ERROR,
					   "Line %d: invalid beacon_rate VHT-MCS %d",
					   line, val);
				return 1;
			}
			conf->rate_type = BEACON_RATE_VHT;
			conf->beacon_rate = val;
		} else {
			val = atoi(pos);
			if (val < 10 || val > 10000) {
				wpa_printf(MSG_ERROR,
					   "Line %d: invalid legacy beacon_rate %d",
					   line, val);
				return 1;
			}
			conf->rate_type = BEACON_RATE_LEGACY;
			conf->beacon_rate = val;
		}
	} else if (os_strcmp(buf, "preamble") == 0) {
		if (atoi(pos))
			conf->preamble = SHORT_PREAMBLE;
		else
			conf->preamble = LONG_PREAMBLE;
	} else if (os_strcmp(buf, "ignore_broadcast_ssid") == 0) {
		bss->ignore_broadcast_ssid = atoi(pos);
	} else if (os_strcmp(buf, "no_probe_resp_if_max_sta") == 0) {
		bss->no_probe_resp_if_max_sta = atoi(pos);
	} else if (os_strcmp(buf, "wep_default_key") == 0) {
		bss->ssid.wep.idx = atoi(pos);
		if (bss->ssid.wep.idx > 3) {
			wpa_printf(MSG_ERROR,
				   "Invalid wep_default_key index %d",
				   bss->ssid.wep.idx);
			return 1;
		}
	} else if (os_strcmp(buf, "wep_key0") == 0 ||
		   os_strcmp(buf, "wep_key1") == 0 ||
		   os_strcmp(buf, "wep_key2") == 0 ||
		   os_strcmp(buf, "wep_key3") == 0) {
		if (hostapd_config_read_wep(&bss->ssid.wep,
					    buf[7] - '0', pos)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid WEP key '%s'",
				   line, buf);
			return 1;
		}
#ifndef CONFIG_NO_VLAN
	} else if (os_strcmp(buf, "dynamic_vlan") == 0) {
		bss->ssid.dynamic_vlan = atoi(pos);
	} else if (os_strcmp(buf, "per_sta_vif") == 0) {
		bss->ssid.per_sta_vif = atoi(pos);
	} else if (os_strcmp(buf, "vlan_file") == 0) {
		if (hostapd_config_read_vlan_file(bss, pos)) {
			wpa_printf(MSG_ERROR, "Line %d: failed to read VLAN file '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "vlan_naming") == 0) {
		bss->ssid.vlan_naming = atoi(pos);
		if (bss->ssid.vlan_naming >= DYNAMIC_VLAN_NAMING_END ||
		    bss->ssid.vlan_naming < 0) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid naming scheme %d",
				   line, bss->ssid.vlan_naming);
			return 1;
		}
#ifdef CONFIG_FULL_DYNAMIC_VLAN
	} else if (os_strcmp(buf, "vlan_tagged_interface") == 0) {
		os_free(bss->ssid.vlan_tagged_interface);
		bss->ssid.vlan_tagged_interface = os_strdup(pos);
#endif /* CONFIG_FULL_DYNAMIC_VLAN */
#endif /* CONFIG_NO_VLAN */
	} else if (os_strcmp(buf, "ap_table_max_size") == 0) {
		conf->ap_table_max_size = atoi(pos);
	} else if (os_strcmp(buf, "ap_table_expiration_time") == 0) {
		conf->ap_table_expiration_time = atoi(pos);
	} else if (os_strncmp(buf, "tx_queue_", 9) == 0) {
		if (hostapd_config_tx_queue(conf, buf, pos)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid TX queue item",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "wme_enabled") == 0 ||
		   os_strcmp(buf, "wmm_enabled") == 0) {
		bss->wmm_enabled = atoi(pos);
	} else if (os_strcmp(buf, "uapsd_advertisement_enabled") == 0) {
		bss->wmm_uapsd = atoi(pos);
	} else if (os_strncmp(buf, "wme_ac_", 7) == 0 ||
		   os_strncmp(buf, "wmm_ac_", 7) == 0) {
		if (hostapd_config_wmm_ac(conf->wmm_ac_params, buf, pos)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid WMM ac item",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "bss") == 0) {
		if (hostapd_config_bss(conf, pos)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid bss item",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "bssid") == 0) {
		if (hwaddr_aton(pos, bss->bssid)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid bssid item",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "use_driver_iface_addr") == 0) {
		conf->use_driver_iface_addr = atoi(pos);
#ifdef CONFIG_IEEE80211W
	} else if (os_strcmp(buf, "ieee80211w") == 0) {
		bss->ieee80211w = atoi(pos);
	} else if (os_strcmp(buf, "group_mgmt_cipher") == 0) {
		if (os_strcmp(pos, "AES-128-CMAC") == 0) {
			bss->group_mgmt_cipher = WPA_CIPHER_AES_128_CMAC;
		} else if (os_strcmp(pos, "BIP-GMAC-128") == 0) {
			bss->group_mgmt_cipher = WPA_CIPHER_BIP_GMAC_128;
		} else if (os_strcmp(pos, "BIP-GMAC-256") == 0) {
			bss->group_mgmt_cipher = WPA_CIPHER_BIP_GMAC_256;
		} else if (os_strcmp(pos, "BIP-CMAC-256") == 0) {
			bss->group_mgmt_cipher = WPA_CIPHER_BIP_CMAC_256;
		} else {
			wpa_printf(MSG_ERROR, "Line %d: invalid group_mgmt_cipher: %s",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "assoc_sa_query_max_timeout") == 0) {
		bss->assoc_sa_query_max_timeout = atoi(pos);
		if (bss->assoc_sa_query_max_timeout == 0) {
			wpa_printf(MSG_ERROR, "Line %d: invalid assoc_sa_query_max_timeout",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "assoc_sa_query_retry_timeout") == 0) {
		bss->assoc_sa_query_retry_timeout = atoi(pos);
		if (bss->assoc_sa_query_retry_timeout == 0) {
			wpa_printf(MSG_ERROR, "Line %d: invalid assoc_sa_query_retry_timeout",
				   line);
			return 1;
		}
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_IEEE80211N
	} else if (os_strcmp(buf, "ieee80211n") == 0) {
		conf->ieee80211n = atoi(pos);
	} else if (os_strcmp(buf, "ht_capab") == 0) {
		if (hostapd_config_ht_capab(conf, pos) < 0) {
			wpa_printf(MSG_ERROR, "Line %d: invalid ht_capab",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "require_ht") == 0) {
		conf->require_ht = atoi(pos);
	} else if (os_strcmp(buf, "obss_interval") == 0) {
		conf->obss_interval = atoi(pos);
#endif /* CONFIG_IEEE80211N */
#ifdef CONFIG_IEEE80211AC
	} else if (os_strcmp(buf, "ieee80211ac") == 0) {
		conf->ieee80211ac = atoi(pos);
	} else if (os_strcmp(buf, "vht_capab") == 0) {
		if (hostapd_config_vht_capab(conf, pos) < 0) {
			wpa_printf(MSG_ERROR, "Line %d: invalid vht_capab",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "require_vht") == 0) {
		conf->require_vht = atoi(pos);
	} else if (os_strcmp(buf, "vht_oper_chwidth") == 0) {
		conf->vht_oper_chwidth = atoi(pos);
	} else if (os_strcmp(buf, "vht_oper_centr_freq_seg0_idx") == 0) {
		conf->vht_oper_centr_freq_seg0_idx = atoi(pos);
	} else if (os_strcmp(buf, "vht_oper_centr_freq_seg1_idx") == 0) {
		conf->vht_oper_centr_freq_seg1_idx = atoi(pos);
	} else if (os_strcmp(buf, "vendor_vht") == 0) {
		bss->vendor_vht = atoi(pos);
	} else if (os_strcmp(buf, "use_sta_nsts") == 0) {
		bss->use_sta_nsts = atoi(pos);
#endif /* CONFIG_IEEE80211AC */
#ifdef CONFIG_IEEE80211AX
	} else if (os_strcmp(buf, "ieee80211ax") == 0) {
		conf->ieee80211ax = atoi(pos);
	} else if (os_strcmp(buf, "he_su_beamformer") == 0) {
		conf->he_phy_capab.he_su_beamformer = atoi(pos);
	} else if (os_strcmp(buf, "he_su_beamformee") == 0) {
		conf->he_phy_capab.he_su_beamformee = atoi(pos);
	} else if (os_strcmp(buf, "he_mu_beamformer") == 0) {
		conf->he_phy_capab.he_mu_beamformer = atoi(pos);
	} else if (os_strcmp(buf, "he_bss_color") == 0) {
		conf->he_op.he_bss_color = atoi(pos);
	} else if (os_strcmp(buf, "he_default_pe_duration") == 0) {
		conf->he_op.he_default_pe_duration = atoi(pos);
	} else if (os_strcmp(buf, "he_twt_required") == 0) {
		conf->he_op.he_twt_required = atoi(pos);
	} else if (os_strcmp(buf, "he_rts_threshold") == 0) {
		conf->he_op.he_rts_threshold = atoi(pos);
#endif /* CONFIG_IEEE80211AX */
	} else if (os_strcmp(buf, "max_listen_interval") == 0) {
		bss->max_listen_interval = atoi(pos);
	} else if (os_strcmp(buf, "disable_pmksa_caching") == 0) {
		bss->disable_pmksa_caching = atoi(pos);
	} else if (os_strcmp(buf, "okc") == 0) {
		bss->okc = atoi(pos);
#ifdef CONFIG_WPS
	} else if (os_strcmp(buf, "wps_state") == 0) {
		bss->wps_state = atoi(pos);
		if (bss->wps_state < 0 || bss->wps_state > 2) {
			wpa_printf(MSG_ERROR, "Line %d: invalid wps_state",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "wps_independent") == 0) {
		bss->wps_independent = atoi(pos);
	} else if (os_strcmp(buf, "ap_setup_locked") == 0) {
		bss->ap_setup_locked = atoi(pos);
	} else if (os_strcmp(buf, "uuid") == 0) {
		if (uuid_str2bin(pos, bss->uuid)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid UUID", line);
			return 1;
		}
	} else if (os_strcmp(buf, "wps_pin_requests") == 0) {
		os_free(bss->wps_pin_requests);
		bss->wps_pin_requests = os_strdup(pos);
	} else if (os_strcmp(buf, "device_name") == 0) {
		if (os_strlen(pos) > WPS_DEV_NAME_MAX_LEN) {
			wpa_printf(MSG_ERROR, "Line %d: Too long "
				   "device_name", line);
			return 1;
		}
		os_free(bss->device_name);
		bss->device_name = os_strdup(pos);
	} else if (os_strcmp(buf, "manufacturer") == 0) {
		if (os_strlen(pos) > 64) {
			wpa_printf(MSG_ERROR, "Line %d: Too long manufacturer",
				   line);
			return 1;
		}
		os_free(bss->manufacturer);
		bss->manufacturer = os_strdup(pos);
	} else if (os_strcmp(buf, "model_name") == 0) {
		if (os_strlen(pos) > 32) {
			wpa_printf(MSG_ERROR, "Line %d: Too long model_name",
				   line);
			return 1;
		}
		os_free(bss->model_name);
		bss->model_name = os_strdup(pos);
	} else if (os_strcmp(buf, "model_number") == 0) {
		if (os_strlen(pos) > 32) {
			wpa_printf(MSG_ERROR, "Line %d: Too long model_number",
				   line);
			return 1;
		}
		os_free(bss->model_number);
		bss->model_number = os_strdup(pos);
	} else if (os_strcmp(buf, "serial_number") == 0) {
		if (os_strlen(pos) > 32) {
			wpa_printf(MSG_ERROR, "Line %d: Too long serial_number",
				   line);
			return 1;
		}
		os_free(bss->serial_number);
		bss->serial_number = os_strdup(pos);
	} else if (os_strcmp(buf, "device_type") == 0) {
		if (wps_dev_type_str2bin(pos, bss->device_type))
			return 1;
	} else if (os_strcmp(buf, "config_methods") == 0) {
		os_free(bss->config_methods);
		bss->config_methods = os_strdup(pos);
	} else if (os_strcmp(buf, "os_version") == 0) {
		if (hexstr2bin(pos, bss->os_version, 4)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid os_version",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "ap_pin") == 0) {
		os_free(bss->ap_pin);
		if (*pos == '\0')
			bss->ap_pin = NULL;
		else
			bss->ap_pin = os_strdup(pos);
	} else if (os_strcmp(buf, "skip_cred_build") == 0) {
		bss->skip_cred_build = atoi(pos);
	} else if (os_strcmp(buf, "extra_cred") == 0) {
		os_free(bss->extra_cred);
		bss->extra_cred = (u8 *) os_readfile(pos, &bss->extra_cred_len);
		if (bss->extra_cred == NULL) {
			wpa_printf(MSG_ERROR, "Line %d: could not read Credentials from '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "wps_cred_processing") == 0) {
		bss->wps_cred_processing = atoi(pos);
	} else if (os_strcmp(buf, "ap_settings") == 0) {
		os_free(bss->ap_settings);
		bss->ap_settings =
			(u8 *) os_readfile(pos, &bss->ap_settings_len);
		if (bss->ap_settings == NULL) {
			wpa_printf(MSG_ERROR, "Line %d: could not read AP Settings from '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "upnp_iface") == 0) {
		os_free(bss->upnp_iface);
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
	} else if (os_strcmp(buf, "pbc_in_m1") == 0) {
		bss->pbc_in_m1 = atoi(pos);
	} else if (os_strcmp(buf, "server_id") == 0) {
		os_free(bss->server_id);
		bss->server_id = os_strdup(pos);
#ifdef CONFIG_WPS_NFC
	} else if (os_strcmp(buf, "wps_nfc_dev_pw_id") == 0) {
		bss->wps_nfc_dev_pw_id = atoi(pos);
		if (bss->wps_nfc_dev_pw_id < 0x10 ||
		    bss->wps_nfc_dev_pw_id > 0xffff) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid wps_nfc_dev_pw_id value",
				   line);
			return 1;
		}
		bss->wps_nfc_pw_from_config = 1;
	} else if (os_strcmp(buf, "wps_nfc_dh_pubkey") == 0) {
		wpabuf_free(bss->wps_nfc_dh_pubkey);
		bss->wps_nfc_dh_pubkey = wpabuf_parse_bin(pos);
		bss->wps_nfc_pw_from_config = 1;
	} else if (os_strcmp(buf, "wps_nfc_dh_privkey") == 0) {
		wpabuf_free(bss->wps_nfc_dh_privkey);
		bss->wps_nfc_dh_privkey = wpabuf_parse_bin(pos);
		bss->wps_nfc_pw_from_config = 1;
	} else if (os_strcmp(buf, "wps_nfc_dev_pw") == 0) {
		wpabuf_free(bss->wps_nfc_dev_pw);
		bss->wps_nfc_dev_pw = wpabuf_parse_bin(pos);
		bss->wps_nfc_pw_from_config = 1;
#endif /* CONFIG_WPS_NFC */
#endif /* CONFIG_WPS */
#ifdef CONFIG_P2P_MANAGER
	} else if (os_strcmp(buf, "manage_p2p") == 0) {
		if (atoi(pos))
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
		if (atoi(pos))
			bss->tdls |= TDLS_PROHIBIT;
		else
			bss->tdls &= ~TDLS_PROHIBIT;
	} else if (os_strcmp(buf, "tdls_prohibit_chan_switch") == 0) {
		if (atoi(pos))
			bss->tdls |= TDLS_PROHIBIT_CHAN_SWITCH;
		else
			bss->tdls &= ~TDLS_PROHIBIT_CHAN_SWITCH;
#ifdef CONFIG_RSN_TESTING
	} else if (os_strcmp(buf, "rsn_testing") == 0) {
		extern int rsn_testing;
		rsn_testing = atoi(pos);
#endif /* CONFIG_RSN_TESTING */
	} else if (os_strcmp(buf, "time_advertisement") == 0) {
		bss->time_advertisement = atoi(pos);
	} else if (os_strcmp(buf, "time_zone") == 0) {
		size_t tz_len = os_strlen(pos);
		if (tz_len < 4 || tz_len > 255) {
			wpa_printf(MSG_DEBUG, "Line %d: invalid time_zone",
				   line);
			return 1;
		}
		os_free(bss->time_zone);
		bss->time_zone = os_strdup(pos);
		if (bss->time_zone == NULL)
			return 1;
#ifdef CONFIG_WNM_AP
	} else if (os_strcmp(buf, "wnm_sleep_mode") == 0) {
		bss->wnm_sleep_mode = atoi(pos);
	} else if (os_strcmp(buf, "wnm_sleep_mode_no_keys") == 0) {
		bss->wnm_sleep_mode_no_keys = atoi(pos);
	} else if (os_strcmp(buf, "bss_transition") == 0) {
		bss->bss_transition = atoi(pos);
#endif /* CONFIG_WNM_AP */
#ifdef CONFIG_INTERWORKING
	} else if (os_strcmp(buf, "interworking") == 0) {
		bss->interworking = atoi(pos);
	} else if (os_strcmp(buf, "access_network_type") == 0) {
		bss->access_network_type = atoi(pos);
		if (bss->access_network_type < 0 ||
		    bss->access_network_type > 15) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid access_network_type",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "internet") == 0) {
		bss->internet = atoi(pos);
	} else if (os_strcmp(buf, "asra") == 0) {
		bss->asra = atoi(pos);
	} else if (os_strcmp(buf, "esr") == 0) {
		bss->esr = atoi(pos);
	} else if (os_strcmp(buf, "uesa") == 0) {
		bss->uesa = atoi(pos);
	} else if (os_strcmp(buf, "venue_group") == 0) {
		bss->venue_group = atoi(pos);
		bss->venue_info_set = 1;
	} else if (os_strcmp(buf, "venue_type") == 0) {
		bss->venue_type = atoi(pos);
		bss->venue_info_set = 1;
	} else if (os_strcmp(buf, "hessid") == 0) {
		if (hwaddr_aton(pos, bss->hessid)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid hessid", line);
			return 1;
		}
	} else if (os_strcmp(buf, "roaming_consortium") == 0) {
		if (parse_roaming_consortium(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "venue_name") == 0) {
		if (parse_venue_name(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "venue_url") == 0) {
		if (parse_venue_url(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "network_auth_type") == 0) {
		u8 auth_type;
		u16 redirect_url_len;
		if (hexstr2bin(pos, &auth_type, 1)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid network_auth_type '%s'",
				   line, pos);
			return 1;
		}
		if (auth_type == 0 || auth_type == 2)
			redirect_url_len = os_strlen(pos + 2);
		else
			redirect_url_len = 0;
		os_free(bss->network_auth_type);
		bss->network_auth_type = os_malloc(redirect_url_len + 3 + 1);
		if (bss->network_auth_type == NULL)
			return 1;
		*bss->network_auth_type = auth_type;
		WPA_PUT_LE16(bss->network_auth_type + 1, redirect_url_len);
		if (redirect_url_len)
			os_memcpy(bss->network_auth_type + 3, pos + 2,
				  redirect_url_len);
		bss->network_auth_type_len = 3 + redirect_url_len;
	} else if (os_strcmp(buf, "ipaddr_type_availability") == 0) {
		if (hexstr2bin(pos, &bss->ipaddr_type_availability, 1)) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid ipaddr_type_availability '%s'",
				   line, pos);
			bss->ipaddr_type_configured = 0;
			return 1;
		}
		bss->ipaddr_type_configured = 1;
	} else if (os_strcmp(buf, "domain_name") == 0) {
		int j, num_domains, domain_len, domain_list_len = 0;
		char *tok_start, *tok_prev;
		u8 *domain_list, *domain_ptr;

		domain_list_len = os_strlen(pos) + 1;
		domain_list = os_malloc(domain_list_len);
		if (domain_list == NULL)
			return 1;

		domain_ptr = domain_list;
		tok_prev = pos;
		num_domains = 1;
		while ((tok_prev = os_strchr(tok_prev, ','))) {
			num_domains++;
			tok_prev++;
		}
		tok_prev = pos;
		for (j = 0; j < num_domains; j++) {
			tok_start = os_strchr(tok_prev, ',');
			if (tok_start) {
				domain_len = tok_start - tok_prev;
				*domain_ptr = domain_len;
				os_memcpy(domain_ptr + 1, tok_prev, domain_len);
				domain_ptr += domain_len + 1;
				tok_prev = ++tok_start;
			} else {
				domain_len = os_strlen(tok_prev);
				*domain_ptr = domain_len;
				os_memcpy(domain_ptr + 1, tok_prev, domain_len);
				domain_ptr += domain_len + 1;
			}
		}

		os_free(bss->domain_name);
		bss->domain_name = domain_list;
		bss->domain_name_len = domain_list_len;
	} else if (os_strcmp(buf, "anqp_3gpp_cell_net") == 0) {
		if (parse_3gpp_cell_net(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "nai_realm") == 0) {
		if (parse_nai_realm(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "anqp_elem") == 0) {
		if (parse_anqp_elem(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "gas_frag_limit") == 0) {
		int val = atoi(pos);

		if (val <= 0) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid gas_frag_limit '%s'",
				   line, pos);
			return 1;
		}
		bss->gas_frag_limit = val;
	} else if (os_strcmp(buf, "gas_comeback_delay") == 0) {
		bss->gas_comeback_delay = atoi(pos);
	} else if (os_strcmp(buf, "qos_map_set") == 0) {
		if (parse_qos_map_set(bss, pos, line) < 0)
			return 1;
#endif /* CONFIG_INTERWORKING */
#ifdef CONFIG_RADIUS_TEST
	} else if (os_strcmp(buf, "dump_msk_file") == 0) {
		os_free(bss->dump_msk_file);
		bss->dump_msk_file = os_strdup(pos);
#endif /* CONFIG_RADIUS_TEST */
#ifdef CONFIG_PROXYARP
	} else if (os_strcmp(buf, "proxy_arp") == 0) {
		bss->proxy_arp = atoi(pos);
#endif /* CONFIG_PROXYARP */
#ifdef CONFIG_HS20
	} else if (os_strcmp(buf, "hs20") == 0) {
		bss->hs20 = atoi(pos);
	} else if (os_strcmp(buf, "disable_dgaf") == 0) {
		bss->disable_dgaf = atoi(pos);
	} else if (os_strcmp(buf, "na_mcast_to_ucast") == 0) {
		bss->na_mcast_to_ucast = atoi(pos);
	} else if (os_strcmp(buf, "osen") == 0) {
		bss->osen = atoi(pos);
	} else if (os_strcmp(buf, "anqp_domain_id") == 0) {
		bss->anqp_domain_id = atoi(pos);
	} else if (os_strcmp(buf, "hs20_deauth_req_timeout") == 0) {
		bss->hs20_deauth_req_timeout = atoi(pos);
	} else if (os_strcmp(buf, "hs20_oper_friendly_name") == 0) {
		if (hs20_parse_oper_friendly_name(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "hs20_wan_metrics") == 0) {
		if (hs20_parse_wan_metrics(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "hs20_conn_capab") == 0) {
		if (hs20_parse_conn_capab(bss, pos, line) < 0) {
			return 1;
		}
	} else if (os_strcmp(buf, "hs20_operating_class") == 0) {
		u8 *oper_class;
		size_t oper_class_len;
		oper_class_len = os_strlen(pos);
		if (oper_class_len < 2 || (oper_class_len & 0x01)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid hs20_operating_class '%s'",
				   line, pos);
			return 1;
		}
		oper_class_len /= 2;
		oper_class = os_malloc(oper_class_len);
		if (oper_class == NULL)
			return 1;
		if (hexstr2bin(pos, oper_class, oper_class_len)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid hs20_operating_class '%s'",
				   line, pos);
			os_free(oper_class);
			return 1;
		}
		os_free(bss->hs20_operating_class);
		bss->hs20_operating_class = oper_class;
		bss->hs20_operating_class_len = oper_class_len;
	} else if (os_strcmp(buf, "hs20_icon") == 0) {
		if (hs20_parse_icon(bss, pos) < 0) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid hs20_icon '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "osu_ssid") == 0) {
		if (hs20_parse_osu_ssid(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "osu_server_uri") == 0) {
		if (hs20_parse_osu_server_uri(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "osu_friendly_name") == 0) {
		if (hs20_parse_osu_friendly_name(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "osu_nai") == 0) {
		if (hs20_parse_osu_nai(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "osu_nai2") == 0) {
		if (hs20_parse_osu_nai2(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "osu_method_list") == 0) {
		if (hs20_parse_osu_method_list(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "osu_icon") == 0) {
		if (hs20_parse_osu_icon(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "osu_service_desc") == 0) {
		if (hs20_parse_osu_service_desc(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "operator_icon") == 0) {
		if (hs20_parse_operator_icon(bss, pos, line) < 0)
			return 1;
	} else if (os_strcmp(buf, "subscr_remediation_url") == 0) {
		os_free(bss->subscr_remediation_url);
		bss->subscr_remediation_url = os_strdup(pos);
	} else if (os_strcmp(buf, "subscr_remediation_method") == 0) {
		bss->subscr_remediation_method = atoi(pos);
	} else if (os_strcmp(buf, "hs20_t_c_filename") == 0) {
		os_free(bss->t_c_filename);
		bss->t_c_filename = os_strdup(pos);
	} else if (os_strcmp(buf, "hs20_t_c_timestamp") == 0) {
		bss->t_c_timestamp = strtol(pos, NULL, 0);
	} else if (os_strcmp(buf, "hs20_t_c_server_url") == 0) {
		os_free(bss->t_c_server_url);
		bss->t_c_server_url = os_strdup(pos);
#endif /* CONFIG_HS20 */
#ifdef CONFIG_MBO
	} else if (os_strcmp(buf, "mbo") == 0) {
		bss->mbo_enabled = atoi(pos);
	} else if (os_strcmp(buf, "mbo_cell_data_conn_pref") == 0) {
		bss->mbo_cell_data_conn_pref = atoi(pos);
	} else if (os_strcmp(buf, "oce") == 0) {
		bss->oce = atoi(pos);
#endif /* CONFIG_MBO */
#ifdef CONFIG_TESTING_OPTIONS
#define PARSE_TEST_PROBABILITY(_val)				\
	} else if (os_strcmp(buf, #_val) == 0) {		\
		char *end;					\
								\
		conf->_val = strtod(pos, &end);			\
		if (*end || conf->_val < 0.0 ||			\
		    conf->_val > 1.0) {				\
			wpa_printf(MSG_ERROR,			\
				   "Line %d: Invalid value '%s'", \
				   line, pos);			\
			return 1;				\
		}
	PARSE_TEST_PROBABILITY(ignore_probe_probability)
	PARSE_TEST_PROBABILITY(ignore_auth_probability)
	PARSE_TEST_PROBABILITY(ignore_assoc_probability)
	PARSE_TEST_PROBABILITY(ignore_reassoc_probability)
	PARSE_TEST_PROBABILITY(corrupt_gtk_rekey_mic_probability)
	} else if (os_strcmp(buf, "ecsa_ie_only") == 0) {
		conf->ecsa_ie_only = atoi(pos);
	} else if (os_strcmp(buf, "bss_load_test") == 0) {
		WPA_PUT_LE16(bss->bss_load_test, atoi(pos));
		pos = os_strchr(pos, ':');
		if (pos == NULL) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid bss_load_test",
				   line);
			return 1;
		}
		pos++;
		bss->bss_load_test[2] = atoi(pos);
		pos = os_strchr(pos, ':');
		if (pos == NULL) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid bss_load_test",
				   line);
			return 1;
		}
		pos++;
		WPA_PUT_LE16(&bss->bss_load_test[3], atoi(pos));
		bss->bss_load_test_set = 1;
	} else if (os_strcmp(buf, "radio_measurements") == 0) {
		/*
		 * DEPRECATED: This parameter will be removed in the future.
		 * Use rrm_neighbor_report instead.
		 */
		int val = atoi(pos);

		if (val & BIT(0))
			bss->radio_measurements[0] |=
				WLAN_RRM_CAPS_NEIGHBOR_REPORT;
	} else if (os_strcmp(buf, "own_ie_override") == 0) {
		struct wpabuf *tmp;
		size_t len = os_strlen(pos) / 2;

		tmp = wpabuf_alloc(len);
		if (!tmp)
			return 1;

		if (hexstr2bin(pos, wpabuf_put(tmp, len), len)) {
			wpabuf_free(tmp);
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid own_ie_override '%s'",
				   line, pos);
			return 1;
		}

		wpabuf_free(bss->own_ie_override);
		bss->own_ie_override = tmp;
	} else if (os_strcmp(buf, "sae_reflection_attack") == 0) {
		bss->sae_reflection_attack = atoi(pos);
	} else if (os_strcmp(buf, "sae_commit_override") == 0) {
		wpabuf_free(bss->sae_commit_override);
		bss->sae_commit_override = wpabuf_parse_bin(pos);
#endif /* CONFIG_TESTING_OPTIONS */
#ifdef CONFIG_SAE
	} else if (os_strcmp(buf, "sae_password") == 0) {
		if (parse_sae_password(bss, pos) < 0) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid sae_password",
				   line);
			return 1;
		}
#endif /* CONFIG_SAE */
	} else if (os_strcmp(buf, "vendor_elements") == 0) {
		if (parse_wpabuf_hex(line, buf, &bss->vendor_elements, pos))
			return 1;
	} else if (os_strcmp(buf, "assocresp_elements") == 0) {
		if (parse_wpabuf_hex(line, buf, &bss->assocresp_elements, pos))
			return 1;
	} else if (os_strcmp(buf, "sae_anti_clogging_threshold") == 0) {
		bss->sae_anti_clogging_threshold = atoi(pos);
	} else if (os_strcmp(buf, "sae_sync") == 0) {
		bss->sae_sync = atoi(pos);
	} else if (os_strcmp(buf, "sae_groups") == 0) {
		if (hostapd_parse_intlist(&bss->sae_groups, pos)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid sae_groups value '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "sae_require_mfp") == 0) {
		bss->sae_require_mfp = atoi(pos);
	} else if (os_strcmp(buf, "local_pwr_constraint") == 0) {
		int val = atoi(pos);
		if (val < 0 || val > 255) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid local_pwr_constraint %d (expected 0..255)",
				   line, val);
			return 1;
		}
		conf->local_pwr_constraint = val;
	} else if (os_strcmp(buf, "spectrum_mgmt_required") == 0) {
		conf->spectrum_mgmt_required = atoi(pos);
	} else if (os_strcmp(buf, "wowlan_triggers") == 0) {
		os_free(bss->wowlan_triggers);
		bss->wowlan_triggers = os_strdup(pos);
#ifdef CONFIG_FST
	} else if (os_strcmp(buf, "fst_group_id") == 0) {
		size_t len = os_strlen(pos);

		if (!len || len >= sizeof(conf->fst_cfg.group_id)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid fst_group_id value '%s'",
				   line, pos);
			return 1;
		}

		if (conf->fst_cfg.group_id[0]) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Duplicate fst_group value '%s'",
				   line, pos);
			return 1;
		}

		os_strlcpy(conf->fst_cfg.group_id, pos,
			   sizeof(conf->fst_cfg.group_id));
	} else if (os_strcmp(buf, "fst_priority") == 0) {
		char *endp;
		long int val;

		if (!*pos) {
			wpa_printf(MSG_ERROR,
				   "Line %d: fst_priority value not supplied (expected 1..%u)",
				   line, FST_MAX_PRIO_VALUE);
			return -1;
		}

		val = strtol(pos, &endp, 0);
		if (*endp || val < 1 || val > FST_MAX_PRIO_VALUE) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid fst_priority %ld (%s) (expected 1..%u)",
				   line, val, pos, FST_MAX_PRIO_VALUE);
			return 1;
		}
		conf->fst_cfg.priority = (u8) val;
	} else if (os_strcmp(buf, "fst_llt") == 0) {
		char *endp;
		long int val;

		if (!*pos) {
			wpa_printf(MSG_ERROR,
				   "Line %d: fst_llt value not supplied (expected 1..%u)",
				   line, FST_MAX_LLT_MS);
			return -1;
		}
		val = strtol(pos, &endp, 0);
		if (*endp || val < 1 ||
		    (unsigned long int) val > FST_MAX_LLT_MS) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid fst_llt %ld (%s) (expected 1..%u)",
				   line, val, pos, FST_MAX_LLT_MS);
			return 1;
		}
		conf->fst_cfg.llt = (u32) val;
#endif /* CONFIG_FST */
	} else if (os_strcmp(buf, "track_sta_max_num") == 0) {
		conf->track_sta_max_num = atoi(pos);
	} else if (os_strcmp(buf, "track_sta_max_age") == 0) {
		conf->track_sta_max_age = atoi(pos);
	} else if (os_strcmp(buf, "no_probe_resp_if_seen_on") == 0) {
		os_free(bss->no_probe_resp_if_seen_on);
		bss->no_probe_resp_if_seen_on = os_strdup(pos);
	} else if (os_strcmp(buf, "no_auth_if_seen_on") == 0) {
		os_free(bss->no_auth_if_seen_on);
		bss->no_auth_if_seen_on = os_strdup(pos);
	} else if (os_strcmp(buf, "lci") == 0) {
		wpabuf_free(conf->lci);
		conf->lci = wpabuf_parse_bin(pos);
		if (conf->lci && wpabuf_len(conf->lci) == 0) {
			wpabuf_free(conf->lci);
			conf->lci = NULL;
		}
	} else if (os_strcmp(buf, "civic") == 0) {
		wpabuf_free(conf->civic);
		conf->civic = wpabuf_parse_bin(pos);
		if (conf->civic && wpabuf_len(conf->civic) == 0) {
			wpabuf_free(conf->civic);
			conf->civic = NULL;
		}
	} else if (os_strcmp(buf, "rrm_neighbor_report") == 0) {
		if (atoi(pos))
			bss->radio_measurements[0] |=
				WLAN_RRM_CAPS_NEIGHBOR_REPORT;
	} else if (os_strcmp(buf, "rrm_beacon_report") == 0) {
		if (atoi(pos))
			bss->radio_measurements[0] |=
				WLAN_RRM_CAPS_BEACON_REPORT_PASSIVE |
				WLAN_RRM_CAPS_BEACON_REPORT_ACTIVE |
				WLAN_RRM_CAPS_BEACON_REPORT_TABLE;
	} else if (os_strcmp(buf, "gas_address3") == 0) {
		bss->gas_address3 = atoi(pos);
	} else if (os_strcmp(buf, "stationary_ap") == 0) {
		conf->stationary_ap = atoi(pos);
	} else if (os_strcmp(buf, "ftm_responder") == 0) {
		bss->ftm_responder = atoi(pos);
	} else if (os_strcmp(buf, "ftm_initiator") == 0) {
		bss->ftm_initiator = atoi(pos);
#ifdef CONFIG_FILS
	} else if (os_strcmp(buf, "fils_cache_id") == 0) {
		if (hexstr2bin(pos, bss->fils_cache_id, FILS_CACHE_ID_LEN)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid fils_cache_id '%s'",
				   line, pos);
			return 1;
		}
		bss->fils_cache_id_set = 1;
	} else if (os_strcmp(buf, "fils_realm") == 0) {
		if (parse_fils_realm(bss, pos) < 0)
			return 1;
	} else if (os_strcmp(buf, "fils_dh_group") == 0) {
		bss->fils_dh_group = atoi(pos);
	} else if (os_strcmp(buf, "dhcp_server") == 0) {
		if (hostapd_parse_ip_addr(pos, &bss->dhcp_server)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid IP address '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "dhcp_rapid_commit_proxy") == 0) {
		bss->dhcp_rapid_commit_proxy = atoi(pos);
	} else if (os_strcmp(buf, "fils_hlp_wait_time") == 0) {
		bss->fils_hlp_wait_time = atoi(pos);
	} else if (os_strcmp(buf, "dhcp_server_port") == 0) {
		bss->dhcp_server_port = atoi(pos);
	} else if (os_strcmp(buf, "dhcp_relay_port") == 0) {
		bss->dhcp_relay_port = atoi(pos);
#endif /* CONFIG_FILS */
	} else if (os_strcmp(buf, "multicast_to_unicast") == 0) {
		bss->multicast_to_unicast = atoi(pos);
	} else if (os_strcmp(buf, "broadcast_deauth") == 0) {
		bss->broadcast_deauth = atoi(pos);
#ifdef CONFIG_DPP
	} else if (os_strcmp(buf, "dpp_connector") == 0) {
		os_free(bss->dpp_connector);
		bss->dpp_connector = os_strdup(pos);
	} else if (os_strcmp(buf, "dpp_netaccesskey") == 0) {
		if (parse_wpabuf_hex(line, buf, &bss->dpp_netaccesskey, pos))
			return 1;
	} else if (os_strcmp(buf, "dpp_netaccesskey_expiry") == 0) {
		bss->dpp_netaccesskey_expiry = strtol(pos, NULL, 0);
	} else if (os_strcmp(buf, "dpp_csign") == 0) {
		if (parse_wpabuf_hex(line, buf, &bss->dpp_csign, pos))
			return 1;
#endif /* CONFIG_DPP */
#ifdef CONFIG_OWE
	} else if (os_strcmp(buf, "owe_transition_bssid") == 0) {
		if (hwaddr_aton(pos, bss->owe_transition_bssid)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid owe_transition_bssid",
				   line);
			return 1;
		}
	} else if (os_strcmp(buf, "owe_transition_ssid") == 0) {
		size_t slen;
		char *str = wpa_config_parse_string(pos, &slen);

		if (!str || slen < 1 || slen > SSID_MAX_LEN) {
			wpa_printf(MSG_ERROR, "Line %d: invalid SSID '%s'",
				   line, pos);
			os_free(str);
			return 1;
		}
		os_memcpy(bss->owe_transition_ssid, str, slen);
		bss->owe_transition_ssid_len = slen;
		os_free(str);
	} else if (os_strcmp(buf, "owe_transition_ifname") == 0) {
		os_strlcpy(bss->owe_transition_ifname, pos,
			   sizeof(bss->owe_transition_ifname));
	} else if (os_strcmp(buf, "owe_groups") == 0) {
		if (hostapd_parse_intlist(&bss->owe_groups, pos)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid owe_groups value '%s'",
				   line, pos);
			return 1;
		}
	} else if (os_strcmp(buf, "coloc_intf_reporting") == 0) {
		bss->coloc_intf_reporting = atoi(pos);
#endif /* CONFIG_OWE */
	} else {
		wpa_printf(MSG_ERROR,
			   "Line %d: unknown configuration item '%s'",
			   line, buf);
		return 1;
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
	FILE *f;
	char buf[4096], *pos;
	int line = 0;
	int errors = 0;
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

	conf->last_bss = conf->bss[0];

	while (fgets(buf, sizeof(buf), f)) {
		struct hostapd_bss_config *bss;

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
		errors += hostapd_config_fill(conf, bss, buf, pos, line);
	}

	fclose(f);

	for (i = 0; i < conf->num_bss; i++)
		hostapd_set_security_params(conf->bss[i], 1);

	if (hostapd_config_check(conf, 1))
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


int hostapd_set_iface(struct hostapd_config *conf,
		      struct hostapd_bss_config *bss, const char *field,
		      char *value)
{
	int errors;
	size_t i;

	errors = hostapd_config_fill(conf, bss, field, value, 0);
	if (errors) {
		wpa_printf(MSG_INFO, "Failed to set configuration field '%s' "
			   "to value '%s'", field, value);
		return -1;
	}

	for (i = 0; i < conf->num_bss; i++)
		hostapd_set_security_params(conf->bss[i], 0);

	if (hostapd_config_check(conf, 0)) {
		wpa_printf(MSG_ERROR, "Configuration check failed");
		return -1;
	}

	return 0;
}
