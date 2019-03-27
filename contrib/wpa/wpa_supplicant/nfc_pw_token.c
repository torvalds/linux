/*
 * nfc_pw_token - Tool for building NFC password tokens for WPS
 * Copyright (c) 2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "utils/common.h"
#include "crypto/random.h"
#include "wpa_supplicant_i.h"
#include "config.h"
#include "wps_supplicant.h"


static void print_bin(const char *title, const struct wpabuf *buf)
{
	size_t i, len;
	const u8 *pos;

	if (buf == NULL)
		return;

	printf("%s=", title);

	pos = wpabuf_head(buf);
	len = wpabuf_len(buf);
	for (i = 0; i < len; i++)
		printf("%02X", *pos++);

	printf("\n");
}


int main(int argc, char *argv[])
{
	struct wpa_supplicant wpa_s;
	int ret = -1;
	struct wpabuf *buf = NULL, *ndef = NULL;
	char txt[1000];

	if (os_program_init())
		return -1;
	random_init(NULL);

	os_memset(&wpa_s, 0, sizeof(wpa_s));
	wpa_s.conf = os_zalloc(sizeof(*wpa_s.conf));
	if (wpa_s.conf == NULL)
		goto fail;

	buf = wpas_wps_nfc_token(&wpa_s, 0);
	if (buf == NULL)
		goto fail;

	ndef = ndef_build_wifi(buf);
	if (ndef == NULL)
		goto fail;

	wpa_snprintf_hex_uppercase(txt, sizeof(txt), wpabuf_head(buf),
				   wpabuf_len(buf));
	printf("#WPS=%s\n", txt);

	wpa_snprintf_hex_uppercase(txt, sizeof(txt), wpabuf_head(ndef),
				   wpabuf_len(ndef));
	printf("#NDEF=%s\n", txt);

	printf("wps_nfc_dev_pw_id=%d\n", wpa_s.conf->wps_nfc_dev_pw_id);
	print_bin("wps_nfc_dh_pubkey", wpa_s.conf->wps_nfc_dh_pubkey);
	print_bin("wps_nfc_dh_privkey", wpa_s.conf->wps_nfc_dh_privkey);
	print_bin("wps_nfc_dev_pw", wpa_s.conf->wps_nfc_dev_pw);

	ret = 0;
fail:
	wpabuf_free(ndef);
	wpabuf_free(buf);
	wpa_config_free(wpa_s.conf);
	random_deinit();
	os_program_deinit();

	return ret;
}
