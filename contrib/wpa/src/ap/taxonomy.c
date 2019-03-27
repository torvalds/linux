/*
 * hostapd / Client taxonomy
 * Copyright (c) 2015 Google, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * Parse a series of IEs, as in Probe Request or (Re)Association Request frames,
 * and render them to a descriptive string. The tag number of standard options
 * is written to the string, while the vendor ID and subtag are written for
 * vendor options.
 *
 * Example strings:
 * 0,1,50,45,221(00904c,51)
 * 0,1,33,36,48,45,221(00904c,51),221(0050f2,2)
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "common/wpa_ctrl.h"
#include "hostapd.h"
#include "sta_info.h"
#include "taxonomy.h"


/* Copy a string with no funny schtuff allowed; only alphanumerics. */
static void no_mischief_strncpy(char *dst, const char *src, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		unsigned char s = src[i];
		int is_lower = s >= 'a' && s <= 'z';
		int is_upper = s >= 'A' && s <= 'Z';
		int is_digit = s >= '0' && s <= '9';

		if (is_lower || is_upper || is_digit) {
			/* TODO: if any manufacturer uses Unicode within the
			 * WPS header, it will get mangled here. */
			dst[i] = s;
		} else {
			/* Note that even spaces will be transformed to
			 * underscores, so 'Nexus 7' will turn into 'Nexus_7'.
			 * This is deliberate, to make the string easier to
			 * parse. */
			dst[i] = '_';
		}
	}
}


static int get_wps_name(char *name, size_t name_len,
			const u8 *data, size_t data_len)
{
	/* Inside the WPS IE are a series of attributes, using two byte IDs
	 * and two byte lengths. We're looking for the model name, if
	 * present. */
	while (data_len >= 4) {
		u16 id, elen;

		id = WPA_GET_BE16(data);
		elen = WPA_GET_BE16(data + 2);
		data += 4;
		data_len -= 4;

		if (elen > data_len)
			return 0;

		if (id == 0x1023) {
			/* Model name, like 'Nexus 7' */
			size_t n = (elen < name_len) ? elen : name_len;
			no_mischief_strncpy(name, (const char *) data, n);
			return n;
		}

		data += elen;
		data_len -= elen;
	}

	return 0;
}


static void ie_to_string(char *fstr, size_t fstr_len, const struct wpabuf *ies)
{
	char *fpos = fstr;
	char *fend = fstr + fstr_len;
	char htcap[7 + 4 + 1]; /* ",htcap:" + %04hx + trailing NUL */
	char htagg[7 + 2 + 1]; /* ",htagg:" + %02hx + trailing NUL */
	char htmcs[7 + 8 + 1]; /* ",htmcs:" + %08x + trailing NUL */
	char vhtcap[8 + 8 + 1]; /* ",vhtcap:" + %08x + trailing NUL */
	char vhtrxmcs[10 + 8 + 1]; /* ",vhtrxmcs:" + %08x + trailing NUL */
	char vhttxmcs[10 + 8 + 1]; /* ",vhttxmcs:" + %08x + trailing NUL */
#define MAX_EXTCAP	254
	char extcap[8 + 2 * MAX_EXTCAP + 1]; /* ",extcap:" + hex + trailing NUL
					      */
	char txpow[7 + 4 + 1]; /* ",txpow:" + %04hx + trailing NUL */
#define WPS_NAME_LEN		32
	char wps[WPS_NAME_LEN + 5 + 1]; /* room to prepend ",wps:" + trailing
					 * NUL */
	int num = 0;
	const u8 *ie;
	size_t ie_len;
	int ret;

	os_memset(htcap, 0, sizeof(htcap));
	os_memset(htagg, 0, sizeof(htagg));
	os_memset(htmcs, 0, sizeof(htmcs));
	os_memset(vhtcap, 0, sizeof(vhtcap));
	os_memset(vhtrxmcs, 0, sizeof(vhtrxmcs));
	os_memset(vhttxmcs, 0, sizeof(vhttxmcs));
	os_memset(extcap, 0, sizeof(extcap));
	os_memset(txpow, 0, sizeof(txpow));
	os_memset(wps, 0, sizeof(wps));
	*fpos = '\0';

	if (!ies)
		return;
	ie = wpabuf_head(ies);
	ie_len = wpabuf_len(ies);

	while (ie_len >= 2) {
		u8 id, elen;
		char *sep = (num++ == 0) ? "" : ",";

		id = *ie++;
		elen = *ie++;
		ie_len -= 2;

		if (elen > ie_len)
			break;

		if (id == WLAN_EID_VENDOR_SPECIFIC && elen >= 4) {
			/* Vendor specific */
			if (WPA_GET_BE32(ie) == WPS_IE_VENDOR_TYPE) {
				/* WPS */
				char model_name[WPS_NAME_LEN + 1];
				const u8 *data = &ie[4];
				size_t data_len = elen - 4;

				os_memset(model_name, 0, sizeof(model_name));
				if (get_wps_name(model_name, WPS_NAME_LEN, data,
						 data_len)) {
					os_snprintf(wps, sizeof(wps),
						    ",wps:%s", model_name);
				}
			}

			ret = os_snprintf(fpos, fend - fpos,
					  "%s%d(%02x%02x%02x,%d)",
					  sep, id, ie[0], ie[1], ie[2], ie[3]);
		} else {
			if (id == WLAN_EID_HT_CAP && elen >= 2) {
				/* HT Capabilities (802.11n) */
				os_snprintf(htcap, sizeof(htcap),
					    ",htcap:%04hx",
					    WPA_GET_LE16(ie));
			}
			if (id == WLAN_EID_HT_CAP && elen >= 3) {
				/* HT Capabilities (802.11n), A-MPDU information
				 */
				os_snprintf(htagg, sizeof(htagg),
					    ",htagg:%02hx", (u16) ie[2]);
			}
			if (id == WLAN_EID_HT_CAP && elen >= 7) {
				/* HT Capabilities (802.11n), MCS information */
				os_snprintf(htmcs, sizeof(htmcs),
					    ",htmcs:%08hx",
					    (u16) WPA_GET_LE32(ie + 3));
			}
			if (id == WLAN_EID_VHT_CAP && elen >= 4) {
				/* VHT Capabilities (802.11ac) */
				os_snprintf(vhtcap, sizeof(vhtcap),
					    ",vhtcap:%08x",
					    WPA_GET_LE32(ie));
			}
			if (id == WLAN_EID_VHT_CAP && elen >= 8) {
				/* VHT Capabilities (802.11ac), RX MCS
				 * information */
				os_snprintf(vhtrxmcs, sizeof(vhtrxmcs),
					    ",vhtrxmcs:%08x",
					    WPA_GET_LE32(ie + 4));
			}
			if (id == WLAN_EID_VHT_CAP && elen >= 12) {
				/* VHT Capabilities (802.11ac), TX MCS
				 * information */
				os_snprintf(vhttxmcs, sizeof(vhttxmcs),
					    ",vhttxmcs:%08x",
					    WPA_GET_LE32(ie + 8));
			}
			if (id == WLAN_EID_EXT_CAPAB) {
				/* Extended Capabilities */
				int i;
				int len = (elen < MAX_EXTCAP) ? elen :
					MAX_EXTCAP;
				char *p = extcap;

				p += os_snprintf(extcap, sizeof(extcap),
						 ",extcap:");
				for (i = 0; i < len; i++) {
					int lim;

					lim = sizeof(extcap) -
						os_strlen(extcap);
					if (lim <= 0)
						break;
					p += os_snprintf(p, lim, "%02x",
							 *(ie + i));
				}
			}
			if (id == WLAN_EID_PWR_CAPABILITY && elen == 2) {
				/* TX Power */
				os_snprintf(txpow, sizeof(txpow),
					    ",txpow:%04hx",
					    WPA_GET_LE16(ie));
			}

			ret = os_snprintf(fpos, fend - fpos, "%s%d", sep, id);
		}
		if (os_snprintf_error(fend - fpos, ret))
			goto fail;
		fpos += ret;

		ie += elen;
		ie_len -= elen;
	}

	ret = os_snprintf(fpos, fend - fpos, "%s%s%s%s%s%s%s%s%s",
			  htcap, htagg, htmcs, vhtcap, vhtrxmcs, vhttxmcs,
			  txpow, extcap, wps);
	if (os_snprintf_error(fend - fpos, ret)) {
	fail:
		fstr[0] = '\0';
	}
}


int retrieve_sta_taxonomy(const struct hostapd_data *hapd,
			  struct sta_info *sta, char *buf, size_t buflen)
{
	int ret;
	char *pos, *end;

	if (!sta->probe_ie_taxonomy || !sta->assoc_ie_taxonomy)
		return 0;

	ret = os_snprintf(buf, buflen, "wifi4|probe:");
	if (os_snprintf_error(buflen, ret))
		return 0;
	pos = buf + ret;
	end = buf + buflen;

	ie_to_string(pos, end - pos, sta->probe_ie_taxonomy);
	pos = os_strchr(pos, '\0');
	if (pos >= end)
		return 0;
	ret = os_snprintf(pos, end - pos, "|assoc:");
	if (os_snprintf_error(end - pos, ret))
		return 0;
	pos += ret;
	ie_to_string(pos, end - pos, sta->assoc_ie_taxonomy);
	pos = os_strchr(pos, '\0');
	return pos - buf;
}


void taxonomy_sta_info_probe_req(const struct hostapd_data *hapd,
				 struct sta_info *sta,
				 const u8 *ie, size_t ie_len)
{
	wpabuf_free(sta->probe_ie_taxonomy);
	sta->probe_ie_taxonomy = wpabuf_alloc_copy(ie, ie_len);
}


void taxonomy_hostapd_sta_info_probe_req(const struct hostapd_data *hapd,
					 struct hostapd_sta_info *info,
					 const u8 *ie, size_t ie_len)
{
	wpabuf_free(info->probe_ie_taxonomy);
	info->probe_ie_taxonomy = wpabuf_alloc_copy(ie, ie_len);
}


void taxonomy_sta_info_assoc_req(const struct hostapd_data *hapd,
				 struct sta_info *sta,
				 const u8 *ie, size_t ie_len)
{
	wpabuf_free(sta->assoc_ie_taxonomy);
	sta->assoc_ie_taxonomy = wpabuf_alloc_copy(ie, ie_len);
}
