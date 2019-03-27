/*
 * UPnP WPS Device - Web connections
 * Copyright (c) 2000-2003 Intel Corporation
 * Copyright (c) 2006-2007 Sony Corporation
 * Copyright (c) 2008-2009 Atheros Communications
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * See wps_upnp.c for more details on licensing and code history.
 */

#include "includes.h"

#include "common.h"
#include "base64.h"
#include "uuid.h"
#include "httpread.h"
#include "http_server.h"
#include "wps_i.h"
#include "wps_upnp.h"
#include "wps_upnp_i.h"
#include "upnp_xml.h"

/***************************************************************************
 * Web connections (we serve pages of info about ourselves, handle
 * requests, etc. etc.).
 **************************************************************************/

#define WEB_CONNECTION_TIMEOUT_SEC 30   /* Drop web connection after t.o. */
#define WEB_CONNECTION_MAX_READ 8000    /* Max we'll read for TCP request */
#define MAX_WEB_CONNECTIONS 10          /* max simultaneous web connects */


static const char *urn_wfawlanconfig =
	"urn:schemas-wifialliance-org:service:WFAWLANConfig:1";
static const char *http_server_hdr =
	"Server: unspecified, UPnP/1.0, unspecified\r\n";
static const char *http_connection_close =
	"Connection: close\r\n";

/*
 * "Files" that we serve via HTTP. The format of these files is given by
 * WFA WPS specifications. Extra white space has been removed to save space.
 */

static const char wps_scpd_xml[] =
"<?xml version=\"1.0\"?>\n"
"<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n"
"<specVersion><major>1</major><minor>0</minor></specVersion>\n"
"<actionList>\n"
"<action>\n"
"<name>GetDeviceInfo</name>\n"
"<argumentList>\n"
"<argument>\n"
"<name>NewDeviceInfo</name>\n"
"<direction>out</direction>\n"
"<relatedStateVariable>DeviceInfo</relatedStateVariable>\n"
"</argument>\n"
"</argumentList>\n"
"</action>\n"
"<action>\n"
"<name>PutMessage</name>\n"
"<argumentList>\n"
"<argument>\n"
"<name>NewInMessage</name>\n"
"<direction>in</direction>\n"
"<relatedStateVariable>InMessage</relatedStateVariable>\n"
"</argument>\n"
"<argument>\n"
"<name>NewOutMessage</name>\n"
"<direction>out</direction>\n"
"<relatedStateVariable>OutMessage</relatedStateVariable>\n"
"</argument>\n"
"</argumentList>\n"
"</action>\n"
"<action>\n"
"<name>PutWLANResponse</name>\n"
"<argumentList>\n"
"<argument>\n"
"<name>NewMessage</name>\n"
"<direction>in</direction>\n"
"<relatedStateVariable>Message</relatedStateVariable>\n"
"</argument>\n"
"<argument>\n"
"<name>NewWLANEventType</name>\n"
"<direction>in</direction>\n"
"<relatedStateVariable>WLANEventType</relatedStateVariable>\n"
"</argument>\n"
"<argument>\n"
"<name>NewWLANEventMAC</name>\n"
"<direction>in</direction>\n"
"<relatedStateVariable>WLANEventMAC</relatedStateVariable>\n"
"</argument>\n"
"</argumentList>\n"
"</action>\n"
"<action>\n"
"<name>SetSelectedRegistrar</name>\n"
"<argumentList>\n"
"<argument>\n"
"<name>NewMessage</name>\n"
"<direction>in</direction>\n"
"<relatedStateVariable>Message</relatedStateVariable>\n"
"</argument>\n"
"</argumentList>\n"
"</action>\n"
"</actionList>\n"
"<serviceStateTable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>Message</name>\n"
"<dataType>bin.base64</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>InMessage</name>\n"
"<dataType>bin.base64</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>OutMessage</name>\n"
"<dataType>bin.base64</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>DeviceInfo</name>\n"
"<dataType>bin.base64</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"yes\">\n"
"<name>APStatus</name>\n"
"<dataType>ui1</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"yes\">\n"
"<name>STAStatus</name>\n"
"<dataType>ui1</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"yes\">\n"
"<name>WLANEvent</name>\n"
"<dataType>bin.base64</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>WLANEventType</name>\n"
"<dataType>ui1</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>WLANEventMAC</name>\n"
"<dataType>string</dataType>\n"
"</stateVariable>\n"
"<stateVariable sendEvents=\"no\">\n"
"<name>WLANResponse</name>\n"
"<dataType>bin.base64</dataType>\n"
"</stateVariable>\n"
"</serviceStateTable>\n"
"</scpd>\n"
;


static const char *wps_device_xml_prefix =
	"<?xml version=\"1.0\"?>\n"
	"<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\n"
	"<specVersion>\n"
	"<major>1</major>\n"
	"<minor>0</minor>\n"
	"</specVersion>\n"
	"<device>\n"
	"<deviceType>urn:schemas-wifialliance-org:device:WFADevice:1"
	"</deviceType>\n";

static const char *wps_device_xml_postfix =
	"<serviceList>\n"
	"<service>\n"
	"<serviceType>urn:schemas-wifialliance-org:service:WFAWLANConfig:1"
	"</serviceType>\n"
	"<serviceId>urn:wifialliance-org:serviceId:WFAWLANConfig1</serviceId>"
	"\n"
	"<SCPDURL>" UPNP_WPS_SCPD_XML_FILE "</SCPDURL>\n"
	"<controlURL>" UPNP_WPS_DEVICE_CONTROL_FILE "</controlURL>\n"
	"<eventSubURL>" UPNP_WPS_DEVICE_EVENT_FILE "</eventSubURL>\n"
	"</service>\n"
	"</serviceList>\n"
	"</device>\n"
	"</root>\n";


/* format_wps_device_xml -- produce content of "file" wps_device.xml
 * (UPNP_WPS_DEVICE_XML_FILE)
 */
static void format_wps_device_xml(struct upnp_wps_device_interface *iface,
				  struct upnp_wps_device_sm *sm,
				  struct wpabuf *buf)
{
	const char *s;
	char uuid_string[80];

	wpabuf_put_str(buf, wps_device_xml_prefix);

	/*
	 * Add required fields with default values if not configured. Add
	 * optional and recommended fields only if configured.
	 */
	s = iface->wps->friendly_name;
	s = ((s && *s) ? s : "WPS Access Point");
	xml_add_tagged_data(buf, "friendlyName", s);

	s = iface->wps->dev.manufacturer;
	s = ((s && *s) ? s : "");
	xml_add_tagged_data(buf, "manufacturer", s);

	if (iface->wps->manufacturer_url)
		xml_add_tagged_data(buf, "manufacturerURL",
				    iface->wps->manufacturer_url);

	if (iface->wps->model_description)
		xml_add_tagged_data(buf, "modelDescription",
				    iface->wps->model_description);

	s = iface->wps->dev.model_name;
	s = ((s && *s) ? s : "");
	xml_add_tagged_data(buf, "modelName", s);

	if (iface->wps->dev.model_number)
		xml_add_tagged_data(buf, "modelNumber",
				    iface->wps->dev.model_number);

	if (iface->wps->model_url)
		xml_add_tagged_data(buf, "modelURL", iface->wps->model_url);

	if (iface->wps->dev.serial_number)
		xml_add_tagged_data(buf, "serialNumber",
				    iface->wps->dev.serial_number);

	uuid_bin2str(iface->wps->uuid, uuid_string, sizeof(uuid_string));
	s = uuid_string;
	/* Need "uuid:" prefix, thus we can't use xml_add_tagged_data()
	 * easily...
	 */
	wpabuf_put_str(buf, "<UDN>uuid:");
	xml_data_encode(buf, s, os_strlen(s));
	wpabuf_put_str(buf, "</UDN>\n");

	if (iface->wps->upc)
		xml_add_tagged_data(buf, "UPC", iface->wps->upc);

	wpabuf_put_str(buf, wps_device_xml_postfix);
}


static void http_put_reply_code(struct wpabuf *buf, enum http_reply_code code)
{
	wpabuf_put_str(buf, "HTTP/1.1 ");
	switch (code) {
	case HTTP_OK:
		wpabuf_put_str(buf, "200 OK\r\n");
		break;
	case HTTP_BAD_REQUEST:
		wpabuf_put_str(buf, "400 Bad request\r\n");
		break;
	case HTTP_PRECONDITION_FAILED:
		wpabuf_put_str(buf, "412 Precondition failed\r\n");
		break;
	case HTTP_UNIMPLEMENTED:
		wpabuf_put_str(buf, "501 Unimplemented\r\n");
		break;
	case HTTP_INTERNAL_SERVER_ERROR:
	default:
		wpabuf_put_str(buf, "500 Internal server error\r\n");
		break;
	}
}


static void http_put_date(struct wpabuf *buf)
{
	wpabuf_put_str(buf, "Date: ");
	format_date(buf);
	wpabuf_put_str(buf, "\r\n");
}


static void http_put_empty(struct wpabuf *buf, enum http_reply_code code)
{
	http_put_reply_code(buf, code);
	wpabuf_put_str(buf, http_server_hdr);
	wpabuf_put_str(buf, http_connection_close);
	wpabuf_put_str(buf, "Content-Length: 0\r\n"
		       "\r\n");
}


/* Given that we have received a header w/ GET, act upon it
 *
 * Format of GET (case-insensitive):
 *
 * First line must be:
 *      GET /<file> HTTP/1.1
 * Since we don't do anything fancy we just ignore other lines.
 *
 * Our response (if no error) which includes only required lines is:
 * HTTP/1.1 200 OK
 * Connection: close
 * Content-Type: text/xml
 * Date: <rfc1123-date>
 *
 * Header lines must end with \r\n
 * Per RFC 2616, content-length: is not required but connection:close
 * would appear to be required (given that we will be closing it!).
 */
static void web_connection_parse_get(struct upnp_wps_device_sm *sm,
				     struct http_request *hreq,
				     const char *filename)
{
	struct wpabuf *buf; /* output buffer, allocated */
	char *put_length_here;
	char *body_start;
	enum {
		GET_DEVICE_XML_FILE,
		GET_SCPD_XML_FILE
	} req;
	size_t extra_len = 0;
	int body_length;
	char len_buf[10];
	struct upnp_wps_device_interface *iface;

	iface = dl_list_first(&sm->interfaces,
			      struct upnp_wps_device_interface, list);
	if (iface == NULL) {
		http_request_deinit(hreq);
		return;
	}

	/*
	 * It is not required that filenames be case insensitive but it is
	 * allowed and cannot hurt here.
	 */
	if (os_strcasecmp(filename, UPNP_WPS_DEVICE_XML_FILE) == 0) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: HTTP GET for device XML");
		req = GET_DEVICE_XML_FILE;
		extra_len = 3000;
		if (iface->wps->friendly_name)
			extra_len += os_strlen(iface->wps->friendly_name);
		if (iface->wps->manufacturer_url)
			extra_len += os_strlen(iface->wps->manufacturer_url);
		if (iface->wps->model_description)
			extra_len += os_strlen(iface->wps->model_description);
		if (iface->wps->model_url)
			extra_len += os_strlen(iface->wps->model_url);
		if (iface->wps->upc)
			extra_len += os_strlen(iface->wps->upc);
	} else if (!os_strcasecmp(filename, UPNP_WPS_SCPD_XML_FILE)) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: HTTP GET for SCPD XML");
		req = GET_SCPD_XML_FILE;
		extra_len = os_strlen(wps_scpd_xml);
	} else {
		/* File not found */
		wpa_printf(MSG_DEBUG, "WPS UPnP: HTTP GET file not found: %s",
			   filename);
		buf = wpabuf_alloc(200);
		if (buf == NULL) {
			http_request_deinit(hreq);
			return;
		}
		wpabuf_put_str(buf,
			       "HTTP/1.1 404 Not Found\r\n"
			       "Connection: close\r\n");

		http_put_date(buf);

		/* terminating empty line */
		wpabuf_put_str(buf, "\r\n");

		goto send_buf;
	}

	buf = wpabuf_alloc(1000 + extra_len);
	if (buf == NULL) {
		http_request_deinit(hreq);
		return;
	}

	wpabuf_put_str(buf,
		       "HTTP/1.1 200 OK\r\n"
		       "Content-Type: text/xml; charset=\"utf-8\"\r\n");
	wpabuf_put_str(buf, "Server: Unspecified, UPnP/1.0, Unspecified\r\n");
	wpabuf_put_str(buf, "Connection: close\r\n");
	wpabuf_put_str(buf, "Content-Length: ");
	/*
	 * We will paste the length in later, leaving some extra whitespace.
	 * HTTP code is supposed to be tolerant of extra whitespace.
	 */
	put_length_here = wpabuf_put(buf, 0);
	wpabuf_put_str(buf, "        \r\n");

	http_put_date(buf);

	/* terminating empty line */
	wpabuf_put_str(buf, "\r\n");

	body_start = wpabuf_put(buf, 0);

	switch (req) {
	case GET_DEVICE_XML_FILE:
		format_wps_device_xml(iface, sm, buf);
		break;
	case GET_SCPD_XML_FILE:
		wpabuf_put_str(buf, wps_scpd_xml);
		break;
	}

	/* Now patch in the content length at the end */
	body_length = (char *) wpabuf_put(buf, 0) - body_start;
	os_snprintf(len_buf, 10, "%d", body_length);
	os_memcpy(put_length_here, len_buf, os_strlen(len_buf));

send_buf:
	http_request_send_and_deinit(hreq, buf);
}


static void wps_upnp_peer_del(struct upnp_wps_peer *peer)
{
	dl_list_del(&peer->list);
	if (peer->wps)
		wps_deinit(peer->wps);
	os_free(peer);
}


static enum http_reply_code
web_process_get_device_info(struct upnp_wps_device_sm *sm,
			    struct wpabuf **reply, const char **replyname)
{
	static const char *name = "NewDeviceInfo";
	struct wps_config cfg;
	struct upnp_wps_device_interface *iface;
	struct upnp_wps_peer *peer;

	iface = dl_list_first(&sm->interfaces,
			      struct upnp_wps_device_interface, list);

	wpa_printf(MSG_DEBUG, "WPS UPnP: GetDeviceInfo");

	if (!iface || iface->ctx->ap_pin == NULL)
		return HTTP_INTERNAL_SERVER_ERROR;

	peer = os_zalloc(sizeof(*peer));
	if (!peer)
		return HTTP_INTERNAL_SERVER_ERROR;

	/*
	 * Request for DeviceInfo, i.e., M1 TLVs. This is a start of WPS
	 * registration over UPnP with the AP acting as an Enrollee. It should
	 * be noted that this is frequently used just to get the device data,
	 * i.e., there may not be any intent to actually complete the
	 * registration.
	 */

	os_memset(&cfg, 0, sizeof(cfg));
	cfg.wps = iface->wps;
	cfg.pin = (u8 *) iface->ctx->ap_pin;
	cfg.pin_len = os_strlen(iface->ctx->ap_pin);
	peer->wps = wps_init(&cfg);
	if (peer->wps) {
		enum wsc_op_code op_code;
		*reply = wps_get_msg(peer->wps, &op_code);
		if (*reply == NULL) {
			wps_deinit(peer->wps);
			peer->wps = NULL;
		}
	} else
		*reply = NULL;
	if (*reply == NULL) {
		wpa_printf(MSG_INFO, "WPS UPnP: Failed to get DeviceInfo");
		os_free(peer);
		return HTTP_INTERNAL_SERVER_ERROR;
	}

	if (dl_list_len(&iface->peers) > 3) {
		struct upnp_wps_peer *old;

		old = dl_list_first(&iface->peers, struct upnp_wps_peer, list);
		if (old) {
			wpa_printf(MSG_DEBUG, "WPS UPnP: Drop oldest active session");
			wps_upnp_peer_del(old);
		}
	}
	dl_list_add_tail(&iface->peers, &peer->list);
	/* TODO: Could schedule a timeout to free the entry */

	*replyname = name;
	return HTTP_OK;
}


static enum http_reply_code
web_process_put_message(struct upnp_wps_device_sm *sm, char *data,
			struct wpabuf **reply, const char **replyname)
{
	struct wpabuf *msg;
	static const char *name = "NewOutMessage";
	enum http_reply_code ret;
	enum wps_process_res res;
	enum wsc_op_code op_code;
	struct upnp_wps_device_interface *iface;
	struct wps_parse_attr attr;
	struct upnp_wps_peer *tmp, *peer;

	iface = dl_list_first(&sm->interfaces,
			      struct upnp_wps_device_interface, list);
	if (!iface)
		return HTTP_INTERNAL_SERVER_ERROR;

	/*
	 * PutMessage is used by external UPnP-based Registrar to perform WPS
	 * operation with the access point itself; as compared with
	 * PutWLANResponse which is for proxying.
	 */
	wpa_printf(MSG_DEBUG, "WPS UPnP: PutMessage");
	msg = xml_get_base64_item(data, "NewInMessage", &ret);
	if (msg == NULL)
		return ret;

	if (wps_parse_msg(msg, &attr)) {
		wpa_printf(MSG_DEBUG,
			   "WPS UPnP: Could not parse PutMessage - NewInMessage");
		wpabuf_free(msg);
		return HTTP_BAD_REQUEST;
	}

	/* Find a matching active peer session */
	peer = NULL;
	dl_list_for_each(tmp, &iface->peers, struct upnp_wps_peer, list) {
		if (!tmp->wps)
			continue;
		if (attr.enrollee_nonce &&
		    os_memcmp(tmp->wps->nonce_e, attr.enrollee_nonce,
			      WPS_NONCE_LEN) != 0)
			continue; /* Enrollee nonce mismatch */
		if (attr.msg_type &&
		    *attr.msg_type != WPS_M2 &&
		    *attr.msg_type != WPS_M2D &&
		    attr.registrar_nonce &&
		    os_memcmp(tmp->wps->nonce_r, attr.registrar_nonce,
			      WPS_NONCE_LEN) != 0)
			continue; /* Registrar nonce mismatch */
		peer = tmp;
		break;
	}
	if (!peer) {
		/*
		  Try to use the first entry in case message could work with
		 * it. The actual handler function will reject this, if needed.
		 * This maintains older behavior where only a single peer entry
		 * was supported.
		 */
		peer = dl_list_first(&iface->peers, struct upnp_wps_peer, list);
	}
	if (!peer || !peer->wps) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: No active peer entry found");
		wpabuf_free(msg);
		return HTTP_BAD_REQUEST;
	}

	res = wps_process_msg(peer->wps, WSC_UPnP, msg);
	if (res == WPS_FAILURE) {
		*reply = NULL;
		wpa_printf(MSG_DEBUG, "WPS UPnP: Drop active peer session");
		wps_upnp_peer_del(peer);
	} else {
		*reply = wps_get_msg(peer->wps, &op_code);
	}
	wpabuf_free(msg);
	if (*reply == NULL)
		return HTTP_INTERNAL_SERVER_ERROR;
	*replyname = name;
	return HTTP_OK;
}


static enum http_reply_code
web_process_put_wlan_response(struct upnp_wps_device_sm *sm, char *data,
			      struct wpabuf **reply, const char **replyname)
{
	struct wpabuf *msg;
	enum http_reply_code ret;
	u8 macaddr[ETH_ALEN];
	int ev_type;
	int type;
	char *val;
	struct upnp_wps_device_interface *iface;
	int ok = 0;

	/*
	 * External UPnP-based Registrar is passing us a message to be proxied
	 * over to a Wi-Fi -based client of ours.
	 */

	wpa_printf(MSG_DEBUG, "WPS UPnP: PutWLANResponse");
	msg = xml_get_base64_item(data, "NewMessage", &ret);
	if (msg == NULL) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: Could not extract NewMessage "
			   "from PutWLANResponse");
		return ret;
	}
	val = xml_get_first_item(data, "NewWLANEventType");
	if (val == NULL) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: No NewWLANEventType in "
			   "PutWLANResponse");
		wpabuf_free(msg);
		return UPNP_ARG_VALUE_INVALID;
	}
	ev_type = atol(val);
	os_free(val);
	val = xml_get_first_item(data, "NewWLANEventMAC");
	if (val == NULL) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: No NewWLANEventMAC in "
			   "PutWLANResponse");
		wpabuf_free(msg);
		return UPNP_ARG_VALUE_INVALID;
	}
	if (hwaddr_aton(val, macaddr)) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: Invalid NewWLANEventMAC in "
			   "PutWLANResponse: '%s'", val);
#ifdef CONFIG_WPS_STRICT
		{
			struct wps_parse_attr attr;
			if (wps_parse_msg(msg, &attr) < 0 || attr.version2) {
				wpabuf_free(msg);
				os_free(val);
				return UPNP_ARG_VALUE_INVALID;
			}
		}
#endif /* CONFIG_WPS_STRICT */
		if (hwaddr_aton2(val, macaddr) > 0) {
			/*
			 * At least some versions of Intel PROset seem to be
			 * using dot-deliminated MAC address format here.
			 */
			wpa_printf(MSG_DEBUG, "WPS UPnP: Workaround - allow "
				   "incorrect MAC address format in "
				   "NewWLANEventMAC: %s -> " MACSTR,
				   val, MAC2STR(macaddr));
		} else {
			wpabuf_free(msg);
			os_free(val);
			return UPNP_ARG_VALUE_INVALID;
		}
	}
	os_free(val);
	if (ev_type == UPNP_WPS_WLANEVENT_TYPE_EAP) {
		struct wps_parse_attr attr;
		if (wps_parse_msg(msg, &attr) < 0 ||
		    attr.msg_type == NULL)
			type = -1;
		else
			type = *attr.msg_type;
		wpa_printf(MSG_DEBUG, "WPS UPnP: Message Type %d", type);
	} else
		type = -1;
	dl_list_for_each(iface, &sm->interfaces,
			 struct upnp_wps_device_interface, list) {
		if (iface->ctx->rx_req_put_wlan_response &&
		    iface->ctx->rx_req_put_wlan_response(iface->priv, ev_type,
							 macaddr, msg, type)
		    == 0)
			ok = 1;
	}

	if (!ok) {
		wpa_printf(MSG_INFO, "WPS UPnP: Fail: sm->ctx->"
			   "rx_req_put_wlan_response");
		wpabuf_free(msg);
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	wpabuf_free(msg);
	*replyname = NULL;
	*reply = NULL;
	return HTTP_OK;
}


static int find_er_addr(struct subscription *s, struct sockaddr_in *cli)
{
	struct subscr_addr *a;

	dl_list_for_each(a, &s->addr_list, struct subscr_addr, list) {
		if (cli->sin_addr.s_addr == a->saddr.sin_addr.s_addr)
			return 1;
	}
	return 0;
}


static struct subscription * find_er(struct upnp_wps_device_sm *sm,
				     struct sockaddr_in *cli)
{
	struct subscription *s;
	dl_list_for_each(s, &sm->subscriptions, struct subscription, list)
		if (find_er_addr(s, cli))
			return s;
	return NULL;
}


static enum http_reply_code
web_process_set_selected_registrar(struct upnp_wps_device_sm *sm,
				   struct sockaddr_in *cli, char *data,
				   struct wpabuf **reply,
				   const char **replyname)
{
	struct wpabuf *msg;
	enum http_reply_code ret;
	struct subscription *s;
	struct upnp_wps_device_interface *iface;
	int err = 0;

	wpa_printf(MSG_DEBUG, "WPS UPnP: SetSelectedRegistrar");
	s = find_er(sm, cli);
	if (s == NULL) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: Ignore SetSelectedRegistrar "
			   "from unknown ER");
		return UPNP_ACTION_FAILED;
	}
	msg = xml_get_base64_item(data, "NewMessage", &ret);
	if (msg == NULL)
		return ret;
	dl_list_for_each(iface, &sm->interfaces,
			 struct upnp_wps_device_interface, list) {
		if (upnp_er_set_selected_registrar(iface->wps->registrar, s,
						   msg))
			err = 1;
	}
	wpabuf_free(msg);
	if (err)
		return HTTP_INTERNAL_SERVER_ERROR;
	*replyname = NULL;
	*reply = NULL;
	return HTTP_OK;
}


static const char *soap_prefix =
	"<?xml version=\"1.0\"?>\n"
	"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
	"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\n"
	"<s:Body>\n";
static const char *soap_postfix =
	"</s:Body>\n</s:Envelope>\n";

static const char *soap_error_prefix =
	"<s:Fault>\n"
	"<faultcode>s:Client</faultcode>\n"
	"<faultstring>UPnPError</faultstring>\n"
	"<detail>\n"
	"<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">\n";
static const char *soap_error_postfix =
	"<errorDescription>Error</errorDescription>\n"
	"</UPnPError>\n"
	"</detail>\n"
	"</s:Fault>\n";

static void web_connection_send_reply(struct http_request *req,
				      enum http_reply_code ret,
				      const char *action, int action_len,
				      const struct wpabuf *reply,
				      const char *replyname)
{
	struct wpabuf *buf;
	char *replydata;
	char *put_length_here = NULL;
	char *body_start = NULL;

	if (reply) {
		size_t len;
		replydata = (char *) base64_encode(wpabuf_head(reply),
						   wpabuf_len(reply), &len);
	} else
		replydata = NULL;

	/* Parameters of the response:
	 *      action(action_len) -- action we are responding to
	 *      replyname -- a name we need for the reply
	 *      replydata -- NULL or null-terminated string
	 */
	buf = wpabuf_alloc(1000 + (replydata ? os_strlen(replydata) : 0U) +
			   (action_len > 0 ? action_len * 2 : 0));
	if (buf == NULL) {
		wpa_printf(MSG_INFO, "WPS UPnP: Cannot allocate reply to "
			   "POST");
		os_free(replydata);
		http_request_deinit(req);
		return;
	}

	/*
	 * Assuming we will be successful, put in the output header first.
	 * Note: we do not keep connections alive (and httpread does
	 * not support it)... therefore we must have Connection: close.
	 */
	if (ret == HTTP_OK) {
		wpabuf_put_str(buf,
			       "HTTP/1.1 200 OK\r\n"
			       "Content-Type: text/xml; "
			       "charset=\"utf-8\"\r\n");
	} else {
		wpabuf_printf(buf, "HTTP/1.1 %d Error\r\n", ret);
	}
	wpabuf_put_str(buf, http_connection_close);

	wpabuf_put_str(buf, "Content-Length: ");
	/*
	 * We will paste the length in later, leaving some extra whitespace.
	 * HTTP code is supposed to be tolerant of extra whitespace.
	 */
	put_length_here = wpabuf_put(buf, 0);
	wpabuf_put_str(buf, "        \r\n");

	http_put_date(buf);

	/* terminating empty line */
	wpabuf_put_str(buf, "\r\n");

	body_start = wpabuf_put(buf, 0);

	if (ret == HTTP_OK) {
		wpabuf_put_str(buf, soap_prefix);
		wpabuf_put_str(buf, "<u:");
		wpabuf_put_data(buf, action, action_len);
		wpabuf_put_str(buf, "Response xmlns:u=\"");
		wpabuf_put_str(buf, urn_wfawlanconfig);
		wpabuf_put_str(buf, "\">\n");
		if (replydata && replyname) {
			/* TODO: might possibly need to escape part of reply
			 * data? ...
			 * probably not, unlikely to have ampersand(&) or left
			 * angle bracket (<) in it...
			 */
			wpabuf_printf(buf, "<%s>", replyname);
			wpabuf_put_str(buf, replydata);
			wpabuf_printf(buf, "</%s>\n", replyname);
		}
		wpabuf_put_str(buf, "</u:");
		wpabuf_put_data(buf, action, action_len);
		wpabuf_put_str(buf, "Response>\n");
		wpabuf_put_str(buf, soap_postfix);
	} else {
		/* Error case */
		wpabuf_put_str(buf, soap_prefix);
		wpabuf_put_str(buf, soap_error_prefix);
		wpabuf_printf(buf, "<errorCode>%d</errorCode>\n", ret);
		wpabuf_put_str(buf, soap_error_postfix);
		wpabuf_put_str(buf, soap_postfix);
	}
	os_free(replydata);

	/* Now patch in the content length at the end */
	if (body_start && put_length_here) {
		int body_length = (char *) wpabuf_put(buf, 0) - body_start;
		char len_buf[10];
		os_snprintf(len_buf, sizeof(len_buf), "%d", body_length);
		os_memcpy(put_length_here, len_buf, os_strlen(len_buf));
	}

	http_request_send_and_deinit(req, buf);
}


static const char * web_get_action(struct http_request *req,
				   size_t *action_len)
{
	const char *match;
	int match_len;
	char *b;
	char *action;

	*action_len = 0;
	/* The SOAPAction line of the header tells us what we want to do */
	b = http_request_get_hdr_line(req, "SOAPAction:");
	if (b == NULL)
		return NULL;
	if (*b == '"')
		b++;
	else
		return NULL;
	match = urn_wfawlanconfig;
	match_len = os_strlen(urn_wfawlanconfig) - 1;
	if (os_strncasecmp(b, match, match_len))
		return NULL;
	b += match_len;
	/* skip over version */
	while (isgraph(*b) && *b != '#')
		b++;
	if (*b != '#')
		return NULL;
	b++;
	/* Following the sharp(#) should be the action and a double quote */
	action = b;
	while (isgraph(*b) && *b != '"')
		b++;
	if (*b != '"')
		return NULL;
	*action_len = b - action;
	return action;
}


/* Given that we have received a header w/ POST, act upon it
 *
 * Format of POST (case-insensitive):
 *
 * First line must be:
 *      POST /<file> HTTP/1.1
 * Since we don't do anything fancy we just ignore other lines.
 *
 * Our response (if no error) which includes only required lines is:
 * HTTP/1.1 200 OK
 * Connection: close
 * Content-Type: text/xml
 * Date: <rfc1123-date>
 *
 * Header lines must end with \r\n
 * Per RFC 2616, content-length: is not required but connection:close
 * would appear to be required (given that we will be closing it!).
 */
static void web_connection_parse_post(struct upnp_wps_device_sm *sm,
				      struct sockaddr_in *cli,
				      struct http_request *req,
				      const char *filename)
{
	enum http_reply_code ret;
	char *data = http_request_get_data(req); /* body of http msg */
	const char *action = NULL;
	size_t action_len = 0;
	const char *replyname = NULL; /* argument name for the reply */
	struct wpabuf *reply = NULL; /* data for the reply */

	if (os_strcasecmp(filename, UPNP_WPS_DEVICE_CONTROL_FILE)) {
		wpa_printf(MSG_INFO, "WPS UPnP: Invalid POST filename %s",
			   filename);
		ret = HTTP_NOT_FOUND;
		goto bad;
	}

	ret = UPNP_INVALID_ACTION;
	action = web_get_action(req, &action_len);
	if (action == NULL)
		goto bad;

	if (!os_strncasecmp("GetDeviceInfo", action, action_len))
		ret = web_process_get_device_info(sm, &reply, &replyname);
	else if (!os_strncasecmp("PutMessage", action, action_len))
		ret = web_process_put_message(sm, data, &reply, &replyname);
	else if (!os_strncasecmp("PutWLANResponse", action, action_len))
		ret = web_process_put_wlan_response(sm, data, &reply,
						    &replyname);
	else if (!os_strncasecmp("SetSelectedRegistrar", action, action_len))
		ret = web_process_set_selected_registrar(sm, cli, data, &reply,
							 &replyname);
	else
		wpa_printf(MSG_INFO, "WPS UPnP: Unknown POST type");

bad:
	if (ret != HTTP_OK)
		wpa_printf(MSG_INFO, "WPS UPnP: POST failure ret=%d", ret);
	web_connection_send_reply(req, ret, action, action_len, reply,
				  replyname);
	wpabuf_free(reply);
}


/* Given that we have received a header w/ SUBSCRIBE, act upon it
 *
 * Format of SUBSCRIBE (case-insensitive):
 *
 * First line must be:
 *      SUBSCRIBE /wps_event HTTP/1.1
 *
 * Our response (if no error) which includes only required lines is:
 * HTTP/1.1 200 OK
 * Server: xx, UPnP/1.0, xx
 * SID: uuid:xxxxxxxxx
 * Timeout: Second-<n>
 * Content-Length: 0
 * Date: xxxx
 *
 * Header lines must end with \r\n
 * Per RFC 2616, content-length: is not required but connection:close
 * would appear to be required (given that we will be closing it!).
 */
static void web_connection_parse_subscribe(struct upnp_wps_device_sm *sm,
					   struct http_request *req,
					   const char *filename)
{
	struct wpabuf *buf;
	char *b;
	char *hdr = http_request_get_hdr(req);
	char *h;
	char *match;
	int match_len;
	char *end;
	int len;
	int got_nt = 0;
	u8 uuid[UUID_LEN];
	int got_uuid = 0;
	char *callback_urls = NULL;
	struct subscription *s = NULL;
	enum http_reply_code ret = HTTP_INTERNAL_SERVER_ERROR;

	buf = wpabuf_alloc(1000);
	if (buf == NULL) {
		http_request_deinit(req);
		return;
	}

	wpa_hexdump_ascii(MSG_DEBUG, "WPS UPnP: HTTP SUBSCRIBE",
			  (u8 *) hdr, os_strlen(hdr));

	/* Parse/validate headers */
	h = hdr;
	/* First line: SUBSCRIBE /wps_event HTTP/1.1
	 * has already been parsed.
	 */
	if (os_strcasecmp(filename, UPNP_WPS_DEVICE_EVENT_FILE) != 0) {
		ret = HTTP_PRECONDITION_FAILED;
		goto error;
	}
	wpa_printf(MSG_DEBUG, "WPS UPnP: HTTP SUBSCRIBE for event");
	end = os_strchr(h, '\n');

	while (end) {
		/* Option line by option line */
		h = end + 1;
		end = os_strchr(h, '\n');
		if (end == NULL)
			break; /* no unterminated lines allowed */

		/* NT assures that it is our type of subscription;
		 * not used for a renewal.
		 **/
		match = "NT:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			match = "upnp:event";
			match_len = os_strlen(match);
			if (os_strncasecmp(h, match, match_len) != 0) {
				ret = HTTP_BAD_REQUEST;
				goto error;
			}
			got_nt = 1;
			continue;
		}
		/* HOST should refer to us */
#if 0
		match = "HOST:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			.....
		}
#endif
		/* CALLBACK gives one or more URLs for NOTIFYs
		 * to be sent as a result of the subscription.
		 * Each URL is enclosed in angle brackets.
		 */
		match = "CALLBACK:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			len = end - h;
			os_free(callback_urls);
			callback_urls = dup_binstr(h, len);
			if (callback_urls == NULL) {
				ret = HTTP_INTERNAL_SERVER_ERROR;
				goto error;
			}
			if (len > 0 && callback_urls[len - 1] == '\r')
				callback_urls[len - 1] = '\0';
			continue;
		}
		/* SID is only for renewal */
		match = "SID:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			match = "uuid:";
			match_len = os_strlen(match);
			if (os_strncasecmp(h, match, match_len) != 0) {
				ret = HTTP_BAD_REQUEST;
				goto error;
			}
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			if (uuid_str2bin(h, uuid)) {
				ret = HTTP_BAD_REQUEST;
				goto error;
			}
			got_uuid = 1;
			continue;
		}
		/* TIMEOUT is requested timeout, but apparently we can
		 * just ignore this.
		 */
	}

	if (got_uuid) {
		/* renewal */
		wpa_printf(MSG_DEBUG, "WPS UPnP: Subscription renewal");
		if (callback_urls) {
			ret = HTTP_BAD_REQUEST;
			goto error;
		}
		s = subscription_renew(sm, uuid);
		if (s == NULL) {
			char str[80];
			uuid_bin2str(uuid, str, sizeof(str));
			wpa_printf(MSG_DEBUG, "WPS UPnP: Could not find "
				   "SID %s", str);
			ret = HTTP_PRECONDITION_FAILED;
			goto error;
		}
	} else if (callback_urls) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: New subscription");
		if (!got_nt) {
			ret = HTTP_PRECONDITION_FAILED;
			goto error;
		}
		s = subscription_start(sm, callback_urls);
		if (s == NULL) {
			ret = HTTP_INTERNAL_SERVER_ERROR;
			goto error;
		}
	} else {
		ret = HTTP_PRECONDITION_FAILED;
		goto error;
	}

	/* success */
	http_put_reply_code(buf, HTTP_OK);
	wpabuf_put_str(buf, http_server_hdr);
	wpabuf_put_str(buf, http_connection_close);
	wpabuf_put_str(buf, "Content-Length: 0\r\n");
	wpabuf_put_str(buf, "SID: uuid:");
	/* subscription id */
	b = wpabuf_put(buf, 0);
	uuid_bin2str(s->uuid, b, 80);
	wpa_printf(MSG_DEBUG, "WPS UPnP: Assigned SID %s", b);
	wpabuf_put(buf, os_strlen(b));
	wpabuf_put_str(buf, "\r\n");
	wpabuf_printf(buf, "Timeout: Second-%d\r\n", UPNP_SUBSCRIBE_SEC);
	http_put_date(buf);
	/* And empty line to terminate header: */
	wpabuf_put_str(buf, "\r\n");

	os_free(callback_urls);
	http_request_send_and_deinit(req, buf);
	return;

error:
	/* Per UPnP spec:
	* Errors
	* Incompatible headers
	*   400 Bad Request. If SID header and one of NT or CALLBACK headers
	*     are present, the publisher must respond with HTTP error
	*     400 Bad Request.
	* Missing or invalid CALLBACK
	*   412 Precondition Failed. If CALLBACK header is missing or does not
	*     contain a valid HTTP URL, the publisher must respond with HTTP
	*     error 412 Precondition Failed.
	* Invalid NT
	*   412 Precondition Failed. If NT header does not equal upnp:event,
	*     the publisher must respond with HTTP error 412 Precondition
	*     Failed.
	* [For resubscription, use 412 if unknown uuid].
	* Unable to accept subscription
	*   5xx. If a publisher is not able to accept a subscription (such as
	*     due to insufficient resources), it must respond with a
	*     HTTP 500-series error code.
	*   599 Too many subscriptions (not a standard HTTP error)
	*/
	wpa_printf(MSG_DEBUG, "WPS UPnP: SUBSCRIBE failed - return %d", ret);
	http_put_empty(buf, ret);
	http_request_send_and_deinit(req, buf);
	os_free(callback_urls);
}


/* Given that we have received a header w/ UNSUBSCRIBE, act upon it
 *
 * Format of UNSUBSCRIBE (case-insensitive):
 *
 * First line must be:
 *      UNSUBSCRIBE /wps_event HTTP/1.1
 *
 * Our response (if no error) which includes only required lines is:
 * HTTP/1.1 200 OK
 * Content-Length: 0
 *
 * Header lines must end with \r\n
 * Per RFC 2616, content-length: is not required but connection:close
 * would appear to be required (given that we will be closing it!).
 */
static void web_connection_parse_unsubscribe(struct upnp_wps_device_sm *sm,
					     struct http_request *req,
					     const char *filename)
{
	struct wpabuf *buf;
	char *hdr = http_request_get_hdr(req);
	char *h;
	char *match;
	int match_len;
	char *end;
	u8 uuid[UUID_LEN];
	int got_uuid = 0;
	struct subscription *s = NULL;
	enum http_reply_code ret = HTTP_INTERNAL_SERVER_ERROR;

	/* Parse/validate headers */
	h = hdr;
	/* First line: UNSUBSCRIBE /wps_event HTTP/1.1
	 * has already been parsed.
	 */
	if (os_strcasecmp(filename, UPNP_WPS_DEVICE_EVENT_FILE) != 0) {
		ret = HTTP_PRECONDITION_FAILED;
		goto send_msg;
	}
	wpa_printf(MSG_DEBUG, "WPS UPnP: HTTP UNSUBSCRIBE for event");
	end = os_strchr(h, '\n');

	while (end) {
		/* Option line by option line */
		h = end + 1;
		end = os_strchr(h, '\n');
		if (end == NULL)
			break; /* no unterminated lines allowed */

		/* HOST should refer to us */
#if 0
		match = "HOST:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			.....
		}
#endif
		match = "SID:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			match = "uuid:";
			match_len = os_strlen(match);
			if (os_strncasecmp(h, match, match_len) != 0) {
				ret = HTTP_BAD_REQUEST;
				goto send_msg;
			}
			h += match_len;
			while (*h == ' ' || *h == '\t')
				h++;
			if (uuid_str2bin(h, uuid)) {
				ret = HTTP_BAD_REQUEST;
				goto send_msg;
			}
			got_uuid = 1;
			continue;
		}

		match = "NT:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			ret = HTTP_BAD_REQUEST;
			goto send_msg;
		}

		match = "CALLBACK:";
		match_len = os_strlen(match);
		if (os_strncasecmp(h, match, match_len) == 0) {
			ret = HTTP_BAD_REQUEST;
			goto send_msg;
		}
	}

	if (got_uuid) {
		char str[80];

		uuid_bin2str(uuid, str, sizeof(str));

		s = subscription_find(sm, uuid);
		if (s) {
			struct subscr_addr *sa;
			sa = dl_list_first(&s->addr_list, struct subscr_addr,
					   list);
			wpa_printf(MSG_DEBUG,
				   "WPS UPnP: Unsubscribing %p (SID %s) %s",
				   s, str, (sa && sa->domain_and_port) ?
				   sa->domain_and_port : "-null-");
			dl_list_del(&s->list);
			subscription_destroy(s);
		} else {
			wpa_printf(MSG_INFO,
				   "WPS UPnP: Could not find matching subscription to unsubscribe (SID %s)",
				   str);
			ret = HTTP_PRECONDITION_FAILED;
			goto send_msg;
		}
	} else {
		wpa_printf(MSG_INFO, "WPS UPnP: Unsubscribe fails (not "
			   "found)");
		ret = HTTP_PRECONDITION_FAILED;
		goto send_msg;
	}

	ret = HTTP_OK;

send_msg:
	buf = wpabuf_alloc(200);
	if (buf == NULL) {
		http_request_deinit(req);
		return;
	}
	http_put_empty(buf, ret);
	http_request_send_and_deinit(req, buf);
}


/* Send error in response to unknown requests */
static void web_connection_unimplemented(struct http_request *req)
{
	struct wpabuf *buf;
	buf = wpabuf_alloc(200);
	if (buf == NULL) {
		http_request_deinit(req);
		return;
	}
	http_put_empty(buf, HTTP_UNIMPLEMENTED);
	http_request_send_and_deinit(req, buf);
}



/* Called when we have gotten an apparently valid http request.
 */
static void web_connection_check_data(void *ctx, struct http_request *req)
{
	struct upnp_wps_device_sm *sm = ctx;
	enum httpread_hdr_type htype = http_request_get_type(req);
	char *filename = http_request_get_uri(req);
	struct sockaddr_in *cli = http_request_get_cli_addr(req);

	if (!filename) {
		wpa_printf(MSG_INFO, "WPS UPnP: Could not get HTTP URI");
		http_request_deinit(req);
		return;
	}
	/* Trim leading slashes from filename */
	while (*filename == '/')
		filename++;

	wpa_printf(MSG_DEBUG, "WPS UPnP: Got HTTP request type %d from %s:%d",
		   htype, inet_ntoa(cli->sin_addr), htons(cli->sin_port));

	switch (htype) {
	case HTTPREAD_HDR_TYPE_GET:
		web_connection_parse_get(sm, req, filename);
		break;
	case HTTPREAD_HDR_TYPE_POST:
		web_connection_parse_post(sm, cli, req, filename);
		break;
	case HTTPREAD_HDR_TYPE_SUBSCRIBE:
		web_connection_parse_subscribe(sm, req, filename);
		break;
	case HTTPREAD_HDR_TYPE_UNSUBSCRIBE:
		web_connection_parse_unsubscribe(sm, req, filename);
		break;

		/* We are not required to support M-POST; just plain
		 * POST is supposed to work, so we only support that.
		 * If for some reason we need to support M-POST, it is
		 * mostly the same as POST, with small differences.
		 */
	default:
		/* Send 501 for anything else */
		web_connection_unimplemented(req);
		break;
	}
}


/*
 * Listening for web connections
 * We have a single TCP listening port, and hand off connections as we get
 * them.
 */

void web_listener_stop(struct upnp_wps_device_sm *sm)
{
	http_server_deinit(sm->web_srv);
	sm->web_srv = NULL;
}


int web_listener_start(struct upnp_wps_device_sm *sm)
{
	struct in_addr addr;
	addr.s_addr = sm->ip_addr;
	sm->web_srv = http_server_init(&addr, -1, web_connection_check_data,
				       sm);
	if (sm->web_srv == NULL) {
		web_listener_stop(sm);
		return -1;
	}
	sm->web_port = http_server_get_port(sm->web_srv);

	return 0;
}
