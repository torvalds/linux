/*
 * WPA Supplicant / Configuration parser and common functions
 * Copyright (c) 2003-2018, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "utils/uuid.h"
#include "utils/ip_addr.h"
#include "common/ieee802_1x_defs.h"
#include "crypto/sha1.h"
#include "rsn_supp/wpa.h"
#include "eap_peer/eap.h"
#include "p2p/p2p.h"
#include "fst/fst.h"
#include "config.h"


#if !defined(CONFIG_CTRL_IFACE) && defined(CONFIG_NO_CONFIG_WRITE)
#define NO_CONFIG_WRITE
#endif

/*
 * Structure for network configuration parsing. This data is used to implement
 * a generic parser for each network block variable. The table of configuration
 * variables is defined below in this file (ssid_fields[]).
 */
struct parse_data {
	/* Configuration variable name */
	char *name;

	/* Parser function for this variable. The parser functions return 0 or 1
	 * to indicate success. Value 0 indicates that the parameter value may
	 * have changed while value 1 means that the value did not change.
	 * Error cases (failure to parse the string) are indicated by returning
	 * -1. */
	int (*parser)(const struct parse_data *data, struct wpa_ssid *ssid,
		      int line, const char *value);

#ifndef NO_CONFIG_WRITE
	/* Writer function (i.e., to get the variable in text format from
	 * internal presentation). */
	char * (*writer)(const struct parse_data *data, struct wpa_ssid *ssid);
#endif /* NO_CONFIG_WRITE */

	/* Variable specific parameters for the parser. */
	void *param1, *param2, *param3, *param4;

	/* 0 = this variable can be included in debug output and ctrl_iface
	 * 1 = this variable contains key/private data and it must not be
	 *     included in debug output unless explicitly requested. In
	 *     addition, this variable will not be readable through the
	 *     ctrl_iface.
	 */
	int key_data;
};


static int wpa_config_parse_str(const struct parse_data *data,
				struct wpa_ssid *ssid,
				int line, const char *value)
{
	size_t res_len, *dst_len, prev_len;
	char **dst, *tmp;

	if (os_strcmp(value, "NULL") == 0) {
		wpa_printf(MSG_DEBUG, "Unset configuration string '%s'",
			   data->name);
		tmp = NULL;
		res_len = 0;
		goto set;
	}

	tmp = wpa_config_parse_string(value, &res_len);
	if (tmp == NULL) {
		wpa_printf(MSG_ERROR, "Line %d: failed to parse %s '%s'.",
			   line, data->name,
			   data->key_data ? "[KEY DATA REMOVED]" : value);
		return -1;
	}

	if (data->key_data) {
		wpa_hexdump_ascii_key(MSG_MSGDUMP, data->name,
				      (u8 *) tmp, res_len);
	} else {
		wpa_hexdump_ascii(MSG_MSGDUMP, data->name,
				  (u8 *) tmp, res_len);
	}

	if (data->param3 && res_len < (size_t) data->param3) {
		wpa_printf(MSG_ERROR, "Line %d: too short %s (len=%lu "
			   "min_len=%ld)", line, data->name,
			   (unsigned long) res_len, (long) data->param3);
		os_free(tmp);
		return -1;
	}

	if (data->param4 && res_len > (size_t) data->param4) {
		wpa_printf(MSG_ERROR, "Line %d: too long %s (len=%lu "
			   "max_len=%ld)", line, data->name,
			   (unsigned long) res_len, (long) data->param4);
		os_free(tmp);
		return -1;
	}

set:
	dst = (char **) (((u8 *) ssid) + (long) data->param1);
	dst_len = (size_t *) (((u8 *) ssid) + (long) data->param2);

	if (data->param2)
		prev_len = *dst_len;
	else if (*dst)
		prev_len = os_strlen(*dst);
	else
		prev_len = 0;
	if ((*dst == NULL && tmp == NULL) ||
	    (*dst && tmp && prev_len == res_len &&
	     os_memcmp(*dst, tmp, res_len) == 0)) {
		/* No change to the previously configured value */
		os_free(tmp);
		return 1;
	}

	os_free(*dst);
	*dst = tmp;
	if (data->param2)
		*dst_len = res_len;

	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_string_ascii(const u8 *value, size_t len)
{
	char *buf;

	buf = os_malloc(len + 3);
	if (buf == NULL)
		return NULL;
	buf[0] = '"';
	os_memcpy(buf + 1, value, len);
	buf[len + 1] = '"';
	buf[len + 2] = '\0';

	return buf;
}


static char * wpa_config_write_string_hex(const u8 *value, size_t len)
{
	char *buf;

	buf = os_zalloc(2 * len + 1);
	if (buf == NULL)
		return NULL;
	wpa_snprintf_hex(buf, 2 * len + 1, value, len);

	return buf;
}


static char * wpa_config_write_string(const u8 *value, size_t len)
{
	if (value == NULL)
		return NULL;

	if (is_hex(value, len))
		return wpa_config_write_string_hex(value, len);
	else
		return wpa_config_write_string_ascii(value, len);
}


static char * wpa_config_write_str(const struct parse_data *data,
				   struct wpa_ssid *ssid)
{
	size_t len;
	char **src;

	src = (char **) (((u8 *) ssid) + (long) data->param1);
	if (*src == NULL)
		return NULL;

	if (data->param2)
		len = *((size_t *) (((u8 *) ssid) + (long) data->param2));
	else
		len = os_strlen(*src);

	return wpa_config_write_string((const u8 *) *src, len);
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_int(const struct parse_data *data,
				struct wpa_ssid *ssid,
				int line, const char *value)
{
	int val, *dst;
	char *end;

	dst = (int *) (((u8 *) ssid) + (long) data->param1);
	val = strtol(value, &end, 0);
	if (*end) {
		wpa_printf(MSG_ERROR, "Line %d: invalid number \"%s\"",
			   line, value);
		return -1;
	}

	if (*dst == val)
		return 1;
	*dst = val;
	wpa_printf(MSG_MSGDUMP, "%s=%d (0x%x)", data->name, *dst, *dst);

	if (data->param3 && *dst < (long) data->param3) {
		wpa_printf(MSG_ERROR, "Line %d: too small %s (value=%d "
			   "min_value=%ld)", line, data->name, *dst,
			   (long) data->param3);
		*dst = (long) data->param3;
		return -1;
	}

	if (data->param4 && *dst > (long) data->param4) {
		wpa_printf(MSG_ERROR, "Line %d: too large %s (value=%d "
			   "max_value=%ld)", line, data->name, *dst,
			   (long) data->param4);
		*dst = (long) data->param4;
		return -1;
	}

	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_int(const struct parse_data *data,
				   struct wpa_ssid *ssid)
{
	int *src, res;
	char *value;

	src = (int *) (((u8 *) ssid) + (long) data->param1);

	value = os_malloc(20);
	if (value == NULL)
		return NULL;
	res = os_snprintf(value, 20, "%d", *src);
	if (os_snprintf_error(20, res)) {
		os_free(value);
		return NULL;
	}
	value[20 - 1] = '\0';
	return value;
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_addr_list(const struct parse_data *data,
				      int line, const char *value,
				      u8 **list, size_t *num, char *name,
				      u8 abort_on_error, u8 masked)
{
	const char *pos;
	u8 *buf, *n, addr[2 * ETH_ALEN];
	size_t count;

	buf = NULL;
	count = 0;

	pos = value;
	while (pos && *pos) {
		while (*pos == ' ')
			pos++;

		if (hwaddr_masked_aton(pos, addr, &addr[ETH_ALEN], masked)) {
			if (abort_on_error || count == 0) {
				wpa_printf(MSG_ERROR,
					   "Line %d: Invalid %s address '%s'",
					   line, name, value);
				os_free(buf);
				return -1;
			}
			/* continue anyway since this could have been from a
			 * truncated configuration file line */
			wpa_printf(MSG_INFO,
				   "Line %d: Ignore likely truncated %s address '%s'",
				   line, name, pos);
		} else {
			n = os_realloc_array(buf, count + 1, 2 * ETH_ALEN);
			if (n == NULL) {
				os_free(buf);
				return -1;
			}
			buf = n;
			os_memmove(buf + 2 * ETH_ALEN, buf,
				   count * 2 * ETH_ALEN);
			os_memcpy(buf, addr, 2 * ETH_ALEN);
			count++;
			wpa_printf(MSG_MSGDUMP,
				   "%s: addr=" MACSTR " mask=" MACSTR,
				   name, MAC2STR(addr),
				   MAC2STR(&addr[ETH_ALEN]));
		}

		pos = os_strchr(pos, ' ');
	}

	os_free(*list);
	*list = buf;
	*num = count;

	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_addr_list(const struct parse_data *data,
					 const u8 *list, size_t num, char *name)
{
	char *value, *end, *pos;
	int res;
	size_t i;

	if (list == NULL || num == 0)
		return NULL;

	value = os_malloc(2 * 20 * num);
	if (value == NULL)
		return NULL;
	pos = value;
	end = value + 2 * 20 * num;

	for (i = num; i > 0; i--) {
		const u8 *a = list + (i - 1) * 2 * ETH_ALEN;
		const u8 *m = a + ETH_ALEN;

		if (i < num)
			*pos++ = ' ';
		res = hwaddr_mask_txt(pos, end - pos, a, m);
		if (res < 0) {
			os_free(value);
			return NULL;
		}
		pos += res;
	}

	return value;
}
#endif /* NO_CONFIG_WRITE */

static int wpa_config_parse_bssid(const struct parse_data *data,
				  struct wpa_ssid *ssid, int line,
				  const char *value)
{
	if (value[0] == '\0' || os_strcmp(value, "\"\"") == 0 ||
	    os_strcmp(value, "any") == 0) {
		ssid->bssid_set = 0;
		wpa_printf(MSG_MSGDUMP, "BSSID any");
		return 0;
	}
	if (hwaddr_aton(value, ssid->bssid)) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid BSSID '%s'.",
			   line, value);
		return -1;
	}
	ssid->bssid_set = 1;
	wpa_hexdump(MSG_MSGDUMP, "BSSID", ssid->bssid, ETH_ALEN);
	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_bssid(const struct parse_data *data,
				     struct wpa_ssid *ssid)
{
	char *value;
	int res;

	if (!ssid->bssid_set)
		return NULL;

	value = os_malloc(20);
	if (value == NULL)
		return NULL;
	res = os_snprintf(value, 20, MACSTR, MAC2STR(ssid->bssid));
	if (os_snprintf_error(20, res)) {
		os_free(value);
		return NULL;
	}
	value[20 - 1] = '\0';
	return value;
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_bssid_hint(const struct parse_data *data,
				       struct wpa_ssid *ssid, int line,
				       const char *value)
{
	if (value[0] == '\0' || os_strcmp(value, "\"\"") == 0 ||
	    os_strcmp(value, "any") == 0) {
		ssid->bssid_hint_set = 0;
		wpa_printf(MSG_MSGDUMP, "BSSID hint any");
		return 0;
	}
	if (hwaddr_aton(value, ssid->bssid_hint)) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid BSSID hint '%s'.",
			   line, value);
		return -1;
	}
	ssid->bssid_hint_set = 1;
	wpa_hexdump(MSG_MSGDUMP, "BSSID hint", ssid->bssid_hint, ETH_ALEN);
	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_bssid_hint(const struct parse_data *data,
					  struct wpa_ssid *ssid)
{
	char *value;
	int res;

	if (!ssid->bssid_hint_set)
		return NULL;

	value = os_malloc(20);
	if (!value)
		return NULL;
	res = os_snprintf(value, 20, MACSTR, MAC2STR(ssid->bssid_hint));
	if (os_snprintf_error(20, res)) {
		os_free(value);
		return NULL;
	}
	return value;
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_bssid_blacklist(const struct parse_data *data,
					    struct wpa_ssid *ssid, int line,
					    const char *value)
{
	return wpa_config_parse_addr_list(data, line, value,
					  &ssid->bssid_blacklist,
					  &ssid->num_bssid_blacklist,
					  "bssid_blacklist", 1, 1);
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_bssid_blacklist(const struct parse_data *data,
					       struct wpa_ssid *ssid)
{
	return wpa_config_write_addr_list(data, ssid->bssid_blacklist,
					  ssid->num_bssid_blacklist,
					  "bssid_blacklist");
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_bssid_whitelist(const struct parse_data *data,
					    struct wpa_ssid *ssid, int line,
					    const char *value)
{
	return wpa_config_parse_addr_list(data, line, value,
					  &ssid->bssid_whitelist,
					  &ssid->num_bssid_whitelist,
					  "bssid_whitelist", 1, 1);
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_bssid_whitelist(const struct parse_data *data,
					       struct wpa_ssid *ssid)
{
	return wpa_config_write_addr_list(data, ssid->bssid_whitelist,
					  ssid->num_bssid_whitelist,
					  "bssid_whitelist");
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_psk(const struct parse_data *data,
				struct wpa_ssid *ssid, int line,
				const char *value)
{
#ifdef CONFIG_EXT_PASSWORD
	if (os_strncmp(value, "ext:", 4) == 0) {
		str_clear_free(ssid->passphrase);
		ssid->passphrase = NULL;
		ssid->psk_set = 0;
		os_free(ssid->ext_psk);
		ssid->ext_psk = os_strdup(value + 4);
		if (ssid->ext_psk == NULL)
			return -1;
		wpa_printf(MSG_DEBUG, "PSK: External password '%s'",
			   ssid->ext_psk);
		return 0;
	}
#endif /* CONFIG_EXT_PASSWORD */

	if (*value == '"') {
#ifndef CONFIG_NO_PBKDF2
		const char *pos;
		size_t len;

		value++;
		pos = os_strrchr(value, '"');
		if (pos)
			len = pos - value;
		else
			len = os_strlen(value);
		if (len < 8 || len > 63) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid passphrase "
				   "length %lu (expected: 8..63) '%s'.",
				   line, (unsigned long) len, value);
			return -1;
		}
		wpa_hexdump_ascii_key(MSG_MSGDUMP, "PSK (ASCII passphrase)",
				      (u8 *) value, len);
		if (has_ctrl_char((u8 *) value, len)) {
			wpa_printf(MSG_ERROR,
				   "Line %d: Invalid passphrase character",
				   line);
			return -1;
		}
		if (ssid->passphrase && os_strlen(ssid->passphrase) == len &&
		    os_memcmp(ssid->passphrase, value, len) == 0) {
			/* No change to the previously configured value */
			return 1;
		}
		ssid->psk_set = 0;
		str_clear_free(ssid->passphrase);
		ssid->passphrase = dup_binstr(value, len);
		if (ssid->passphrase == NULL)
			return -1;
		return 0;
#else /* CONFIG_NO_PBKDF2 */
		wpa_printf(MSG_ERROR, "Line %d: ASCII passphrase not "
			   "supported.", line);
		return -1;
#endif /* CONFIG_NO_PBKDF2 */
	}

	if (hexstr2bin(value, ssid->psk, PMK_LEN) ||
	    value[PMK_LEN * 2] != '\0') {
		wpa_printf(MSG_ERROR, "Line %d: Invalid PSK '%s'.",
			   line, value);
		return -1;
	}

	str_clear_free(ssid->passphrase);
	ssid->passphrase = NULL;

	ssid->psk_set = 1;
	wpa_hexdump_key(MSG_MSGDUMP, "PSK", ssid->psk, PMK_LEN);
	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_psk(const struct parse_data *data,
				   struct wpa_ssid *ssid)
{
#ifdef CONFIG_EXT_PASSWORD
	if (ssid->ext_psk) {
		size_t len = 4 + os_strlen(ssid->ext_psk) + 1;
		char *buf = os_malloc(len);
		int res;

		if (buf == NULL)
			return NULL;
		res = os_snprintf(buf, len, "ext:%s", ssid->ext_psk);
		if (os_snprintf_error(len, res)) {
			os_free(buf);
			buf = NULL;
		}
		return buf;
	}
#endif /* CONFIG_EXT_PASSWORD */

	if (ssid->passphrase)
		return wpa_config_write_string_ascii(
			(const u8 *) ssid->passphrase,
			os_strlen(ssid->passphrase));

	if (ssid->psk_set)
		return wpa_config_write_string_hex(ssid->psk, PMK_LEN);

	return NULL;
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_proto(const struct parse_data *data,
				  struct wpa_ssid *ssid, int line,
				  const char *value)
{
	int val = 0, last, errors = 0;
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
		if (os_strcmp(start, "WPA") == 0)
			val |= WPA_PROTO_WPA;
		else if (os_strcmp(start, "RSN") == 0 ||
			 os_strcmp(start, "WPA2") == 0)
			val |= WPA_PROTO_RSN;
		else if (os_strcmp(start, "OSEN") == 0)
			val |= WPA_PROTO_OSEN;
		else {
			wpa_printf(MSG_ERROR, "Line %d: invalid proto '%s'",
				   line, start);
			errors++;
		}

		if (last)
			break;
		start = end + 1;
	}
	os_free(buf);

	if (val == 0) {
		wpa_printf(MSG_ERROR,
			   "Line %d: no proto values configured.", line);
		errors++;
	}

	if (!errors && ssid->proto == val)
		return 1;
	wpa_printf(MSG_MSGDUMP, "proto: 0x%x", val);
	ssid->proto = val;
	return errors ? -1 : 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_proto(const struct parse_data *data,
				     struct wpa_ssid *ssid)
{
	int ret;
	char *buf, *pos, *end;

	pos = buf = os_zalloc(20);
	if (buf == NULL)
		return NULL;
	end = buf + 20;

	if (ssid->proto & WPA_PROTO_WPA) {
		ret = os_snprintf(pos, end - pos, "%sWPA",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret))
			return buf;
		pos += ret;
	}

	if (ssid->proto & WPA_PROTO_RSN) {
		ret = os_snprintf(pos, end - pos, "%sRSN",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret))
			return buf;
		pos += ret;
	}

	if (ssid->proto & WPA_PROTO_OSEN) {
		ret = os_snprintf(pos, end - pos, "%sOSEN",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret))
			return buf;
		pos += ret;
	}

	if (pos == buf) {
		os_free(buf);
		buf = NULL;
	}

	return buf;
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_key_mgmt(const struct parse_data *data,
				     struct wpa_ssid *ssid, int line,
				     const char *value)
{
	int val = 0, last, errors = 0;
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
		else if (os_strcmp(start, "IEEE8021X") == 0)
			val |= WPA_KEY_MGMT_IEEE8021X_NO_WPA;
		else if (os_strcmp(start, "NONE") == 0)
			val |= WPA_KEY_MGMT_NONE;
		else if (os_strcmp(start, "WPA-NONE") == 0)
			val |= WPA_KEY_MGMT_WPA_NONE;
#ifdef CONFIG_IEEE80211R
		else if (os_strcmp(start, "FT-PSK") == 0)
			val |= WPA_KEY_MGMT_FT_PSK;
		else if (os_strcmp(start, "FT-EAP") == 0)
			val |= WPA_KEY_MGMT_FT_IEEE8021X;
#ifdef CONFIG_SHA384
		else if (os_strcmp(start, "FT-EAP-SHA384") == 0)
			val |= WPA_KEY_MGMT_FT_IEEE8021X_SHA384;
#endif /* CONFIG_SHA384 */
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_IEEE80211W
		else if (os_strcmp(start, "WPA-PSK-SHA256") == 0)
			val |= WPA_KEY_MGMT_PSK_SHA256;
		else if (os_strcmp(start, "WPA-EAP-SHA256") == 0)
			val |= WPA_KEY_MGMT_IEEE8021X_SHA256;
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_WPS
		else if (os_strcmp(start, "WPS") == 0)
			val |= WPA_KEY_MGMT_WPS;
#endif /* CONFIG_WPS */
#ifdef CONFIG_SAE
		else if (os_strcmp(start, "SAE") == 0)
			val |= WPA_KEY_MGMT_SAE;
		else if (os_strcmp(start, "FT-SAE") == 0)
			val |= WPA_KEY_MGMT_FT_SAE;
#endif /* CONFIG_SAE */
#ifdef CONFIG_HS20
		else if (os_strcmp(start, "OSEN") == 0)
			val |= WPA_KEY_MGMT_OSEN;
#endif /* CONFIG_HS20 */
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
#ifdef CONFIG_IEEE80211R
		else if (os_strcmp(start, "FT-FILS-SHA256") == 0)
			val |= WPA_KEY_MGMT_FT_FILS_SHA256;
		else if (os_strcmp(start, "FT-FILS-SHA384") == 0)
			val |= WPA_KEY_MGMT_FT_FILS_SHA384;
#endif /* CONFIG_IEEE80211R */
#endif /* CONFIG_FILS */
#ifdef CONFIG_OWE
		else if (os_strcmp(start, "OWE") == 0)
			val |= WPA_KEY_MGMT_OWE;
#endif /* CONFIG_OWE */
#ifdef CONFIG_DPP
		else if (os_strcmp(start, "DPP") == 0)
			val |= WPA_KEY_MGMT_DPP;
#endif /* CONFIG_DPP */
		else {
			wpa_printf(MSG_ERROR, "Line %d: invalid key_mgmt '%s'",
				   line, start);
			errors++;
		}

		if (last)
			break;
		start = end + 1;
	}
	os_free(buf);

	if (val == 0) {
		wpa_printf(MSG_ERROR,
			   "Line %d: no key_mgmt values configured.", line);
		errors++;
	}

	if (!errors && ssid->key_mgmt == val)
		return 1;
	wpa_printf(MSG_MSGDUMP, "key_mgmt: 0x%x", val);
	ssid->key_mgmt = val;
	return errors ? -1 : 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_key_mgmt(const struct parse_data *data,
					struct wpa_ssid *ssid)
{
	char *buf, *pos, *end;
	int ret;

	pos = buf = os_zalloc(100);
	if (buf == NULL)
		return NULL;
	end = buf + 100;

	if (ssid->key_mgmt & WPA_KEY_MGMT_PSK) {
		ret = os_snprintf(pos, end - pos, "%sWPA-PSK",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}

	if (ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X) {
		ret = os_snprintf(pos, end - pos, "%sWPA-EAP",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}

	if (ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		ret = os_snprintf(pos, end - pos, "%sIEEE8021X",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}

	if (ssid->key_mgmt & WPA_KEY_MGMT_NONE) {
		ret = os_snprintf(pos, end - pos, "%sNONE",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}

	if (ssid->key_mgmt & WPA_KEY_MGMT_WPA_NONE) {
		ret = os_snprintf(pos, end - pos, "%sWPA-NONE",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}

#ifdef CONFIG_IEEE80211R
	if (ssid->key_mgmt & WPA_KEY_MGMT_FT_PSK) {
		ret = os_snprintf(pos, end - pos, "%sFT-PSK",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}

	if (ssid->key_mgmt & WPA_KEY_MGMT_FT_IEEE8021X) {
		ret = os_snprintf(pos, end - pos, "%sFT-EAP",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}

#ifdef CONFIG_SHA384
	if (ssid->key_mgmt & WPA_KEY_MGMT_FT_IEEE8021X_SHA384) {
		ret = os_snprintf(pos, end - pos, "%sFT-EAP-SHA384",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}
#endif /* CONFIG_SHA384 */
#endif /* CONFIG_IEEE80211R */

#ifdef CONFIG_IEEE80211W
	if (ssid->key_mgmt & WPA_KEY_MGMT_PSK_SHA256) {
		ret = os_snprintf(pos, end - pos, "%sWPA-PSK-SHA256",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}

	if (ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_SHA256) {
		ret = os_snprintf(pos, end - pos, "%sWPA-EAP-SHA256",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_WPS
	if (ssid->key_mgmt & WPA_KEY_MGMT_WPS) {
		ret = os_snprintf(pos, end - pos, "%sWPS",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}
#endif /* CONFIG_WPS */

#ifdef CONFIG_SAE
	if (ssid->key_mgmt & WPA_KEY_MGMT_SAE) {
		ret = os_snprintf(pos, end - pos, "%sSAE",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}

	if (ssid->key_mgmt & WPA_KEY_MGMT_FT_SAE) {
		ret = os_snprintf(pos, end - pos, "%sFT-SAE",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}
#endif /* CONFIG_SAE */

#ifdef CONFIG_HS20
	if (ssid->key_mgmt & WPA_KEY_MGMT_OSEN) {
		ret = os_snprintf(pos, end - pos, "%sOSEN",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}
#endif /* CONFIG_HS20 */

#ifdef CONFIG_SUITEB
	if (ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_SUITE_B) {
		ret = os_snprintf(pos, end - pos, "%sWPA-EAP-SUITE-B",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}
#endif /* CONFIG_SUITEB */

#ifdef CONFIG_SUITEB192
	if (ssid->key_mgmt & WPA_KEY_MGMT_IEEE8021X_SUITE_B_192) {
		ret = os_snprintf(pos, end - pos, "%sWPA-EAP-SUITE-B-192",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}
#endif /* CONFIG_SUITEB192 */

#ifdef CONFIG_FILS
	if (ssid->key_mgmt & WPA_KEY_MGMT_FILS_SHA256) {
		ret = os_snprintf(pos, end - pos, "%sFILS-SHA256",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}
	if (ssid->key_mgmt & WPA_KEY_MGMT_FILS_SHA384) {
		ret = os_snprintf(pos, end - pos, "%sFILS-SHA384",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}
#ifdef CONFIG_IEEE80211R
	if (ssid->key_mgmt & WPA_KEY_MGMT_FT_FILS_SHA256) {
		ret = os_snprintf(pos, end - pos, "%sFT-FILS-SHA256",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}
	if (ssid->key_mgmt & WPA_KEY_MGMT_FT_FILS_SHA384) {
		ret = os_snprintf(pos, end - pos, "%sFT-FILS-SHA384",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}
#endif /* CONFIG_IEEE80211R */
#endif /* CONFIG_FILS */

	if (pos == buf) {
		os_free(buf);
		buf = NULL;
	}

	return buf;
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_cipher(int line, const char *value)
{
#ifdef CONFIG_NO_WPA
	return -1;
#else /* CONFIG_NO_WPA */
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
#endif /* CONFIG_NO_WPA */
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_cipher(int cipher)
{
#ifdef CONFIG_NO_WPA
	return NULL;
#else /* CONFIG_NO_WPA */
	char *buf = os_zalloc(50);
	if (buf == NULL)
		return NULL;

	if (wpa_write_ciphers(buf, buf + 50, cipher, " ") < 0) {
		os_free(buf);
		return NULL;
	}

	return buf;
#endif /* CONFIG_NO_WPA */
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_pairwise(const struct parse_data *data,
				     struct wpa_ssid *ssid, int line,
				     const char *value)
{
	int val;
	val = wpa_config_parse_cipher(line, value);
	if (val == -1)
		return -1;
	if (val & ~WPA_ALLOWED_PAIRWISE_CIPHERS) {
		wpa_printf(MSG_ERROR, "Line %d: not allowed pairwise cipher "
			   "(0x%x).", line, val);
		return -1;
	}

	if (ssid->pairwise_cipher == val)
		return 1;
	wpa_printf(MSG_MSGDUMP, "pairwise: 0x%x", val);
	ssid->pairwise_cipher = val;
	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_pairwise(const struct parse_data *data,
					struct wpa_ssid *ssid)
{
	return wpa_config_write_cipher(ssid->pairwise_cipher);
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_group(const struct parse_data *data,
				  struct wpa_ssid *ssid, int line,
				  const char *value)
{
	int val;
	val = wpa_config_parse_cipher(line, value);
	if (val == -1)
		return -1;

	/*
	 * Backwards compatibility - filter out WEP ciphers that were previously
	 * allowed.
	 */
	val &= ~(WPA_CIPHER_WEP104 | WPA_CIPHER_WEP40);

	if (val & ~WPA_ALLOWED_GROUP_CIPHERS) {
		wpa_printf(MSG_ERROR, "Line %d: not allowed group cipher "
			   "(0x%x).", line, val);
		return -1;
	}

	if (ssid->group_cipher == val)
		return 1;
	wpa_printf(MSG_MSGDUMP, "group: 0x%x", val);
	ssid->group_cipher = val;
	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_group(const struct parse_data *data,
				     struct wpa_ssid *ssid)
{
	return wpa_config_write_cipher(ssid->group_cipher);
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_group_mgmt(const struct parse_data *data,
				       struct wpa_ssid *ssid, int line,
				       const char *value)
{
	int val;

	val = wpa_config_parse_cipher(line, value);
	if (val == -1)
		return -1;

	if (val & ~WPA_ALLOWED_GROUP_MGMT_CIPHERS) {
		wpa_printf(MSG_ERROR,
			   "Line %d: not allowed group management cipher (0x%x).",
			   line, val);
		return -1;
	}

	if (ssid->group_mgmt_cipher == val)
		return 1;
	wpa_printf(MSG_MSGDUMP, "group_mgmt: 0x%x", val);
	ssid->group_mgmt_cipher = val;
	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_group_mgmt(const struct parse_data *data,
					  struct wpa_ssid *ssid)
{
	return wpa_config_write_cipher(ssid->group_mgmt_cipher);
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_auth_alg(const struct parse_data *data,
				     struct wpa_ssid *ssid, int line,
				     const char *value)
{
	int val = 0, last, errors = 0;
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
		if (os_strcmp(start, "OPEN") == 0)
			val |= WPA_AUTH_ALG_OPEN;
		else if (os_strcmp(start, "SHARED") == 0)
			val |= WPA_AUTH_ALG_SHARED;
		else if (os_strcmp(start, "LEAP") == 0)
			val |= WPA_AUTH_ALG_LEAP;
		else {
			wpa_printf(MSG_ERROR, "Line %d: invalid auth_alg '%s'",
				   line, start);
			errors++;
		}

		if (last)
			break;
		start = end + 1;
	}
	os_free(buf);

	if (val == 0) {
		wpa_printf(MSG_ERROR,
			   "Line %d: no auth_alg values configured.", line);
		errors++;
	}

	if (!errors && ssid->auth_alg == val)
		return 1;
	wpa_printf(MSG_MSGDUMP, "auth_alg: 0x%x", val);
	ssid->auth_alg = val;
	return errors ? -1 : 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_auth_alg(const struct parse_data *data,
					struct wpa_ssid *ssid)
{
	char *buf, *pos, *end;
	int ret;

	pos = buf = os_zalloc(30);
	if (buf == NULL)
		return NULL;
	end = buf + 30;

	if (ssid->auth_alg & WPA_AUTH_ALG_OPEN) {
		ret = os_snprintf(pos, end - pos, "%sOPEN",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}

	if (ssid->auth_alg & WPA_AUTH_ALG_SHARED) {
		ret = os_snprintf(pos, end - pos, "%sSHARED",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}

	if (ssid->auth_alg & WPA_AUTH_ALG_LEAP) {
		ret = os_snprintf(pos, end - pos, "%sLEAP",
				  pos == buf ? "" : " ");
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}

	if (pos == buf) {
		os_free(buf);
		buf = NULL;
	}

	return buf;
}
#endif /* NO_CONFIG_WRITE */


static int * wpa_config_parse_int_array(const char *value)
{
	int *freqs;
	size_t used, len;
	const char *pos;

	used = 0;
	len = 10;
	freqs = os_calloc(len + 1, sizeof(int));
	if (freqs == NULL)
		return NULL;

	pos = value;
	while (pos) {
		while (*pos == ' ')
			pos++;
		if (used == len) {
			int *n;
			size_t i;
			n = os_realloc_array(freqs, len * 2 + 1, sizeof(int));
			if (n == NULL) {
				os_free(freqs);
				return NULL;
			}
			for (i = len; i <= len * 2; i++)
				n[i] = 0;
			freqs = n;
			len *= 2;
		}

		freqs[used] = atoi(pos);
		if (freqs[used] == 0)
			break;
		used++;
		pos = os_strchr(pos + 1, ' ');
	}

	return freqs;
}


static int wpa_config_parse_scan_freq(const struct parse_data *data,
				      struct wpa_ssid *ssid, int line,
				      const char *value)
{
	int *freqs;

	freqs = wpa_config_parse_int_array(value);
	if (freqs == NULL)
		return -1;
	if (freqs[0] == 0) {
		os_free(freqs);
		freqs = NULL;
	}
	os_free(ssid->scan_freq);
	ssid->scan_freq = freqs;

	return 0;
}


static int wpa_config_parse_freq_list(const struct parse_data *data,
				      struct wpa_ssid *ssid, int line,
				      const char *value)
{
	int *freqs;

	freqs = wpa_config_parse_int_array(value);
	if (freqs == NULL)
		return -1;
	if (freqs[0] == 0) {
		os_free(freqs);
		freqs = NULL;
	}
	os_free(ssid->freq_list);
	ssid->freq_list = freqs;

	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_freqs(const struct parse_data *data,
				     const int *freqs)
{
	char *buf, *pos, *end;
	int i, ret;
	size_t count;

	if (freqs == NULL)
		return NULL;

	count = 0;
	for (i = 0; freqs[i]; i++)
		count++;

	pos = buf = os_zalloc(10 * count + 1);
	if (buf == NULL)
		return NULL;
	end = buf + 10 * count + 1;

	for (i = 0; freqs[i]; i++) {
		ret = os_snprintf(pos, end - pos, "%s%u",
				  i == 0 ? "" : " ", freqs[i]);
		if (os_snprintf_error(end - pos, ret)) {
			end[-1] = '\0';
			return buf;
		}
		pos += ret;
	}

	return buf;
}


static char * wpa_config_write_scan_freq(const struct parse_data *data,
					 struct wpa_ssid *ssid)
{
	return wpa_config_write_freqs(data, ssid->scan_freq);
}


static char * wpa_config_write_freq_list(const struct parse_data *data,
					 struct wpa_ssid *ssid)
{
	return wpa_config_write_freqs(data, ssid->freq_list);
}
#endif /* NO_CONFIG_WRITE */


#ifdef IEEE8021X_EAPOL
static int wpa_config_parse_eap(const struct parse_data *data,
				struct wpa_ssid *ssid, int line,
				const char *value)
{
	int last, errors = 0;
	char *start, *end, *buf;
	struct eap_method_type *methods = NULL, *tmp;
	size_t num_methods = 0;

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
		tmp = methods;
		methods = os_realloc_array(methods, num_methods + 1,
					   sizeof(*methods));
		if (methods == NULL) {
			os_free(tmp);
			os_free(buf);
			return -1;
		}
		methods[num_methods].method = eap_peer_get_type(
			start, &methods[num_methods].vendor);
		if (methods[num_methods].vendor == EAP_VENDOR_IETF &&
		    methods[num_methods].method == EAP_TYPE_NONE) {
			wpa_printf(MSG_ERROR, "Line %d: unknown EAP method "
				   "'%s'", line, start);
			wpa_printf(MSG_ERROR, "You may need to add support for"
				   " this EAP method during wpa_supplicant\n"
				   "build time configuration.\n"
				   "See README for more information.");
			errors++;
		} else if (methods[num_methods].vendor == EAP_VENDOR_IETF &&
			   methods[num_methods].method == EAP_TYPE_LEAP)
			ssid->leap++;
		else
			ssid->non_leap++;
		num_methods++;
		if (last)
			break;
		start = end + 1;
	}
	os_free(buf);

	tmp = methods;
	methods = os_realloc_array(methods, num_methods + 1, sizeof(*methods));
	if (methods == NULL) {
		os_free(tmp);
		return -1;
	}
	methods[num_methods].vendor = EAP_VENDOR_IETF;
	methods[num_methods].method = EAP_TYPE_NONE;
	num_methods++;

	if (!errors && ssid->eap.eap_methods) {
		struct eap_method_type *prev_m;
		size_t i, j, prev_methods, match = 0;

		prev_m = ssid->eap.eap_methods;
		for (i = 0; prev_m[i].vendor != EAP_VENDOR_IETF ||
			     prev_m[i].method != EAP_TYPE_NONE; i++) {
			/* Count the methods */
		}
		prev_methods = i + 1;

		for (i = 0; prev_methods == num_methods && i < prev_methods;
		     i++) {
			for (j = 0; j < num_methods; j++) {
				if (prev_m[i].vendor == methods[j].vendor &&
				    prev_m[i].method == methods[j].method) {
					match++;
					break;
				}
			}
		}
		if (match == num_methods) {
			os_free(methods);
			return 1;
		}
	}
	wpa_hexdump(MSG_MSGDUMP, "eap methods",
		    (u8 *) methods, num_methods * sizeof(*methods));
	os_free(ssid->eap.eap_methods);
	ssid->eap.eap_methods = methods;
	return errors ? -1 : 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_eap(const struct parse_data *data,
				   struct wpa_ssid *ssid)
{
	int i, ret;
	char *buf, *pos, *end;
	const struct eap_method_type *eap_methods = ssid->eap.eap_methods;
	const char *name;

	if (eap_methods == NULL)
		return NULL;

	pos = buf = os_zalloc(100);
	if (buf == NULL)
		return NULL;
	end = buf + 100;

	for (i = 0; eap_methods[i].vendor != EAP_VENDOR_IETF ||
		     eap_methods[i].method != EAP_TYPE_NONE; i++) {
		name = eap_get_name(eap_methods[i].vendor,
				    eap_methods[i].method);
		if (name) {
			ret = os_snprintf(pos, end - pos, "%s%s",
					  pos == buf ? "" : " ", name);
			if (os_snprintf_error(end - pos, ret))
				break;
			pos += ret;
		}
	}

	end[-1] = '\0';

	return buf;
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_password(const struct parse_data *data,
				     struct wpa_ssid *ssid, int line,
				     const char *value)
{
	u8 *hash;

	if (os_strcmp(value, "NULL") == 0) {
		if (!ssid->eap.password)
			return 1; /* Already unset */
		wpa_printf(MSG_DEBUG, "Unset configuration string 'password'");
		bin_clear_free(ssid->eap.password, ssid->eap.password_len);
		ssid->eap.password = NULL;
		ssid->eap.password_len = 0;
		return 0;
	}

#ifdef CONFIG_EXT_PASSWORD
	if (os_strncmp(value, "ext:", 4) == 0) {
		char *name = os_strdup(value + 4);
		if (name == NULL)
			return -1;
		bin_clear_free(ssid->eap.password, ssid->eap.password_len);
		ssid->eap.password = (u8 *) name;
		ssid->eap.password_len = os_strlen(name);
		ssid->eap.flags &= ~EAP_CONFIG_FLAGS_PASSWORD_NTHASH;
		ssid->eap.flags |= EAP_CONFIG_FLAGS_EXT_PASSWORD;
		return 0;
	}
#endif /* CONFIG_EXT_PASSWORD */

	if (os_strncmp(value, "hash:", 5) != 0) {
		char *tmp;
		size_t res_len;

		tmp = wpa_config_parse_string(value, &res_len);
		if (tmp == NULL) {
			wpa_printf(MSG_ERROR, "Line %d: failed to parse "
				   "password.", line);
			return -1;
		}
		wpa_hexdump_ascii_key(MSG_MSGDUMP, data->name,
				      (u8 *) tmp, res_len);

		bin_clear_free(ssid->eap.password, ssid->eap.password_len);
		ssid->eap.password = (u8 *) tmp;
		ssid->eap.password_len = res_len;
		ssid->eap.flags &= ~EAP_CONFIG_FLAGS_PASSWORD_NTHASH;
		ssid->eap.flags &= ~EAP_CONFIG_FLAGS_EXT_PASSWORD;

		return 0;
	}


	/* NtPasswordHash: hash:<32 hex digits> */
	if (os_strlen(value + 5) != 2 * 16) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid password hash length "
			   "(expected 32 hex digits)", line);
		return -1;
	}

	hash = os_malloc(16);
	if (hash == NULL)
		return -1;

	if (hexstr2bin(value + 5, hash, 16)) {
		os_free(hash);
		wpa_printf(MSG_ERROR, "Line %d: Invalid password hash", line);
		return -1;
	}

	wpa_hexdump_key(MSG_MSGDUMP, data->name, hash, 16);

	if (ssid->eap.password && ssid->eap.password_len == 16 &&
	    os_memcmp(ssid->eap.password, hash, 16) == 0 &&
	    (ssid->eap.flags & EAP_CONFIG_FLAGS_PASSWORD_NTHASH)) {
		bin_clear_free(hash, 16);
		return 1;
	}
	bin_clear_free(ssid->eap.password, ssid->eap.password_len);
	ssid->eap.password = hash;
	ssid->eap.password_len = 16;
	ssid->eap.flags |= EAP_CONFIG_FLAGS_PASSWORD_NTHASH;
	ssid->eap.flags &= ~EAP_CONFIG_FLAGS_EXT_PASSWORD;

	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_password(const struct parse_data *data,
					struct wpa_ssid *ssid)
{
	char *buf;

	if (ssid->eap.password == NULL)
		return NULL;

#ifdef CONFIG_EXT_PASSWORD
	if (ssid->eap.flags & EAP_CONFIG_FLAGS_EXT_PASSWORD) {
		buf = os_zalloc(4 + ssid->eap.password_len + 1);
		if (buf == NULL)
			return NULL;
		os_memcpy(buf, "ext:", 4);
		os_memcpy(buf + 4, ssid->eap.password, ssid->eap.password_len);
		return buf;
	}
#endif /* CONFIG_EXT_PASSWORD */

	if (!(ssid->eap.flags & EAP_CONFIG_FLAGS_PASSWORD_NTHASH)) {
		return wpa_config_write_string(
			ssid->eap.password, ssid->eap.password_len);
	}

	buf = os_malloc(5 + 32 + 1);
	if (buf == NULL)
		return NULL;

	os_memcpy(buf, "hash:", 5);
	wpa_snprintf_hex(buf + 5, 32 + 1, ssid->eap.password, 16);

	return buf;
}
#endif /* NO_CONFIG_WRITE */
#endif /* IEEE8021X_EAPOL */


static int wpa_config_parse_wep_key(u8 *key, size_t *len, int line,
				    const char *value, int idx)
{
	char *buf, title[20];
	int res;

	buf = wpa_config_parse_string(value, len);
	if (buf == NULL) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid WEP key %d '%s'.",
			   line, idx, value);
		return -1;
	}
	if (*len > MAX_WEP_KEY_LEN) {
		wpa_printf(MSG_ERROR, "Line %d: Too long WEP key %d '%s'.",
			   line, idx, value);
		os_free(buf);
		return -1;
	}
	if (*len && *len != 5 && *len != 13 && *len != 16) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid WEP key length %u - "
			   "this network block will be ignored",
			   line, (unsigned int) *len);
	}
	os_memcpy(key, buf, *len);
	str_clear_free(buf);
	res = os_snprintf(title, sizeof(title), "wep_key%d", idx);
	if (!os_snprintf_error(sizeof(title), res))
		wpa_hexdump_key(MSG_MSGDUMP, title, key, *len);
	return 0;
}


static int wpa_config_parse_wep_key0(const struct parse_data *data,
				     struct wpa_ssid *ssid, int line,
				     const char *value)
{
	return wpa_config_parse_wep_key(ssid->wep_key[0],
					&ssid->wep_key_len[0], line,
					value, 0);
}


static int wpa_config_parse_wep_key1(const struct parse_data *data,
				     struct wpa_ssid *ssid, int line,
				     const char *value)
{
	return wpa_config_parse_wep_key(ssid->wep_key[1],
					&ssid->wep_key_len[1], line,
					value, 1);
}


static int wpa_config_parse_wep_key2(const struct parse_data *data,
				     struct wpa_ssid *ssid, int line,
				     const char *value)
{
	return wpa_config_parse_wep_key(ssid->wep_key[2],
					&ssid->wep_key_len[2], line,
					value, 2);
}


static int wpa_config_parse_wep_key3(const struct parse_data *data,
				     struct wpa_ssid *ssid, int line,
				     const char *value)
{
	return wpa_config_parse_wep_key(ssid->wep_key[3],
					&ssid->wep_key_len[3], line,
					value, 3);
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_wep_key(struct wpa_ssid *ssid, int idx)
{
	if (ssid->wep_key_len[idx] == 0)
		return NULL;
	return wpa_config_write_string(ssid->wep_key[idx],
				       ssid->wep_key_len[idx]);
}


static char * wpa_config_write_wep_key0(const struct parse_data *data,
					struct wpa_ssid *ssid)
{
	return wpa_config_write_wep_key(ssid, 0);
}


static char * wpa_config_write_wep_key1(const struct parse_data *data,
					struct wpa_ssid *ssid)
{
	return wpa_config_write_wep_key(ssid, 1);
}


static char * wpa_config_write_wep_key2(const struct parse_data *data,
					struct wpa_ssid *ssid)
{
	return wpa_config_write_wep_key(ssid, 2);
}


static char * wpa_config_write_wep_key3(const struct parse_data *data,
					struct wpa_ssid *ssid)
{
	return wpa_config_write_wep_key(ssid, 3);
}
#endif /* NO_CONFIG_WRITE */


#ifdef CONFIG_P2P

static int wpa_config_parse_go_p2p_dev_addr(const struct parse_data *data,
					    struct wpa_ssid *ssid, int line,
					    const char *value)
{
	if (value[0] == '\0' || os_strcmp(value, "\"\"") == 0 ||
	    os_strcmp(value, "any") == 0) {
		os_memset(ssid->go_p2p_dev_addr, 0, ETH_ALEN);
		wpa_printf(MSG_MSGDUMP, "GO P2P Device Address any");
		return 0;
	}
	if (hwaddr_aton(value, ssid->go_p2p_dev_addr)) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid GO P2P Device Address '%s'.",
			   line, value);
		return -1;
	}
	ssid->bssid_set = 1;
	wpa_printf(MSG_MSGDUMP, "GO P2P Device Address " MACSTR,
		   MAC2STR(ssid->go_p2p_dev_addr));
	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_go_p2p_dev_addr(const struct parse_data *data,
					       struct wpa_ssid *ssid)
{
	char *value;
	int res;

	if (is_zero_ether_addr(ssid->go_p2p_dev_addr))
		return NULL;

	value = os_malloc(20);
	if (value == NULL)
		return NULL;
	res = os_snprintf(value, 20, MACSTR, MAC2STR(ssid->go_p2p_dev_addr));
	if (os_snprintf_error(20, res)) {
		os_free(value);
		return NULL;
	}
	value[20 - 1] = '\0';
	return value;
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_p2p_client_list(const struct parse_data *data,
					    struct wpa_ssid *ssid, int line,
					    const char *value)
{
	return wpa_config_parse_addr_list(data, line, value,
					  &ssid->p2p_client_list,
					  &ssid->num_p2p_clients,
					  "p2p_client_list", 0, 0);
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_p2p_client_list(const struct parse_data *data,
					       struct wpa_ssid *ssid)
{
	return wpa_config_write_addr_list(data, ssid->p2p_client_list,
					  ssid->num_p2p_clients,
					  "p2p_client_list");
}
#endif /* NO_CONFIG_WRITE */


static int wpa_config_parse_psk_list(const struct parse_data *data,
				     struct wpa_ssid *ssid, int line,
				     const char *value)
{
	struct psk_list_entry *p;
	const char *pos;

	p = os_zalloc(sizeof(*p));
	if (p == NULL)
		return -1;

	pos = value;
	if (os_strncmp(pos, "P2P-", 4) == 0) {
		p->p2p = 1;
		pos += 4;
	}

	if (hwaddr_aton(pos, p->addr)) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid psk_list address '%s'",
			   line, pos);
		os_free(p);
		return -1;
	}
	pos += 17;
	if (*pos != '-') {
		wpa_printf(MSG_ERROR, "Line %d: Invalid psk_list '%s'",
			   line, pos);
		os_free(p);
		return -1;
	}
	pos++;

	if (hexstr2bin(pos, p->psk, PMK_LEN) || pos[PMK_LEN * 2] != '\0') {
		wpa_printf(MSG_ERROR, "Line %d: Invalid psk_list PSK '%s'",
			   line, pos);
		os_free(p);
		return -1;
	}

	dl_list_add(&ssid->psk_list, &p->list);

	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_psk_list(const struct parse_data *data,
					struct wpa_ssid *ssid)
{
	return NULL;
}
#endif /* NO_CONFIG_WRITE */

#endif /* CONFIG_P2P */


#ifdef CONFIG_MESH

static int wpa_config_parse_mesh_basic_rates(const struct parse_data *data,
					     struct wpa_ssid *ssid, int line,
					     const char *value)
{
	int *rates = wpa_config_parse_int_array(value);

	if (rates == NULL) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid mesh_basic_rates '%s'",
			   line, value);
		return -1;
	}
	if (rates[0] == 0) {
		os_free(rates);
		rates = NULL;
	}

	os_free(ssid->mesh_basic_rates);
	ssid->mesh_basic_rates = rates;

	return 0;
}


#ifndef NO_CONFIG_WRITE

static char * wpa_config_write_mesh_basic_rates(const struct parse_data *data,
						struct wpa_ssid *ssid)
{
	return wpa_config_write_freqs(data, ssid->mesh_basic_rates);
}

#endif /* NO_CONFIG_WRITE */

#endif /* CONFIG_MESH */


#ifdef CONFIG_MACSEC

static int wpa_config_parse_mka_cak(const struct parse_data *data,
				    struct wpa_ssid *ssid, int line,
				    const char *value)
{
	if (hexstr2bin(value, ssid->mka_cak, MACSEC_CAK_LEN) ||
	    value[MACSEC_CAK_LEN * 2] != '\0') {
		wpa_printf(MSG_ERROR, "Line %d: Invalid MKA-CAK '%s'.",
			   line, value);
		return -1;
	}

	ssid->mka_psk_set |= MKA_PSK_SET_CAK;

	wpa_hexdump_key(MSG_MSGDUMP, "MKA-CAK", ssid->mka_cak, MACSEC_CAK_LEN);
	return 0;
}


static int wpa_config_parse_mka_ckn(const struct parse_data *data,
				    struct wpa_ssid *ssid, int line,
				    const char *value)
{
	if (hexstr2bin(value, ssid->mka_ckn, MACSEC_CKN_LEN) ||
	    value[MACSEC_CKN_LEN * 2] != '\0') {
		wpa_printf(MSG_ERROR, "Line %d: Invalid MKA-CKN '%s'.",
			   line, value);
		return -1;
	}

	ssid->mka_psk_set |= MKA_PSK_SET_CKN;

	wpa_hexdump_key(MSG_MSGDUMP, "MKA-CKN", ssid->mka_ckn, MACSEC_CKN_LEN);
	return 0;
}


#ifndef NO_CONFIG_WRITE

static char * wpa_config_write_mka_cak(const struct parse_data *data,
				       struct wpa_ssid *ssid)
{
	if (!(ssid->mka_psk_set & MKA_PSK_SET_CAK))
		return NULL;

	return wpa_config_write_string_hex(ssid->mka_cak, MACSEC_CAK_LEN);
}


static char * wpa_config_write_mka_ckn(const struct parse_data *data,
				       struct wpa_ssid *ssid)
{
	if (!(ssid->mka_psk_set & MKA_PSK_SET_CKN))
		return NULL;
	return wpa_config_write_string_hex(ssid->mka_ckn, MACSEC_CKN_LEN);
}

#endif /* NO_CONFIG_WRITE */

#endif /* CONFIG_MACSEC */


static int wpa_config_parse_peerkey(const struct parse_data *data,
				    struct wpa_ssid *ssid, int line,
				    const char *value)
{
	wpa_printf(MSG_INFO, "NOTE: Obsolete peerkey parameter ignored");
	return 0;
}


#ifndef NO_CONFIG_WRITE
static char * wpa_config_write_peerkey(const struct parse_data *data,
				       struct wpa_ssid *ssid)
{
	return NULL;
}
#endif /* NO_CONFIG_WRITE */


/* Helper macros for network block parser */

#ifdef OFFSET
#undef OFFSET
#endif /* OFFSET */
/* OFFSET: Get offset of a variable within the wpa_ssid structure */
#define OFFSET(v) ((void *) &((struct wpa_ssid *) 0)->v)

/* STR: Define a string variable for an ASCII string; f = field name */
#ifdef NO_CONFIG_WRITE
#define _STR(f) #f, wpa_config_parse_str, OFFSET(f)
#define _STRe(f) #f, wpa_config_parse_str, OFFSET(eap.f)
#else /* NO_CONFIG_WRITE */
#define _STR(f) #f, wpa_config_parse_str, wpa_config_write_str, OFFSET(f)
#define _STRe(f) #f, wpa_config_parse_str, wpa_config_write_str, OFFSET(eap.f)
#endif /* NO_CONFIG_WRITE */
#define STR(f) _STR(f), NULL, NULL, NULL, 0
#define STRe(f) _STRe(f), NULL, NULL, NULL, 0
#define STR_KEY(f) _STR(f), NULL, NULL, NULL, 1
#define STR_KEYe(f) _STRe(f), NULL, NULL, NULL, 1

/* STR_LEN: Define a string variable with a separate variable for storing the
 * data length. Unlike STR(), this can be used to store arbitrary binary data
 * (i.e., even nul termination character). */
#define _STR_LEN(f) _STR(f), OFFSET(f ## _len)
#define _STR_LENe(f) _STRe(f), OFFSET(eap.f ## _len)
#define STR_LEN(f) _STR_LEN(f), NULL, NULL, 0
#define STR_LENe(f) _STR_LENe(f), NULL, NULL, 0
#define STR_LEN_KEY(f) _STR_LEN(f), NULL, NULL, 1

/* STR_RANGE: Like STR_LEN(), but with minimum and maximum allowed length
 * explicitly specified. */
#define _STR_RANGE(f, min, max) _STR_LEN(f), (void *) (min), (void *) (max)
#define STR_RANGE(f, min, max) _STR_RANGE(f, min, max), 0
#define STR_RANGE_KEY(f, min, max) _STR_RANGE(f, min, max), 1

#ifdef NO_CONFIG_WRITE
#define _INT(f) #f, wpa_config_parse_int, OFFSET(f), (void *) 0
#define _INTe(f) #f, wpa_config_parse_int, OFFSET(eap.f), (void *) 0
#else /* NO_CONFIG_WRITE */
#define _INT(f) #f, wpa_config_parse_int, wpa_config_write_int, \
	OFFSET(f), (void *) 0
#define _INTe(f) #f, wpa_config_parse_int, wpa_config_write_int, \
	OFFSET(eap.f), (void *) 0
#endif /* NO_CONFIG_WRITE */

/* INT: Define an integer variable */
#define INT(f) _INT(f), NULL, NULL, 0
#define INTe(f) _INTe(f), NULL, NULL, 0

/* INT_RANGE: Define an integer variable with allowed value range */
#define INT_RANGE(f, min, max) _INT(f), (void *) (min), (void *) (max), 0

/* FUNC: Define a configuration variable that uses a custom function for
 * parsing and writing the value. */
#ifdef NO_CONFIG_WRITE
#define _FUNC(f) #f, wpa_config_parse_ ## f, NULL, NULL, NULL, NULL
#else /* NO_CONFIG_WRITE */
#define _FUNC(f) #f, wpa_config_parse_ ## f, wpa_config_write_ ## f, \
	NULL, NULL, NULL, NULL
#endif /* NO_CONFIG_WRITE */
#define FUNC(f) _FUNC(f), 0
#define FUNC_KEY(f) _FUNC(f), 1

/*
 * Table of network configuration variables. This table is used to parse each
 * network configuration variable, e.g., each line in wpa_supplicant.conf file
 * that is inside a network block.
 *
 * This table is generated using the helper macros defined above and with
 * generous help from the C pre-processor. The field name is stored as a string
 * into .name and for STR and INT types, the offset of the target buffer within
 * struct wpa_ssid is stored in .param1. .param2 (if not NULL) is similar
 * offset to the field containing the length of the configuration variable.
 * .param3 and .param4 can be used to mark the allowed range (length for STR
 * and value for INT).
 *
 * For each configuration line in wpa_supplicant.conf, the parser goes through
 * this table and select the entry that matches with the field name. The parser
 * function (.parser) is then called to parse the actual value of the field.
 *
 * This kind of mechanism makes it easy to add new configuration parameters,
 * since only one line needs to be added into this table and into the
 * struct wpa_ssid definition if the new variable is either a string or
 * integer. More complex types will need to use their own parser and writer
 * functions.
 */
static const struct parse_data ssid_fields[] = {
	{ STR_RANGE(ssid, 0, SSID_MAX_LEN) },
	{ INT_RANGE(scan_ssid, 0, 1) },
	{ FUNC(bssid) },
	{ FUNC(bssid_hint) },
	{ FUNC(bssid_blacklist) },
	{ FUNC(bssid_whitelist) },
	{ FUNC_KEY(psk) },
	{ INT(mem_only_psk) },
	{ STR_KEY(sae_password) },
	{ STR(sae_password_id) },
	{ FUNC(proto) },
	{ FUNC(key_mgmt) },
	{ INT(bg_scan_period) },
	{ FUNC(pairwise) },
	{ FUNC(group) },
	{ FUNC(group_mgmt) },
	{ FUNC(auth_alg) },
	{ FUNC(scan_freq) },
	{ FUNC(freq_list) },
	{ INT_RANGE(ht, 0, 1) },
	{ INT_RANGE(vht, 0, 1) },
	{ INT_RANGE(ht40, -1, 1) },
	{ INT_RANGE(max_oper_chwidth, VHT_CHANWIDTH_USE_HT,
		    VHT_CHANWIDTH_80P80MHZ) },
	{ INT(vht_center_freq1) },
	{ INT(vht_center_freq2) },
#ifdef IEEE8021X_EAPOL
	{ FUNC(eap) },
	{ STR_LENe(identity) },
	{ STR_LENe(anonymous_identity) },
	{ STR_LENe(imsi_identity) },
	{ FUNC_KEY(password) },
	{ STRe(ca_cert) },
	{ STRe(ca_path) },
	{ STRe(client_cert) },
	{ STRe(private_key) },
	{ STR_KEYe(private_key_passwd) },
	{ STRe(dh_file) },
	{ STRe(subject_match) },
	{ STRe(altsubject_match) },
	{ STRe(domain_suffix_match) },
	{ STRe(domain_match) },
	{ STRe(ca_cert2) },
	{ STRe(ca_path2) },
	{ STRe(client_cert2) },
	{ STRe(private_key2) },
	{ STR_KEYe(private_key2_passwd) },
	{ STRe(dh_file2) },
	{ STRe(subject_match2) },
	{ STRe(altsubject_match2) },
	{ STRe(domain_suffix_match2) },
	{ STRe(domain_match2) },
	{ STRe(phase1) },
	{ STRe(phase2) },
	{ STRe(pcsc) },
	{ STR_KEYe(pin) },
	{ STRe(engine_id) },
	{ STRe(key_id) },
	{ STRe(cert_id) },
	{ STRe(ca_cert_id) },
	{ STR_KEYe(pin2) },
	{ STRe(engine2_id) },
	{ STRe(key2_id) },
	{ STRe(cert2_id) },
	{ STRe(ca_cert2_id) },
	{ INTe(engine) },
	{ INTe(engine2) },
	{ INT(eapol_flags) },
	{ INTe(sim_num) },
	{ STRe(openssl_ciphers) },
	{ INTe(erp) },
#endif /* IEEE8021X_EAPOL */
	{ FUNC_KEY(wep_key0) },
	{ FUNC_KEY(wep_key1) },
	{ FUNC_KEY(wep_key2) },
	{ FUNC_KEY(wep_key3) },
	{ INT(wep_tx_keyidx) },
	{ INT(priority) },
#ifdef IEEE8021X_EAPOL
	{ INT(eap_workaround) },
	{ STRe(pac_file) },
	{ INTe(fragment_size) },
	{ INTe(ocsp) },
#endif /* IEEE8021X_EAPOL */
#ifdef CONFIG_MESH
	{ INT_RANGE(mode, 0, 5) },
	{ INT_RANGE(no_auto_peer, 0, 1) },
	{ INT_RANGE(mesh_rssi_threshold, -255, 1) },
#else /* CONFIG_MESH */
	{ INT_RANGE(mode, 0, 4) },
#endif /* CONFIG_MESH */
	{ INT_RANGE(proactive_key_caching, 0, 1) },
	{ INT_RANGE(disabled, 0, 2) },
	{ STR(id_str) },
#ifdef CONFIG_IEEE80211W
	{ INT_RANGE(ieee80211w, 0, 2) },
#endif /* CONFIG_IEEE80211W */
	{ FUNC(peerkey) /* obsolete - removed */ },
	{ INT_RANGE(mixed_cell, 0, 1) },
	{ INT_RANGE(frequency, 0, 65000) },
	{ INT_RANGE(fixed_freq, 0, 1) },
#ifdef CONFIG_ACS
	{ INT_RANGE(acs, 0, 1) },
#endif /* CONFIG_ACS */
#ifdef CONFIG_MESH
	{ FUNC(mesh_basic_rates) },
	{ INT(dot11MeshMaxRetries) },
	{ INT(dot11MeshRetryTimeout) },
	{ INT(dot11MeshConfirmTimeout) },
	{ INT(dot11MeshHoldingTimeout) },
#endif /* CONFIG_MESH */
	{ INT(wpa_ptk_rekey) },
	{ INT(group_rekey) },
	{ STR(bgscan) },
	{ INT_RANGE(ignore_broadcast_ssid, 0, 2) },
#ifdef CONFIG_P2P
	{ FUNC(go_p2p_dev_addr) },
	{ FUNC(p2p_client_list) },
	{ FUNC(psk_list) },
#endif /* CONFIG_P2P */
#ifdef CONFIG_HT_OVERRIDES
	{ INT_RANGE(disable_ht, 0, 1) },
	{ INT_RANGE(disable_ht40, -1, 1) },
	{ INT_RANGE(disable_sgi, 0, 1) },
	{ INT_RANGE(disable_ldpc, 0, 1) },
	{ INT_RANGE(ht40_intolerant, 0, 1) },
	{ INT_RANGE(disable_max_amsdu, -1, 1) },
	{ INT_RANGE(ampdu_factor, -1, 3) },
	{ INT_RANGE(ampdu_density, -1, 7) },
	{ STR(ht_mcs) },
#endif /* CONFIG_HT_OVERRIDES */
#ifdef CONFIG_VHT_OVERRIDES
	{ INT_RANGE(disable_vht, 0, 1) },
	{ INT(vht_capa) },
	{ INT(vht_capa_mask) },
	{ INT_RANGE(vht_rx_mcs_nss_1, -1, 3) },
	{ INT_RANGE(vht_rx_mcs_nss_2, -1, 3) },
	{ INT_RANGE(vht_rx_mcs_nss_3, -1, 3) },
	{ INT_RANGE(vht_rx_mcs_nss_4, -1, 3) },
	{ INT_RANGE(vht_rx_mcs_nss_5, -1, 3) },
	{ INT_RANGE(vht_rx_mcs_nss_6, -1, 3) },
	{ INT_RANGE(vht_rx_mcs_nss_7, -1, 3) },
	{ INT_RANGE(vht_rx_mcs_nss_8, -1, 3) },
	{ INT_RANGE(vht_tx_mcs_nss_1, -1, 3) },
	{ INT_RANGE(vht_tx_mcs_nss_2, -1, 3) },
	{ INT_RANGE(vht_tx_mcs_nss_3, -1, 3) },
	{ INT_RANGE(vht_tx_mcs_nss_4, -1, 3) },
	{ INT_RANGE(vht_tx_mcs_nss_5, -1, 3) },
	{ INT_RANGE(vht_tx_mcs_nss_6, -1, 3) },
	{ INT_RANGE(vht_tx_mcs_nss_7, -1, 3) },
	{ INT_RANGE(vht_tx_mcs_nss_8, -1, 3) },
#endif /* CONFIG_VHT_OVERRIDES */
	{ INT(ap_max_inactivity) },
	{ INT(dtim_period) },
	{ INT(beacon_int) },
#ifdef CONFIG_MACSEC
	{ INT_RANGE(macsec_policy, 0, 1) },
	{ INT_RANGE(macsec_integ_only, 0, 1) },
	{ INT_RANGE(macsec_port, 1, 65534) },
	{ INT_RANGE(mka_priority, 0, 255) },
	{ FUNC_KEY(mka_cak) },
	{ FUNC_KEY(mka_ckn) },
#endif /* CONFIG_MACSEC */
#ifdef CONFIG_HS20
	{ INT(update_identifier) },
	{ STR_RANGE(roaming_consortium_selection, 0, MAX_ROAMING_CONS_OI_LEN) },
#endif /* CONFIG_HS20 */
	{ INT_RANGE(mac_addr, 0, 2) },
	{ INT_RANGE(pbss, 0, 2) },
	{ INT_RANGE(wps_disabled, 0, 1) },
	{ INT_RANGE(fils_dh_group, 0, 65535) },
#ifdef CONFIG_DPP
	{ STR(dpp_connector) },
	{ STR_LEN(dpp_netaccesskey) },
	{ INT(dpp_netaccesskey_expiry) },
	{ STR_LEN(dpp_csign) },
#endif /* CONFIG_DPP */
	{ INT_RANGE(owe_group, 0, 65535) },
	{ INT_RANGE(owe_only, 0, 1) },
};

#undef OFFSET
#undef _STR
#undef STR
#undef STR_KEY
#undef _STR_LEN
#undef STR_LEN
#undef STR_LEN_KEY
#undef _STR_RANGE
#undef STR_RANGE
#undef STR_RANGE_KEY
#undef _INT
#undef INT
#undef INT_RANGE
#undef _FUNC
#undef FUNC
#undef FUNC_KEY
#define NUM_SSID_FIELDS ARRAY_SIZE(ssid_fields)


/**
 * wpa_config_add_prio_network - Add a network to priority lists
 * @config: Configuration data from wpa_config_read()
 * @ssid: Pointer to the network configuration to be added to the list
 * Returns: 0 on success, -1 on failure
 *
 * This function is used to add a network block to the priority list of
 * networks. This must be called for each network when reading in the full
 * configuration. In addition, this can be used indirectly when updating
 * priorities by calling wpa_config_update_prio_list().
 */
int wpa_config_add_prio_network(struct wpa_config *config,
				struct wpa_ssid *ssid)
{
	int prio;
	struct wpa_ssid *prev, **nlist;

	/*
	 * Add to an existing priority list if one is available for the
	 * configured priority level for this network.
	 */
	for (prio = 0; prio < config->num_prio; prio++) {
		prev = config->pssid[prio];
		if (prev->priority == ssid->priority) {
			while (prev->pnext)
				prev = prev->pnext;
			prev->pnext = ssid;
			return 0;
		}
	}

	/* First network for this priority - add a new priority list */
	nlist = os_realloc_array(config->pssid, config->num_prio + 1,
				 sizeof(struct wpa_ssid *));
	if (nlist == NULL)
		return -1;

	for (prio = 0; prio < config->num_prio; prio++) {
		if (nlist[prio]->priority < ssid->priority) {
			os_memmove(&nlist[prio + 1], &nlist[prio],
				   (config->num_prio - prio) *
				   sizeof(struct wpa_ssid *));
			break;
		}
	}

	nlist[prio] = ssid;
	config->num_prio++;
	config->pssid = nlist;

	return 0;
}


/**
 * wpa_config_update_prio_list - Update network priority list
 * @config: Configuration data from wpa_config_read()
 * Returns: 0 on success, -1 on failure
 *
 * This function is called to update the priority list of networks in the
 * configuration when a network is being added or removed. This is also called
 * if a priority for a network is changed.
 */
int wpa_config_update_prio_list(struct wpa_config *config)
{
	struct wpa_ssid *ssid;
	int ret = 0;

	os_free(config->pssid);
	config->pssid = NULL;
	config->num_prio = 0;

	ssid = config->ssid;
	while (ssid) {
		ssid->pnext = NULL;
		if (wpa_config_add_prio_network(config, ssid) < 0)
			ret = -1;
		ssid = ssid->next;
	}

	return ret;
}


#ifdef IEEE8021X_EAPOL
static void eap_peer_config_free(struct eap_peer_config *eap)
{
	os_free(eap->eap_methods);
	bin_clear_free(eap->identity, eap->identity_len);
	os_free(eap->anonymous_identity);
	os_free(eap->imsi_identity);
	bin_clear_free(eap->password, eap->password_len);
	os_free(eap->ca_cert);
	os_free(eap->ca_path);
	os_free(eap->client_cert);
	os_free(eap->private_key);
	str_clear_free(eap->private_key_passwd);
	os_free(eap->dh_file);
	os_free(eap->subject_match);
	os_free(eap->altsubject_match);
	os_free(eap->domain_suffix_match);
	os_free(eap->domain_match);
	os_free(eap->ca_cert2);
	os_free(eap->ca_path2);
	os_free(eap->client_cert2);
	os_free(eap->private_key2);
	str_clear_free(eap->private_key2_passwd);
	os_free(eap->dh_file2);
	os_free(eap->subject_match2);
	os_free(eap->altsubject_match2);
	os_free(eap->domain_suffix_match2);
	os_free(eap->domain_match2);
	os_free(eap->phase1);
	os_free(eap->phase2);
	os_free(eap->pcsc);
	str_clear_free(eap->pin);
	os_free(eap->engine_id);
	os_free(eap->key_id);
	os_free(eap->cert_id);
	os_free(eap->ca_cert_id);
	os_free(eap->key2_id);
	os_free(eap->cert2_id);
	os_free(eap->ca_cert2_id);
	str_clear_free(eap->pin2);
	os_free(eap->engine2_id);
	os_free(eap->otp);
	os_free(eap->pending_req_otp);
	os_free(eap->pac_file);
	bin_clear_free(eap->new_password, eap->new_password_len);
	str_clear_free(eap->external_sim_resp);
	os_free(eap->openssl_ciphers);
}
#endif /* IEEE8021X_EAPOL */


/**
 * wpa_config_free_ssid - Free network/ssid configuration data
 * @ssid: Configuration data for the network
 *
 * This function frees all resources allocated for the network configuration
 * data.
 */
void wpa_config_free_ssid(struct wpa_ssid *ssid)
{
	struct psk_list_entry *psk;

	os_free(ssid->ssid);
	str_clear_free(ssid->passphrase);
	os_free(ssid->ext_psk);
	str_clear_free(ssid->sae_password);
	os_free(ssid->sae_password_id);
#ifdef IEEE8021X_EAPOL
	eap_peer_config_free(&ssid->eap);
#endif /* IEEE8021X_EAPOL */
	os_free(ssid->id_str);
	os_free(ssid->scan_freq);
	os_free(ssid->freq_list);
	os_free(ssid->bgscan);
	os_free(ssid->p2p_client_list);
	os_free(ssid->bssid_blacklist);
	os_free(ssid->bssid_whitelist);
#ifdef CONFIG_HT_OVERRIDES
	os_free(ssid->ht_mcs);
#endif /* CONFIG_HT_OVERRIDES */
#ifdef CONFIG_MESH
	os_free(ssid->mesh_basic_rates);
#endif /* CONFIG_MESH */
#ifdef CONFIG_HS20
	os_free(ssid->roaming_consortium_selection);
#endif /* CONFIG_HS20 */
	os_free(ssid->dpp_connector);
	bin_clear_free(ssid->dpp_netaccesskey, ssid->dpp_netaccesskey_len);
	os_free(ssid->dpp_csign);
	while ((psk = dl_list_first(&ssid->psk_list, struct psk_list_entry,
				    list))) {
		dl_list_del(&psk->list);
		bin_clear_free(psk, sizeof(*psk));
	}
	bin_clear_free(ssid, sizeof(*ssid));
}


void wpa_config_free_cred(struct wpa_cred *cred)
{
	size_t i;

	os_free(cred->realm);
	str_clear_free(cred->username);
	str_clear_free(cred->password);
	os_free(cred->ca_cert);
	os_free(cred->client_cert);
	os_free(cred->private_key);
	str_clear_free(cred->private_key_passwd);
	os_free(cred->imsi);
	str_clear_free(cred->milenage);
	for (i = 0; i < cred->num_domain; i++)
		os_free(cred->domain[i]);
	os_free(cred->domain);
	os_free(cred->domain_suffix_match);
	os_free(cred->eap_method);
	os_free(cred->phase1);
	os_free(cred->phase2);
	os_free(cred->excluded_ssid);
	os_free(cred->roaming_partner);
	os_free(cred->provisioning_sp);
	for (i = 0; i < cred->num_req_conn_capab; i++)
		os_free(cred->req_conn_capab_port[i]);
	os_free(cred->req_conn_capab_port);
	os_free(cred->req_conn_capab_proto);
	os_free(cred);
}


void wpa_config_flush_blobs(struct wpa_config *config)
{
#ifndef CONFIG_NO_CONFIG_BLOBS
	struct wpa_config_blob *blob, *prev;

	blob = config->blobs;
	config->blobs = NULL;
	while (blob) {
		prev = blob;
		blob = blob->next;
		wpa_config_free_blob(prev);
	}
#endif /* CONFIG_NO_CONFIG_BLOBS */
}


/**
 * wpa_config_free - Free configuration data
 * @config: Configuration data from wpa_config_read()
 *
 * This function frees all resources allocated for the configuration data by
 * wpa_config_read().
 */
void wpa_config_free(struct wpa_config *config)
{
	struct wpa_ssid *ssid, *prev = NULL;
	struct wpa_cred *cred, *cprev;
	int i;

	ssid = config->ssid;
	while (ssid) {
		prev = ssid;
		ssid = ssid->next;
		wpa_config_free_ssid(prev);
	}

	cred = config->cred;
	while (cred) {
		cprev = cred;
		cred = cred->next;
		wpa_config_free_cred(cprev);
	}

	wpa_config_flush_blobs(config);

	wpabuf_free(config->wps_vendor_ext_m1);
	for (i = 0; i < MAX_WPS_VENDOR_EXT; i++)
		wpabuf_free(config->wps_vendor_ext[i]);
	os_free(config->ctrl_interface);
	os_free(config->ctrl_interface_group);
	os_free(config->opensc_engine_path);
	os_free(config->pkcs11_engine_path);
	os_free(config->pkcs11_module_path);
	os_free(config->openssl_ciphers);
	os_free(config->pcsc_reader);
	str_clear_free(config->pcsc_pin);
	os_free(config->driver_param);
	os_free(config->device_name);
	os_free(config->manufacturer);
	os_free(config->model_name);
	os_free(config->model_number);
	os_free(config->serial_number);
	os_free(config->config_methods);
	os_free(config->p2p_ssid_postfix);
	os_free(config->pssid);
	os_free(config->p2p_pref_chan);
	os_free(config->p2p_no_go_freq.range);
	os_free(config->autoscan);
	os_free(config->freq_list);
	wpabuf_free(config->wps_nfc_dh_pubkey);
	wpabuf_free(config->wps_nfc_dh_privkey);
	wpabuf_free(config->wps_nfc_dev_pw);
	os_free(config->ext_password_backend);
	os_free(config->sae_groups);
	wpabuf_free(config->ap_vendor_elements);
	os_free(config->osu_dir);
	os_free(config->bgscan);
	os_free(config->wowlan_triggers);
	os_free(config->fst_group_id);
	os_free(config->sched_scan_plans);
#ifdef CONFIG_MBO
	os_free(config->non_pref_chan);
#endif /* CONFIG_MBO */

	os_free(config);
}


/**
 * wpa_config_foreach_network - Iterate over each configured network
 * @config: Configuration data from wpa_config_read()
 * @func: Callback function to process each network
 * @arg: Opaque argument to pass to callback function
 *
 * Iterate over the set of configured networks calling the specified
 * function for each item. We guard against callbacks removing the
 * supplied network.
 */
void wpa_config_foreach_network(struct wpa_config *config,
				void (*func)(void *, struct wpa_ssid *),
				void *arg)
{
	struct wpa_ssid *ssid, *next;

	ssid = config->ssid;
	while (ssid) {
		next = ssid->next;
		func(arg, ssid);
		ssid = next;
	}
}


/**
 * wpa_config_get_network - Get configured network based on id
 * @config: Configuration data from wpa_config_read()
 * @id: Unique network id to search for
 * Returns: Network configuration or %NULL if not found
 */
struct wpa_ssid * wpa_config_get_network(struct wpa_config *config, int id)
{
	struct wpa_ssid *ssid;

	ssid = config->ssid;
	while (ssid) {
		if (id == ssid->id)
			break;
		ssid = ssid->next;
	}

	return ssid;
}


/**
 * wpa_config_add_network - Add a new network with empty configuration
 * @config: Configuration data from wpa_config_read()
 * Returns: The new network configuration or %NULL if operation failed
 */
struct wpa_ssid * wpa_config_add_network(struct wpa_config *config)
{
	int id;
	struct wpa_ssid *ssid, *last = NULL;

	id = -1;
	ssid = config->ssid;
	while (ssid) {
		if (ssid->id > id)
			id = ssid->id;
		last = ssid;
		ssid = ssid->next;
	}
	id++;

	ssid = os_zalloc(sizeof(*ssid));
	if (ssid == NULL)
		return NULL;
	ssid->id = id;
	dl_list_init(&ssid->psk_list);
	if (last)
		last->next = ssid;
	else
		config->ssid = ssid;

	wpa_config_update_prio_list(config);

	return ssid;
}


/**
 * wpa_config_remove_network - Remove a configured network based on id
 * @config: Configuration data from wpa_config_read()
 * @id: Unique network id to search for
 * Returns: 0 on success, or -1 if the network was not found
 */
int wpa_config_remove_network(struct wpa_config *config, int id)
{
	struct wpa_ssid *ssid, *prev = NULL;

	ssid = config->ssid;
	while (ssid) {
		if (id == ssid->id)
			break;
		prev = ssid;
		ssid = ssid->next;
	}

	if (ssid == NULL)
		return -1;

	if (prev)
		prev->next = ssid->next;
	else
		config->ssid = ssid->next;

	wpa_config_update_prio_list(config);
	wpa_config_free_ssid(ssid);
	return 0;
}


/**
 * wpa_config_set_network_defaults - Set network default values
 * @ssid: Pointer to network configuration data
 */
void wpa_config_set_network_defaults(struct wpa_ssid *ssid)
{
	ssid->proto = DEFAULT_PROTO;
	ssid->pairwise_cipher = DEFAULT_PAIRWISE;
	ssid->group_cipher = DEFAULT_GROUP;
	ssid->key_mgmt = DEFAULT_KEY_MGMT;
	ssid->bg_scan_period = DEFAULT_BG_SCAN_PERIOD;
	ssid->ht = 1;
#ifdef IEEE8021X_EAPOL
	ssid->eapol_flags = DEFAULT_EAPOL_FLAGS;
	ssid->eap_workaround = DEFAULT_EAP_WORKAROUND;
	ssid->eap.fragment_size = DEFAULT_FRAGMENT_SIZE;
	ssid->eap.sim_num = DEFAULT_USER_SELECTED_SIM;
#endif /* IEEE8021X_EAPOL */
#ifdef CONFIG_MESH
	ssid->dot11MeshMaxRetries = DEFAULT_MESH_MAX_RETRIES;
	ssid->dot11MeshRetryTimeout = DEFAULT_MESH_RETRY_TIMEOUT;
	ssid->dot11MeshConfirmTimeout = DEFAULT_MESH_CONFIRM_TIMEOUT;
	ssid->dot11MeshHoldingTimeout = DEFAULT_MESH_HOLDING_TIMEOUT;
	ssid->mesh_rssi_threshold = DEFAULT_MESH_RSSI_THRESHOLD;
#endif /* CONFIG_MESH */
#ifdef CONFIG_HT_OVERRIDES
	ssid->disable_ht = DEFAULT_DISABLE_HT;
	ssid->disable_ht40 = DEFAULT_DISABLE_HT40;
	ssid->disable_sgi = DEFAULT_DISABLE_SGI;
	ssid->disable_ldpc = DEFAULT_DISABLE_LDPC;
	ssid->disable_max_amsdu = DEFAULT_DISABLE_MAX_AMSDU;
	ssid->ampdu_factor = DEFAULT_AMPDU_FACTOR;
	ssid->ampdu_density = DEFAULT_AMPDU_DENSITY;
#endif /* CONFIG_HT_OVERRIDES */
#ifdef CONFIG_VHT_OVERRIDES
	ssid->vht_rx_mcs_nss_1 = -1;
	ssid->vht_rx_mcs_nss_2 = -1;
	ssid->vht_rx_mcs_nss_3 = -1;
	ssid->vht_rx_mcs_nss_4 = -1;
	ssid->vht_rx_mcs_nss_5 = -1;
	ssid->vht_rx_mcs_nss_6 = -1;
	ssid->vht_rx_mcs_nss_7 = -1;
	ssid->vht_rx_mcs_nss_8 = -1;
	ssid->vht_tx_mcs_nss_1 = -1;
	ssid->vht_tx_mcs_nss_2 = -1;
	ssid->vht_tx_mcs_nss_3 = -1;
	ssid->vht_tx_mcs_nss_4 = -1;
	ssid->vht_tx_mcs_nss_5 = -1;
	ssid->vht_tx_mcs_nss_6 = -1;
	ssid->vht_tx_mcs_nss_7 = -1;
	ssid->vht_tx_mcs_nss_8 = -1;
#endif /* CONFIG_VHT_OVERRIDES */
	ssid->proactive_key_caching = -1;
#ifdef CONFIG_IEEE80211W
	ssid->ieee80211w = MGMT_FRAME_PROTECTION_DEFAULT;
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_MACSEC
	ssid->mka_priority = DEFAULT_PRIO_NOT_KEY_SERVER;
#endif /* CONFIG_MACSEC */
	ssid->mac_addr = -1;
}


/**
 * wpa_config_set - Set a variable in network configuration
 * @ssid: Pointer to network configuration data
 * @var: Variable name, e.g., "ssid"
 * @value: Variable value
 * @line: Line number in configuration file or 0 if not used
 * Returns: 0 on success with possible change in the value, 1 on success with
 * no change to previously configured value, or -1 on failure
 *
 * This function can be used to set network configuration variables based on
 * both the configuration file and management interface input. The value
 * parameter must be in the same format as the text-based configuration file is
 * using. For example, strings are using double quotation marks.
 */
int wpa_config_set(struct wpa_ssid *ssid, const char *var, const char *value,
		   int line)
{
	size_t i;
	int ret = 0;

	if (ssid == NULL || var == NULL || value == NULL)
		return -1;

	for (i = 0; i < NUM_SSID_FIELDS; i++) {
		const struct parse_data *field = &ssid_fields[i];
		if (os_strcmp(var, field->name) != 0)
			continue;

		ret = field->parser(field, ssid, line, value);
		if (ret < 0) {
			if (line) {
				wpa_printf(MSG_ERROR, "Line %d: failed to "
					   "parse %s '%s'.", line, var, value);
			}
			ret = -1;
		}
		break;
	}
	if (i == NUM_SSID_FIELDS) {
		if (line) {
			wpa_printf(MSG_ERROR, "Line %d: unknown network field "
				   "'%s'.", line, var);
		}
		ret = -1;
	}

	return ret;
}


int wpa_config_set_quoted(struct wpa_ssid *ssid, const char *var,
			  const char *value)
{
	size_t len;
	char *buf;
	int ret;

	len = os_strlen(value);
	buf = os_malloc(len + 3);
	if (buf == NULL)
		return -1;
	buf[0] = '"';
	os_memcpy(buf + 1, value, len);
	buf[len + 1] = '"';
	buf[len + 2] = '\0';
	ret = wpa_config_set(ssid, var, buf, 0);
	os_free(buf);
	return ret;
}


/**
 * wpa_config_get_all - Get all options from network configuration
 * @ssid: Pointer to network configuration data
 * @get_keys: Determines if keys/passwords will be included in returned list
 *	(if they may be exported)
 * Returns: %NULL terminated list of all set keys and their values in the form
 * of [key1, val1, key2, val2, ... , NULL]
 *
 * This function can be used to get list of all configured network properties.
 * The caller is responsible for freeing the returned list and all its
 * elements.
 */
char ** wpa_config_get_all(struct wpa_ssid *ssid, int get_keys)
{
#ifdef NO_CONFIG_WRITE
	return NULL;
#else /* NO_CONFIG_WRITE */
	const struct parse_data *field;
	char *key, *value;
	size_t i;
	char **props;
	int fields_num;

	get_keys = get_keys && ssid->export_keys;

	props = os_calloc(2 * NUM_SSID_FIELDS + 1, sizeof(char *));
	if (!props)
		return NULL;

	fields_num = 0;
	for (i = 0; i < NUM_SSID_FIELDS; i++) {
		field = &ssid_fields[i];
		if (field->key_data && !get_keys)
			continue;
		value = field->writer(field, ssid);
		if (value == NULL)
			continue;
		if (os_strlen(value) == 0) {
			os_free(value);
			continue;
		}

		key = os_strdup(field->name);
		if (key == NULL) {
			os_free(value);
			goto err;
		}

		props[fields_num * 2] = key;
		props[fields_num * 2 + 1] = value;

		fields_num++;
	}

	return props;

err:
	for (i = 0; props[i]; i++)
		os_free(props[i]);
	os_free(props);
	return NULL;
#endif /* NO_CONFIG_WRITE */
}


#ifndef NO_CONFIG_WRITE
/**
 * wpa_config_get - Get a variable in network configuration
 * @ssid: Pointer to network configuration data
 * @var: Variable name, e.g., "ssid"
 * Returns: Value of the variable or %NULL on failure
 *
 * This function can be used to get network configuration variables. The
 * returned value is a copy of the configuration variable in text format, i.e,.
 * the same format that the text-based configuration file and wpa_config_set()
 * are using for the value. The caller is responsible for freeing the returned
 * value.
 */
char * wpa_config_get(struct wpa_ssid *ssid, const char *var)
{
	size_t i;

	if (ssid == NULL || var == NULL)
		return NULL;

	for (i = 0; i < NUM_SSID_FIELDS; i++) {
		const struct parse_data *field = &ssid_fields[i];
		if (os_strcmp(var, field->name) == 0) {
			char *ret = field->writer(field, ssid);

			if (ret && has_newline(ret)) {
				wpa_printf(MSG_ERROR,
					   "Found newline in value for %s; not returning it",
					   var);
				os_free(ret);
				ret = NULL;
			}

			return ret;
		}
	}

	return NULL;
}


/**
 * wpa_config_get_no_key - Get a variable in network configuration (no keys)
 * @ssid: Pointer to network configuration data
 * @var: Variable name, e.g., "ssid"
 * Returns: Value of the variable or %NULL on failure
 *
 * This function can be used to get network configuration variable like
 * wpa_config_get(). The only difference is that this functions does not expose
 * key/password material from the configuration. In case a key/password field
 * is requested, the returned value is an empty string or %NULL if the variable
 * is not set or "*" if the variable is set (regardless of its value). The
 * returned value is a copy of the configuration variable in text format, i.e,.
 * the same format that the text-based configuration file and wpa_config_set()
 * are using for the value. The caller is responsible for freeing the returned
 * value.
 */
char * wpa_config_get_no_key(struct wpa_ssid *ssid, const char *var)
{
	size_t i;

	if (ssid == NULL || var == NULL)
		return NULL;

	for (i = 0; i < NUM_SSID_FIELDS; i++) {
		const struct parse_data *field = &ssid_fields[i];
		if (os_strcmp(var, field->name) == 0) {
			char *res = field->writer(field, ssid);
			if (field->key_data) {
				if (res && res[0]) {
					wpa_printf(MSG_DEBUG, "Do not allow "
						   "key_data field to be "
						   "exposed");
					str_clear_free(res);
					return os_strdup("*");
				}

				os_free(res);
				return NULL;
			}
			return res;
		}
	}

	return NULL;
}
#endif /* NO_CONFIG_WRITE */


/**
 * wpa_config_update_psk - Update WPA PSK based on passphrase and SSID
 * @ssid: Pointer to network configuration data
 *
 * This function must be called to update WPA PSK when either SSID or the
 * passphrase has changed for the network configuration.
 */
void wpa_config_update_psk(struct wpa_ssid *ssid)
{
#ifndef CONFIG_NO_PBKDF2
	pbkdf2_sha1(ssid->passphrase, ssid->ssid, ssid->ssid_len, 4096,
		    ssid->psk, PMK_LEN);
	wpa_hexdump_key(MSG_MSGDUMP, "PSK (from passphrase)",
			ssid->psk, PMK_LEN);
	ssid->psk_set = 1;
#endif /* CONFIG_NO_PBKDF2 */
}


static int wpa_config_set_cred_req_conn_capab(struct wpa_cred *cred,
					      const char *value)
{
	u8 *proto;
	int **port;
	int *ports, *nports;
	const char *pos;
	unsigned int num_ports;

	proto = os_realloc_array(cred->req_conn_capab_proto,
				 cred->num_req_conn_capab + 1, sizeof(u8));
	if (proto == NULL)
		return -1;
	cred->req_conn_capab_proto = proto;

	port = os_realloc_array(cred->req_conn_capab_port,
				cred->num_req_conn_capab + 1, sizeof(int *));
	if (port == NULL)
		return -1;
	cred->req_conn_capab_port = port;

	proto[cred->num_req_conn_capab] = atoi(value);

	pos = os_strchr(value, ':');
	if (pos == NULL) {
		port[cred->num_req_conn_capab] = NULL;
		cred->num_req_conn_capab++;
		return 0;
	}
	pos++;

	ports = NULL;
	num_ports = 0;

	while (*pos) {
		nports = os_realloc_array(ports, num_ports + 1, sizeof(int));
		if (nports == NULL) {
			os_free(ports);
			return -1;
		}
		ports = nports;
		ports[num_ports++] = atoi(pos);

		pos = os_strchr(pos, ',');
		if (pos == NULL)
			break;
		pos++;
	}

	nports = os_realloc_array(ports, num_ports + 1, sizeof(int));
	if (nports == NULL) {
		os_free(ports);
		return -1;
	}
	ports = nports;
	ports[num_ports] = -1;

	port[cred->num_req_conn_capab] = ports;
	cred->num_req_conn_capab++;
	return 0;
}


static int wpa_config_set_cred_roaming_consortiums(struct wpa_cred *cred,
						   const char *value)
{
	u8 roaming_consortiums[MAX_ROAMING_CONS][MAX_ROAMING_CONS_OI_LEN];
	size_t roaming_consortiums_len[MAX_ROAMING_CONS];
	unsigned int num_roaming_consortiums = 0;
	const char *pos, *end;
	size_t len;

	os_memset(roaming_consortiums, 0, sizeof(roaming_consortiums));
	os_memset(roaming_consortiums_len, 0, sizeof(roaming_consortiums_len));

	for (pos = value;;) {
		end = os_strchr(pos, ',');
		len = end ? (size_t) (end - pos) : os_strlen(pos);
		if (!end && len == 0)
			break;
		if (len == 0 || (len & 1) != 0 ||
		    len / 2 > MAX_ROAMING_CONS_OI_LEN ||
		    hexstr2bin(pos,
			       roaming_consortiums[num_roaming_consortiums],
			       len / 2) < 0) {
			wpa_printf(MSG_INFO,
				   "Invalid roaming_consortiums entry: %s",
				   pos);
			return -1;
		}
		roaming_consortiums_len[num_roaming_consortiums] = len / 2;
		num_roaming_consortiums++;

		if (!end)
			break;

		if (num_roaming_consortiums >= MAX_ROAMING_CONS) {
			wpa_printf(MSG_INFO,
				   "Too many roaming_consortiums OIs");
			return -1;
		}

		pos = end + 1;
	}

	os_memcpy(cred->roaming_consortiums, roaming_consortiums,
		  sizeof(roaming_consortiums));
	os_memcpy(cred->roaming_consortiums_len, roaming_consortiums_len,
		  sizeof(roaming_consortiums_len));
	cred->num_roaming_consortiums = num_roaming_consortiums;

	return 0;
}


int wpa_config_set_cred(struct wpa_cred *cred, const char *var,
			const char *value, int line)
{
	char *val;
	size_t len;
	int res;

	if (os_strcmp(var, "temporary") == 0) {
		cred->temporary = atoi(value);
		return 0;
	}

	if (os_strcmp(var, "priority") == 0) {
		cred->priority = atoi(value);
		return 0;
	}

	if (os_strcmp(var, "sp_priority") == 0) {
		int prio = atoi(value);
		if (prio < 0 || prio > 255)
			return -1;
		cred->sp_priority = prio;
		return 0;
	}

	if (os_strcmp(var, "pcsc") == 0) {
		cred->pcsc = atoi(value);
		return 0;
	}

	if (os_strcmp(var, "eap") == 0) {
		struct eap_method_type method;
		method.method = eap_peer_get_type(value, &method.vendor);
		if (method.vendor == EAP_VENDOR_IETF &&
		    method.method == EAP_TYPE_NONE) {
			wpa_printf(MSG_ERROR, "Line %d: unknown EAP type '%s' "
				   "for a credential", line, value);
			return -1;
		}
		os_free(cred->eap_method);
		cred->eap_method = os_malloc(sizeof(*cred->eap_method));
		if (cred->eap_method == NULL)
			return -1;
		os_memcpy(cred->eap_method, &method, sizeof(method));
		return 0;
	}

	if (os_strcmp(var, "password") == 0 &&
	    os_strncmp(value, "ext:", 4) == 0) {
		if (has_newline(value))
			return -1;
		str_clear_free(cred->password);
		cred->password = os_strdup(value);
		cred->ext_password = 1;
		return 0;
	}

	if (os_strcmp(var, "update_identifier") == 0) {
		cred->update_identifier = atoi(value);
		return 0;
	}

	if (os_strcmp(var, "min_dl_bandwidth_home") == 0) {
		cred->min_dl_bandwidth_home = atoi(value);
		return 0;
	}

	if (os_strcmp(var, "min_ul_bandwidth_home") == 0) {
		cred->min_ul_bandwidth_home = atoi(value);
		return 0;
	}

	if (os_strcmp(var, "min_dl_bandwidth_roaming") == 0) {
		cred->min_dl_bandwidth_roaming = atoi(value);
		return 0;
	}

	if (os_strcmp(var, "min_ul_bandwidth_roaming") == 0) {
		cred->min_ul_bandwidth_roaming = atoi(value);
		return 0;
	}

	if (os_strcmp(var, "max_bss_load") == 0) {
		cred->max_bss_load = atoi(value);
		return 0;
	}

	if (os_strcmp(var, "req_conn_capab") == 0)
		return wpa_config_set_cred_req_conn_capab(cred, value);

	if (os_strcmp(var, "ocsp") == 0) {
		cred->ocsp = atoi(value);
		return 0;
	}

	if (os_strcmp(var, "sim_num") == 0) {
		cred->sim_num = atoi(value);
		return 0;
	}

	val = wpa_config_parse_string(value, &len);
	if (val == NULL ||
	    (os_strcmp(var, "excluded_ssid") != 0 &&
	     os_strcmp(var, "roaming_consortium") != 0 &&
	     os_strcmp(var, "required_roaming_consortium") != 0 &&
	     has_newline(val))) {
		wpa_printf(MSG_ERROR, "Line %d: invalid field '%s' string "
			   "value '%s'.", line, var, value);
		os_free(val);
		return -1;
	}

	if (os_strcmp(var, "realm") == 0) {
		os_free(cred->realm);
		cred->realm = val;
		return 0;
	}

	if (os_strcmp(var, "username") == 0) {
		str_clear_free(cred->username);
		cred->username = val;
		return 0;
	}

	if (os_strcmp(var, "password") == 0) {
		str_clear_free(cred->password);
		cred->password = val;
		cred->ext_password = 0;
		return 0;
	}

	if (os_strcmp(var, "ca_cert") == 0) {
		os_free(cred->ca_cert);
		cred->ca_cert = val;
		return 0;
	}

	if (os_strcmp(var, "client_cert") == 0) {
		os_free(cred->client_cert);
		cred->client_cert = val;
		return 0;
	}

	if (os_strcmp(var, "private_key") == 0) {
		os_free(cred->private_key);
		cred->private_key = val;
		return 0;
	}

	if (os_strcmp(var, "private_key_passwd") == 0) {
		str_clear_free(cred->private_key_passwd);
		cred->private_key_passwd = val;
		return 0;
	}

	if (os_strcmp(var, "imsi") == 0) {
		os_free(cred->imsi);
		cred->imsi = val;
		return 0;
	}

	if (os_strcmp(var, "milenage") == 0) {
		str_clear_free(cred->milenage);
		cred->milenage = val;
		return 0;
	}

	if (os_strcmp(var, "domain_suffix_match") == 0) {
		os_free(cred->domain_suffix_match);
		cred->domain_suffix_match = val;
		return 0;
	}

	if (os_strcmp(var, "domain") == 0) {
		char **new_domain;
		new_domain = os_realloc_array(cred->domain,
					      cred->num_domain + 1,
					      sizeof(char *));
		if (new_domain == NULL) {
			os_free(val);
			return -1;
		}
		new_domain[cred->num_domain++] = val;
		cred->domain = new_domain;
		return 0;
	}

	if (os_strcmp(var, "phase1") == 0) {
		os_free(cred->phase1);
		cred->phase1 = val;
		return 0;
	}

	if (os_strcmp(var, "phase2") == 0) {
		os_free(cred->phase2);
		cred->phase2 = val;
		return 0;
	}

	if (os_strcmp(var, "roaming_consortium") == 0) {
		if (len < 3 || len > sizeof(cred->roaming_consortium)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid "
				   "roaming_consortium length %d (3..15 "
				   "expected)", line, (int) len);
			os_free(val);
			return -1;
		}
		os_memcpy(cred->roaming_consortium, val, len);
		cred->roaming_consortium_len = len;
		os_free(val);
		return 0;
	}

	if (os_strcmp(var, "required_roaming_consortium") == 0) {
		if (len < 3 || len > sizeof(cred->required_roaming_consortium))
		{
			wpa_printf(MSG_ERROR, "Line %d: invalid "
				   "required_roaming_consortium length %d "
				   "(3..15 expected)", line, (int) len);
			os_free(val);
			return -1;
		}
		os_memcpy(cred->required_roaming_consortium, val, len);
		cred->required_roaming_consortium_len = len;
		os_free(val);
		return 0;
	}

	if (os_strcmp(var, "roaming_consortiums") == 0) {
		res = wpa_config_set_cred_roaming_consortiums(cred, val);
		if (res < 0)
			wpa_printf(MSG_ERROR,
				   "Line %d: invalid roaming_consortiums",
				   line);
		os_free(val);
		return res;
	}

	if (os_strcmp(var, "excluded_ssid") == 0) {
		struct excluded_ssid *e;

		if (len > SSID_MAX_LEN) {
			wpa_printf(MSG_ERROR, "Line %d: invalid "
				   "excluded_ssid length %d", line, (int) len);
			os_free(val);
			return -1;
		}

		e = os_realloc_array(cred->excluded_ssid,
				     cred->num_excluded_ssid + 1,
				     sizeof(struct excluded_ssid));
		if (e == NULL) {
			os_free(val);
			return -1;
		}
		cred->excluded_ssid = e;

		e = &cred->excluded_ssid[cred->num_excluded_ssid++];
		os_memcpy(e->ssid, val, len);
		e->ssid_len = len;

		os_free(val);

		return 0;
	}

	if (os_strcmp(var, "roaming_partner") == 0) {
		struct roaming_partner *p;
		char *pos;

		p = os_realloc_array(cred->roaming_partner,
				     cred->num_roaming_partner + 1,
				     sizeof(struct roaming_partner));
		if (p == NULL) {
			os_free(val);
			return -1;
		}
		cred->roaming_partner = p;

		p = &cred->roaming_partner[cred->num_roaming_partner];

		pos = os_strchr(val, ',');
		if (pos == NULL) {
			os_free(val);
			return -1;
		}
		*pos++ = '\0';
		if (pos - val - 1 >= (int) sizeof(p->fqdn)) {
			os_free(val);
			return -1;
		}
		os_memcpy(p->fqdn, val, pos - val);

		p->exact_match = atoi(pos);

		pos = os_strchr(pos, ',');
		if (pos == NULL) {
			os_free(val);
			return -1;
		}
		*pos++ = '\0';

		p->priority = atoi(pos);

		pos = os_strchr(pos, ',');
		if (pos == NULL) {
			os_free(val);
			return -1;
		}
		*pos++ = '\0';

		if (os_strlen(pos) >= sizeof(p->country)) {
			os_free(val);
			return -1;
		}
		os_memcpy(p->country, pos, os_strlen(pos) + 1);

		cred->num_roaming_partner++;
		os_free(val);

		return 0;
	}

	if (os_strcmp(var, "provisioning_sp") == 0) {
		os_free(cred->provisioning_sp);
		cred->provisioning_sp = val;
		return 0;
	}

	if (line) {
		wpa_printf(MSG_ERROR, "Line %d: unknown cred field '%s'.",
			   line, var);
	}

	os_free(val);

	return -1;
}


static char * alloc_int_str(int val)
{
	const unsigned int bufsize = 20;
	char *buf;
	int res;

	buf = os_malloc(bufsize);
	if (buf == NULL)
		return NULL;
	res = os_snprintf(buf, bufsize, "%d", val);
	if (os_snprintf_error(bufsize, res)) {
		os_free(buf);
		buf = NULL;
	}
	return buf;
}


static char * alloc_strdup(const char *str)
{
	if (str == NULL)
		return NULL;
	return os_strdup(str);
}


char * wpa_config_get_cred_no_key(struct wpa_cred *cred, const char *var)
{
	if (os_strcmp(var, "temporary") == 0)
		return alloc_int_str(cred->temporary);

	if (os_strcmp(var, "priority") == 0)
		return alloc_int_str(cred->priority);

	if (os_strcmp(var, "sp_priority") == 0)
		return alloc_int_str(cred->sp_priority);

	if (os_strcmp(var, "pcsc") == 0)
		return alloc_int_str(cred->pcsc);

	if (os_strcmp(var, "eap") == 0) {
		if (!cred->eap_method)
			return NULL;
		return alloc_strdup(eap_get_name(cred->eap_method[0].vendor,
						 cred->eap_method[0].method));
	}

	if (os_strcmp(var, "update_identifier") == 0)
		return alloc_int_str(cred->update_identifier);

	if (os_strcmp(var, "min_dl_bandwidth_home") == 0)
		return alloc_int_str(cred->min_dl_bandwidth_home);

	if (os_strcmp(var, "min_ul_bandwidth_home") == 0)
		return alloc_int_str(cred->min_ul_bandwidth_home);

	if (os_strcmp(var, "min_dl_bandwidth_roaming") == 0)
		return alloc_int_str(cred->min_dl_bandwidth_roaming);

	if (os_strcmp(var, "min_ul_bandwidth_roaming") == 0)
		return alloc_int_str(cred->min_ul_bandwidth_roaming);

	if (os_strcmp(var, "max_bss_load") == 0)
		return alloc_int_str(cred->max_bss_load);

	if (os_strcmp(var, "req_conn_capab") == 0) {
		unsigned int i;
		char *buf, *end, *pos;
		int ret;

		if (!cred->num_req_conn_capab)
			return NULL;

		buf = os_malloc(4000);
		if (buf == NULL)
			return NULL;
		pos = buf;
		end = pos + 4000;
		for (i = 0; i < cred->num_req_conn_capab; i++) {
			int *ports;

			ret = os_snprintf(pos, end - pos, "%s%u",
					  i > 0 ? "\n" : "",
					  cred->req_conn_capab_proto[i]);
			if (os_snprintf_error(end - pos, ret))
				return buf;
			pos += ret;

			ports = cred->req_conn_capab_port[i];
			if (ports) {
				int j;
				for (j = 0; ports[j] != -1; j++) {
					ret = os_snprintf(pos, end - pos,
							  "%s%d",
							  j > 0 ? "," : ":",
							  ports[j]);
					if (os_snprintf_error(end - pos, ret))
						return buf;
					pos += ret;
				}
			}
		}

		return buf;
	}

	if (os_strcmp(var, "ocsp") == 0)
		return alloc_int_str(cred->ocsp);

	if (os_strcmp(var, "realm") == 0)
		return alloc_strdup(cred->realm);

	if (os_strcmp(var, "username") == 0)
		return alloc_strdup(cred->username);

	if (os_strcmp(var, "password") == 0) {
		if (!cred->password)
			return NULL;
		return alloc_strdup("*");
	}

	if (os_strcmp(var, "ca_cert") == 0)
		return alloc_strdup(cred->ca_cert);

	if (os_strcmp(var, "client_cert") == 0)
		return alloc_strdup(cred->client_cert);

	if (os_strcmp(var, "private_key") == 0)
		return alloc_strdup(cred->private_key);

	if (os_strcmp(var, "private_key_passwd") == 0) {
		if (!cred->private_key_passwd)
			return NULL;
		return alloc_strdup("*");
	}

	if (os_strcmp(var, "imsi") == 0)
		return alloc_strdup(cred->imsi);

	if (os_strcmp(var, "milenage") == 0) {
		if (!(cred->milenage))
			return NULL;
		return alloc_strdup("*");
	}

	if (os_strcmp(var, "domain_suffix_match") == 0)
		return alloc_strdup(cred->domain_suffix_match);

	if (os_strcmp(var, "domain") == 0) {
		unsigned int i;
		char *buf, *end, *pos;
		int ret;

		if (!cred->num_domain)
			return NULL;

		buf = os_malloc(4000);
		if (buf == NULL)
			return NULL;
		pos = buf;
		end = pos + 4000;

		for (i = 0; i < cred->num_domain; i++) {
			ret = os_snprintf(pos, end - pos, "%s%s",
					  i > 0 ? "\n" : "", cred->domain[i]);
			if (os_snprintf_error(end - pos, ret))
				return buf;
			pos += ret;
		}

		return buf;
	}

	if (os_strcmp(var, "phase1") == 0)
		return alloc_strdup(cred->phase1);

	if (os_strcmp(var, "phase2") == 0)
		return alloc_strdup(cred->phase2);

	if (os_strcmp(var, "roaming_consortium") == 0) {
		size_t buflen;
		char *buf;

		if (!cred->roaming_consortium_len)
			return NULL;
		buflen = cred->roaming_consortium_len * 2 + 1;
		buf = os_malloc(buflen);
		if (buf == NULL)
			return NULL;
		wpa_snprintf_hex(buf, buflen, cred->roaming_consortium,
				 cred->roaming_consortium_len);
		return buf;
	}

	if (os_strcmp(var, "required_roaming_consortium") == 0) {
		size_t buflen;
		char *buf;

		if (!cred->required_roaming_consortium_len)
			return NULL;
		buflen = cred->required_roaming_consortium_len * 2 + 1;
		buf = os_malloc(buflen);
		if (buf == NULL)
			return NULL;
		wpa_snprintf_hex(buf, buflen, cred->required_roaming_consortium,
				 cred->required_roaming_consortium_len);
		return buf;
	}

	if (os_strcmp(var, "roaming_consortiums") == 0) {
		size_t buflen;
		char *buf, *pos;
		size_t i;

		if (!cred->num_roaming_consortiums)
			return NULL;
		buflen = cred->num_roaming_consortiums *
			MAX_ROAMING_CONS_OI_LEN * 2 + 1;
		buf = os_malloc(buflen);
		if (!buf)
			return NULL;
		pos = buf;
		for (i = 0; i < cred->num_roaming_consortiums; i++) {
			if (i > 0)
				*pos++ = ',';
			pos += wpa_snprintf_hex(
				pos, buf + buflen - pos,
				cred->roaming_consortiums[i],
				cred->roaming_consortiums_len[i]);
		}
		*pos = '\0';
		return buf;
	}

	if (os_strcmp(var, "excluded_ssid") == 0) {
		unsigned int i;
		char *buf, *end, *pos;

		if (!cred->num_excluded_ssid)
			return NULL;

		buf = os_malloc(4000);
		if (buf == NULL)
			return NULL;
		pos = buf;
		end = pos + 4000;

		for (i = 0; i < cred->num_excluded_ssid; i++) {
			struct excluded_ssid *e;
			int ret;

			e = &cred->excluded_ssid[i];
			ret = os_snprintf(pos, end - pos, "%s%s",
					  i > 0 ? "\n" : "",
					  wpa_ssid_txt(e->ssid, e->ssid_len));
			if (os_snprintf_error(end - pos, ret))
				return buf;
			pos += ret;
		}

		return buf;
	}

	if (os_strcmp(var, "roaming_partner") == 0) {
		unsigned int i;
		char *buf, *end, *pos;

		if (!cred->num_roaming_partner)
			return NULL;

		buf = os_malloc(4000);
		if (buf == NULL)
			return NULL;
		pos = buf;
		end = pos + 4000;

		for (i = 0; i < cred->num_roaming_partner; i++) {
			struct roaming_partner *p;
			int ret;

			p = &cred->roaming_partner[i];
			ret = os_snprintf(pos, end - pos, "%s%s,%d,%u,%s",
					  i > 0 ? "\n" : "",
					  p->fqdn, p->exact_match, p->priority,
					  p->country);
			if (os_snprintf_error(end - pos, ret))
				return buf;
			pos += ret;
		}

		return buf;
	}

	if (os_strcmp(var, "provisioning_sp") == 0)
		return alloc_strdup(cred->provisioning_sp);

	return NULL;
}


struct wpa_cred * wpa_config_get_cred(struct wpa_config *config, int id)
{
	struct wpa_cred *cred;

	cred = config->cred;
	while (cred) {
		if (id == cred->id)
			break;
		cred = cred->next;
	}

	return cred;
}


struct wpa_cred * wpa_config_add_cred(struct wpa_config *config)
{
	int id;
	struct wpa_cred *cred, *last = NULL;

	id = -1;
	cred = config->cred;
	while (cred) {
		if (cred->id > id)
			id = cred->id;
		last = cred;
		cred = cred->next;
	}
	id++;

	cred = os_zalloc(sizeof(*cred));
	if (cred == NULL)
		return NULL;
	cred->id = id;
	cred->sim_num = DEFAULT_USER_SELECTED_SIM;
	if (last)
		last->next = cred;
	else
		config->cred = cred;

	return cred;
}


int wpa_config_remove_cred(struct wpa_config *config, int id)
{
	struct wpa_cred *cred, *prev = NULL;

	cred = config->cred;
	while (cred) {
		if (id == cred->id)
			break;
		prev = cred;
		cred = cred->next;
	}

	if (cred == NULL)
		return -1;

	if (prev)
		prev->next = cred->next;
	else
		config->cred = cred->next;

	wpa_config_free_cred(cred);
	return 0;
}


#ifndef CONFIG_NO_CONFIG_BLOBS
/**
 * wpa_config_get_blob - Get a named configuration blob
 * @config: Configuration data from wpa_config_read()
 * @name: Name of the blob
 * Returns: Pointer to blob data or %NULL if not found
 */
const struct wpa_config_blob * wpa_config_get_blob(struct wpa_config *config,
						   const char *name)
{
	struct wpa_config_blob *blob = config->blobs;

	while (blob) {
		if (os_strcmp(blob->name, name) == 0)
			return blob;
		blob = blob->next;
	}
	return NULL;
}


/**
 * wpa_config_set_blob - Set or add a named configuration blob
 * @config: Configuration data from wpa_config_read()
 * @blob: New value for the blob
 *
 * Adds a new configuration blob or replaces the current value of an existing
 * blob.
 */
void wpa_config_set_blob(struct wpa_config *config,
			 struct wpa_config_blob *blob)
{
	wpa_config_remove_blob(config, blob->name);
	blob->next = config->blobs;
	config->blobs = blob;
}


/**
 * wpa_config_free_blob - Free blob data
 * @blob: Pointer to blob to be freed
 */
void wpa_config_free_blob(struct wpa_config_blob *blob)
{
	if (blob) {
		os_free(blob->name);
		bin_clear_free(blob->data, blob->len);
		os_free(blob);
	}
}


/**
 * wpa_config_remove_blob - Remove a named configuration blob
 * @config: Configuration data from wpa_config_read()
 * @name: Name of the blob to remove
 * Returns: 0 if blob was removed or -1 if blob was not found
 */
int wpa_config_remove_blob(struct wpa_config *config, const char *name)
{
	struct wpa_config_blob *pos = config->blobs, *prev = NULL;

	while (pos) {
		if (os_strcmp(pos->name, name) == 0) {
			if (prev)
				prev->next = pos->next;
			else
				config->blobs = pos->next;
			wpa_config_free_blob(pos);
			return 0;
		}
		prev = pos;
		pos = pos->next;
	}

	return -1;
}
#endif /* CONFIG_NO_CONFIG_BLOBS */


/**
 * wpa_config_alloc_empty - Allocate an empty configuration
 * @ctrl_interface: Control interface parameters, e.g., path to UNIX domain
 * socket
 * @driver_param: Driver parameters
 * Returns: Pointer to allocated configuration data or %NULL on failure
 */
struct wpa_config * wpa_config_alloc_empty(const char *ctrl_interface,
					   const char *driver_param)
{
	struct wpa_config *config;
	const int aCWmin = 4, aCWmax = 10;
	const struct hostapd_wmm_ac_params ac_bk =
		{ aCWmin, aCWmax, 7, 0, 0 }; /* background traffic */
	const struct hostapd_wmm_ac_params ac_be =
		{ aCWmin, aCWmax, 3, 0, 0 }; /* best effort traffic */
	const struct hostapd_wmm_ac_params ac_vi = /* video traffic */
		{ aCWmin - 1, aCWmin, 2, 3000 / 32, 0 };
	const struct hostapd_wmm_ac_params ac_vo = /* voice traffic */
		{ aCWmin - 2, aCWmin - 1, 2, 1500 / 32, 0 };

	config = os_zalloc(sizeof(*config));
	if (config == NULL)
		return NULL;
	config->eapol_version = DEFAULT_EAPOL_VERSION;
	config->ap_scan = DEFAULT_AP_SCAN;
	config->user_mpm = DEFAULT_USER_MPM;
	config->max_peer_links = DEFAULT_MAX_PEER_LINKS;
	config->mesh_max_inactivity = DEFAULT_MESH_MAX_INACTIVITY;
	config->dot11RSNASAERetransPeriod =
		DEFAULT_DOT11_RSNA_SAE_RETRANS_PERIOD;
	config->fast_reauth = DEFAULT_FAST_REAUTH;
	config->p2p_go_intent = DEFAULT_P2P_GO_INTENT;
	config->p2p_intra_bss = DEFAULT_P2P_INTRA_BSS;
	config->p2p_go_freq_change_policy = DEFAULT_P2P_GO_FREQ_MOVE;
	config->p2p_go_max_inactivity = DEFAULT_P2P_GO_MAX_INACTIVITY;
	config->p2p_optimize_listen_chan = DEFAULT_P2P_OPTIMIZE_LISTEN_CHAN;
	config->p2p_go_ctwindow = DEFAULT_P2P_GO_CTWINDOW;
	config->bss_max_count = DEFAULT_BSS_MAX_COUNT;
	config->bss_expiration_age = DEFAULT_BSS_EXPIRATION_AGE;
	config->bss_expiration_scan_count = DEFAULT_BSS_EXPIRATION_SCAN_COUNT;
	config->max_num_sta = DEFAULT_MAX_NUM_STA;
	config->ap_isolate = DEFAULT_AP_ISOLATE;
	config->access_network_type = DEFAULT_ACCESS_NETWORK_TYPE;
	config->scan_cur_freq = DEFAULT_SCAN_CUR_FREQ;
	config->wmm_ac_params[0] = ac_be;
	config->wmm_ac_params[1] = ac_bk;
	config->wmm_ac_params[2] = ac_vi;
	config->wmm_ac_params[3] = ac_vo;
	config->p2p_search_delay = DEFAULT_P2P_SEARCH_DELAY;
	config->rand_addr_lifetime = DEFAULT_RAND_ADDR_LIFETIME;
	config->key_mgmt_offload = DEFAULT_KEY_MGMT_OFFLOAD;
	config->cert_in_cb = DEFAULT_CERT_IN_CB;
	config->wpa_rsc_relaxation = DEFAULT_WPA_RSC_RELAXATION;

#ifdef CONFIG_MBO
	config->mbo_cell_capa = DEFAULT_MBO_CELL_CAPA;
	config->disassoc_imminent_rssi_threshold =
		DEFAULT_DISASSOC_IMMINENT_RSSI_THRESHOLD;
	config->oce = DEFAULT_OCE_SUPPORT;
#endif /* CONFIG_MBO */

	if (ctrl_interface)
		config->ctrl_interface = os_strdup(ctrl_interface);
	if (driver_param)
		config->driver_param = os_strdup(driver_param);
	config->gas_rand_addr_lifetime = DEFAULT_RAND_ADDR_LIFETIME;

	return config;
}


#ifndef CONFIG_NO_STDOUT_DEBUG
/**
 * wpa_config_debug_dump_networks - Debug dump of configured networks
 * @config: Configuration data from wpa_config_read()
 */
void wpa_config_debug_dump_networks(struct wpa_config *config)
{
	int prio;
	struct wpa_ssid *ssid;

	for (prio = 0; prio < config->num_prio; prio++) {
		ssid = config->pssid[prio];
		wpa_printf(MSG_DEBUG, "Priority group %d",
			   ssid->priority);
		while (ssid) {
			wpa_printf(MSG_DEBUG, "   id=%d ssid='%s'",
				   ssid->id,
				   wpa_ssid_txt(ssid->ssid, ssid->ssid_len));
			ssid = ssid->pnext;
		}
	}
}
#endif /* CONFIG_NO_STDOUT_DEBUG */


struct global_parse_data {
	char *name;
	int (*parser)(const struct global_parse_data *data,
		      struct wpa_config *config, int line, const char *value);
	int (*get)(const char *name, struct wpa_config *config, long offset,
		   char *buf, size_t buflen, int pretty_print);
	void *param1, *param2, *param3;
	unsigned int changed_flag;
};


static int wpa_global_config_parse_int(const struct global_parse_data *data,
				       struct wpa_config *config, int line,
				       const char *pos)
{
	int val, *dst;
	char *end;

	dst = (int *) (((u8 *) config) + (long) data->param1);
	val = strtol(pos, &end, 0);
	if (*end) {
		wpa_printf(MSG_ERROR, "Line %d: invalid number \"%s\"",
			   line, pos);
		return -1;
	}
	*dst = val;

	wpa_printf(MSG_DEBUG, "%s=%d", data->name, *dst);

	if (data->param2 && *dst < (long) data->param2) {
		wpa_printf(MSG_ERROR, "Line %d: too small %s (value=%d "
			   "min_value=%ld)", line, data->name, *dst,
			   (long) data->param2);
		*dst = (long) data->param2;
		return -1;
	}

	if (data->param3 && *dst > (long) data->param3) {
		wpa_printf(MSG_ERROR, "Line %d: too large %s (value=%d "
			   "max_value=%ld)", line, data->name, *dst,
			   (long) data->param3);
		*dst = (long) data->param3;
		return -1;
	}

	return 0;
}


static int wpa_global_config_parse_str(const struct global_parse_data *data,
				       struct wpa_config *config, int line,
				       const char *pos)
{
	size_t len;
	char **dst, *tmp;

	len = os_strlen(pos);
	if (data->param2 && len < (size_t) data->param2) {
		wpa_printf(MSG_ERROR, "Line %d: too short %s (len=%lu "
			   "min_len=%ld)", line, data->name,
			   (unsigned long) len, (long) data->param2);
		return -1;
	}

	if (data->param3 && len > (size_t) data->param3) {
		wpa_printf(MSG_ERROR, "Line %d: too long %s (len=%lu "
			   "max_len=%ld)", line, data->name,
			   (unsigned long) len, (long) data->param3);
		return -1;
	}

	if (has_newline(pos)) {
		wpa_printf(MSG_ERROR, "Line %d: invalid %s value with newline",
			   line, data->name);
		return -1;
	}

	tmp = os_strdup(pos);
	if (tmp == NULL)
		return -1;

	dst = (char **) (((u8 *) config) + (long) data->param1);
	os_free(*dst);
	*dst = tmp;
	wpa_printf(MSG_DEBUG, "%s='%s'", data->name, *dst);

	return 0;
}


static int wpa_config_process_bgscan(const struct global_parse_data *data,
				     struct wpa_config *config, int line,
				     const char *pos)
{
	size_t len;
	char *tmp;
	int res;

	tmp = wpa_config_parse_string(pos, &len);
	if (tmp == NULL) {
		wpa_printf(MSG_ERROR, "Line %d: failed to parse %s",
			   line, data->name);
		return -1;
	}

	res = wpa_global_config_parse_str(data, config, line, tmp);
	os_free(tmp);
	return res;
}


static int wpa_global_config_parse_bin(const struct global_parse_data *data,
				       struct wpa_config *config, int line,
				       const char *pos)
{
	struct wpabuf **dst, *tmp;

	tmp = wpabuf_parse_bin(pos);
	if (!tmp)
		return -1;

	dst = (struct wpabuf **) (((u8 *) config) + (long) data->param1);
	wpabuf_free(*dst);
	*dst = tmp;
	wpa_printf(MSG_DEBUG, "%s", data->name);

	return 0;
}


static int wpa_config_process_freq_list(const struct global_parse_data *data,
					struct wpa_config *config, int line,
					const char *value)
{
	int *freqs;

	freqs = wpa_config_parse_int_array(value);
	if (freqs == NULL)
		return -1;
	if (freqs[0] == 0) {
		os_free(freqs);
		freqs = NULL;
	}
	os_free(config->freq_list);
	config->freq_list = freqs;
	return 0;
}


#ifdef CONFIG_P2P
static int wpa_global_config_parse_ipv4(const struct global_parse_data *data,
					struct wpa_config *config, int line,
					const char *pos)
{
	u32 *dst;
	struct hostapd_ip_addr addr;

	if (hostapd_parse_ip_addr(pos, &addr) < 0)
		return -1;
	if (addr.af != AF_INET)
		return -1;

	dst = (u32 *) (((u8 *) config) + (long) data->param1);
	os_memcpy(dst, &addr.u.v4.s_addr, 4);
	wpa_printf(MSG_DEBUG, "%s = 0x%x", data->name,
		   WPA_GET_BE32((u8 *) dst));

	return 0;
}
#endif /* CONFIG_P2P */


static int wpa_config_process_country(const struct global_parse_data *data,
				      struct wpa_config *config, int line,
				      const char *pos)
{
	if (!pos[0] || !pos[1]) {
		wpa_printf(MSG_DEBUG, "Invalid country set");
		return -1;
	}
	config->country[0] = pos[0];
	config->country[1] = pos[1];
	wpa_printf(MSG_DEBUG, "country='%c%c'",
		   config->country[0], config->country[1]);
	return 0;
}


static int wpa_config_process_load_dynamic_eap(
	const struct global_parse_data *data, struct wpa_config *config,
	int line, const char *so)
{
	int ret;
	wpa_printf(MSG_DEBUG, "load_dynamic_eap=%s", so);
	ret = eap_peer_method_load(so);
	if (ret == -2) {
		wpa_printf(MSG_DEBUG, "This EAP type was already loaded - not "
			   "reloading.");
	} else if (ret) {
		wpa_printf(MSG_ERROR, "Line %d: Failed to load dynamic EAP "
			   "method '%s'.", line, so);
		return -1;
	}

	return 0;
}


#ifdef CONFIG_WPS

static int wpa_config_process_uuid(const struct global_parse_data *data,
				   struct wpa_config *config, int line,
				   const char *pos)
{
	char buf[40];
	if (uuid_str2bin(pos, config->uuid)) {
		wpa_printf(MSG_ERROR, "Line %d: invalid UUID", line);
		return -1;
	}
	uuid_bin2str(config->uuid, buf, sizeof(buf));
	wpa_printf(MSG_DEBUG, "uuid=%s", buf);
	return 0;
}


static int wpa_config_process_device_type(
	const struct global_parse_data *data,
	struct wpa_config *config, int line, const char *pos)
{
	return wps_dev_type_str2bin(pos, config->device_type);
}


static int wpa_config_process_os_version(const struct global_parse_data *data,
					 struct wpa_config *config, int line,
					 const char *pos)
{
	if (hexstr2bin(pos, config->os_version, 4)) {
		wpa_printf(MSG_ERROR, "Line %d: invalid os_version", line);
		return -1;
	}
	wpa_printf(MSG_DEBUG, "os_version=%08x",
		   WPA_GET_BE32(config->os_version));
	return 0;
}


static int wpa_config_process_wps_vendor_ext_m1(
	const struct global_parse_data *data,
	struct wpa_config *config, int line, const char *pos)
{
	struct wpabuf *tmp;
	int len = os_strlen(pos) / 2;
	u8 *p;

	if (!len) {
		wpa_printf(MSG_ERROR, "Line %d: "
			   "invalid wps_vendor_ext_m1", line);
		return -1;
	}

	tmp = wpabuf_alloc(len);
	if (tmp) {
		p = wpabuf_put(tmp, len);

		if (hexstr2bin(pos, p, len)) {
			wpa_printf(MSG_ERROR, "Line %d: "
				   "invalid wps_vendor_ext_m1", line);
			wpabuf_free(tmp);
			return -1;
		}

		wpabuf_free(config->wps_vendor_ext_m1);
		config->wps_vendor_ext_m1 = tmp;
	} else {
		wpa_printf(MSG_ERROR, "Can not allocate "
			   "memory for wps_vendor_ext_m1");
		return -1;
	}

	return 0;
}

#endif /* CONFIG_WPS */

#ifdef CONFIG_P2P
static int wpa_config_process_sec_device_type(
	const struct global_parse_data *data,
	struct wpa_config *config, int line, const char *pos)
{
	int idx;

	if (config->num_sec_device_types >= MAX_SEC_DEVICE_TYPES) {
		wpa_printf(MSG_ERROR, "Line %d: too many sec_device_type "
			   "items", line);
		return -1;
	}

	idx = config->num_sec_device_types;

	if (wps_dev_type_str2bin(pos, config->sec_device_type[idx]))
		return -1;

	config->num_sec_device_types++;
	return 0;
}


static int wpa_config_process_p2p_pref_chan(
	const struct global_parse_data *data,
	struct wpa_config *config, int line, const char *pos)
{
	struct p2p_channel *pref = NULL, *n;
	unsigned int num = 0;
	const char *pos2;
	u8 op_class, chan;

	/* format: class:chan,class:chan,... */

	while (*pos) {
		op_class = atoi(pos);
		pos2 = os_strchr(pos, ':');
		if (pos2 == NULL)
			goto fail;
		pos2++;
		chan = atoi(pos2);

		n = os_realloc_array(pref, num + 1,
				     sizeof(struct p2p_channel));
		if (n == NULL)
			goto fail;
		pref = n;
		pref[num].op_class = op_class;
		pref[num].chan = chan;
		num++;

		pos = os_strchr(pos2, ',');
		if (pos == NULL)
			break;
		pos++;
	}

	os_free(config->p2p_pref_chan);
	config->p2p_pref_chan = pref;
	config->num_p2p_pref_chan = num;
	wpa_hexdump(MSG_DEBUG, "P2P: Preferred class/channel pairs",
		    (u8 *) config->p2p_pref_chan,
		    config->num_p2p_pref_chan * sizeof(struct p2p_channel));

	return 0;

fail:
	os_free(pref);
	wpa_printf(MSG_ERROR, "Line %d: Invalid p2p_pref_chan list", line);
	return -1;
}


static int wpa_config_process_p2p_no_go_freq(
	const struct global_parse_data *data,
	struct wpa_config *config, int line, const char *pos)
{
	int ret;

	ret = freq_range_list_parse(&config->p2p_no_go_freq, pos);
	if (ret < 0) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid p2p_no_go_freq", line);
		return -1;
	}

	wpa_printf(MSG_DEBUG, "P2P: p2p_no_go_freq with %u items",
		   config->p2p_no_go_freq.num);

	return 0;
}

#endif /* CONFIG_P2P */


static int wpa_config_process_hessid(
	const struct global_parse_data *data,
	struct wpa_config *config, int line, const char *pos)
{
	if (hwaddr_aton2(pos, config->hessid) < 0) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid hessid '%s'",
			   line, pos);
		return -1;
	}

	return 0;
}


static int wpa_config_process_sae_groups(
	const struct global_parse_data *data,
	struct wpa_config *config, int line, const char *pos)
{
	int *groups = wpa_config_parse_int_array(pos);
	if (groups == NULL) {
		wpa_printf(MSG_ERROR, "Line %d: Invalid sae_groups '%s'",
			   line, pos);
		return -1;
	}

	os_free(config->sae_groups);
	config->sae_groups = groups;

	return 0;
}


static int wpa_config_process_ap_vendor_elements(
	const struct global_parse_data *data,
	struct wpa_config *config, int line, const char *pos)
{
	struct wpabuf *tmp;
	int len = os_strlen(pos) / 2;
	u8 *p;

	if (!len) {
		wpa_printf(MSG_ERROR, "Line %d: invalid ap_vendor_elements",
			   line);
		return -1;
	}

	tmp = wpabuf_alloc(len);
	if (tmp) {
		p = wpabuf_put(tmp, len);

		if (hexstr2bin(pos, p, len)) {
			wpa_printf(MSG_ERROR, "Line %d: invalid "
				   "ap_vendor_elements", line);
			wpabuf_free(tmp);
			return -1;
		}

		wpabuf_free(config->ap_vendor_elements);
		config->ap_vendor_elements = tmp;
	} else {
		wpa_printf(MSG_ERROR, "Cannot allocate memory for "
			   "ap_vendor_elements");
		return -1;
	}

	return 0;
}


#ifdef CONFIG_CTRL_IFACE
static int wpa_config_process_no_ctrl_interface(
	const struct global_parse_data *data,
	struct wpa_config *config, int line, const char *pos)
{
	wpa_printf(MSG_DEBUG, "no_ctrl_interface -> ctrl_interface=NULL");
	os_free(config->ctrl_interface);
	config->ctrl_interface = NULL;
	return 0;
}
#endif /* CONFIG_CTRL_IFACE */


static int wpa_config_get_int(const char *name, struct wpa_config *config,
			      long offset, char *buf, size_t buflen,
			      int pretty_print)
{
	int *val = (int *) (((u8 *) config) + (long) offset);

	if (pretty_print)
		return os_snprintf(buf, buflen, "%s=%d\n", name, *val);
	return os_snprintf(buf, buflen, "%d", *val);
}


static int wpa_config_get_str(const char *name, struct wpa_config *config,
			      long offset, char *buf, size_t buflen,
			      int pretty_print)
{
	char **val = (char **) (((u8 *) config) + (long) offset);
	int res;

	if (pretty_print)
		res = os_snprintf(buf, buflen, "%s=%s\n", name,
				  *val ? *val : "null");
	else if (!*val)
		return -1;
	else
		res = os_snprintf(buf, buflen, "%s", *val);
	if (os_snprintf_error(buflen, res))
		res = -1;

	return res;
}


#ifdef CONFIG_P2P
static int wpa_config_get_ipv4(const char *name, struct wpa_config *config,
			       long offset, char *buf, size_t buflen,
			       int pretty_print)
{
	void *val = ((u8 *) config) + (long) offset;
	int res;
	char addr[INET_ADDRSTRLEN];

	if (!val || !inet_ntop(AF_INET, val, addr, sizeof(addr)))
		return -1;

	if (pretty_print)
		res = os_snprintf(buf, buflen, "%s=%s\n", name, addr);
	else
		res = os_snprintf(buf, buflen, "%s", addr);

	if (os_snprintf_error(buflen, res))
		res = -1;

	return res;
}
#endif /* CONFIG_P2P */


#ifdef OFFSET
#undef OFFSET
#endif /* OFFSET */
/* OFFSET: Get offset of a variable within the wpa_config structure */
#define OFFSET(v) ((void *) &((struct wpa_config *) 0)->v)

#define FUNC(f) #f, wpa_config_process_ ## f, NULL, OFFSET(f), NULL, NULL
#define FUNC_NO_VAR(f) #f, wpa_config_process_ ## f, NULL, NULL, NULL, NULL
#define _INT(f) #f, wpa_global_config_parse_int, wpa_config_get_int, OFFSET(f)
#define INT(f) _INT(f), NULL, NULL
#define INT_RANGE(f, min, max) _INT(f), (void *) min, (void *) max
#define _STR(f) #f, wpa_global_config_parse_str, wpa_config_get_str, OFFSET(f)
#define STR(f) _STR(f), NULL, NULL
#define STR_RANGE(f, min, max) _STR(f), (void *) min, (void *) max
#define BIN(f) #f, wpa_global_config_parse_bin, NULL, OFFSET(f), NULL, NULL
#define IPV4(f) #f, wpa_global_config_parse_ipv4, wpa_config_get_ipv4,  \
	OFFSET(f), NULL, NULL

static const struct global_parse_data global_fields[] = {
#ifdef CONFIG_CTRL_IFACE
	{ STR(ctrl_interface), 0 },
	{ FUNC_NO_VAR(no_ctrl_interface), 0 },
	{ STR(ctrl_interface_group), 0 } /* deprecated */,
#endif /* CONFIG_CTRL_IFACE */
#ifdef CONFIG_MACSEC
	{ INT_RANGE(eapol_version, 1, 3), 0 },
#else /* CONFIG_MACSEC */
	{ INT_RANGE(eapol_version, 1, 2), 0 },
#endif /* CONFIG_MACSEC */
	{ INT(ap_scan), 0 },
	{ FUNC(bgscan), 0 },
#ifdef CONFIG_MESH
	{ INT(user_mpm), 0 },
	{ INT_RANGE(max_peer_links, 0, 255), 0 },
	{ INT(mesh_max_inactivity), 0 },
	{ INT(dot11RSNASAERetransPeriod), 0 },
#endif /* CONFIG_MESH */
	{ INT(disable_scan_offload), 0 },
	{ INT(fast_reauth), 0 },
	{ STR(opensc_engine_path), 0 },
	{ STR(pkcs11_engine_path), 0 },
	{ STR(pkcs11_module_path), 0 },
	{ STR(openssl_ciphers), 0 },
	{ STR(pcsc_reader), 0 },
	{ STR(pcsc_pin), 0 },
	{ INT(external_sim), 0 },
	{ STR(driver_param), 0 },
	{ INT(dot11RSNAConfigPMKLifetime), 0 },
	{ INT(dot11RSNAConfigPMKReauthThreshold), 0 },
	{ INT(dot11RSNAConfigSATimeout), 0 },
#ifndef CONFIG_NO_CONFIG_WRITE
	{ INT(update_config), 0 },
#endif /* CONFIG_NO_CONFIG_WRITE */
	{ FUNC_NO_VAR(load_dynamic_eap), 0 },
#ifdef CONFIG_WPS
	{ FUNC(uuid), CFG_CHANGED_UUID },
	{ INT_RANGE(auto_uuid, 0, 1), 0 },
	{ STR_RANGE(device_name, 0, WPS_DEV_NAME_MAX_LEN),
	  CFG_CHANGED_DEVICE_NAME },
	{ STR_RANGE(manufacturer, 0, 64), CFG_CHANGED_WPS_STRING },
	{ STR_RANGE(model_name, 0, 32), CFG_CHANGED_WPS_STRING },
	{ STR_RANGE(model_number, 0, 32), CFG_CHANGED_WPS_STRING },
	{ STR_RANGE(serial_number, 0, 32), CFG_CHANGED_WPS_STRING },
	{ FUNC(device_type), CFG_CHANGED_DEVICE_TYPE },
	{ FUNC(os_version), CFG_CHANGED_OS_VERSION },
	{ STR(config_methods), CFG_CHANGED_CONFIG_METHODS },
	{ INT_RANGE(wps_cred_processing, 0, 2), 0 },
	{ FUNC(wps_vendor_ext_m1), CFG_CHANGED_VENDOR_EXTENSION },
#endif /* CONFIG_WPS */
#ifdef CONFIG_P2P
	{ FUNC(sec_device_type), CFG_CHANGED_SEC_DEVICE_TYPE },
	{ INT(p2p_listen_reg_class), CFG_CHANGED_P2P_LISTEN_CHANNEL },
	{ INT(p2p_listen_channel), CFG_CHANGED_P2P_LISTEN_CHANNEL },
	{ INT(p2p_oper_reg_class), CFG_CHANGED_P2P_OPER_CHANNEL },
	{ INT(p2p_oper_channel), CFG_CHANGED_P2P_OPER_CHANNEL },
	{ INT_RANGE(p2p_go_intent, 0, 15), 0 },
	{ STR(p2p_ssid_postfix), CFG_CHANGED_P2P_SSID_POSTFIX },
	{ INT_RANGE(persistent_reconnect, 0, 1), 0 },
	{ INT_RANGE(p2p_intra_bss, 0, 1), CFG_CHANGED_P2P_INTRA_BSS },
	{ INT(p2p_group_idle), 0 },
	{ INT_RANGE(p2p_go_freq_change_policy, 0, P2P_GO_FREQ_MOVE_MAX), 0 },
	{ INT_RANGE(p2p_passphrase_len, 8, 63),
	  CFG_CHANGED_P2P_PASSPHRASE_LEN },
	{ FUNC(p2p_pref_chan), CFG_CHANGED_P2P_PREF_CHAN },
	{ FUNC(p2p_no_go_freq), CFG_CHANGED_P2P_PREF_CHAN },
	{ INT_RANGE(p2p_add_cli_chan, 0, 1), 0 },
	{ INT_RANGE(p2p_optimize_listen_chan, 0, 1), 0 },
	{ INT(p2p_go_ht40), 0 },
	{ INT(p2p_go_vht), 0 },
	{ INT(p2p_disabled), 0 },
	{ INT_RANGE(p2p_go_ctwindow, 0, 127), 0 },
	{ INT(p2p_no_group_iface), 0 },
	{ INT_RANGE(p2p_ignore_shared_freq, 0, 1), 0 },
	{ IPV4(ip_addr_go), 0 },
	{ IPV4(ip_addr_mask), 0 },
	{ IPV4(ip_addr_start), 0 },
	{ IPV4(ip_addr_end), 0 },
	{ INT_RANGE(p2p_cli_probe, 0, 1), 0 },
#endif /* CONFIG_P2P */
	{ FUNC(country), CFG_CHANGED_COUNTRY },
	{ INT(bss_max_count), 0 },
	{ INT(bss_expiration_age), 0 },
	{ INT(bss_expiration_scan_count), 0 },
	{ INT_RANGE(filter_ssids, 0, 1), 0 },
	{ INT_RANGE(filter_rssi, -100, 0), 0 },
	{ INT(max_num_sta), 0 },
	{ INT_RANGE(ap_isolate, 0, 1), 0 },
	{ INT_RANGE(disassoc_low_ack, 0, 1), 0 },
#ifdef CONFIG_HS20
	{ INT_RANGE(hs20, 0, 1), 0 },
#endif /* CONFIG_HS20 */
	{ INT_RANGE(interworking, 0, 1), 0 },
	{ FUNC(hessid), 0 },
	{ INT_RANGE(access_network_type, 0, 15), 0 },
	{ INT_RANGE(go_interworking, 0, 1), 0 },
	{ INT_RANGE(go_access_network_type, 0, 15), 0 },
	{ INT_RANGE(go_internet, 0, 1), 0 },
	{ INT_RANGE(go_venue_group, 0, 255), 0 },
	{ INT_RANGE(go_venue_type, 0, 255), 0 },
	{ INT_RANGE(pbc_in_m1, 0, 1), 0 },
	{ STR(autoscan), 0 },
	{ INT_RANGE(wps_nfc_dev_pw_id, 0x10, 0xffff),
	  CFG_CHANGED_NFC_PASSWORD_TOKEN },
	{ BIN(wps_nfc_dh_pubkey), CFG_CHANGED_NFC_PASSWORD_TOKEN },
	{ BIN(wps_nfc_dh_privkey), CFG_CHANGED_NFC_PASSWORD_TOKEN },
	{ BIN(wps_nfc_dev_pw), CFG_CHANGED_NFC_PASSWORD_TOKEN },
	{ STR(ext_password_backend), CFG_CHANGED_EXT_PW_BACKEND },
	{ INT(p2p_go_max_inactivity), 0 },
	{ INT_RANGE(auto_interworking, 0, 1), 0 },
	{ INT(okc), 0 },
	{ INT(pmf), 0 },
	{ FUNC(sae_groups), 0 },
	{ INT(dtim_period), 0 },
	{ INT(beacon_int), 0 },
	{ FUNC(ap_vendor_elements), 0 },
	{ INT_RANGE(ignore_old_scan_res, 0, 1), 0 },
	{ FUNC(freq_list), 0 },
	{ INT(scan_cur_freq), 0 },
	{ INT(sched_scan_interval), 0 },
	{ INT(sched_scan_start_delay), 0 },
	{ INT(tdls_external_control), 0},
	{ STR(osu_dir), 0 },
	{ STR(wowlan_triggers), CFG_CHANGED_WOWLAN_TRIGGERS },
	{ INT(p2p_search_delay), 0},
	{ INT(mac_addr), 0 },
	{ INT(rand_addr_lifetime), 0 },
	{ INT(preassoc_mac_addr), 0 },
	{ INT(key_mgmt_offload), 0},
	{ INT(passive_scan), 0 },
	{ INT(reassoc_same_bss_optim), 0 },
	{ INT(wps_priority), 0},
#ifdef CONFIG_FST
	{ STR_RANGE(fst_group_id, 1, FST_MAX_GROUP_ID_LEN), 0 },
	{ INT_RANGE(fst_priority, 1, FST_MAX_PRIO_VALUE), 0 },
	{ INT_RANGE(fst_llt, 1, FST_MAX_LLT_MS), 0 },
#endif /* CONFIG_FST */
	{ INT_RANGE(cert_in_cb, 0, 1), 0 },
	{ INT_RANGE(wpa_rsc_relaxation, 0, 1), 0 },
	{ STR(sched_scan_plans), CFG_CHANGED_SCHED_SCAN_PLANS },
#ifdef CONFIG_MBO
	{ STR(non_pref_chan), 0 },
	{ INT_RANGE(mbo_cell_capa, MBO_CELL_CAPA_AVAILABLE,
		    MBO_CELL_CAPA_NOT_SUPPORTED), 0 },
	{ INT_RANGE(disassoc_imminent_rssi_threshold, -120, 0), 0 },
	{ INT_RANGE(oce, 0, 3), 0 },
#endif /* CONFIG_MBO */
	{ INT(gas_address3), 0 },
	{ INT_RANGE(ftm_responder, 0, 1), 0 },
	{ INT_RANGE(ftm_initiator, 0, 1), 0 },
	{ INT(gas_rand_addr_lifetime), 0 },
	{ INT_RANGE(gas_rand_mac_addr, 0, 2), 0 },
	{ INT_RANGE(dpp_config_processing, 0, 2), 0 },
	{ INT_RANGE(coloc_intf_reporting, 0, 1), 0 },
};

#undef FUNC
#undef _INT
#undef INT
#undef INT_RANGE
#undef _STR
#undef STR
#undef STR_RANGE
#undef BIN
#undef IPV4
#define NUM_GLOBAL_FIELDS ARRAY_SIZE(global_fields)


int wpa_config_dump_values(struct wpa_config *config, char *buf, size_t buflen)
{
	int result = 0;
	size_t i;

	for (i = 0; i < NUM_GLOBAL_FIELDS; i++) {
		const struct global_parse_data *field = &global_fields[i];
		int tmp;

		if (!field->get)
			continue;

		tmp = field->get(field->name, config, (long) field->param1,
				 buf, buflen, 1);
		if (tmp < 0)
			return -1;
		buf += tmp;
		buflen -= tmp;
		result += tmp;
	}
	return result;
}


int wpa_config_get_value(const char *name, struct wpa_config *config,
			 char *buf, size_t buflen)
{
	size_t i;

	for (i = 0; i < NUM_GLOBAL_FIELDS; i++) {
		const struct global_parse_data *field = &global_fields[i];

		if (os_strcmp(name, field->name) != 0)
			continue;
		if (!field->get)
			break;
		return field->get(name, config, (long) field->param1,
				  buf, buflen, 0);
	}

	return -1;
}


int wpa_config_get_num_global_field_names(void)
{
	return NUM_GLOBAL_FIELDS;
}


const char * wpa_config_get_global_field_name(unsigned int i, int *no_var)
{
	if (i >= NUM_GLOBAL_FIELDS)
		return NULL;

	if (no_var)
		*no_var = !global_fields[i].param1;
	return global_fields[i].name;
}


int wpa_config_process_global(struct wpa_config *config, char *pos, int line)
{
	size_t i;
	int ret = 0;

	for (i = 0; i < NUM_GLOBAL_FIELDS; i++) {
		const struct global_parse_data *field = &global_fields[i];
		size_t flen = os_strlen(field->name);
		if (os_strncmp(pos, field->name, flen) != 0 ||
		    pos[flen] != '=')
			continue;

		if (field->parser(field, config, line, pos + flen + 1)) {
			wpa_printf(MSG_ERROR, "Line %d: failed to "
				   "parse '%s'.", line, pos);
			ret = -1;
		}
		if (field->changed_flag == CFG_CHANGED_NFC_PASSWORD_TOKEN)
			config->wps_nfc_pw_from_config = 1;
		config->changed_parameters |= field->changed_flag;
		break;
	}
	if (i == NUM_GLOBAL_FIELDS) {
#ifdef CONFIG_AP
		if (os_strncmp(pos, "wmm_ac_", 7) == 0) {
			char *tmp = os_strchr(pos, '=');
			if (tmp == NULL) {
				if (line < 0)
					return -1;
				wpa_printf(MSG_ERROR, "Line %d: invalid line "
					   "'%s'", line, pos);
				return -1;
			}
			*tmp++ = '\0';
			if (hostapd_config_wmm_ac(config->wmm_ac_params, pos,
						  tmp)) {
				wpa_printf(MSG_ERROR, "Line %d: invalid WMM "
					   "AC item", line);
				return -1;
			}
		}
#endif /* CONFIG_AP */
		if (line < 0)
			return -1;
		wpa_printf(MSG_ERROR, "Line %d: unknown global field '%s'.",
			   line, pos);
		ret = -1;
	}

	return ret;
}
