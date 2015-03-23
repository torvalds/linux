/*
 * Driver interaction with generic Linux Wireless Extensions
 * Copyright (c) 2003-2010, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 *
 * This file implements a driver interface for the Linux Wireless Extensions.
 * When used with WE-18 or newer, this interface can be used as-is with number
 * of drivers. In addition to this, some of the common functions in this file
 * can be used by other driver interface implementations that use generic WE
 * ioctls, but require private ioctls for some of the functionality.
 */

#include "includes.h"
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <net/if_arp.h>

#include "wireless_copy.h"
#include "common.h"
#include "eloop.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_common.h"
#include "priv_netlink.h"
#include "netlink.h"
#include "linux_ioctl.h"
#include "rfkill.h"
#include "driver.h"
#include "driver_wext.h"


static int wpa_driver_wext_flush_pmkid(void *priv);
static int wpa_driver_wext_get_range(void *priv);
static int wpa_driver_wext_finish_drv_init(struct wpa_driver_wext_data *drv);
static void wpa_driver_wext_disconnect(struct wpa_driver_wext_data *drv);
static int wpa_driver_wext_set_auth_alg(void *priv, int auth_alg);


int wpa_driver_wext_set_auth_param(struct wpa_driver_wext_data *drv,
				   int idx, u32 value)
{
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.param.flags = idx & IW_AUTH_INDEX;
	iwr.u.param.value = value;

	if (ioctl(drv->ioctl_sock, SIOCSIWAUTH, &iwr) < 0) {
		if (errno != EOPNOTSUPP) {
			wpa_printf(MSG_DEBUG, "WEXT: SIOCSIWAUTH(param %d "
				   "value 0x%x) failed: %s)",
				   idx, value, strerror(errno));
		}
		ret = errno == EOPNOTSUPP ? -2 : -1;
	}

	return ret;
}


/**
 * wpa_driver_wext_get_bssid - Get BSSID, SIOCGIWAP
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @bssid: Buffer for BSSID
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_get_bssid(void *priv, u8 *bssid)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);

	if (ioctl(drv->ioctl_sock, SIOCGIWAP, &iwr) < 0) {
		perror("ioctl[SIOCGIWAP]");
		ret = -1;
	}
	os_memcpy(bssid, iwr.u.ap_addr.sa_data, ETH_ALEN);

	return ret;
}


/**
 * wpa_driver_wext_set_bssid - Set BSSID, SIOCSIWAP
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @bssid: BSSID
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_set_bssid(void *priv, const u8 *bssid)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.ap_addr.sa_family = ARPHRD_ETHER;
	if (bssid)
		os_memcpy(iwr.u.ap_addr.sa_data, bssid, ETH_ALEN);
	else
		os_memset(iwr.u.ap_addr.sa_data, 0, ETH_ALEN);

	if (ioctl(drv->ioctl_sock, SIOCSIWAP, &iwr) < 0) {
		perror("ioctl[SIOCSIWAP]");
		ret = -1;
	}

	return ret;
}


/**
 * wpa_driver_wext_get_ssid - Get SSID, SIOCGIWESSID
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @ssid: Buffer for the SSID; must be at least 32 bytes long
 * Returns: SSID length on success, -1 on failure
 */
int wpa_driver_wext_get_ssid(void *priv, u8 *ssid)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.essid.pointer = (caddr_t) ssid;
	iwr.u.essid.length = 32;

	if (ioctl(drv->ioctl_sock, SIOCGIWESSID, &iwr) < 0) {
		perror("ioctl[SIOCGIWESSID]");
		ret = -1;
	} else {
		ret = iwr.u.essid.length;
		if (ret > 32)
			ret = 32;
		/* Some drivers include nul termination in the SSID, so let's
		 * remove it here before further processing. WE-21 changes this
		 * to explicitly require the length _not_ to include nul
		 * termination. */
		if (ret > 0 && ssid[ret - 1] == '\0' &&
		    drv->we_version_compiled < 21)
			ret--;
	}

	return ret;
}


/**
 * wpa_driver_wext_set_ssid - Set SSID, SIOCSIWESSID
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @ssid: SSID
 * @ssid_len: Length of SSID (0..32)
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_set_ssid(void *priv, const u8 *ssid, size_t ssid_len)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;
	char buf[33];

	if (ssid_len > 32)
		return -1;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	/* flags: 1 = ESSID is active, 0 = not (promiscuous) */
	iwr.u.essid.flags = (ssid_len != 0);
	os_memset(buf, 0, sizeof(buf));
	os_memcpy(buf, ssid, ssid_len);
	iwr.u.essid.pointer = (caddr_t) buf;
	if (drv->we_version_compiled < 21) {
		/* For historic reasons, set SSID length to include one extra
		 * character, C string nul termination, even though SSID is
		 * really an octet string that should not be presented as a C
		 * string. Some Linux drivers decrement the length by one and
		 * can thus end up missing the last octet of the SSID if the
		 * length is not incremented here. WE-21 changes this to
		 * explicitly require the length _not_ to include nul
		 * termination. */
		if (ssid_len)
			ssid_len++;
	}
	iwr.u.essid.length = ssid_len;

	if (ioctl(drv->ioctl_sock, SIOCSIWESSID, &iwr) < 0) {
		perror("ioctl[SIOCSIWESSID]");
		ret = -1;
	}

	return ret;
}


/**
 * wpa_driver_wext_set_freq - Set frequency/channel, SIOCSIWFREQ
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @freq: Frequency in MHz
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_set_freq(void *priv, int freq)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.freq.m = freq * 100000;
	iwr.u.freq.e = 1;

	if (ioctl(drv->ioctl_sock, SIOCSIWFREQ, &iwr) < 0) {
		perror("ioctl[SIOCSIWFREQ]");
		ret = -1;
	}

	return ret;
}


static void
wpa_driver_wext_event_wireless_custom(void *ctx, char *custom)
{
	union wpa_event_data data;

	wpa_printf(MSG_MSGDUMP, "WEXT: Custom wireless event: '%s'",
		   custom);

	os_memset(&data, 0, sizeof(data));
	/* Host AP driver */
	if (os_strncmp(custom, "MLME-MICHAELMICFAILURE.indication", 33) == 0) {
		data.michael_mic_failure.unicast =
			os_strstr(custom, " unicast ") != NULL;
		/* TODO: parse parameters(?) */
		wpa_supplicant_event(ctx, EVENT_MICHAEL_MIC_FAILURE, &data);
	} else if (os_strncmp(custom, "ASSOCINFO(ReqIEs=", 17) == 0) {
		char *spos;
		int bytes;
		u8 *req_ies = NULL, *resp_ies = NULL;

		spos = custom + 17;

		bytes = strspn(spos, "0123456789abcdefABCDEF");
		if (!bytes || (bytes & 1))
			return;
		bytes /= 2;

		req_ies = os_malloc(bytes);
		if (req_ies == NULL ||
		    hexstr2bin(spos, req_ies, bytes) < 0)
			goto done;
		data.assoc_info.req_ies = req_ies;
		data.assoc_info.req_ies_len = bytes;

		spos += bytes * 2;

		data.assoc_info.resp_ies = NULL;
		data.assoc_info.resp_ies_len = 0;

		if (os_strncmp(spos, " RespIEs=", 9) == 0) {
			spos += 9;

			bytes = strspn(spos, "0123456789abcdefABCDEF");
			if (!bytes || (bytes & 1))
				goto done;
			bytes /= 2;

			resp_ies = os_malloc(bytes);
			if (resp_ies == NULL ||
			    hexstr2bin(spos, resp_ies, bytes) < 0)
				goto done;
			data.assoc_info.resp_ies = resp_ies;
			data.assoc_info.resp_ies_len = bytes;
		}

		wpa_supplicant_event(ctx, EVENT_ASSOCINFO, &data);

	done:
		os_free(resp_ies);
		os_free(req_ies);
#ifdef CONFIG_PEERKEY
	} else if (os_strncmp(custom, "STKSTART.request=", 17) == 0) {
		if (hwaddr_aton(custom + 17, data.stkstart.peer)) {
			wpa_printf(MSG_DEBUG, "WEXT: unrecognized "
				   "STKSTART.request '%s'", custom + 17);
			return;
		}
		wpa_supplicant_event(ctx, EVENT_STKSTART, &data);
#endif /* CONFIG_PEERKEY */
	}
}


static int wpa_driver_wext_event_wireless_michaelmicfailure(
	void *ctx, const char *ev, size_t len)
{
	const struct iw_michaelmicfailure *mic;
	union wpa_event_data data;

	if (len < sizeof(*mic))
		return -1;

	mic = (const struct iw_michaelmicfailure *) ev;

	wpa_printf(MSG_DEBUG, "Michael MIC failure wireless event: "
		   "flags=0x%x src_addr=" MACSTR, mic->flags,
		   MAC2STR(mic->src_addr.sa_data));

	os_memset(&data, 0, sizeof(data));
	data.michael_mic_failure.unicast = !(mic->flags & IW_MICFAILURE_GROUP);
	wpa_supplicant_event(ctx, EVENT_MICHAEL_MIC_FAILURE, &data);

	return 0;
}


static int wpa_driver_wext_event_wireless_pmkidcand(
	struct wpa_driver_wext_data *drv, const char *ev, size_t len)
{
	const struct iw_pmkid_cand *cand;
	union wpa_event_data data;
	const u8 *addr;

	if (len < sizeof(*cand))
		return -1;

	cand = (const struct iw_pmkid_cand *) ev;
	addr = (const u8 *) cand->bssid.sa_data;

	wpa_printf(MSG_DEBUG, "PMKID candidate wireless event: "
		   "flags=0x%x index=%d bssid=" MACSTR, cand->flags,
		   cand->index, MAC2STR(addr));

	os_memset(&data, 0, sizeof(data));
	os_memcpy(data.pmkid_candidate.bssid, addr, ETH_ALEN);
	data.pmkid_candidate.index = cand->index;
	data.pmkid_candidate.preauth = cand->flags & IW_PMKID_CAND_PREAUTH;
	wpa_supplicant_event(drv->ctx, EVENT_PMKID_CANDIDATE, &data);

	return 0;
}


static int wpa_driver_wext_event_wireless_assocreqie(
	struct wpa_driver_wext_data *drv, const char *ev, int len)
{
	if (len < 0)
		return -1;

	wpa_hexdump(MSG_DEBUG, "AssocReq IE wireless event", (const u8 *) ev,
		    len);
	os_free(drv->assoc_req_ies);
	drv->assoc_req_ies = os_malloc(len);
	if (drv->assoc_req_ies == NULL) {
		drv->assoc_req_ies_len = 0;
		return -1;
	}
	os_memcpy(drv->assoc_req_ies, ev, len);
	drv->assoc_req_ies_len = len;

	return 0;
}


static int wpa_driver_wext_event_wireless_assocrespie(
	struct wpa_driver_wext_data *drv, const char *ev, int len)
{
	if (len < 0)
		return -1;

	wpa_hexdump(MSG_DEBUG, "AssocResp IE wireless event", (const u8 *) ev,
		    len);
	os_free(drv->assoc_resp_ies);
	drv->assoc_resp_ies = os_malloc(len);
	if (drv->assoc_resp_ies == NULL) {
		drv->assoc_resp_ies_len = 0;
		return -1;
	}
	os_memcpy(drv->assoc_resp_ies, ev, len);
	drv->assoc_resp_ies_len = len;

	return 0;
}


static void wpa_driver_wext_event_assoc_ies(struct wpa_driver_wext_data *drv)
{
	union wpa_event_data data;

	if (drv->assoc_req_ies == NULL && drv->assoc_resp_ies == NULL)
		return;

	os_memset(&data, 0, sizeof(data));
	if (drv->assoc_req_ies) {
		data.assoc_info.req_ies = drv->assoc_req_ies;
		data.assoc_info.req_ies_len = drv->assoc_req_ies_len;
	}
	if (drv->assoc_resp_ies) {
		data.assoc_info.resp_ies = drv->assoc_resp_ies;
		data.assoc_info.resp_ies_len = drv->assoc_resp_ies_len;
	}

	wpa_supplicant_event(drv->ctx, EVENT_ASSOCINFO, &data);

	os_free(drv->assoc_req_ies);
	drv->assoc_req_ies = NULL;
	os_free(drv->assoc_resp_ies);
	drv->assoc_resp_ies = NULL;
}


static void wpa_driver_wext_event_wireless(struct wpa_driver_wext_data *drv,
					   char *data, int len)
{
	struct iw_event iwe_buf, *iwe = &iwe_buf;
	char *pos, *end, *custom, *buf;

	pos = data;
	end = data + len;

	while (pos + IW_EV_LCP_LEN <= end) {
		/* Event data may be unaligned, so make a local, aligned copy
		 * before processing. */
		os_memcpy(&iwe_buf, pos, IW_EV_LCP_LEN);
		wpa_printf(MSG_DEBUG, "Wireless event: cmd=0x%x len=%d",
			   iwe->cmd, iwe->len);
		if (iwe->len <= IW_EV_LCP_LEN)
			return;

		custom = pos + IW_EV_POINT_LEN;
		if (drv->we_version_compiled > 18 &&
		    (iwe->cmd == IWEVMICHAELMICFAILURE ||
		     iwe->cmd == IWEVCUSTOM ||
		     iwe->cmd == IWEVASSOCREQIE ||
		     iwe->cmd == IWEVASSOCRESPIE ||
		     iwe->cmd == IWEVPMKIDCAND)) {
			/* WE-19 removed the pointer from struct iw_point */
			char *dpos = (char *) &iwe_buf.u.data.length;
			int dlen = dpos - (char *) &iwe_buf;
			os_memcpy(dpos, pos + IW_EV_LCP_LEN,
				  sizeof(struct iw_event) - dlen);
		} else {
			os_memcpy(&iwe_buf, pos, sizeof(struct iw_event));
			custom += IW_EV_POINT_OFF;
		}

		switch (iwe->cmd) {
		case SIOCGIWAP:
			wpa_printf(MSG_DEBUG, "Wireless event: new AP: "
				   MACSTR,
				   MAC2STR((u8 *) iwe->u.ap_addr.sa_data));
			if (is_zero_ether_addr(
				    (const u8 *) iwe->u.ap_addr.sa_data) ||
			    os_memcmp(iwe->u.ap_addr.sa_data,
				      "\x44\x44\x44\x44\x44\x44", ETH_ALEN) ==
			    0) {
				os_free(drv->assoc_req_ies);
				drv->assoc_req_ies = NULL;
				os_free(drv->assoc_resp_ies);
				drv->assoc_resp_ies = NULL;
				wpa_supplicant_event(drv->ctx, EVENT_DISASSOC,
						     NULL);
			
			} else {
				wpa_driver_wext_event_assoc_ies(drv);
				wpa_supplicant_event(drv->ctx, EVENT_ASSOC,
						     NULL);
			}
			break;
		case IWEVMICHAELMICFAILURE:
			if (custom + iwe->u.data.length > end) {
				wpa_printf(MSG_DEBUG, "WEXT: Invalid "
					   "IWEVMICHAELMICFAILURE length");
				return;
			}
			wpa_driver_wext_event_wireless_michaelmicfailure(
				drv->ctx, custom, iwe->u.data.length);
			break;
		case IWEVCUSTOM:
			if (custom + iwe->u.data.length > end) {
				wpa_printf(MSG_DEBUG, "WEXT: Invalid "
					   "IWEVCUSTOM length");
				return;
			}
			buf = os_malloc(iwe->u.data.length + 1);
			if (buf == NULL)
				return;
			os_memcpy(buf, custom, iwe->u.data.length);
			buf[iwe->u.data.length] = '\0';
			wpa_driver_wext_event_wireless_custom(drv->ctx, buf);
			os_free(buf);
			break;
		case SIOCGIWSCAN:
			drv->scan_complete_events = 1;
			eloop_cancel_timeout(wpa_driver_wext_scan_timeout,
					     drv, drv->ctx);
			wpa_supplicant_event(drv->ctx, EVENT_SCAN_RESULTS,
					     NULL);
			break;
		case IWEVASSOCREQIE:
			if (custom + iwe->u.data.length > end) {
				wpa_printf(MSG_DEBUG, "WEXT: Invalid "
					   "IWEVASSOCREQIE length");
				return;
			}
			wpa_driver_wext_event_wireless_assocreqie(
				drv, custom, iwe->u.data.length);
			break;
		case IWEVASSOCRESPIE:
			if (custom + iwe->u.data.length > end) {
				wpa_printf(MSG_DEBUG, "WEXT: Invalid "
					   "IWEVASSOCRESPIE length");
				return;
			}
			wpa_driver_wext_event_wireless_assocrespie(
				drv, custom, iwe->u.data.length);
			break;
		case IWEVPMKIDCAND:
			if (custom + iwe->u.data.length > end) {
				wpa_printf(MSG_DEBUG, "WEXT: Invalid "
					   "IWEVPMKIDCAND length");
				return;
			}
			wpa_driver_wext_event_wireless_pmkidcand(
				drv, custom, iwe->u.data.length);
			break;
		}

		pos += iwe->len;
	}
}


static void wpa_driver_wext_event_link(struct wpa_driver_wext_data *drv,
				       char *buf, size_t len, int del)
{
	union wpa_event_data event;

	os_memset(&event, 0, sizeof(event));
	if (len > sizeof(event.interface_status.ifname))
		len = sizeof(event.interface_status.ifname) - 1;
	os_memcpy(event.interface_status.ifname, buf, len);
	event.interface_status.ievent = del ? EVENT_INTERFACE_REMOVED :
		EVENT_INTERFACE_ADDED;

	wpa_printf(MSG_DEBUG, "RTM_%sLINK, IFLA_IFNAME: Interface '%s' %s",
		   del ? "DEL" : "NEW",
		   event.interface_status.ifname,
		   del ? "removed" : "added");

	if (os_strcmp(drv->ifname, event.interface_status.ifname) == 0) {
		if (del)
			drv->if_removed = 1;
		else
			drv->if_removed = 0;
	}

	wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_STATUS, &event);
}


static int wpa_driver_wext_own_ifname(struct wpa_driver_wext_data *drv,
				      u8 *buf, size_t len)
{
	int attrlen, rta_len;
	struct rtattr *attr;

	attrlen = len;
	attr = (struct rtattr *) buf;

	rta_len = RTA_ALIGN(sizeof(struct rtattr));
	while (RTA_OK(attr, attrlen)) {
		if (attr->rta_type == IFLA_IFNAME) {
			if (os_strcmp(((char *) attr) + rta_len, drv->ifname)
			    == 0)
				return 1;
			else
				break;
		}
		attr = RTA_NEXT(attr, attrlen);
	}

	return 0;
}


static int wpa_driver_wext_own_ifindex(struct wpa_driver_wext_data *drv,
				       int ifindex, u8 *buf, size_t len)
{
	if (drv->ifindex == ifindex || drv->ifindex2 == ifindex)
		return 1;

	if (drv->if_removed && wpa_driver_wext_own_ifname(drv, buf, len)) {
		drv->ifindex = if_nametoindex(drv->ifname);
		wpa_printf(MSG_DEBUG, "WEXT: Update ifindex for a removed "
			   "interface");
		wpa_driver_wext_finish_drv_init(drv);
		return 1;
	}

	return 0;
}


static void wpa_driver_wext_event_rtm_newlink(void *ctx, struct ifinfomsg *ifi,
					      u8 *buf, size_t len)
{
	struct wpa_driver_wext_data *drv = ctx;
	int attrlen, rta_len;
	struct rtattr *attr;

	if (!wpa_driver_wext_own_ifindex(drv, ifi->ifi_index, buf, len)) {
		wpa_printf(MSG_DEBUG, "Ignore event for foreign ifindex %d",
			   ifi->ifi_index);
		return;
	}

	wpa_printf(MSG_DEBUG, "RTM_NEWLINK: operstate=%d ifi_flags=0x%x "
		   "(%s%s%s%s)",
		   drv->operstate, ifi->ifi_flags,
		   (ifi->ifi_flags & IFF_UP) ? "[UP]" : "",
		   (ifi->ifi_flags & IFF_RUNNING) ? "[RUNNING]" : "",
		   (ifi->ifi_flags & IFF_LOWER_UP) ? "[LOWER_UP]" : "",
		   (ifi->ifi_flags & IFF_DORMANT) ? "[DORMANT]" : "");

	if (!drv->if_disabled && !(ifi->ifi_flags & IFF_UP)) {
		wpa_printf(MSG_DEBUG, "WEXT: Interface down");
		drv->if_disabled = 1;
		wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_DISABLED, NULL);
	}

	if (drv->if_disabled && (ifi->ifi_flags & IFF_UP)) {
		wpa_printf(MSG_DEBUG, "WEXT: Interface up");
		drv->if_disabled = 0;
		wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_ENABLED, NULL);
	}

	/*
	 * Some drivers send the association event before the operup event--in
	 * this case, lifting operstate in wpa_driver_wext_set_operstate()
	 * fails. This will hit us when wpa_supplicant does not need to do
	 * IEEE 802.1X authentication
	 */
	if (drv->operstate == 1 &&
	    (ifi->ifi_flags & (IFF_LOWER_UP | IFF_DORMANT)) == IFF_LOWER_UP &&
	    !(ifi->ifi_flags & IFF_RUNNING))
		netlink_send_oper_ifla(drv->netlink, drv->ifindex,
				       -1, IF_OPER_UP);

	attrlen = len;
	attr = (struct rtattr *) buf;

	rta_len = RTA_ALIGN(sizeof(struct rtattr));
	while (RTA_OK(attr, attrlen)) {
		if (attr->rta_type == IFLA_WIRELESS) {
			wpa_driver_wext_event_wireless(
				drv, ((char *) attr) + rta_len,
				attr->rta_len - rta_len);
		} else if (attr->rta_type == IFLA_IFNAME) {
			wpa_driver_wext_event_link(drv,
						   ((char *) attr) + rta_len,
						   attr->rta_len - rta_len, 0);
		}
		attr = RTA_NEXT(attr, attrlen);
	}
}


static void wpa_driver_wext_event_rtm_dellink(void *ctx, struct ifinfomsg *ifi,
					      u8 *buf, size_t len)
{
	struct wpa_driver_wext_data *drv = ctx;
	int attrlen, rta_len;
	struct rtattr *attr;

	attrlen = len;
	attr = (struct rtattr *) buf;

	rta_len = RTA_ALIGN(sizeof(struct rtattr));
	while (RTA_OK(attr, attrlen)) {
		if (attr->rta_type == IFLA_IFNAME) {
			wpa_driver_wext_event_link(drv,
						   ((char *) attr) + rta_len,
						   attr->rta_len - rta_len, 1);
		}
		attr = RTA_NEXT(attr, attrlen);
	}
}


static void wpa_driver_wext_rfkill_blocked(void *ctx)
{
	wpa_printf(MSG_DEBUG, "WEXT: RFKILL blocked");
	/*
	 * This may be for any interface; use ifdown event to disable
	 * interface.
	 */
}


static void wpa_driver_wext_rfkill_unblocked(void *ctx)
{
	struct wpa_driver_wext_data *drv = ctx;
	wpa_printf(MSG_DEBUG, "WEXT: RFKILL unblocked");
	if (linux_set_iface_flags(drv->ioctl_sock, drv->ifname, 1)) {
		wpa_printf(MSG_DEBUG, "WEXT: Could not set interface UP "
			   "after rfkill unblock");
		return;
	}
	/* rtnetlink ifup handler will report interface as enabled */
}


static void wext_get_phy_name(struct wpa_driver_wext_data *drv)
{
	/* Find phy (radio) to which this interface belongs */
	char buf[90], *pos;
	int f, rv;

	drv->phyname[0] = '\0';
	snprintf(buf, sizeof(buf) - 1, "/sys/class/net/%s/phy80211/name",
		 drv->ifname);
	f = open(buf, O_RDONLY);
	if (f < 0) {
		wpa_printf(MSG_DEBUG, "Could not open file %s: %s",
			   buf, strerror(errno));
		return;
	}

	rv = read(f, drv->phyname, sizeof(drv->phyname) - 1);
	close(f);
	if (rv < 0) {
		wpa_printf(MSG_DEBUG, "Could not read file %s: %s",
			   buf, strerror(errno));
		return;
	}

	drv->phyname[rv] = '\0';
	pos = os_strchr(drv->phyname, '\n');
	if (pos)
		*pos = '\0';
	wpa_printf(MSG_DEBUG, "wext: interface %s phy: %s",
		   drv->ifname, drv->phyname);
}


/**
 * wpa_driver_wext_init - Initialize WE driver interface
 * @ctx: context to be used when calling wpa_supplicant functions,
 * e.g., wpa_supplicant_event()
 * @ifname: interface name, e.g., wlan0
 * Returns: Pointer to private data, %NULL on failure
 */
void * wpa_driver_wext_init(void *ctx, const char *ifname)
{
	struct wpa_driver_wext_data *drv;
	struct netlink_config *cfg;
	struct rfkill_config *rcfg;
	char path[128];
	struct stat buf;

	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL)
		return NULL;
	drv->ctx = ctx;
	os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname));

	os_snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211", ifname);
	if (stat(path, &buf) == 0) {
		wpa_printf(MSG_DEBUG, "WEXT: cfg80211-based driver detected");
		drv->cfg80211 = 1;
		wext_get_phy_name(drv);
	}

	drv->ioctl_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (drv->ioctl_sock < 0) {
		perror("socket(PF_INET,SOCK_DGRAM)");
		goto err1;
	}

	cfg = os_zalloc(sizeof(*cfg));
	if (cfg == NULL)
		goto err1;
	cfg->ctx = drv;
	cfg->newlink_cb = wpa_driver_wext_event_rtm_newlink;
	cfg->dellink_cb = wpa_driver_wext_event_rtm_dellink;
	drv->netlink = netlink_init(cfg);
	if (drv->netlink == NULL) {
		os_free(cfg);
		goto err2;
	}

	rcfg = os_zalloc(sizeof(*rcfg));
	if (rcfg == NULL)
		goto err3;
	rcfg->ctx = drv;
	os_strlcpy(rcfg->ifname, ifname, sizeof(rcfg->ifname));
	rcfg->blocked_cb = wpa_driver_wext_rfkill_blocked;
	rcfg->unblocked_cb = wpa_driver_wext_rfkill_unblocked;
	drv->rfkill = rfkill_init(rcfg);
	if (drv->rfkill == NULL) {
		wpa_printf(MSG_DEBUG, "WEXT: RFKILL status not available");
		os_free(rcfg);
	}

	drv->mlme_sock = -1;

	if (wpa_driver_wext_finish_drv_init(drv) < 0)
		goto err3;

	wpa_driver_wext_set_auth_param(drv, IW_AUTH_WPA_ENABLED, 1);

	return drv;

err3:
	rfkill_deinit(drv->rfkill);
	netlink_deinit(drv->netlink);
err2:
	close(drv->ioctl_sock);
err1:
	os_free(drv);
	return NULL;
}


static void wpa_driver_wext_send_rfkill(void *eloop_ctx, void *timeout_ctx)
{
	wpa_supplicant_event(timeout_ctx, EVENT_INTERFACE_DISABLED, NULL);
}


static int wpa_driver_wext_finish_drv_init(struct wpa_driver_wext_data *drv)
{
	int send_rfkill_event = 0;

	if (linux_set_iface_flags(drv->ioctl_sock, drv->ifname, 1) < 0) {
		if (rfkill_is_blocked(drv->rfkill)) {
			wpa_printf(MSG_DEBUG, "WEXT: Could not yet enable "
				   "interface '%s' due to rfkill",
				   drv->ifname);
			drv->if_disabled = 1;
			send_rfkill_event = 1;
		} else {
			wpa_printf(MSG_ERROR, "WEXT: Could not set "
				   "interface '%s' UP", drv->ifname);
			return -1;
		}
	}

	/*
	 * Make sure that the driver does not have any obsolete PMKID entries.
	 */
	wpa_driver_wext_flush_pmkid(drv);

	if (wpa_driver_wext_set_mode(drv, 0) < 0) {
		wpa_printf(MSG_DEBUG, "Could not configure driver to use "
			   "managed mode");
		/* Try to use it anyway */
	}

	wpa_driver_wext_get_range(drv);

	/*
	 * Unlock the driver's BSSID and force to a random SSID to clear any
	 * previous association the driver might have when the supplicant
	 * starts up.
	 */
	wpa_driver_wext_disconnect(drv);

	drv->ifindex = if_nametoindex(drv->ifname);

	if (os_strncmp(drv->ifname, "wlan", 4) == 0) {
		/*
		 * Host AP driver may use both wlan# and wifi# interface in
		 * wireless events. Since some of the versions included WE-18
		 * support, let's add the alternative ifindex also from
		 * driver_wext.c for the time being. This may be removed at
		 * some point once it is believed that old versions of the
		 * driver are not in use anymore.
		 */
		char ifname2[IFNAMSIZ + 1];
		os_strlcpy(ifname2, drv->ifname, sizeof(ifname2));
		os_memcpy(ifname2, "wifi", 4);
		wpa_driver_wext_alternative_ifindex(drv, ifname2);
	}

	netlink_send_oper_ifla(drv->netlink, drv->ifindex,
			       1, IF_OPER_DORMANT);

	if (send_rfkill_event) {
		eloop_register_timeout(0, 0, wpa_driver_wext_send_rfkill,
				       drv, drv->ctx);
	}

	return 0;
}


/**
 * wpa_driver_wext_deinit - Deinitialize WE driver interface
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 *
 * Shut down driver interface and processing of driver events. Free
 * private data buffer if one was allocated in wpa_driver_wext_init().
 */
void wpa_driver_wext_deinit(void *priv)
{
	struct wpa_driver_wext_data *drv = priv;

	wpa_driver_wext_set_auth_param(drv, IW_AUTH_WPA_ENABLED, 0);

	eloop_cancel_timeout(wpa_driver_wext_scan_timeout, drv, drv->ctx);

	/*
	 * Clear possibly configured driver parameters in order to make it
	 * easier to use the driver after wpa_supplicant has been terminated.
	 */
	wpa_driver_wext_disconnect(drv);

	netlink_send_oper_ifla(drv->netlink, drv->ifindex, 0, IF_OPER_UP);
	netlink_deinit(drv->netlink);
	rfkill_deinit(drv->rfkill);

	if (drv->mlme_sock >= 0)
		eloop_unregister_read_sock(drv->mlme_sock);

	(void) linux_set_iface_flags(drv->ioctl_sock, drv->ifname, 0);

	close(drv->ioctl_sock);
	if (drv->mlme_sock >= 0)
		close(drv->mlme_sock);
	os_free(drv->assoc_req_ies);
	os_free(drv->assoc_resp_ies);
	os_free(drv);
}


/**
 * wpa_driver_wext_scan_timeout - Scan timeout to report scan completion
 * @eloop_ctx: Unused
 * @timeout_ctx: ctx argument given to wpa_driver_wext_init()
 *
 * This function can be used as registered timeout when starting a scan to
 * generate a scan completed event if the driver does not report this.
 */
void wpa_driver_wext_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
	wpa_printf(MSG_DEBUG, "Scan timeout - try to get results");
	wpa_supplicant_event(timeout_ctx, EVENT_SCAN_RESULTS, NULL);
}


//added for wps2.0 @20110519
static int wpa_driver_wext_set_probe_req_ie(struct wpa_driver_wext_data *drv, const u8 *extra_ies,
				size_t extra_ies_len)
{
	unsigned char *pbuf;
	struct iwreq iwr;	
	int ret = 0;

	pbuf = os_malloc(extra_ies_len);
	os_memset(pbuf, 0, extra_ies_len);

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);

	os_memcpy(pbuf, extra_ies, extra_ies_len);
	
	iwr.u.data.pointer = (caddr_t)pbuf;
	iwr.u.data.length = extra_ies_len;
	iwr.u.data.flags = 0x8766;//magic number

	if (ioctl(drv->ioctl_sock, SIOCSIWPRIV, &iwr) < 0) {
		perror("ioctl[SIOCSIWMLME]");
		ret = -1;
	}

	if(pbuf)
		os_free(pbuf);

	return ret;

}

/**
 * wpa_driver_wext_scan - Request the driver to initiate scan
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @param: Scan parameters (specific SSID to scan for (ProbeReq), etc.)
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_scan(void *priv, struct wpa_driver_scan_params *params)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0, timeout;
	struct iw_scan_req req;
	const u8 *ssid = params->ssids[0].ssid;
	size_t ssid_len = params->ssids[0].ssid_len;

	if (ssid_len > IW_ESSID_MAX_SIZE) {
		wpa_printf(MSG_DEBUG, "%s: too long SSID (%lu)",
			   __FUNCTION__, (unsigned long) ssid_len);
		return -1;
	}

	//added for wps2.0 @20110519
	wpa_driver_wext_set_probe_req_ie(drv, params->extra_ies,
				params->extra_ies_len);

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);

	if (ssid && ssid_len) {
		os_memset(&req, 0, sizeof(req));
		req.essid_len = ssid_len;
		req.bssid.sa_family = ARPHRD_ETHER;
		os_memset(req.bssid.sa_data, 0xff, ETH_ALEN);
		os_memcpy(req.essid, ssid, ssid_len);
		iwr.u.data.pointer = (caddr_t) &req;
		iwr.u.data.length = sizeof(req);
		iwr.u.data.flags = IW_SCAN_THIS_ESSID;
	}

	if (ioctl(drv->ioctl_sock, SIOCSIWSCAN, &iwr) < 0) {
		perror("ioctl[SIOCSIWSCAN]");
		ret = -1;
	}

	/* Not all drivers generate "scan completed" wireless event, so try to
	 * read results after a timeout. */
	timeout = 5;
	if (drv->scan_complete_events) {
		/*
		 * The driver seems to deliver SIOCGIWSCAN events to notify
		 * when scan is complete, so use longer timeout to avoid race
		 * conditions with scanning and following association request.
		 */
		timeout = 30;
	}
	wpa_printf(MSG_DEBUG, "Scan requested (ret=%d) - scan timeout %d "
		   "seconds", ret, timeout);
	eloop_cancel_timeout(wpa_driver_wext_scan_timeout, drv, drv->ctx);
	eloop_register_timeout(timeout, 0, wpa_driver_wext_scan_timeout, drv,
			       drv->ctx);

	return ret;
}


static u8 * wpa_driver_wext_giwscan(struct wpa_driver_wext_data *drv,
				    size_t *len)
{
	struct iwreq iwr;
	u8 *res_buf;
	size_t res_buf_len;

	res_buf_len = IW_SCAN_MAX_DATA;
	for (;;) {
		res_buf = os_malloc(res_buf_len);
		if (res_buf == NULL)
			return NULL;
		os_memset(&iwr, 0, sizeof(iwr));
		os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
		iwr.u.data.pointer = res_buf;
		iwr.u.data.length = res_buf_len;

		if (ioctl(drv->ioctl_sock, SIOCGIWSCAN, &iwr) == 0)
			break;

		if (errno == E2BIG && res_buf_len < 65535) {
			os_free(res_buf);
			res_buf = NULL;
			res_buf_len *= 2;
			if (res_buf_len > 65535)
				res_buf_len = 65535; /* 16-bit length field */
			wpa_printf(MSG_DEBUG, "Scan results did not fit - "
				   "trying larger buffer (%lu bytes)",
				   (unsigned long) res_buf_len);
		} else {
			perror("ioctl[SIOCGIWSCAN]");
			os_free(res_buf);
			return NULL;
		}
	}

	if (iwr.u.data.length > res_buf_len) {
		os_free(res_buf);
		return NULL;
	}
	*len = iwr.u.data.length;

	return res_buf;
}


/*
 * Data structure for collecting WEXT scan results. This is needed to allow
 * the various methods of reporting IEs to be combined into a single IE buffer.
 */
struct wext_scan_data {
	struct wpa_scan_res res;
	u8 *ie;
	size_t ie_len;
	u8 ssid[32];
	size_t ssid_len;
	int maxrate;
};


static void wext_get_scan_mode(struct iw_event *iwe,
			       struct wext_scan_data *res)
{
	if (iwe->u.mode == IW_MODE_ADHOC)
		res->res.caps |= IEEE80211_CAP_IBSS;
	else if (iwe->u.mode == IW_MODE_MASTER || iwe->u.mode == IW_MODE_INFRA)
		res->res.caps |= IEEE80211_CAP_ESS;
}


static void wext_get_scan_ssid(struct iw_event *iwe,
			       struct wext_scan_data *res, char *custom,
			       char *end)
{
	int ssid_len = iwe->u.essid.length;
	if (custom + ssid_len > end)
		return;
	if (iwe->u.essid.flags &&
	    ssid_len > 0 &&
	    ssid_len <= IW_ESSID_MAX_SIZE) {
		os_memcpy(res->ssid, custom, ssid_len);
		res->ssid_len = ssid_len;
	}
}


static void wext_get_scan_freq(struct iw_event *iwe,
			       struct wext_scan_data *res)
{
	int divi = 1000000, i;

	if (iwe->u.freq.e == 0) {
		/*
		 * Some drivers do not report frequency, but a channel.
		 * Try to map this to frequency by assuming they are using
		 * IEEE 802.11b/g.  But don't overwrite a previously parsed
		 * frequency if the driver sends both frequency and channel,
		 * since the driver may be sending an A-band channel that we
		 * don't handle here.
		 */

		if (res->res.freq)
			return;

		if (iwe->u.freq.m >= 1 && iwe->u.freq.m <= 13) {
			res->res.freq = 2407 + 5 * iwe->u.freq.m;
			return;
		} else if (iwe->u.freq.m == 14) {
			res->res.freq = 2484;
			return;
		}
	}

	if (iwe->u.freq.e > 6) {
		wpa_printf(MSG_DEBUG, "Invalid freq in scan results (BSSID="
			   MACSTR " m=%d e=%d)",
			   MAC2STR(res->res.bssid), iwe->u.freq.m,
			   iwe->u.freq.e);
		return;
	}

	for (i = 0; i < iwe->u.freq.e; i++)
		divi /= 10;
	res->res.freq = iwe->u.freq.m / divi;
}


static void wext_get_scan_qual(struct wpa_driver_wext_data *drv,
			       struct iw_event *iwe,
			       struct wext_scan_data *res)
{
	res->res.qual = iwe->u.qual.qual;
	res->res.noise = iwe->u.qual.noise;
	res->res.level = iwe->u.qual.level;
	if (iwe->u.qual.updated & IW_QUAL_QUAL_INVALID)
		res->res.flags |= WPA_SCAN_QUAL_INVALID;
	if (iwe->u.qual.updated & IW_QUAL_LEVEL_INVALID)
		res->res.flags |= WPA_SCAN_LEVEL_INVALID;
	if (iwe->u.qual.updated & IW_QUAL_NOISE_INVALID)
		res->res.flags |= WPA_SCAN_NOISE_INVALID;
	if (iwe->u.qual.updated & IW_QUAL_DBM)
		res->res.flags |= WPA_SCAN_LEVEL_DBM;
	if ((iwe->u.qual.updated & IW_QUAL_DBM) ||
	    ((iwe->u.qual.level != 0) &&
	     (iwe->u.qual.level > drv->max_level))) {
		if (iwe->u.qual.level >= 64)
			res->res.level -= 0x100;
		if (iwe->u.qual.noise >= 64)
			res->res.noise -= 0x100;
	}
}


static void wext_get_scan_encode(struct iw_event *iwe,
				 struct wext_scan_data *res)
{
	if (!(iwe->u.data.flags & IW_ENCODE_DISABLED))
		res->res.caps |= IEEE80211_CAP_PRIVACY;
}


static void wext_get_scan_rate(struct iw_event *iwe,
			       struct wext_scan_data *res, char *pos,
			       char *end)
{
	int maxrate;
	char *custom = pos + IW_EV_LCP_LEN;
	struct iw_param p;
	size_t clen;

	clen = iwe->len;
	if (custom + clen > end)
		return;
	maxrate = 0;
	while (((ssize_t) clen) >= (ssize_t) sizeof(struct iw_param)) {
		/* Note: may be misaligned, make a local, aligned copy */
		os_memcpy(&p, custom, sizeof(struct iw_param));
		if (p.value > maxrate)
			maxrate = p.value;
		clen -= sizeof(struct iw_param);
		custom += sizeof(struct iw_param);
	}

	/* Convert the maxrate from WE-style (b/s units) to
	 * 802.11 rates (500000 b/s units).
	 */
	res->maxrate = maxrate / 500000;
}


static void wext_get_scan_iwevgenie(struct iw_event *iwe,
				    struct wext_scan_data *res, char *custom,
				    char *end)
{
	char *genie, *gpos, *gend;
	u8 *tmp;

	if (iwe->u.data.length == 0)
		return;

	gpos = genie = custom;
	gend = genie + iwe->u.data.length;
	if (gend > end) {
		wpa_printf(MSG_INFO, "IWEVGENIE overflow");
		return;
	}

	tmp = os_realloc(res->ie, res->ie_len + gend - gpos);
	if (tmp == NULL)
		return;
	os_memcpy(tmp + res->ie_len, gpos, gend - gpos);
	res->ie = tmp;
	res->ie_len += gend - gpos;
}


static void wext_get_scan_custom(struct iw_event *iwe,
				 struct wext_scan_data *res, char *custom,
				 char *end)
{
	size_t clen;
	u8 *tmp;

	clen = iwe->u.data.length;
	if (custom + clen > end)
		return;

	if (clen > 7 && os_strncmp(custom, "wpa_ie=", 7) == 0) {
		char *spos;
		int bytes;
		spos = custom + 7;
		bytes = custom + clen - spos;
		if (bytes & 1 || bytes == 0)
			return;
		bytes /= 2;
		tmp = os_realloc(res->ie, res->ie_len + bytes);
		if (tmp == NULL)
			return;
		res->ie = tmp;
		if (hexstr2bin(spos, tmp + res->ie_len, bytes) < 0)
			return;
		res->ie_len += bytes;
	} else if (clen > 7 && os_strncmp(custom, "rsn_ie=", 7) == 0) {
		char *spos;
		int bytes;
		spos = custom + 7;
		bytes = custom + clen - spos;
		if (bytes & 1 || bytes == 0)
			return;
		bytes /= 2;
		tmp = os_realloc(res->ie, res->ie_len + bytes);
		if (tmp == NULL)
			return;
		res->ie = tmp;
		if (hexstr2bin(spos, tmp + res->ie_len, bytes) < 0)
			return;
		res->ie_len += bytes;
	} else if (clen > 4 && os_strncmp(custom, "tsf=", 4) == 0) {
		char *spos;
		int bytes;
		u8 bin[8];
		spos = custom + 4;
		bytes = custom + clen - spos;
		if (bytes != 16) {
			wpa_printf(MSG_INFO, "Invalid TSF length (%d)", bytes);
			return;
		}
		bytes /= 2;
		if (hexstr2bin(spos, bin, bytes) < 0) {
			wpa_printf(MSG_DEBUG, "WEXT: Invalid TSF value");
			return;
		}
		res->res.tsf += WPA_GET_BE64(bin);
	}
}


static int wext_19_iw_point(struct wpa_driver_wext_data *drv, u16 cmd)
{
	return drv->we_version_compiled > 18 &&
		(cmd == SIOCGIWESSID || cmd == SIOCGIWENCODE ||
		 cmd == IWEVGENIE || cmd == IWEVCUSTOM);
}


static void wpa_driver_wext_add_scan_entry(struct wpa_scan_results *res,
					   struct wext_scan_data *data)
{
	struct wpa_scan_res **tmp;
	struct wpa_scan_res *r;
	size_t extra_len;
	u8 *pos, *end, *ssid_ie = NULL, *rate_ie = NULL;

	/* Figure out whether we need to fake any IEs */
	pos = data->ie;
	end = pos + data->ie_len;
	while (pos && pos + 1 < end) {
		if (pos + 2 + pos[1] > end)
			break;
		if (pos[0] == WLAN_EID_SSID)
			ssid_ie = pos;
		else if (pos[0] == WLAN_EID_SUPP_RATES)
			rate_ie = pos;
		else if (pos[0] == WLAN_EID_EXT_SUPP_RATES)
			rate_ie = pos;
		pos += 2 + pos[1];
	}

	extra_len = 0;
	if (ssid_ie == NULL)
		extra_len += 2 + data->ssid_len;
	if (rate_ie == NULL && data->maxrate)
		extra_len += 3;

	r = os_zalloc(sizeof(*r) + extra_len + data->ie_len);
	if (r == NULL)
		return;
	os_memcpy(r, &data->res, sizeof(*r));
	r->ie_len = extra_len + data->ie_len;
	pos = (u8 *) (r + 1);
	if (ssid_ie == NULL) {
		/*
		 * Generate a fake SSID IE since the driver did not report
		 * a full IE list.
		 */
		*pos++ = WLAN_EID_SSID;
		*pos++ = data->ssid_len;
		os_memcpy(pos, data->ssid, data->ssid_len);
		pos += data->ssid_len;
	}
	if (rate_ie == NULL && data->maxrate) {
		/*
		 * Generate a fake Supported Rates IE since the driver did not
		 * report a full IE list.
		 */
		*pos++ = WLAN_EID_SUPP_RATES;
		*pos++ = 1;
		*pos++ = data->maxrate;
	}
	if (data->ie)
		os_memcpy(pos, data->ie, data->ie_len);

	tmp = os_realloc(res->res,
			 (res->num + 1) * sizeof(struct wpa_scan_res *));
	if (tmp == NULL) {
		os_free(r);
		return;
	}
	tmp[res->num++] = r;
	res->res = tmp;
}
				      

/**
 * wpa_driver_wext_get_scan_results - Fetch the latest scan results
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * Returns: Scan results on success, -1 on failure
 */
struct wpa_scan_results * wpa_driver_wext_get_scan_results(void *priv)
{
	struct wpa_driver_wext_data *drv = priv;
	size_t ap_num = 0, len;
	int first;
	u8 *res_buf;
	struct iw_event iwe_buf, *iwe = &iwe_buf;
	char *pos, *end, *custom;
	struct wpa_scan_results *res;
	struct wext_scan_data data;

	res_buf = wpa_driver_wext_giwscan(drv, &len);
	if (res_buf == NULL)
		return NULL;

	ap_num = 0;
	first = 1;

	res = os_zalloc(sizeof(*res));
	if (res == NULL) {
		os_free(res_buf);
		return NULL;
	}

	pos = (char *) res_buf;
	end = (char *) res_buf + len;
	os_memset(&data, 0, sizeof(data));

	while (pos + IW_EV_LCP_LEN <= end) {
		/* Event data may be unaligned, so make a local, aligned copy
		 * before processing. */
		os_memcpy(&iwe_buf, pos, IW_EV_LCP_LEN);
		if (iwe->len <= IW_EV_LCP_LEN)
			break;

		custom = pos + IW_EV_POINT_LEN;
		if (wext_19_iw_point(drv, iwe->cmd)) {
			/* WE-19 removed the pointer from struct iw_point */
			char *dpos = (char *) &iwe_buf.u.data.length;
			int dlen = dpos - (char *) &iwe_buf;
			os_memcpy(dpos, pos + IW_EV_LCP_LEN,
				  sizeof(struct iw_event) - dlen);
		} else {
			os_memcpy(&iwe_buf, pos, sizeof(struct iw_event));
			custom += IW_EV_POINT_OFF;
		}

		switch (iwe->cmd) {
		case SIOCGIWAP:
			if (!first)
				wpa_driver_wext_add_scan_entry(res, &data);
			first = 0;
			os_free(data.ie);
			os_memset(&data, 0, sizeof(data));
			os_memcpy(data.res.bssid,
				  iwe->u.ap_addr.sa_data, ETH_ALEN);
			break;
		case SIOCGIWMODE:
			wext_get_scan_mode(iwe, &data);
			break;
		case SIOCGIWESSID:
			wext_get_scan_ssid(iwe, &data, custom, end);
			break;
		case SIOCGIWFREQ:
			wext_get_scan_freq(iwe, &data);
			break;
		case IWEVQUAL:
			wext_get_scan_qual(drv, iwe, &data);
			break;
		case SIOCGIWENCODE:
			wext_get_scan_encode(iwe, &data);
			break;
		case SIOCGIWRATE:
			wext_get_scan_rate(iwe, &data, pos, end);
			break;
		case IWEVGENIE:
			wext_get_scan_iwevgenie(iwe, &data, custom, end);
			break;
		case IWEVCUSTOM:
			wext_get_scan_custom(iwe, &data, custom, end);
			break;
		}

		pos += iwe->len;
	}
	os_free(res_buf);
	res_buf = NULL;
	if (!first)
		wpa_driver_wext_add_scan_entry(res, &data);
	os_free(data.ie);

	wpa_printf(MSG_DEBUG, "Received %lu bytes of scan results (%lu BSSes)",
		   (unsigned long) len, (unsigned long) res->num);

	return res;
}


static int wpa_driver_wext_get_range(void *priv)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iw_range *range;
	struct iwreq iwr;
	int minlen;
	size_t buflen;

	/*
	 * Use larger buffer than struct iw_range in order to allow the
	 * structure to grow in the future.
	 */
	buflen = sizeof(struct iw_range) + 500;
	range = os_zalloc(buflen);
	if (range == NULL)
		return -1;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.data.pointer = (caddr_t) range;
	iwr.u.data.length = buflen;

	minlen = ((char *) &range->enc_capa) - (char *) range +
		sizeof(range->enc_capa);

	if (ioctl(drv->ioctl_sock, SIOCGIWRANGE, &iwr) < 0) {
		perror("ioctl[SIOCGIWRANGE]");
		os_free(range);
		return -1;
	} else if (iwr.u.data.length >= minlen &&
		   range->we_version_compiled >= 18) {
		wpa_printf(MSG_DEBUG, "SIOCGIWRANGE: WE(compiled)=%d "
			   "WE(source)=%d enc_capa=0x%x",
			   range->we_version_compiled,
			   range->we_version_source,
			   range->enc_capa);
		drv->has_capability = 1;
		drv->we_version_compiled = range->we_version_compiled;
		if (range->enc_capa & IW_ENC_CAPA_WPA) {
			drv->capa.key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_WPA |
				WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK;
		}
		if (range->enc_capa & IW_ENC_CAPA_WPA2) {
			drv->capa.key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
				WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK;
		}
		drv->capa.enc |= WPA_DRIVER_CAPA_ENC_WEP40 |
			WPA_DRIVER_CAPA_ENC_WEP104;
		if (range->enc_capa & IW_ENC_CAPA_CIPHER_TKIP)
			drv->capa.enc |= WPA_DRIVER_CAPA_ENC_TKIP;
		if (range->enc_capa & IW_ENC_CAPA_CIPHER_CCMP)
			drv->capa.enc |= WPA_DRIVER_CAPA_ENC_CCMP;
		if (range->enc_capa & IW_ENC_CAPA_4WAY_HANDSHAKE)
			drv->capa.flags |= WPA_DRIVER_FLAGS_4WAY_HANDSHAKE;
		drv->capa.auth = WPA_DRIVER_AUTH_OPEN |
			WPA_DRIVER_AUTH_SHARED |
			WPA_DRIVER_AUTH_LEAP;
		drv->capa.max_scan_ssids = 1;

		wpa_printf(MSG_DEBUG, "  capabilities: key_mgmt 0x%x enc 0x%x "
			   "flags 0x%x",
			   drv->capa.key_mgmt, drv->capa.enc, drv->capa.flags);
	} else {
		wpa_printf(MSG_DEBUG, "SIOCGIWRANGE: too old (short) data - "
			   "assuming WPA is not supported");
	}

	drv->max_level = range->max_qual.level;

	os_free(range);
	return 0;
}


static int wpa_driver_wext_set_psk(struct wpa_driver_wext_data *drv,
				   const u8 *psk)
{
	struct iw_encode_ext *ext;
	struct iwreq iwr;
	int ret;

	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);

	if (!(drv->capa.flags & WPA_DRIVER_FLAGS_4WAY_HANDSHAKE))
		return 0;

	if (!psk)
		return 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);

	ext = os_zalloc(sizeof(*ext) + PMK_LEN);
	if (ext == NULL)
		return -1;

	iwr.u.encoding.pointer = (caddr_t) ext;
	iwr.u.encoding.length = sizeof(*ext) + PMK_LEN;
	ext->key_len = PMK_LEN;
	os_memcpy(&ext->key, psk, ext->key_len);
	ext->alg = IW_ENCODE_ALG_PMK;

	ret = ioctl(drv->ioctl_sock, SIOCSIWENCODEEXT, &iwr);
	if (ret < 0)
		perror("ioctl[SIOCSIWENCODEEXT] PMK");
	os_free(ext);

	return ret;
}


static int wpa_driver_wext_set_key_ext(void *priv, enum wpa_alg alg,
				       const u8 *addr, int key_idx,
				       int set_tx, const u8 *seq,
				       size_t seq_len,
				       const u8 *key, size_t key_len)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;
	struct iw_encode_ext *ext;

	if (seq_len > IW_ENCODE_SEQ_MAX_SIZE) {
		wpa_printf(MSG_DEBUG, "%s: Invalid seq_len %lu",
			   __FUNCTION__, (unsigned long) seq_len);
		return -1;
	}

	ext = os_zalloc(sizeof(*ext) + key_len);
	if (ext == NULL)
		return -1;
	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.encoding.flags = key_idx + 1;
	iwr.u.encoding.flags |= IW_ENCODE_TEMP;
	if (alg == WPA_ALG_NONE)
		iwr.u.encoding.flags |= IW_ENCODE_DISABLED;
	iwr.u.encoding.pointer = (caddr_t) ext;
	iwr.u.encoding.length = sizeof(*ext) + key_len;

	if (addr == NULL || is_broadcast_ether_addr(addr))
		ext->ext_flags |= IW_ENCODE_EXT_GROUP_KEY;
	if (set_tx)
		ext->ext_flags |= IW_ENCODE_EXT_SET_TX_KEY;

	ext->addr.sa_family = ARPHRD_ETHER;
	if (addr)
		os_memcpy(ext->addr.sa_data, addr, ETH_ALEN);
	else
		os_memset(ext->addr.sa_data, 0xff, ETH_ALEN);
	if (key && key_len) {
		os_memcpy(ext + 1, key, key_len);
		ext->key_len = key_len;
	}
	switch (alg) {
	case WPA_ALG_NONE:
		ext->alg = IW_ENCODE_ALG_NONE;
		break;
	case WPA_ALG_WEP:
		ext->alg = IW_ENCODE_ALG_WEP;
		break;
	case WPA_ALG_TKIP:
		ext->alg = IW_ENCODE_ALG_TKIP;
		break;
	case WPA_ALG_CCMP:
		ext->alg = IW_ENCODE_ALG_CCMP;
		break;
	case WPA_ALG_PMK:
		ext->alg = IW_ENCODE_ALG_PMK;
		break;
#ifdef CONFIG_IEEE80211W
	case WPA_ALG_IGTK:
		ext->alg = IW_ENCODE_ALG_AES_CMAC;
		break;
#endif /* CONFIG_IEEE80211W */
	default:
		wpa_printf(MSG_DEBUG, "%s: Unknown algorithm %d",
			   __FUNCTION__, alg);
		os_free(ext);
		return -1;
	}

	if (seq && seq_len) {
		ext->ext_flags |= IW_ENCODE_EXT_RX_SEQ_VALID;
		os_memcpy(ext->rx_seq, seq, seq_len);
	}

	if (ioctl(drv->ioctl_sock, SIOCSIWENCODEEXT, &iwr) < 0) {
		ret = errno == EOPNOTSUPP ? -2 : -1;
		if (errno == ENODEV) {
			/*
			 * ndiswrapper seems to be returning incorrect error
			 * code.. */
			ret = -2;
		}

		perror("ioctl[SIOCSIWENCODEEXT]");
	}

	os_free(ext);
	return ret;
}


/**
 * wpa_driver_wext_set_key - Configure encryption key
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @priv: Private driver interface data
 * @alg: Encryption algorithm (%WPA_ALG_NONE, %WPA_ALG_WEP,
 *	%WPA_ALG_TKIP, %WPA_ALG_CCMP); %WPA_ALG_NONE clears the key.
 * @addr: Address of the peer STA or ff:ff:ff:ff:ff:ff for
 *	broadcast/default keys
 * @key_idx: key index (0..3), usually 0 for unicast keys
 * @set_tx: Configure this key as the default Tx key (only used when
 *	driver does not support separate unicast/individual key
 * @seq: Sequence number/packet number, seq_len octets, the next
 *	packet number to be used for in replay protection; configured
 *	for Rx keys (in most cases, this is only used with broadcast
 *	keys and set to zero for unicast keys)
 * @seq_len: Length of the seq, depends on the algorithm:
 *	TKIP: 6 octets, CCMP: 6 octets
 * @key: Key buffer; TKIP: 16-byte temporal key, 8-byte Tx Mic key,
 *	8-byte Rx Mic Key
 * @key_len: Length of the key buffer in octets (WEP: 5 or 13,
 *	TKIP: 32, CCMP: 16)
 * Returns: 0 on success, -1 on failure
 *
 * This function uses SIOCSIWENCODEEXT by default, but tries to use
 * SIOCSIWENCODE if the extended ioctl fails when configuring a WEP key.
 */
int wpa_driver_wext_set_key(const char *ifname, void *priv, enum wpa_alg alg,
			    const u8 *addr, int key_idx,
			    int set_tx, const u8 *seq, size_t seq_len,
			    const u8 *key, size_t key_len)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	wpa_printf(MSG_DEBUG, "%s: alg=%d key_idx=%d set_tx=%d seq_len=%lu "
		   "key_len=%lu",
		   __FUNCTION__, alg, key_idx, set_tx,
		   (unsigned long) seq_len, (unsigned long) key_len);

	ret = wpa_driver_wext_set_key_ext(drv, alg, addr, key_idx, set_tx,
					  seq, seq_len, key, key_len);
	if (ret == 0)
		return 0;

	if (ret == -2 &&
	    (alg == WPA_ALG_NONE || alg == WPA_ALG_WEP)) {
		wpa_printf(MSG_DEBUG, "Driver did not support "
			   "SIOCSIWENCODEEXT, trying SIOCSIWENCODE");
		ret = 0;
	} else {
		wpa_printf(MSG_DEBUG, "Driver did not support "
			   "SIOCSIWENCODEEXT");
		return ret;
	}

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.encoding.flags = key_idx + 1;
	iwr.u.encoding.flags |= IW_ENCODE_TEMP;
	if (alg == WPA_ALG_NONE)
		iwr.u.encoding.flags |= IW_ENCODE_DISABLED;
	iwr.u.encoding.pointer = (caddr_t) key;
	iwr.u.encoding.length = key_len;

	if (ioctl(drv->ioctl_sock, SIOCSIWENCODE, &iwr) < 0) {
		perror("ioctl[SIOCSIWENCODE]");
		ret = -1;
	}

	if (set_tx && alg != WPA_ALG_NONE) {
		os_memset(&iwr, 0, sizeof(iwr));
		os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
		iwr.u.encoding.flags = key_idx + 1;
		iwr.u.encoding.flags |= IW_ENCODE_TEMP;
		iwr.u.encoding.pointer = (caddr_t) NULL;
		iwr.u.encoding.length = 0;
		if (ioctl(drv->ioctl_sock, SIOCSIWENCODE, &iwr) < 0) {
			perror("ioctl[SIOCSIWENCODE] (set_tx)");
			ret = -1;
		}
	}

	return ret;
}


static int wpa_driver_wext_set_countermeasures(void *priv,
					       int enabled)
{
	struct wpa_driver_wext_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	return wpa_driver_wext_set_auth_param(drv,
					      IW_AUTH_TKIP_COUNTERMEASURES,
					      enabled);
}


static int wpa_driver_wext_set_drop_unencrypted(void *priv,
						int enabled)
{
	struct wpa_driver_wext_data *drv = priv;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	drv->use_crypt = enabled;
	return wpa_driver_wext_set_auth_param(drv, IW_AUTH_DROP_UNENCRYPTED,
					      enabled);
}


static int wpa_driver_wext_mlme(struct wpa_driver_wext_data *drv,
				const u8 *addr, int cmd, int reason_code)
{
	struct iwreq iwr;
	struct iw_mlme mlme;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	os_memset(&mlme, 0, sizeof(mlme));
	mlme.cmd = cmd;
	mlme.reason_code = reason_code;
	mlme.addr.sa_family = ARPHRD_ETHER;
	os_memcpy(mlme.addr.sa_data, addr, ETH_ALEN);
	iwr.u.data.pointer = (caddr_t) &mlme;
	iwr.u.data.length = sizeof(mlme);

	if (ioctl(drv->ioctl_sock, SIOCSIWMLME, &iwr) < 0) {
		perror("ioctl[SIOCSIWMLME]");
		ret = -1;
	}

	return ret;
}


static void wpa_driver_wext_disconnect(struct wpa_driver_wext_data *drv)
{
	struct iwreq iwr;
	const u8 null_bssid[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };
	u8 ssid[32];
	int i;

	/*
	 * Only force-disconnect when the card is in infrastructure mode,
	 * otherwise the driver might interpret the cleared BSSID and random
	 * SSID as an attempt to create a new ad-hoc network.
	 */
	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	if (ioctl(drv->ioctl_sock, SIOCGIWMODE, &iwr) < 0) {
		perror("ioctl[SIOCGIWMODE]");
		iwr.u.mode = IW_MODE_INFRA;
	}

	if (iwr.u.mode == IW_MODE_INFRA) {
		if (drv->cfg80211) {
			/*
			 * cfg80211 supports SIOCSIWMLME commands, so there is
			 * no need for the random SSID hack, but clear the
			 * BSSID and SSID.
			 */
			if (wpa_driver_wext_set_bssid(drv, null_bssid) < 0 ||
			    wpa_driver_wext_set_ssid(drv, (u8 *) "", 0) < 0) {
				wpa_printf(MSG_DEBUG, "WEXT: Failed to clear "
					   "to disconnect");
			}
			return;
		}
		/*
		 * Clear the BSSID selection and set a random SSID to make sure
		 * the driver will not be trying to associate with something
		 * even if it does not understand SIOCSIWMLME commands (or
		 * tries to associate automatically after deauth/disassoc).
		 */
		for (i = 0; i < 32; i++)
			ssid[i] = rand() & 0xFF;
		if (wpa_driver_wext_set_bssid(drv, null_bssid) < 0 ||
		    wpa_driver_wext_set_ssid(drv, ssid, 32) < 0) {
			wpa_printf(MSG_DEBUG, "WEXT: Failed to set bogus "
				   "BSSID/SSID to disconnect");
		}
	}
}


static int wpa_driver_wext_deauthenticate(void *priv, const u8 *addr,
					  int reason_code)
{
	struct wpa_driver_wext_data *drv = priv;
	int ret;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	ret = wpa_driver_wext_mlme(drv, addr, IW_MLME_DEAUTH, reason_code);
	wpa_driver_wext_disconnect(drv);
	return ret;
}


static int wpa_driver_wext_disassociate(void *priv, const u8 *addr,
					int reason_code)
{
	struct wpa_driver_wext_data *drv = priv;
	int ret;
	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);
	ret = wpa_driver_wext_mlme(drv, addr, IW_MLME_DISASSOC, reason_code);
	wpa_driver_wext_disconnect(drv);
	return ret;
}


static int wpa_driver_wext_set_gen_ie(void *priv, const u8 *ie,
				      size_t ie_len)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.data.pointer = (caddr_t) ie;
	iwr.u.data.length = ie_len;

	if (ioctl(drv->ioctl_sock, SIOCSIWGENIE, &iwr) < 0) {
		perror("ioctl[SIOCSIWGENIE]");
		ret = -1;
	}

	return ret;
}


int wpa_driver_wext_cipher2wext(int cipher)
{
	switch (cipher) {
	case CIPHER_NONE:
		return IW_AUTH_CIPHER_NONE;
	case CIPHER_WEP40:
		return IW_AUTH_CIPHER_WEP40;
	case CIPHER_TKIP:
		return IW_AUTH_CIPHER_TKIP;
	case CIPHER_CCMP:
		return IW_AUTH_CIPHER_CCMP;
	case CIPHER_WEP104:
		return IW_AUTH_CIPHER_WEP104;
	default:
		return 0;
	}
}


int wpa_driver_wext_keymgmt2wext(int keymgmt)
{
	switch (keymgmt) {
	case KEY_MGMT_802_1X:
	case KEY_MGMT_802_1X_NO_WPA:
		return IW_AUTH_KEY_MGMT_802_1X;
	case KEY_MGMT_PSK:
		return IW_AUTH_KEY_MGMT_PSK;
	default:
		return 0;
	}
}


static int
wpa_driver_wext_auth_alg_fallback(struct wpa_driver_wext_data *drv,
				  struct wpa_driver_associate_params *params)
{
	struct iwreq iwr;
	int ret = 0;

	wpa_printf(MSG_DEBUG, "WEXT: Driver did not support "
		   "SIOCSIWAUTH for AUTH_ALG, trying SIOCSIWENCODE");

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	/* Just changing mode, not actual keys */
	iwr.u.encoding.flags = 0;
	iwr.u.encoding.pointer = (caddr_t) NULL;
	iwr.u.encoding.length = 0;

	/*
	 * Note: IW_ENCODE_{OPEN,RESTRICTED} can be interpreted to mean two
	 * different things. Here they are used to indicate Open System vs.
	 * Shared Key authentication algorithm. However, some drivers may use
	 * them to select between open/restricted WEP encrypted (open = allow
	 * both unencrypted and encrypted frames; restricted = only allow
	 * encrypted frames).
	 */

	if (!drv->use_crypt) {
		iwr.u.encoding.flags |= IW_ENCODE_DISABLED;
	} else {
		if (params->auth_alg & WPA_AUTH_ALG_OPEN)
			iwr.u.encoding.flags |= IW_ENCODE_OPEN;
		if (params->auth_alg & WPA_AUTH_ALG_SHARED)
			iwr.u.encoding.flags |= IW_ENCODE_RESTRICTED;
	}

	if (ioctl(drv->ioctl_sock, SIOCSIWENCODE, &iwr) < 0) {
		perror("ioctl[SIOCSIWENCODE]");
		ret = -1;
	}

	return ret;
}


int wpa_driver_wext_associate(void *priv,
			      struct wpa_driver_associate_params *params)
{
	struct wpa_driver_wext_data *drv = priv;
	int ret = 0;
	int allow_unencrypted_eapol;
	int value;

	wpa_printf(MSG_DEBUG, "%s", __FUNCTION__);

	if (drv->cfg80211) {
		/*
		 * Stop cfg80211 from trying to associate before we are done
		 * with all parameters.
		 */
		wpa_driver_wext_set_ssid(drv, (u8 *) "", 0);
	}

	if (wpa_driver_wext_set_drop_unencrypted(drv, params->drop_unencrypted)
	    < 0)
		ret = -1;
	if (wpa_driver_wext_set_auth_alg(drv, params->auth_alg) < 0)
		ret = -1;
	if (wpa_driver_wext_set_mode(drv, params->mode) < 0)
		ret = -1;

	/*
	 * If the driver did not support SIOCSIWAUTH, fallback to
	 * SIOCSIWENCODE here.
	 */
	if (drv->auth_alg_fallback &&
	    wpa_driver_wext_auth_alg_fallback(drv, params) < 0)
		ret = -1;

	if (!params->bssid &&
	    wpa_driver_wext_set_bssid(drv, NULL) < 0)
		ret = -1;

	/* TODO: should consider getting wpa version and cipher/key_mgmt suites
	 * from configuration, not from here, where only the selected suite is
	 * available */
	if (wpa_driver_wext_set_gen_ie(drv, params->wpa_ie, params->wpa_ie_len)
	    < 0)
		ret = -1;
	if (params->wpa_ie == NULL || params->wpa_ie_len == 0)
		value = IW_AUTH_WPA_VERSION_DISABLED;
	else if (params->wpa_ie[0] == WLAN_EID_RSN)
		value = IW_AUTH_WPA_VERSION_WPA2;
	else
		value = IW_AUTH_WPA_VERSION_WPA;
	if (wpa_driver_wext_set_auth_param(drv,
					   IW_AUTH_WPA_VERSION, value) < 0)
		ret = -1;
	value = wpa_driver_wext_cipher2wext(params->pairwise_suite);
	if (wpa_driver_wext_set_auth_param(drv,
					   IW_AUTH_CIPHER_PAIRWISE, value) < 0)
		ret = -1;
	value = wpa_driver_wext_cipher2wext(params->group_suite);
	if (wpa_driver_wext_set_auth_param(drv,
					   IW_AUTH_CIPHER_GROUP, value) < 0)
		ret = -1;
	value = wpa_driver_wext_keymgmt2wext(params->key_mgmt_suite);
	if (wpa_driver_wext_set_auth_param(drv,
					   IW_AUTH_KEY_MGMT, value) < 0)
		ret = -1;
	value = params->key_mgmt_suite != KEY_MGMT_NONE ||
		params->pairwise_suite != CIPHER_NONE ||
		params->group_suite != CIPHER_NONE ||
		params->wpa_ie_len;
	if (wpa_driver_wext_set_auth_param(drv,
					   IW_AUTH_PRIVACY_INVOKED, value) < 0)
		ret = -1;

	/* Allow unencrypted EAPOL messages even if pairwise keys are set when
	 * not using WPA. IEEE 802.1X specifies that these frames are not
	 * encrypted, but WPA encrypts them when pairwise keys are in use. */
	if (params->key_mgmt_suite == KEY_MGMT_802_1X ||
	    params->key_mgmt_suite == KEY_MGMT_PSK)
		allow_unencrypted_eapol = 0;
	else
		allow_unencrypted_eapol = 1;

	if (wpa_driver_wext_set_psk(drv, params->psk) < 0)
		ret = -1;
	if (wpa_driver_wext_set_auth_param(drv,
					   IW_AUTH_RX_UNENCRYPTED_EAPOL,
					   allow_unencrypted_eapol) < 0)
		ret = -1;
#ifdef CONFIG_IEEE80211W
	switch (params->mgmt_frame_protection) {
	case NO_MGMT_FRAME_PROTECTION:
		value = IW_AUTH_MFP_DISABLED;
		break;
	case MGMT_FRAME_PROTECTION_OPTIONAL:
		value = IW_AUTH_MFP_OPTIONAL;
		break;
	case MGMT_FRAME_PROTECTION_REQUIRED:
		value = IW_AUTH_MFP_REQUIRED;
		break;
	};
	if (wpa_driver_wext_set_auth_param(drv, IW_AUTH_MFP, value) < 0)
		ret = -1;
#endif /* CONFIG_IEEE80211W */
	if (params->freq && wpa_driver_wext_set_freq(drv, params->freq) < 0)
		ret = -1;
	if (!drv->cfg80211 &&
	    wpa_driver_wext_set_ssid(drv, params->ssid, params->ssid_len) < 0)
		ret = -1;
	if (params->bssid &&
	    wpa_driver_wext_set_bssid(drv, params->bssid) < 0)
		ret = -1;
	if (drv->cfg80211 &&
	    wpa_driver_wext_set_ssid(drv, params->ssid, params->ssid_len) < 0)
		ret = -1;

	return ret;
}


static int wpa_driver_wext_set_auth_alg(void *priv, int auth_alg)
{
	struct wpa_driver_wext_data *drv = priv;
	int algs = 0, res;

	if (auth_alg & WPA_AUTH_ALG_OPEN)
		algs |= IW_AUTH_ALG_OPEN_SYSTEM;
	if (auth_alg & WPA_AUTH_ALG_SHARED)
		algs |= IW_AUTH_ALG_SHARED_KEY;
	if (auth_alg & WPA_AUTH_ALG_LEAP)
		algs |= IW_AUTH_ALG_LEAP;
	if (algs == 0) {
		/* at least one algorithm should be set */
		algs = IW_AUTH_ALG_OPEN_SYSTEM;
	}

	res = wpa_driver_wext_set_auth_param(drv, IW_AUTH_80211_AUTH_ALG,
					     algs);
	drv->auth_alg_fallback = res == -2;
	return res;
}


/**
 * wpa_driver_wext_set_mode - Set wireless mode (infra/adhoc), SIOCSIWMODE
 * @priv: Pointer to private wext data from wpa_driver_wext_init()
 * @mode: 0 = infra/BSS (associate with an AP), 1 = adhoc/IBSS
 * Returns: 0 on success, -1 on failure
 */
int wpa_driver_wext_set_mode(void *priv, int mode)
{
	struct wpa_driver_wext_data *drv = priv;
	struct iwreq iwr;
	int ret = -1;
	unsigned int new_mode = mode ? IW_MODE_ADHOC : IW_MODE_INFRA;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	iwr.u.mode = new_mode;
	if (ioctl(drv->ioctl_sock, SIOCSIWMODE, &iwr) == 0) {
		ret = 0;
		goto done;
	}

	if (errno != EBUSY) {
		perror("ioctl[SIOCSIWMODE]");
		goto done;
	}

	/* mac80211 doesn't allow mode changes while the device is up, so if
	 * the device isn't in the mode we're about to change to, take device
	 * down, try to set the mode again, and bring it back up.
	 */
	if (ioctl(drv->ioctl_sock, SIOCGIWMODE, &iwr) < 0) {
		perror("ioctl[SIOCGIWMODE]");
		goto done;
	}

	if (iwr.u.mode == new_mode) {
		ret = 0;
		goto done;
	}

	if (linux_set_iface_flags(drv->ioctl_sock, drv->ifname, 0) == 0) {
		/* Try to set the mode again while the interface is down */
		iwr.u.mode = new_mode;
		if (ioctl(drv->ioctl_sock, SIOCSIWMODE, &iwr) < 0)
			perror("ioctl[SIOCSIWMODE]");
		else
			ret = 0;

		(void) linux_set_iface_flags(drv->ioctl_sock, drv->ifname, 1);
	}

done:
	return ret;
}


static int wpa_driver_wext_pmksa(struct wpa_driver_wext_data *drv,
				 u32 cmd, const u8 *bssid, const u8 *pmkid)
{
	struct iwreq iwr;
	struct iw_pmksa pmksa;
	int ret = 0;

	os_memset(&iwr, 0, sizeof(iwr));
	os_strlcpy(iwr.ifr_name, drv->ifname, IFNAMSIZ);
	os_memset(&pmksa, 0, sizeof(pmksa));
	pmksa.cmd = cmd;
	pmksa.bssid.sa_family = ARPHRD_ETHER;
	if (bssid)
		os_memcpy(pmksa.bssid.sa_data, bssid, ETH_ALEN);
	if (pmkid)
		os_memcpy(pmksa.pmkid, pmkid, IW_PMKID_LEN);
	iwr.u.data.pointer = (caddr_t) &pmksa;
	iwr.u.data.length = sizeof(pmksa);

	if (ioctl(drv->ioctl_sock, SIOCSIWPMKSA, &iwr) < 0) {
		if (errno != EOPNOTSUPP)
			perror("ioctl[SIOCSIWPMKSA]");
		ret = -1;
	}

	return ret;
}


static int wpa_driver_wext_add_pmkid(void *priv, const u8 *bssid,
				     const u8 *pmkid)
{
	struct wpa_driver_wext_data *drv = priv;
	return wpa_driver_wext_pmksa(drv, IW_PMKSA_ADD, bssid, pmkid);
}


static int wpa_driver_wext_remove_pmkid(void *priv, const u8 *bssid,
		 			const u8 *pmkid)
{
	struct wpa_driver_wext_data *drv = priv;
	return wpa_driver_wext_pmksa(drv, IW_PMKSA_REMOVE, bssid, pmkid);
}


static int wpa_driver_wext_flush_pmkid(void *priv)
{
	struct wpa_driver_wext_data *drv = priv;
	return wpa_driver_wext_pmksa(drv, IW_PMKSA_FLUSH, NULL, NULL);
}


int wpa_driver_wext_get_capa(void *priv, struct wpa_driver_capa *capa)
{
	struct wpa_driver_wext_data *drv = priv;
	if (!drv->has_capability)
		return -1;
	os_memcpy(capa, &drv->capa, sizeof(*capa));
	return 0;
}


int wpa_driver_wext_alternative_ifindex(struct wpa_driver_wext_data *drv,
					const char *ifname)
{
	if (ifname == NULL) {
		drv->ifindex2 = -1;
		return 0;
	}

	drv->ifindex2 = if_nametoindex(ifname);
	if (drv->ifindex2 <= 0)
		return -1;

	wpa_printf(MSG_DEBUG, "Added alternative ifindex %d (%s) for "
		   "wireless events", drv->ifindex2, ifname);

	return 0;
}


int wpa_driver_wext_set_operstate(void *priv, int state)
{
	struct wpa_driver_wext_data *drv = priv;

	wpa_printf(MSG_DEBUG, "%s: operstate %d->%d (%s)",
		   __func__, drv->operstate, state, state ? "UP" : "DORMANT");
	drv->operstate = state;
	return netlink_send_oper_ifla(drv->netlink, drv->ifindex, -1,
				      state ? IF_OPER_UP : IF_OPER_DORMANT);
}


int wpa_driver_wext_get_version(struct wpa_driver_wext_data *drv)
{
	return drv->we_version_compiled;
}


static const char * wext_get_radio_name(void *priv)
{
	struct wpa_driver_wext_data *drv = priv;
	return drv->phyname;
}


const struct wpa_driver_ops wpa_driver_wext_ops = {
	.name = "wext",
	.desc = "Linux wireless extensions (generic)",
	.get_bssid = wpa_driver_wext_get_bssid,
	.get_ssid = wpa_driver_wext_get_ssid,
	.set_key = wpa_driver_wext_set_key,
	.set_countermeasures = wpa_driver_wext_set_countermeasures,
	.scan2 = wpa_driver_wext_scan,
	.get_scan_results2 = wpa_driver_wext_get_scan_results,
	.deauthenticate = wpa_driver_wext_deauthenticate,
	.disassociate = wpa_driver_wext_disassociate,
	.associate = wpa_driver_wext_associate,
	.init = wpa_driver_wext_init,
	.deinit = wpa_driver_wext_deinit,
	.add_pmkid = wpa_driver_wext_add_pmkid,
	.remove_pmkid = wpa_driver_wext_remove_pmkid,
	.flush_pmkid = wpa_driver_wext_flush_pmkid,
	.get_capa = wpa_driver_wext_get_capa,
	.set_operstate = wpa_driver_wext_set_operstate,
	.get_radio_name = wext_get_radio_name,
};
