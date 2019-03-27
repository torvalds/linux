/*
 * hostapd / EAP user database
 * Copyright (c) 2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#ifdef CONFIG_SQLITE
#include <sqlite3.h>
#endif /* CONFIG_SQLITE */

#include "common.h"
#include "eap_common/eap_wsc_common.h"
#include "eap_server/eap_methods.h"
#include "eap_server/eap.h"
#include "ap_config.h"
#include "hostapd.h"

#ifdef CONFIG_SQLITE

static void set_user_methods(struct hostapd_eap_user *user, const char *methods)
{
	char *buf, *start;
	int num_methods;

	buf = os_strdup(methods);
	if (buf == NULL)
		return;

	os_memset(&user->methods, 0, sizeof(user->methods));
	num_methods = 0;
	start = buf;
	while (*start) {
		char *pos3 = os_strchr(start, ',');
		if (pos3)
			*pos3++ = '\0';
		user->methods[num_methods].method =
			eap_server_get_type(start,
					    &user->methods[num_methods].vendor);
		if (user->methods[num_methods].vendor == EAP_VENDOR_IETF &&
		    user->methods[num_methods].method == EAP_TYPE_NONE) {
			if (os_strcmp(start, "TTLS-PAP") == 0) {
				user->ttls_auth |= EAP_TTLS_AUTH_PAP;
				goto skip_eap;
			}
			if (os_strcmp(start, "TTLS-CHAP") == 0) {
				user->ttls_auth |= EAP_TTLS_AUTH_CHAP;
				goto skip_eap;
			}
			if (os_strcmp(start, "TTLS-MSCHAP") == 0) {
				user->ttls_auth |= EAP_TTLS_AUTH_MSCHAP;
				goto skip_eap;
			}
			if (os_strcmp(start, "TTLS-MSCHAPV2") == 0) {
				user->ttls_auth |= EAP_TTLS_AUTH_MSCHAPV2;
				goto skip_eap;
			}
			wpa_printf(MSG_INFO, "DB: Unsupported EAP type '%s'",
				   start);
			os_free(buf);
			return;
		}

		num_methods++;
		if (num_methods >= EAP_MAX_METHODS)
			break;
	skip_eap:
		if (pos3 == NULL)
			break;
		start = pos3;
	}

	os_free(buf);
}


static int get_user_cb(void *ctx, int argc, char *argv[], char *col[])
{
	struct hostapd_eap_user *user = ctx;
	int i;

	for (i = 0; i < argc; i++) {
		if (os_strcmp(col[i], "password") == 0 && argv[i]) {
			bin_clear_free(user->password, user->password_len);
			user->password_len = os_strlen(argv[i]);
			user->password = (u8 *) os_strdup(argv[i]);
			user->next = (void *) 1;
		} else if (os_strcmp(col[i], "methods") == 0 && argv[i]) {
			set_user_methods(user, argv[i]);
		} else if (os_strcmp(col[i], "remediation") == 0 && argv[i]) {
			user->remediation = strlen(argv[i]) > 0;
		} else if (os_strcmp(col[i], "t_c_timestamp") == 0 && argv[i]) {
			user->t_c_timestamp = strtol(argv[i], NULL, 10);
		}
	}

	return 0;
}


static int get_wildcard_cb(void *ctx, int argc, char *argv[], char *col[])
{
	struct hostapd_eap_user *user = ctx;
	int i, id = -1, methods = -1;
	size_t len;

	for (i = 0; i < argc; i++) {
		if (os_strcmp(col[i], "identity") == 0 && argv[i])
			id = i;
		else if (os_strcmp(col[i], "methods") == 0 && argv[i])
			methods = i;
	}

	if (id < 0 || methods < 0)
		return 0;

	len = os_strlen(argv[id]);
	if (len <= user->identity_len &&
	    os_memcmp(argv[id], user->identity, len) == 0 &&
	    (user->password == NULL || len > user->password_len)) {
		bin_clear_free(user->password, user->password_len);
		user->password_len = os_strlen(argv[id]);
		user->password = (u8 *) os_strdup(argv[id]);
		user->next = (void *) 1;
		set_user_methods(user, argv[methods]);
	}

	return 0;
}


static const struct hostapd_eap_user *
eap_user_sqlite_get(struct hostapd_data *hapd, const u8 *identity,
		    size_t identity_len, int phase2)
{
	sqlite3 *db;
	struct hostapd_eap_user *user = NULL;
	char id_str[256], cmd[300];
	size_t i;

	if (identity_len >= sizeof(id_str)) {
		wpa_printf(MSG_DEBUG, "%s: identity len too big: %d >= %d",
			   __func__, (int) identity_len,
			   (int) (sizeof(id_str)));
		return NULL;
	}
	os_memcpy(id_str, identity, identity_len);
	id_str[identity_len] = '\0';
	for (i = 0; i < identity_len; i++) {
		if (id_str[i] >= 'a' && id_str[i] <= 'z')
			continue;
		if (id_str[i] >= 'A' && id_str[i] <= 'Z')
			continue;
		if (id_str[i] >= '0' && id_str[i] <= '9')
			continue;
		if (id_str[i] == '-' || id_str[i] == '_' || id_str[i] == '.' ||
		    id_str[i] == ',' || id_str[i] == '@' || id_str[i] == '\\' ||
		    id_str[i] == '!' || id_str[i] == '#' || id_str[i] == '%' ||
		    id_str[i] == '=' || id_str[i] == ' ')
			continue;
		wpa_printf(MSG_INFO, "DB: Unsupported character in identity");
		return NULL;
	}

	bin_clear_free(hapd->tmp_eap_user.identity,
		       hapd->tmp_eap_user.identity_len);
	bin_clear_free(hapd->tmp_eap_user.password,
		       hapd->tmp_eap_user.password_len);
	os_memset(&hapd->tmp_eap_user, 0, sizeof(hapd->tmp_eap_user));
	hapd->tmp_eap_user.phase2 = phase2;
	hapd->tmp_eap_user.identity = os_zalloc(identity_len + 1);
	if (hapd->tmp_eap_user.identity == NULL)
		return NULL;
	os_memcpy(hapd->tmp_eap_user.identity, identity, identity_len);

	if (sqlite3_open(hapd->conf->eap_user_sqlite, &db)) {
		wpa_printf(MSG_INFO, "DB: Failed to open database %s: %s",
			   hapd->conf->eap_user_sqlite, sqlite3_errmsg(db));
		sqlite3_close(db);
		return NULL;
	}

	os_snprintf(cmd, sizeof(cmd),
		    "SELECT * FROM users WHERE identity='%s' AND phase2=%d;",
		    id_str, phase2);
	wpa_printf(MSG_DEBUG, "DB: %s", cmd);
	if (sqlite3_exec(db, cmd, get_user_cb, &hapd->tmp_eap_user, NULL) !=
	    SQLITE_OK) {
		wpa_printf(MSG_DEBUG,
			   "DB: Failed to complete SQL operation: %s  db: %s",
			   sqlite3_errmsg(db), hapd->conf->eap_user_sqlite);
	} else if (hapd->tmp_eap_user.next)
		user = &hapd->tmp_eap_user;

	if (user == NULL && !phase2) {
		os_snprintf(cmd, sizeof(cmd),
			    "SELECT identity,methods FROM wildcards;");
		wpa_printf(MSG_DEBUG, "DB: %s", cmd);
		if (sqlite3_exec(db, cmd, get_wildcard_cb, &hapd->tmp_eap_user,
				 NULL) != SQLITE_OK) {
			wpa_printf(MSG_DEBUG,
				   "DB: Failed to complete SQL operation: %s  db: %s",
				   sqlite3_errmsg(db),
				   hapd->conf->eap_user_sqlite);
		} else if (hapd->tmp_eap_user.next) {
			user = &hapd->tmp_eap_user;
			os_free(user->identity);
			user->identity = user->password;
			user->identity_len = user->password_len;
			user->password = NULL;
			user->password_len = 0;
		}
	}

	sqlite3_close(db);

	return user;
}

#endif /* CONFIG_SQLITE */


const struct hostapd_eap_user *
hostapd_get_eap_user(struct hostapd_data *hapd, const u8 *identity,
		     size_t identity_len, int phase2)
{
	const struct hostapd_bss_config *conf = hapd->conf;
	struct hostapd_eap_user *user = conf->eap_user;

#ifdef CONFIG_WPS
	if (conf->wps_state && identity_len == WSC_ID_ENROLLEE_LEN &&
	    os_memcmp(identity, WSC_ID_ENROLLEE, WSC_ID_ENROLLEE_LEN) == 0) {
		static struct hostapd_eap_user wsc_enrollee;
		os_memset(&wsc_enrollee, 0, sizeof(wsc_enrollee));
		wsc_enrollee.methods[0].method = eap_server_get_type(
			"WSC", &wsc_enrollee.methods[0].vendor);
		return &wsc_enrollee;
	}

	if (conf->wps_state && identity_len == WSC_ID_REGISTRAR_LEN &&
	    os_memcmp(identity, WSC_ID_REGISTRAR, WSC_ID_REGISTRAR_LEN) == 0) {
		static struct hostapd_eap_user wsc_registrar;
		os_memset(&wsc_registrar, 0, sizeof(wsc_registrar));
		wsc_registrar.methods[0].method = eap_server_get_type(
			"WSC", &wsc_registrar.methods[0].vendor);
		wsc_registrar.password = (u8 *) conf->ap_pin;
		wsc_registrar.password_len = conf->ap_pin ?
			os_strlen(conf->ap_pin) : 0;
		return &wsc_registrar;
	}
#endif /* CONFIG_WPS */

	while (user) {
		if (!phase2 && user->identity == NULL) {
			/* Wildcard match */
			break;
		}

		if (user->phase2 == !!phase2 && user->wildcard_prefix &&
		    identity_len >= user->identity_len &&
		    os_memcmp(user->identity, identity, user->identity_len) ==
		    0) {
			/* Wildcard prefix match */
			break;
		}

		if (user->phase2 == !!phase2 &&
		    user->identity_len == identity_len &&
		    os_memcmp(user->identity, identity, identity_len) == 0)
			break;
		user = user->next;
	}

#ifdef CONFIG_SQLITE
	if (user == NULL && conf->eap_user_sqlite) {
		return eap_user_sqlite_get(hapd, identity, identity_len,
					   phase2);
	}
#endif /* CONFIG_SQLITE */

	return user;
}
