/*
 * WPA Supplicant / Configuration backend: text file
 * Copyright (c) 2003-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * This file implements a configuration backend for text files. All the
 * configuration information is stored in a text file that uses a format
 * described in the sample configuration file, wpa_supplicant.conf.
 */

#include "includes.h"
#ifdef ANDROID
#include <sys/stat.h>
#endif /* ANDROID */

#include "common.h"
#include "config.h"
#include "base64.h"
#include "uuid.h"
#include "common/ieee802_1x_defs.h"
#include "p2p/p2p.h"
#include "eap_peer/eap_methods.h"
#include "eap_peer/eap.h"


static int newline_terminated(const char *buf, size_t buflen)
{
	size_t len = os_strlen(buf);
	if (len == 0)
		return 0;
	if (len == buflen - 1 && buf[buflen - 1] != '\r' &&
	    buf[len - 1] != '\n')
		return 0;
	return 1;
}


static void skip_line_end(FILE *stream)
{
	char buf[100];
	while (fgets(buf, sizeof(buf), stream)) {
		buf[sizeof(buf) - 1] = '\0';
		if (newline_terminated(buf, sizeof(buf)))
			return;
	}
}


/**
 * wpa_config_get_line - Read the next configuration file line
 * @s: Buffer for the line
 * @size: The buffer length
 * @stream: File stream to read from
 * @line: Pointer to a variable storing the file line number
 * @_pos: Buffer for the pointer to the beginning of data on the text line or
 * %NULL if not needed (returned value used instead)
 * Returns: Pointer to the beginning of data on the text line or %NULL if no
 * more text lines are available.
 *
 * This function reads the next non-empty line from the configuration file and
 * removes comments. The returned string is guaranteed to be null-terminated.
 */
static char * wpa_config_get_line(char *s, int size, FILE *stream, int *line,
				  char **_pos)
{
	char *pos, *end, *sstart;

	while (fgets(s, size, stream)) {
		(*line)++;
		s[size - 1] = '\0';
		if (!newline_terminated(s, size)) {
			/*
			 * The line was truncated - skip rest of it to avoid
			 * confusing error messages.
			 */
			wpa_printf(MSG_INFO, "Long line in configuration file "
				   "truncated");
			skip_line_end(stream);
		}
		pos = s;

		/* Skip white space from the beginning of line. */
		while (*pos == ' ' || *pos == '\t' || *pos == '\r')
			pos++;

		/* Skip comment lines and empty lines */
		if (*pos == '#' || *pos == '\n' || *pos == '\0')
			continue;

		/*
		 * Remove # comments unless they are within a double quoted
		 * string.
		 */
		sstart = os_strchr(pos, '"');
		if (sstart)
			sstart = os_strrchr(sstart + 1, '"');
		if (!sstart)
			sstart = pos;
		end = os_strchr(sstart, '#');
		if (end)
			*end-- = '\0';
		else
			end = pos + os_strlen(pos) - 1;

		/* Remove trailing white space. */
		while (end > pos &&
		       (*end == '\n' || *end == ' ' || *end == '\t' ||
			*end == '\r'))
			*end-- = '\0';

		if (*pos == '\0')
			continue;

		if (_pos)
			*_pos = pos;
		return pos;
	}

	if (_pos)
		*_pos = NULL;
	return NULL;
}


static int wpa_config_validate_network(struct wpa_ssid *ssid, int line)
{
	int errors = 0;

	if (ssid->passphrase) {
		if (ssid->psk_set) {
			wpa_printf(MSG_ERROR, "Line %d: both PSK and "
				   "passphrase configured.", line);
			errors++;
		}
		wpa_config_update_psk(ssid);
	}

	if (ssid->disabled == 2)
		ssid->p2p_persistent_group = 1;

	if ((ssid->group_cipher & WPA_CIPHER_CCMP) &&
	    !(ssid->pairwise_cipher & (WPA_CIPHER_CCMP | WPA_CIPHER_CCMP_256 |
				       WPA_CIPHER_GCMP | WPA_CIPHER_GCMP_256 |
				       WPA_CIPHER_NONE))) {
		/* Group cipher cannot be stronger than the pairwise cipher. */
		wpa_printf(MSG_DEBUG, "Line %d: removed CCMP from group cipher"
			   " list since it was not allowed for pairwise "
			   "cipher", line);
		ssid->group_cipher &= ~WPA_CIPHER_CCMP;
	}

	if (ssid->mode == WPAS_MODE_MESH &&
	    (ssid->key_mgmt != WPA_KEY_MGMT_NONE &&
	    ssid->key_mgmt != WPA_KEY_MGMT_SAE)) {
		wpa_printf(MSG_ERROR,
			   "Line %d: key_mgmt for mesh network should be open or SAE",
			   line);
		errors++;
	}

	return errors;
}


static struct wpa_ssid * wpa_config_read_network(FILE *f, int *line, int id)
{
	struct wpa_ssid *ssid;
	int errors = 0, end = 0;
	char buf[2000], *pos, *pos2;

	wpa_printf(MSG_MSGDUMP, "Line: %d - start of a new network block",
		   *line);
	ssid = os_zalloc(sizeof(*ssid));
	if (ssid == NULL)
		return NULL;
	dl_list_init(&ssid->psk_list);
	ssid->id = id;

	wpa_config_set_network_defaults(ssid);

	while (wpa_config_get_line(buf, sizeof(buf), f, line, &pos)) {
		if (os_strcmp(pos, "}") == 0) {
			end = 1;
			break;
		}

		pos2 = os_strchr(pos, '=');
		if (pos2 == NULL) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid SSID line "
				   "'%s'.", *line, pos);
			errors++;
			continue;
		}

		*pos2++ = '\0';
		if (*pos2 == '"') {
			if (os_strchr(pos2 + 1, '"') == NULL) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "quotation '%s'.", *line, pos2);
				errors++;
				continue;
			}
		}

		if (wpa_config_set(ssid, pos, pos2, *line) < 0)
			errors++;
	}

	if (!end) {
		wpa_printf(MSG_ERROR, "Line %d: network block was not "
			   "terminated properly.", *line);
		errors++;
	}

	errors += wpa_config_validate_network(ssid, *line);

	if (errors) {
		wpa_config_free_ssid(ssid);
		ssid = NULL;
	}

	return ssid;
}


static struct wpa_cred * wpa_config_read_cred(FILE *f, int *line, int id)
{
	struct wpa_cred *cred;
	int errors = 0, end = 0;
	char buf[256], *pos, *pos2;

	wpa_printf(MSG_MSGDUMP, "Line: %d - start of a new cred block", *line);
	cred = os_zalloc(sizeof(*cred));
	if (cred == NULL)
		return NULL;
	cred->id = id;
	cred->sim_num = DEFAULT_USER_SELECTED_SIM;

	while (wpa_config_get_line(buf, sizeof(buf), f, line, &pos)) {
		if (os_strcmp(pos, "}") == 0) {
			end = 1;
			break;
		}

		pos2 = os_strchr(pos, '=');
		if (pos2 == NULL) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid cred line "
				   "'%s'.", *line, pos);
			errors++;
			continue;
		}

		*pos2++ = '\0';
		if (*pos2 == '"') {
			if (os_strchr(pos2 + 1, '"') == NULL) {
				wpa_printf(MSG_ERROR, "Line %d: invalid "
					   "quotation '%s'.", *line, pos2);
				errors++;
				continue;
			}
		}

		if (wpa_config_set_cred(cred, pos, pos2, *line) < 0)
			errors++;
	}

	if (!end) {
		wpa_printf(MSG_ERROR, "Line %d: cred block was not "
			   "terminated properly.", *line);
		errors++;
	}

	if (errors) {
		wpa_config_free_cred(cred);
		cred = NULL;
	}

	return cred;
}


#ifndef CONFIG_NO_CONFIG_BLOBS
static struct wpa_config_blob * wpa_config_read_blob(FILE *f, int *line,
						     const char *name)
{
	struct wpa_config_blob *blob;
	char buf[256], *pos;
	unsigned char *encoded = NULL, *nencoded;
	int end = 0;
	size_t encoded_len = 0, len;

	wpa_printf(MSG_MSGDUMP, "Line: %d - start of a new named blob '%s'",
		   *line, name);

	while (wpa_config_get_line(buf, sizeof(buf), f, line, &pos)) {
		if (os_strcmp(pos, "}") == 0) {
			end = 1;
			break;
		}

		len = os_strlen(pos);
		nencoded = os_realloc(encoded, encoded_len + len);
		if (nencoded == NULL) {
			wpa_printf(MSG_ERROR, "Line %d: not enough memory for "
				   "blob", *line);
			os_free(encoded);
			return NULL;
		}
		encoded = nencoded;
		os_memcpy(encoded + encoded_len, pos, len);
		encoded_len += len;
	}

	if (!end || !encoded) {
		wpa_printf(MSG_ERROR, "Line %d: blob was not terminated "
			   "properly", *line);
		os_free(encoded);
		return NULL;
	}

	blob = os_zalloc(sizeof(*blob));
	if (blob == NULL) {
		os_free(encoded);
		return NULL;
	}
	blob->name = os_strdup(name);
	blob->data = base64_decode(encoded, encoded_len, &blob->len);
	os_free(encoded);

	if (blob->name == NULL || blob->data == NULL) {
		wpa_config_free_blob(blob);
		return NULL;
	}

	return blob;
}


static int wpa_config_process_blob(struct wpa_config *config, FILE *f,
				   int *line, char *bname)
{
	char *name_end;
	struct wpa_config_blob *blob;

	name_end = os_strchr(bname, '=');
	if (name_end == NULL) {
		wpa_printf(MSG_ERROR, "Line %d: no blob name terminator",
			   *line);
		return -1;
	}
	*name_end = '\0';

	blob = wpa_config_read_blob(f, line, bname);
	if (blob == NULL) {
		wpa_printf(MSG_ERROR, "Line %d: failed to read blob %s",
			   *line, bname);
		return -1;
	}
	wpa_config_set_blob(config, blob);
	return 0;
}
#endif /* CONFIG_NO_CONFIG_BLOBS */


struct wpa_config * wpa_config_read(const char *name, struct wpa_config *cfgp)
{
	FILE *f;
	char buf[512], *pos;
	int errors = 0, line = 0;
	struct wpa_ssid *ssid, *tail, *head;
	struct wpa_cred *cred, *cred_tail, *cred_head;
	struct wpa_config *config;
	int id = 0;
	int cred_id = 0;

	if (name == NULL)
		return NULL;
	if (cfgp)
		config = cfgp;
	else
		config = wpa_config_alloc_empty(NULL, NULL);
	if (config == NULL) {
		wpa_printf(MSG_ERROR, "Failed to allocate config file "
			   "structure");
		return NULL;
	}
	tail = head = config->ssid;
	while (tail && tail->next)
		tail = tail->next;
	cred_tail = cred_head = config->cred;
	while (cred_tail && cred_tail->next)
		cred_tail = cred_tail->next;

	wpa_printf(MSG_DEBUG, "Reading configuration file '%s'", name);
	f = fopen(name, "r");
	if (f == NULL) {
		wpa_printf(MSG_ERROR, "Failed to open config file '%s', "
			   "error: %s", name, strerror(errno));
		if (config != cfgp)
			os_free(config);
		return NULL;
	}

	while (wpa_config_get_line(buf, sizeof(buf), f, &line, &pos)) {
		if (os_strcmp(pos, "network={") == 0) {
			ssid = wpa_config_read_network(f, &line, id++);
			if (ssid == NULL) {
				wpa_printf(MSG_ERROR, "Line %d: failed to "
					   "parse network block.", line);
				errors++;
				continue;
			}
			if (head == NULL) {
				head = tail = ssid;
			} else {
				tail->next = ssid;
				tail = ssid;
			}
			if (wpa_config_add_prio_network(config, ssid)) {
				wpa_printf(MSG_ERROR, "Line %d: failed to add "
					   "network block to priority list.",
					   line);
				errors++;
				continue;
			}
		} else if (os_strcmp(pos, "cred={") == 0) {
			cred = wpa_config_read_cred(f, &line, cred_id++);
			if (cred == NULL) {
				wpa_printf(MSG_ERROR, "Line %d: failed to "
					   "parse cred block.", line);
				errors++;
				continue;
			}
			if (cred_head == NULL) {
				cred_head = cred_tail = cred;
			} else {
				cred_tail->next = cred;
				cred_tail = cred;
			}
#ifndef CONFIG_NO_CONFIG_BLOBS
		} else if (os_strncmp(pos, "blob-base64-", 12) == 0) {
			if (wpa_config_process_blob(config, f, &line, pos + 12)
			    < 0) {
				wpa_printf(MSG_ERROR, "Line %d: failed to "
					   "process blob.", line);
				errors++;
				continue;
			}
#endif /* CONFIG_NO_CONFIG_BLOBS */
		} else if (wpa_config_process_global(config, pos, line) < 0) {
			wpa_printf(MSG_ERROR, "Line %d: Invalid configuration "
				   "line '%s'.", line, pos);
			errors++;
			continue;
		}
	}

	fclose(f);

	config->ssid = head;
	wpa_config_debug_dump_networks(config);
	config->cred = cred_head;

#ifndef WPA_IGNORE_CONFIG_ERRORS
	if (errors) {
		if (config != cfgp)
			wpa_config_free(config);
		config = NULL;
		head = NULL;
	}
#endif /* WPA_IGNORE_CONFIG_ERRORS */

	return config;
}


#ifndef CONFIG_NO_CONFIG_WRITE

static void write_str(FILE *f, const char *field, struct wpa_ssid *ssid)
{
	char *value = wpa_config_get(ssid, field);
	if (value == NULL)
		return;
	fprintf(f, "\t%s=%s\n", field, value);
	os_free(value);
}


static void write_int(FILE *f, const char *field, int value, int def)
{
	if (value == def)
		return;
	fprintf(f, "\t%s=%d\n", field, value);
}


static void write_bssid(FILE *f, struct wpa_ssid *ssid)
{
	char *value = wpa_config_get(ssid, "bssid");
	if (value == NULL)
		return;
	fprintf(f, "\tbssid=%s\n", value);
	os_free(value);
}


static void write_bssid_hint(FILE *f, struct wpa_ssid *ssid)
{
	char *value = wpa_config_get(ssid, "bssid_hint");

	if (!value)
		return;
	fprintf(f, "\tbssid_hint=%s\n", value);
	os_free(value);
}


static void write_psk(FILE *f, struct wpa_ssid *ssid)
{
	char *value;

	if (ssid->mem_only_psk)
		return;

	value = wpa_config_get(ssid, "psk");
	if (value == NULL)
		return;
	fprintf(f, "\tpsk=%s\n", value);
	os_free(value);
}


static void write_proto(FILE *f, struct wpa_ssid *ssid)
{
	char *value;

	if (ssid->proto == DEFAULT_PROTO)
		return;

	value = wpa_config_get(ssid, "proto");
	if (value == NULL)
		return;
	if (value[0])
		fprintf(f, "\tproto=%s\n", value);
	os_free(value);
}


static void write_key_mgmt(FILE *f, struct wpa_ssid *ssid)
{
	char *value;

	if (ssid->key_mgmt == DEFAULT_KEY_MGMT)
		return;

	value = wpa_config_get(ssid, "key_mgmt");
	if (value == NULL)
		return;
	if (value[0])
		fprintf(f, "\tkey_mgmt=%s\n", value);
	os_free(value);
}


static void write_pairwise(FILE *f, struct wpa_ssid *ssid)
{
	char *value;

	if (ssid->pairwise_cipher == DEFAULT_PAIRWISE)
		return;

	value = wpa_config_get(ssid, "pairwise");
	if (value == NULL)
		return;
	if (value[0])
		fprintf(f, "\tpairwise=%s\n", value);
	os_free(value);
}


static void write_group(FILE *f, struct wpa_ssid *ssid)
{
	char *value;

	if (ssid->group_cipher == DEFAULT_GROUP)
		return;

	value = wpa_config_get(ssid, "group");
	if (value == NULL)
		return;
	if (value[0])
		fprintf(f, "\tgroup=%s\n", value);
	os_free(value);
}


static void write_group_mgmt(FILE *f, struct wpa_ssid *ssid)
{
	char *value;

	if (!ssid->group_mgmt_cipher)
		return;

	value = wpa_config_get(ssid, "group_mgmt");
	if (!value)
		return;
	if (value[0])
		fprintf(f, "\tgroup_mgmt=%s\n", value);
	os_free(value);
}


static void write_auth_alg(FILE *f, struct wpa_ssid *ssid)
{
	char *value;

	if (ssid->auth_alg == 0)
		return;

	value = wpa_config_get(ssid, "auth_alg");
	if (value == NULL)
		return;
	if (value[0])
		fprintf(f, "\tauth_alg=%s\n", value);
	os_free(value);
}


#ifdef IEEE8021X_EAPOL
static void write_eap(FILE *f, struct wpa_ssid *ssid)
{
	char *value;

	value = wpa_config_get(ssid, "eap");
	if (value == NULL)
		return;

	if (value[0])
		fprintf(f, "\teap=%s\n", value);
	os_free(value);
}
#endif /* IEEE8021X_EAPOL */


static void write_wep_key(FILE *f, int idx, struct wpa_ssid *ssid)
{
	char field[20], *value;
	int res;

	res = os_snprintf(field, sizeof(field), "wep_key%d", idx);
	if (os_snprintf_error(sizeof(field), res))
		return;
	value = wpa_config_get(ssid, field);
	if (value) {
		fprintf(f, "\t%s=%s\n", field, value);
		os_free(value);
	}
}


#ifdef CONFIG_P2P

static void write_go_p2p_dev_addr(FILE *f, struct wpa_ssid *ssid)
{
	char *value = wpa_config_get(ssid, "go_p2p_dev_addr");
	if (value == NULL)
		return;
	fprintf(f, "\tgo_p2p_dev_addr=%s\n", value);
	os_free(value);
}

static void write_p2p_client_list(FILE *f, struct wpa_ssid *ssid)
{
	char *value = wpa_config_get(ssid, "p2p_client_list");
	if (value == NULL)
		return;
	fprintf(f, "\tp2p_client_list=%s\n", value);
	os_free(value);
}


static void write_psk_list(FILE *f, struct wpa_ssid *ssid)
{
	struct psk_list_entry *psk;
	char hex[32 * 2 + 1];

	dl_list_for_each(psk, &ssid->psk_list, struct psk_list_entry, list) {
		wpa_snprintf_hex(hex, sizeof(hex), psk->psk, sizeof(psk->psk));
		fprintf(f, "\tpsk_list=%s" MACSTR "-%s\n",
			psk->p2p ? "P2P-" : "", MAC2STR(psk->addr), hex);
	}
}

#endif /* CONFIG_P2P */


#ifdef CONFIG_MACSEC

static void write_mka_cak(FILE *f, struct wpa_ssid *ssid)
{
	char *value;

	if (!(ssid->mka_psk_set & MKA_PSK_SET_CAK))
		return;

	value = wpa_config_get(ssid, "mka_cak");
	if (!value)
		return;
	fprintf(f, "\tmka_cak=%s\n", value);
	os_free(value);
}


static void write_mka_ckn(FILE *f, struct wpa_ssid *ssid)
{
	char *value;

	if (!(ssid->mka_psk_set & MKA_PSK_SET_CKN))
		return;

	value = wpa_config_get(ssid, "mka_ckn");
	if (!value)
		return;
	fprintf(f, "\tmka_ckn=%s\n", value);
	os_free(value);
}

#endif /* CONFIG_MACSEC */


static void wpa_config_write_network(FILE *f, struct wpa_ssid *ssid)
{
	int i;

#define STR(t) write_str(f, #t, ssid)
#define INT(t) write_int(f, #t, ssid->t, 0)
#define INTe(t) write_int(f, #t, ssid->eap.t, 0)
#define INT_DEF(t, def) write_int(f, #t, ssid->t, def)
#define INT_DEFe(t, def) write_int(f, #t, ssid->eap.t, def)

	STR(ssid);
	INT(scan_ssid);
	write_bssid(f, ssid);
	write_bssid_hint(f, ssid);
	write_str(f, "bssid_blacklist", ssid);
	write_str(f, "bssid_whitelist", ssid);
	write_psk(f, ssid);
	INT(mem_only_psk);
	STR(sae_password);
	STR(sae_password_id);
	write_proto(f, ssid);
	write_key_mgmt(f, ssid);
	INT_DEF(bg_scan_period, DEFAULT_BG_SCAN_PERIOD);
	write_pairwise(f, ssid);
	write_group(f, ssid);
	write_group_mgmt(f, ssid);
	write_auth_alg(f, ssid);
	STR(bgscan);
	STR(autoscan);
	STR(scan_freq);
#ifdef IEEE8021X_EAPOL
	write_eap(f, ssid);
	STR(identity);
	STR(anonymous_identity);
	STR(imsi_identity);
	STR(password);
	STR(ca_cert);
	STR(ca_path);
	STR(client_cert);
	STR(private_key);
	STR(private_key_passwd);
	STR(dh_file);
	STR(subject_match);
	STR(altsubject_match);
	STR(domain_suffix_match);
	STR(domain_match);
	STR(ca_cert2);
	STR(ca_path2);
	STR(client_cert2);
	STR(private_key2);
	STR(private_key2_passwd);
	STR(dh_file2);
	STR(subject_match2);
	STR(altsubject_match2);
	STR(domain_suffix_match2);
	STR(domain_match2);
	STR(phase1);
	STR(phase2);
	STR(pcsc);
	STR(pin);
	STR(engine_id);
	STR(key_id);
	STR(cert_id);
	STR(ca_cert_id);
	STR(key2_id);
	STR(pin2);
	STR(engine2_id);
	STR(cert2_id);
	STR(ca_cert2_id);
	INTe(engine);
	INTe(engine2);
	INT_DEF(eapol_flags, DEFAULT_EAPOL_FLAGS);
	STR(openssl_ciphers);
	INTe(erp);
#endif /* IEEE8021X_EAPOL */
	for (i = 0; i < 4; i++)
		write_wep_key(f, i, ssid);
	INT(wep_tx_keyidx);
	INT(priority);
#ifdef IEEE8021X_EAPOL
	INT_DEF(eap_workaround, DEFAULT_EAP_WORKAROUND);
	STR(pac_file);
	INT_DEFe(fragment_size, DEFAULT_FRAGMENT_SIZE);
	INTe(ocsp);
	INT_DEFe(sim_num, DEFAULT_USER_SELECTED_SIM);
#endif /* IEEE8021X_EAPOL */
	INT(mode);
	INT(no_auto_peer);
	INT(frequency);
	INT(fixed_freq);
#ifdef CONFIG_ACS
	INT(acs);
#endif /* CONFIG_ACS */
	write_int(f, "proactive_key_caching", ssid->proactive_key_caching, -1);
	INT(disabled);
	INT(mixed_cell);
	INT(vht);
	INT_DEF(ht, 1);
	INT(ht40);
	INT(max_oper_chwidth);
	INT(vht_center_freq1);
	INT(vht_center_freq2);
	INT(pbss);
	INT(wps_disabled);
	INT(fils_dh_group);
#ifdef CONFIG_IEEE80211W
	write_int(f, "ieee80211w", ssid->ieee80211w,
		  MGMT_FRAME_PROTECTION_DEFAULT);
#endif /* CONFIG_IEEE80211W */
	STR(id_str);
#ifdef CONFIG_P2P
	write_go_p2p_dev_addr(f, ssid);
	write_p2p_client_list(f, ssid);
	write_psk_list(f, ssid);
#endif /* CONFIG_P2P */
	INT(ap_max_inactivity);
	INT(dtim_period);
	INT(beacon_int);
#ifdef CONFIG_MACSEC
	INT(macsec_policy);
	write_mka_cak(f, ssid);
	write_mka_ckn(f, ssid);
	INT(macsec_integ_only);
	INT(macsec_port);
	INT_DEF(mka_priority, DEFAULT_PRIO_NOT_KEY_SERVER);
#endif /* CONFIG_MACSEC */
#ifdef CONFIG_HS20
	INT(update_identifier);
	STR(roaming_consortium_selection);
#endif /* CONFIG_HS20 */
	write_int(f, "mac_addr", ssid->mac_addr, -1);
#ifdef CONFIG_MESH
	STR(mesh_basic_rates);
	INT_DEF(dot11MeshMaxRetries, DEFAULT_MESH_MAX_RETRIES);
	INT_DEF(dot11MeshRetryTimeout, DEFAULT_MESH_RETRY_TIMEOUT);
	INT_DEF(dot11MeshConfirmTimeout, DEFAULT_MESH_CONFIRM_TIMEOUT);
	INT_DEF(dot11MeshHoldingTimeout, DEFAULT_MESH_HOLDING_TIMEOUT);
	INT_DEF(mesh_rssi_threshold, DEFAULT_MESH_RSSI_THRESHOLD);
#endif /* CONFIG_MESH */
	INT(wpa_ptk_rekey);
	INT(group_rekey);
	INT(ignore_broadcast_ssid);
#ifdef CONFIG_DPP
	STR(dpp_connector);
	STR(dpp_netaccesskey);
	INT(dpp_netaccesskey_expiry);
	STR(dpp_csign);
#endif /* CONFIG_DPP */
	INT(owe_group);
	INT(owe_only);
#ifdef CONFIG_HT_OVERRIDES
	INT_DEF(disable_ht, DEFAULT_DISABLE_HT);
	INT_DEF(disable_ht40, DEFAULT_DISABLE_HT40);
	INT_DEF(disable_sgi, DEFAULT_DISABLE_SGI);
	INT_DEF(disable_ldpc, DEFAULT_DISABLE_LDPC);
	INT(ht40_intolerant);
	INT_DEF(disable_max_amsdu, DEFAULT_DISABLE_MAX_AMSDU);
	INT_DEF(ampdu_factor, DEFAULT_AMPDU_FACTOR);
	INT_DEF(ampdu_density, DEFAULT_AMPDU_DENSITY);
	STR(ht_mcs);
#endif /* CONFIG_HT_OVERRIDES */
#ifdef CONFIG_VHT_OVERRIDES
	INT(disable_vht);
	INT(vht_capa);
	INT(vht_capa_mask);
	INT_DEF(vht_rx_mcs_nss_1, -1);
	INT_DEF(vht_rx_mcs_nss_2, -1);
	INT_DEF(vht_rx_mcs_nss_3, -1);
	INT_DEF(vht_rx_mcs_nss_4, -1);
	INT_DEF(vht_rx_mcs_nss_5, -1);
	INT_DEF(vht_rx_mcs_nss_6, -1);
	INT_DEF(vht_rx_mcs_nss_7, -1);
	INT_DEF(vht_rx_mcs_nss_8, -1);
	INT_DEF(vht_tx_mcs_nss_1, -1);
	INT_DEF(vht_tx_mcs_nss_2, -1);
	INT_DEF(vht_tx_mcs_nss_3, -1);
	INT_DEF(vht_tx_mcs_nss_4, -1);
	INT_DEF(vht_tx_mcs_nss_5, -1);
	INT_DEF(vht_tx_mcs_nss_6, -1);
	INT_DEF(vht_tx_mcs_nss_7, -1);
	INT_DEF(vht_tx_mcs_nss_8, -1);
#endif /* CONFIG_VHT_OVERRIDES */

#undef STR
#undef INT
#undef INT_DEF
}


static void wpa_config_write_cred(FILE *f, struct wpa_cred *cred)
{
	size_t i;

	if (cred->priority)
		fprintf(f, "\tpriority=%d\n", cred->priority);
	if (cred->pcsc)
		fprintf(f, "\tpcsc=%d\n", cred->pcsc);
	if (cred->realm)
		fprintf(f, "\trealm=\"%s\"\n", cred->realm);
	if (cred->username)
		fprintf(f, "\tusername=\"%s\"\n", cred->username);
	if (cred->password && cred->ext_password)
		fprintf(f, "\tpassword=ext:%s\n", cred->password);
	else if (cred->password)
		fprintf(f, "\tpassword=\"%s\"\n", cred->password);
	if (cred->ca_cert)
		fprintf(f, "\tca_cert=\"%s\"\n", cred->ca_cert);
	if (cred->client_cert)
		fprintf(f, "\tclient_cert=\"%s\"\n", cred->client_cert);
	if (cred->private_key)
		fprintf(f, "\tprivate_key=\"%s\"\n", cred->private_key);
	if (cred->private_key_passwd)
		fprintf(f, "\tprivate_key_passwd=\"%s\"\n",
			cred->private_key_passwd);
	if (cred->imsi)
		fprintf(f, "\timsi=\"%s\"\n", cred->imsi);
	if (cred->milenage)
		fprintf(f, "\tmilenage=\"%s\"\n", cred->milenage);
	for (i = 0; i < cred->num_domain; i++)
		fprintf(f, "\tdomain=\"%s\"\n", cred->domain[i]);
	if (cred->domain_suffix_match)
		fprintf(f, "\tdomain_suffix_match=\"%s\"\n",
			cred->domain_suffix_match);
	if (cred->roaming_consortium_len) {
		fprintf(f, "\troaming_consortium=");
		for (i = 0; i < cred->roaming_consortium_len; i++)
			fprintf(f, "%02x", cred->roaming_consortium[i]);
		fprintf(f, "\n");
	}
	if (cred->eap_method) {
		const char *name;
		name = eap_get_name(cred->eap_method[0].vendor,
				    cred->eap_method[0].method);
		if (name)
			fprintf(f, "\teap=%s\n", name);
	}
	if (cred->phase1)
		fprintf(f, "\tphase1=\"%s\"\n", cred->phase1);
	if (cred->phase2)
		fprintf(f, "\tphase2=\"%s\"\n", cred->phase2);
	if (cred->excluded_ssid) {
		size_t j;
		for (i = 0; i < cred->num_excluded_ssid; i++) {
			struct excluded_ssid *e = &cred->excluded_ssid[i];
			fprintf(f, "\texcluded_ssid=");
			for (j = 0; j < e->ssid_len; j++)
				fprintf(f, "%02x", e->ssid[j]);
			fprintf(f, "\n");
		}
	}
	if (cred->roaming_partner) {
		for (i = 0; i < cred->num_roaming_partner; i++) {
			struct roaming_partner *p = &cred->roaming_partner[i];
			fprintf(f, "\troaming_partner=\"%s,%d,%u,%s\"\n",
				p->fqdn, p->exact_match, p->priority,
				p->country);
		}
	}
	if (cred->update_identifier)
		fprintf(f, "\tupdate_identifier=%d\n", cred->update_identifier);

	if (cred->provisioning_sp)
		fprintf(f, "\tprovisioning_sp=\"%s\"\n", cred->provisioning_sp);
	if (cred->sp_priority)
		fprintf(f, "\tsp_priority=%d\n", cred->sp_priority);

	if (cred->min_dl_bandwidth_home)
		fprintf(f, "\tmin_dl_bandwidth_home=%u\n",
			cred->min_dl_bandwidth_home);
	if (cred->min_ul_bandwidth_home)
		fprintf(f, "\tmin_ul_bandwidth_home=%u\n",
			cred->min_ul_bandwidth_home);
	if (cred->min_dl_bandwidth_roaming)
		fprintf(f, "\tmin_dl_bandwidth_roaming=%u\n",
			cred->min_dl_bandwidth_roaming);
	if (cred->min_ul_bandwidth_roaming)
		fprintf(f, "\tmin_ul_bandwidth_roaming=%u\n",
			cred->min_ul_bandwidth_roaming);

	if (cred->max_bss_load)
		fprintf(f, "\tmax_bss_load=%u\n",
			cred->max_bss_load);

	if (cred->ocsp)
		fprintf(f, "\tocsp=%d\n", cred->ocsp);

	if (cred->num_req_conn_capab) {
		for (i = 0; i < cred->num_req_conn_capab; i++) {
			int *ports;

			fprintf(f, "\treq_conn_capab=%u",
				cred->req_conn_capab_proto[i]);
			ports = cred->req_conn_capab_port[i];
			if (ports) {
				int j;
				for (j = 0; ports[j] != -1; j++) {
					fprintf(f, "%s%d", j > 0 ? "," : ":",
						ports[j]);
				}
			}
			fprintf(f, "\n");
		}
	}

	if (cred->required_roaming_consortium_len) {
		fprintf(f, "\trequired_roaming_consortium=");
		for (i = 0; i < cred->required_roaming_consortium_len; i++)
			fprintf(f, "%02x",
				cred->required_roaming_consortium[i]);
		fprintf(f, "\n");
	}

	if (cred->num_roaming_consortiums) {
		size_t j;

		fprintf(f, "\troaming_consortiums=\"");
		for (i = 0; i < cred->num_roaming_consortiums; i++) {
			if (i > 0)
				fprintf(f, ",");
			for (j = 0; j < cred->roaming_consortiums_len[i]; j++)
				fprintf(f, "%02x",
					cred->roaming_consortiums[i][j]);
		}
		fprintf(f, "\"\n");
	}

	if (cred->sim_num != DEFAULT_USER_SELECTED_SIM)
		fprintf(f, "\tsim_num=%d\n", cred->sim_num);
}


#ifndef CONFIG_NO_CONFIG_BLOBS
static int wpa_config_write_blob(FILE *f, struct wpa_config_blob *blob)
{
	unsigned char *encoded;

	encoded = base64_encode(blob->data, blob->len, NULL);
	if (encoded == NULL)
		return -1;

	fprintf(f, "\nblob-base64-%s={\n%s}\n", blob->name, encoded);
	os_free(encoded);
	return 0;
}
#endif /* CONFIG_NO_CONFIG_BLOBS */


static void write_global_bin(FILE *f, const char *field,
			     const struct wpabuf *val)
{
	size_t i;
	const u8 *pos;

	if (val == NULL)
		return;

	fprintf(f, "%s=", field);
	pos = wpabuf_head(val);
	for (i = 0; i < wpabuf_len(val); i++)
		fprintf(f, "%02X", *pos++);
	fprintf(f, "\n");
}


static void wpa_config_write_global(FILE *f, struct wpa_config *config)
{
#ifdef CONFIG_CTRL_IFACE
	if (config->ctrl_interface)
		fprintf(f, "ctrl_interface=%s\n", config->ctrl_interface);
	if (config->ctrl_interface_group)
		fprintf(f, "ctrl_interface_group=%s\n",
			config->ctrl_interface_group);
#endif /* CONFIG_CTRL_IFACE */
	if (config->eapol_version != DEFAULT_EAPOL_VERSION)
		fprintf(f, "eapol_version=%d\n", config->eapol_version);
	if (config->ap_scan != DEFAULT_AP_SCAN)
		fprintf(f, "ap_scan=%d\n", config->ap_scan);
	if (config->disable_scan_offload)
		fprintf(f, "disable_scan_offload=%d\n",
			config->disable_scan_offload);
	if (config->fast_reauth != DEFAULT_FAST_REAUTH)
		fprintf(f, "fast_reauth=%d\n", config->fast_reauth);
	if (config->opensc_engine_path)
		fprintf(f, "opensc_engine_path=%s\n",
			config->opensc_engine_path);
	if (config->pkcs11_engine_path)
		fprintf(f, "pkcs11_engine_path=%s\n",
			config->pkcs11_engine_path);
	if (config->pkcs11_module_path)
		fprintf(f, "pkcs11_module_path=%s\n",
			config->pkcs11_module_path);
	if (config->openssl_ciphers)
		fprintf(f, "openssl_ciphers=%s\n", config->openssl_ciphers);
	if (config->pcsc_reader)
		fprintf(f, "pcsc_reader=%s\n", config->pcsc_reader);
	if (config->pcsc_pin)
		fprintf(f, "pcsc_pin=%s\n", config->pcsc_pin);
	if (config->driver_param)
		fprintf(f, "driver_param=%s\n", config->driver_param);
	if (config->dot11RSNAConfigPMKLifetime)
		fprintf(f, "dot11RSNAConfigPMKLifetime=%u\n",
			config->dot11RSNAConfigPMKLifetime);
	if (config->dot11RSNAConfigPMKReauthThreshold)
		fprintf(f, "dot11RSNAConfigPMKReauthThreshold=%u\n",
			config->dot11RSNAConfigPMKReauthThreshold);
	if (config->dot11RSNAConfigSATimeout)
		fprintf(f, "dot11RSNAConfigSATimeout=%u\n",
			config->dot11RSNAConfigSATimeout);
	if (config->update_config)
		fprintf(f, "update_config=%d\n", config->update_config);
#ifdef CONFIG_WPS
	if (!is_nil_uuid(config->uuid)) {
		char buf[40];
		uuid_bin2str(config->uuid, buf, sizeof(buf));
		fprintf(f, "uuid=%s\n", buf);
	}
	if (config->auto_uuid)
		fprintf(f, "auto_uuid=%d\n", config->auto_uuid);
	if (config->device_name)
		fprintf(f, "device_name=%s\n", config->device_name);
	if (config->manufacturer)
		fprintf(f, "manufacturer=%s\n", config->manufacturer);
	if (config->model_name)
		fprintf(f, "model_name=%s\n", config->model_name);
	if (config->model_number)
		fprintf(f, "model_number=%s\n", config->model_number);
	if (config->serial_number)
		fprintf(f, "serial_number=%s\n", config->serial_number);
	{
		char _buf[WPS_DEV_TYPE_BUFSIZE], *buf;
		buf = wps_dev_type_bin2str(config->device_type,
					   _buf, sizeof(_buf));
		if (os_strcmp(buf, "0-00000000-0") != 0)
			fprintf(f, "device_type=%s\n", buf);
	}
	if (WPA_GET_BE32(config->os_version))
		fprintf(f, "os_version=%08x\n",
			WPA_GET_BE32(config->os_version));
	if (config->config_methods)
		fprintf(f, "config_methods=%s\n", config->config_methods);
	if (config->wps_cred_processing)
		fprintf(f, "wps_cred_processing=%d\n",
			config->wps_cred_processing);
	if (config->wps_vendor_ext_m1) {
		int i, len = wpabuf_len(config->wps_vendor_ext_m1);
		const u8 *p = wpabuf_head_u8(config->wps_vendor_ext_m1);
		if (len > 0) {
			fprintf(f, "wps_vendor_ext_m1=");
			for (i = 0; i < len; i++)
				fprintf(f, "%02x", *p++);
			fprintf(f, "\n");
		}
	}
#endif /* CONFIG_WPS */
#ifdef CONFIG_P2P
	{
		int i;
		char _buf[WPS_DEV_TYPE_BUFSIZE], *buf;

		for (i = 0; i < config->num_sec_device_types; i++) {
			buf = wps_dev_type_bin2str(config->sec_device_type[i],
						   _buf, sizeof(_buf));
			if (buf)
				fprintf(f, "sec_device_type=%s\n", buf);
		}
	}
	if (config->p2p_listen_reg_class)
		fprintf(f, "p2p_listen_reg_class=%d\n",
			config->p2p_listen_reg_class);
	if (config->p2p_listen_channel)
		fprintf(f, "p2p_listen_channel=%d\n",
			config->p2p_listen_channel);
	if (config->p2p_oper_reg_class)
		fprintf(f, "p2p_oper_reg_class=%d\n",
			config->p2p_oper_reg_class);
	if (config->p2p_oper_channel)
		fprintf(f, "p2p_oper_channel=%d\n", config->p2p_oper_channel);
	if (config->p2p_go_intent != DEFAULT_P2P_GO_INTENT)
		fprintf(f, "p2p_go_intent=%d\n", config->p2p_go_intent);
	if (config->p2p_ssid_postfix)
		fprintf(f, "p2p_ssid_postfix=%s\n", config->p2p_ssid_postfix);
	if (config->persistent_reconnect)
		fprintf(f, "persistent_reconnect=%d\n",
			config->persistent_reconnect);
	if (config->p2p_intra_bss != DEFAULT_P2P_INTRA_BSS)
		fprintf(f, "p2p_intra_bss=%d\n", config->p2p_intra_bss);
	if (config->p2p_group_idle)
		fprintf(f, "p2p_group_idle=%d\n", config->p2p_group_idle);
	if (config->p2p_passphrase_len)
		fprintf(f, "p2p_passphrase_len=%u\n",
			config->p2p_passphrase_len);
	if (config->p2p_pref_chan) {
		unsigned int i;
		fprintf(f, "p2p_pref_chan=");
		for (i = 0; i < config->num_p2p_pref_chan; i++) {
			fprintf(f, "%s%u:%u", i > 0 ? "," : "",
				config->p2p_pref_chan[i].op_class,
				config->p2p_pref_chan[i].chan);
		}
		fprintf(f, "\n");
	}
	if (config->p2p_no_go_freq.num) {
		char *val = freq_range_list_str(&config->p2p_no_go_freq);
		if (val) {
			fprintf(f, "p2p_no_go_freq=%s\n", val);
			os_free(val);
		}
	}
	if (config->p2p_add_cli_chan)
		fprintf(f, "p2p_add_cli_chan=%d\n", config->p2p_add_cli_chan);
	if (config->p2p_optimize_listen_chan !=
	    DEFAULT_P2P_OPTIMIZE_LISTEN_CHAN)
		fprintf(f, "p2p_optimize_listen_chan=%d\n",
			config->p2p_optimize_listen_chan);
	if (config->p2p_go_ht40)
		fprintf(f, "p2p_go_ht40=%d\n", config->p2p_go_ht40);
	if (config->p2p_go_vht)
		fprintf(f, "p2p_go_vht=%d\n", config->p2p_go_vht);
	if (config->p2p_go_ctwindow != DEFAULT_P2P_GO_CTWINDOW)
		fprintf(f, "p2p_go_ctwindow=%d\n", config->p2p_go_ctwindow);
	if (config->p2p_disabled)
		fprintf(f, "p2p_disabled=%d\n", config->p2p_disabled);
	if (config->p2p_no_group_iface)
		fprintf(f, "p2p_no_group_iface=%d\n",
			config->p2p_no_group_iface);
	if (config->p2p_ignore_shared_freq)
		fprintf(f, "p2p_ignore_shared_freq=%d\n",
			config->p2p_ignore_shared_freq);
	if (config->p2p_cli_probe)
		fprintf(f, "p2p_cli_probe=%d\n", config->p2p_cli_probe);
	if (config->p2p_go_freq_change_policy != DEFAULT_P2P_GO_FREQ_MOVE)
		fprintf(f, "p2p_go_freq_change_policy=%u\n",
			config->p2p_go_freq_change_policy);
	if (WPA_GET_BE32(config->ip_addr_go))
		fprintf(f, "ip_addr_go=%u.%u.%u.%u\n",
			config->ip_addr_go[0], config->ip_addr_go[1],
			config->ip_addr_go[2], config->ip_addr_go[3]);
	if (WPA_GET_BE32(config->ip_addr_mask))
		fprintf(f, "ip_addr_mask=%u.%u.%u.%u\n",
			config->ip_addr_mask[0], config->ip_addr_mask[1],
			config->ip_addr_mask[2], config->ip_addr_mask[3]);
	if (WPA_GET_BE32(config->ip_addr_start))
		fprintf(f, "ip_addr_start=%u.%u.%u.%u\n",
			config->ip_addr_start[0], config->ip_addr_start[1],
			config->ip_addr_start[2], config->ip_addr_start[3]);
	if (WPA_GET_BE32(config->ip_addr_end))
		fprintf(f, "ip_addr_end=%u.%u.%u.%u\n",
			config->ip_addr_end[0], config->ip_addr_end[1],
			config->ip_addr_end[2], config->ip_addr_end[3]);
#endif /* CONFIG_P2P */
	if (config->country[0] && config->country[1]) {
		fprintf(f, "country=%c%c\n",
			config->country[0], config->country[1]);
	}
	if (config->bss_max_count != DEFAULT_BSS_MAX_COUNT)
		fprintf(f, "bss_max_count=%u\n", config->bss_max_count);
	if (config->bss_expiration_age != DEFAULT_BSS_EXPIRATION_AGE)
		fprintf(f, "bss_expiration_age=%u\n",
			config->bss_expiration_age);
	if (config->bss_expiration_scan_count !=
	    DEFAULT_BSS_EXPIRATION_SCAN_COUNT)
		fprintf(f, "bss_expiration_scan_count=%u\n",
			config->bss_expiration_scan_count);
	if (config->filter_ssids)
		fprintf(f, "filter_ssids=%d\n", config->filter_ssids);
	if (config->filter_rssi)
		fprintf(f, "filter_rssi=%d\n", config->filter_rssi);
	if (config->max_num_sta != DEFAULT_MAX_NUM_STA)
		fprintf(f, "max_num_sta=%u\n", config->max_num_sta);
	if (config->ap_isolate != DEFAULT_AP_ISOLATE)
		fprintf(f, "ap_isolate=%u\n", config->ap_isolate);
	if (config->disassoc_low_ack)
		fprintf(f, "disassoc_low_ack=%d\n", config->disassoc_low_ack);
#ifdef CONFIG_HS20
	if (config->hs20)
		fprintf(f, "hs20=1\n");
#endif /* CONFIG_HS20 */
#ifdef CONFIG_INTERWORKING
	if (config->interworking)
		fprintf(f, "interworking=%d\n", config->interworking);
	if (!is_zero_ether_addr(config->hessid))
		fprintf(f, "hessid=" MACSTR "\n", MAC2STR(config->hessid));
	if (config->access_network_type != DEFAULT_ACCESS_NETWORK_TYPE)
		fprintf(f, "access_network_type=%d\n",
			config->access_network_type);
	if (config->go_interworking)
		fprintf(f, "go_interworking=%d\n", config->go_interworking);
	if (config->go_access_network_type)
		fprintf(f, "go_access_network_type=%d\n",
			config->go_access_network_type);
	if (config->go_internet)
		fprintf(f, "go_internet=%d\n", config->go_internet);
	if (config->go_venue_group)
		fprintf(f, "go_venue_group=%d\n", config->go_venue_group);
	if (config->go_venue_type)
		fprintf(f, "go_venue_type=%d\n", config->go_venue_type);
#endif /* CONFIG_INTERWORKING */
	if (config->pbc_in_m1)
		fprintf(f, "pbc_in_m1=%d\n", config->pbc_in_m1);
	if (config->wps_nfc_pw_from_config) {
		if (config->wps_nfc_dev_pw_id)
			fprintf(f, "wps_nfc_dev_pw_id=%d\n",
				config->wps_nfc_dev_pw_id);
		write_global_bin(f, "wps_nfc_dh_pubkey",
				 config->wps_nfc_dh_pubkey);
		write_global_bin(f, "wps_nfc_dh_privkey",
				 config->wps_nfc_dh_privkey);
		write_global_bin(f, "wps_nfc_dev_pw", config->wps_nfc_dev_pw);
	}

	if (config->ext_password_backend)
		fprintf(f, "ext_password_backend=%s\n",
			config->ext_password_backend);
	if (config->p2p_go_max_inactivity != DEFAULT_P2P_GO_MAX_INACTIVITY)
		fprintf(f, "p2p_go_max_inactivity=%d\n",
			config->p2p_go_max_inactivity);
	if (config->auto_interworking)
		fprintf(f, "auto_interworking=%d\n",
			config->auto_interworking);
	if (config->okc)
		fprintf(f, "okc=%d\n", config->okc);
	if (config->pmf)
		fprintf(f, "pmf=%d\n", config->pmf);
	if (config->dtim_period)
		fprintf(f, "dtim_period=%d\n", config->dtim_period);
	if (config->beacon_int)
		fprintf(f, "beacon_int=%d\n", config->beacon_int);

	if (config->sae_groups) {
		int i;
		fprintf(f, "sae_groups=");
		for (i = 0; config->sae_groups[i] > 0; i++) {
			fprintf(f, "%s%d", i > 0 ? " " : "",
				config->sae_groups[i]);
		}
		fprintf(f, "\n");
	}

	if (config->ap_vendor_elements) {
		int i, len = wpabuf_len(config->ap_vendor_elements);
		const u8 *p = wpabuf_head_u8(config->ap_vendor_elements);
		if (len > 0) {
			fprintf(f, "ap_vendor_elements=");
			for (i = 0; i < len; i++)
				fprintf(f, "%02x", *p++);
			fprintf(f, "\n");
		}
	}

	if (config->ignore_old_scan_res)
		fprintf(f, "ignore_old_scan_res=%d\n",
			config->ignore_old_scan_res);

	if (config->freq_list && config->freq_list[0]) {
		int i;
		fprintf(f, "freq_list=");
		for (i = 0; config->freq_list[i]; i++) {
			fprintf(f, "%s%d", i > 0 ? " " : "",
				config->freq_list[i]);
		}
		fprintf(f, "\n");
	}
	if (config->scan_cur_freq != DEFAULT_SCAN_CUR_FREQ)
		fprintf(f, "scan_cur_freq=%d\n", config->scan_cur_freq);

	if (config->sched_scan_interval)
		fprintf(f, "sched_scan_interval=%u\n",
			config->sched_scan_interval);

	if (config->sched_scan_start_delay)
		fprintf(f, "sched_scan_start_delay=%u\n",
			config->sched_scan_start_delay);

	if (config->external_sim)
		fprintf(f, "external_sim=%d\n", config->external_sim);

	if (config->tdls_external_control)
		fprintf(f, "tdls_external_control=%d\n",
			config->tdls_external_control);

	if (config->wowlan_triggers)
		fprintf(f, "wowlan_triggers=%s\n",
			config->wowlan_triggers);

	if (config->bgscan)
		fprintf(f, "bgscan=\"%s\"\n", config->bgscan);

	if (config->autoscan)
		fprintf(f, "autoscan=%s\n", config->autoscan);

	if (config->p2p_search_delay != DEFAULT_P2P_SEARCH_DELAY)
		fprintf(f, "p2p_search_delay=%u\n",
			config->p2p_search_delay);

	if (config->mac_addr)
		fprintf(f, "mac_addr=%d\n", config->mac_addr);

	if (config->rand_addr_lifetime != DEFAULT_RAND_ADDR_LIFETIME)
		fprintf(f, "rand_addr_lifetime=%u\n",
			config->rand_addr_lifetime);

	if (config->preassoc_mac_addr)
		fprintf(f, "preassoc_mac_addr=%d\n", config->preassoc_mac_addr);

	if (config->key_mgmt_offload != DEFAULT_KEY_MGMT_OFFLOAD)
		fprintf(f, "key_mgmt_offload=%d\n", config->key_mgmt_offload);

	if (config->user_mpm != DEFAULT_USER_MPM)
		fprintf(f, "user_mpm=%d\n", config->user_mpm);

	if (config->max_peer_links != DEFAULT_MAX_PEER_LINKS)
		fprintf(f, "max_peer_links=%d\n", config->max_peer_links);

	if (config->cert_in_cb != DEFAULT_CERT_IN_CB)
		fprintf(f, "cert_in_cb=%d\n", config->cert_in_cb);

	if (config->mesh_max_inactivity != DEFAULT_MESH_MAX_INACTIVITY)
		fprintf(f, "mesh_max_inactivity=%d\n",
			config->mesh_max_inactivity);

	if (config->dot11RSNASAERetransPeriod !=
	    DEFAULT_DOT11_RSNA_SAE_RETRANS_PERIOD)
		fprintf(f, "dot11RSNASAERetransPeriod=%d\n",
			config->dot11RSNASAERetransPeriod);

	if (config->passive_scan)
		fprintf(f, "passive_scan=%d\n", config->passive_scan);

	if (config->reassoc_same_bss_optim)
		fprintf(f, "reassoc_same_bss_optim=%d\n",
			config->reassoc_same_bss_optim);

	if (config->wps_priority)
		fprintf(f, "wps_priority=%d\n", config->wps_priority);

	if (config->wpa_rsc_relaxation != DEFAULT_WPA_RSC_RELAXATION)
		fprintf(f, "wpa_rsc_relaxation=%d\n",
			config->wpa_rsc_relaxation);

	if (config->sched_scan_plans)
		fprintf(f, "sched_scan_plans=%s\n", config->sched_scan_plans);

#ifdef CONFIG_MBO
	if (config->non_pref_chan)
		fprintf(f, "non_pref_chan=%s\n", config->non_pref_chan);
	if (config->mbo_cell_capa != DEFAULT_MBO_CELL_CAPA)
		fprintf(f, "mbo_cell_capa=%u\n", config->mbo_cell_capa);
	if (config->disassoc_imminent_rssi_threshold !=
	    DEFAULT_DISASSOC_IMMINENT_RSSI_THRESHOLD)
		fprintf(f, "disassoc_imminent_rssi_threshold=%d\n",
			config->disassoc_imminent_rssi_threshold);
	if (config->oce != DEFAULT_OCE_SUPPORT)
		fprintf(f, "oce=%u\n", config->oce);
#endif /* CONFIG_MBO */

	if (config->gas_address3)
		fprintf(f, "gas_address3=%d\n", config->gas_address3);

	if (config->ftm_responder)
		fprintf(f, "ftm_responder=%d\n", config->ftm_responder);
	if (config->ftm_initiator)
		fprintf(f, "ftm_initiator=%d\n", config->ftm_initiator);

	if (config->osu_dir)
		fprintf(f, "osu_dir=%s\n", config->osu_dir);

	if (config->fst_group_id)
		fprintf(f, "fst_group_id=%s\n", config->fst_group_id);
	if (config->fst_priority)
		fprintf(f, "fst_priority=%d\n", config->fst_priority);
	if (config->fst_llt)
		fprintf(f, "fst_llt=%d\n", config->fst_llt);

	if (config->gas_rand_addr_lifetime != DEFAULT_RAND_ADDR_LIFETIME)
		fprintf(f, "gas_rand_addr_lifetime=%u\n",
			config->gas_rand_addr_lifetime);
	if (config->gas_rand_mac_addr)
		fprintf(f, "gas_rand_mac_addr=%d\n", config->gas_rand_mac_addr);
	if (config->dpp_config_processing)
		fprintf(f, "dpp_config_processing=%d\n",
			config->dpp_config_processing);
	if (config->coloc_intf_reporting)
		fprintf(f, "coloc_intf_reporting=%d\n",
			config->coloc_intf_reporting);
}

#endif /* CONFIG_NO_CONFIG_WRITE */


int wpa_config_write(const char *name, struct wpa_config *config)
{
#ifndef CONFIG_NO_CONFIG_WRITE
	FILE *f;
	struct wpa_ssid *ssid;
	struct wpa_cred *cred;
#ifndef CONFIG_NO_CONFIG_BLOBS
	struct wpa_config_blob *blob;
#endif /* CONFIG_NO_CONFIG_BLOBS */
	int ret = 0;
	const char *orig_name = name;
	int tmp_len = os_strlen(name) + 5; /* allow space for .tmp suffix */
	char *tmp_name = os_malloc(tmp_len);

	if (tmp_name) {
		os_snprintf(tmp_name, tmp_len, "%s.tmp", name);
		name = tmp_name;
	}

	wpa_printf(MSG_DEBUG, "Writing configuration file '%s'", name);

	f = fopen(name, "w");
	if (f == NULL) {
		wpa_printf(MSG_DEBUG, "Failed to open '%s' for writing", name);
		os_free(tmp_name);
		return -1;
	}

	wpa_config_write_global(f, config);

	for (cred = config->cred; cred; cred = cred->next) {
		if (cred->temporary)
			continue;
		fprintf(f, "\ncred={\n");
		wpa_config_write_cred(f, cred);
		fprintf(f, "}\n");
	}

	for (ssid = config->ssid; ssid; ssid = ssid->next) {
		if (ssid->key_mgmt == WPA_KEY_MGMT_WPS || ssid->temporary)
			continue; /* do not save temporary networks */
		if (wpa_key_mgmt_wpa_psk(ssid->key_mgmt) && !ssid->psk_set &&
		    !ssid->passphrase)
			continue; /* do not save invalid network */
		fprintf(f, "\nnetwork={\n");
		wpa_config_write_network(f, ssid);
		fprintf(f, "}\n");
	}

#ifndef CONFIG_NO_CONFIG_BLOBS
	for (blob = config->blobs; blob; blob = blob->next) {
		ret = wpa_config_write_blob(f, blob);
		if (ret)
			break;
	}
#endif /* CONFIG_NO_CONFIG_BLOBS */

	os_fdatasync(f);

	fclose(f);

	if (tmp_name) {
		int chmod_ret = 0;

#ifdef ANDROID
		chmod_ret = chmod(tmp_name,
				  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
#endif /* ANDROID */
		if (chmod_ret != 0 || rename(tmp_name, orig_name) != 0)
			ret = -1;

		os_free(tmp_name);
	}

	wpa_printf(MSG_DEBUG, "Configuration file '%s' written %ssuccessfully",
		   orig_name, ret ? "un" : "");
	return ret;
#else /* CONFIG_NO_CONFIG_WRITE */
	return -1;
#endif /* CONFIG_NO_CONFIG_WRITE */
}
