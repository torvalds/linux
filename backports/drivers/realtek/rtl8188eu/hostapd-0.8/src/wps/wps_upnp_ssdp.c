/*
 * UPnP SSDP for WPS
 * Copyright (c) 2000-2003 Intel Corporation
 * Copyright (c) 2006-2007 Sony Corporation
 * Copyright (c) 2008-2009 Atheros Communications
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * See wps_upnp.c for more details on licensing and code history.
 */

#include "includes.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/route.h>

#include "common.h"
#include "uuid.h"
#include "eloop.h"
#include "wps.h"
#include "wps_upnp.h"
#include "wps_upnp_i.h"

#define UPNP_CACHE_SEC (UPNP_CACHE_SEC_MIN + 1) /* cache time we use */
#define UPNP_CACHE_SEC_MIN 1800 /* min cachable time per UPnP standard */
#define UPNP_ADVERTISE_REPEAT 2 /* no more than 3 */
#define MAX_MSEARCH 20          /* max simultaneous M-SEARCH replies ongoing */
#define SSDP_TARGET  "239.0.0.0"
#define SSDP_NETMASK "255.0.0.0"


/* Check tokens for equality, where tokens consist of letters, digits,
 * underscore and hyphen, and are matched case insensitive.
 */
static int token_eq(const char *s1, const char *s2)
{
	int c1;
	int c2;
	int end1 = 0;
	int end2 = 0;
	for (;;) {
		c1 = *s1++;
		c2 = *s2++;
		if (isalpha(c1) && isupper(c1))
			c1 = tolower(c1);
		if (isalpha(c2) && isupper(c2))
			c2 = tolower(c2);
		end1 = !(isalnum(c1) || c1 == '_' || c1 == '-');
		end2 = !(isalnum(c2) || c2 == '_' || c2 == '-');
		if (end1 || end2 || c1 != c2)
			break;
	}
	return end1 && end2; /* reached end of both words? */
}


/* Return length of token (see above for definition of token) */
static int token_length(const char *s)
{
	const char *begin = s;
	for (;; s++) {
		int c = *s;
		int end = !(isalnum(c) || c == '_' || c == '-');
		if (end)
			break;
	}
	return s - begin;
}


/* return length of interword separation.
 * This accepts only spaces/tabs and thus will not traverse a line
 * or buffer ending.
 */
static int word_separation_length(const char *s)
{
	const char *begin = s;
	for (;; s++) {
		int c = *s;
		if (c == ' ' || c == '\t')
			continue;
		break;
	}
	return s - begin;
}


/* No. of chars through (including) end of line */
static int line_length(const char *l)
{
	const char *lp = l;
	while (*lp && *lp != '\n')
		lp++;
	if (*lp == '\n')
		lp++;
	return lp - l;
}


/* No. of chars excluding trailing whitespace */
static int line_length_stripped(const char *l)
{
	const char *lp = l + line_length(l);
	while (lp > l && !isgraph(lp[-1]))
		lp--;
	return lp - l;
}


static int str_starts(const char *str, const char *start)
{
	return os_strncmp(str, start, os_strlen(start)) == 0;
}


/***************************************************************************
 * Advertisements.
 * These are multicast to the world to tell them we are here.
 * The individual packets are spread out in time to limit loss,
 * and then after a much longer period of time the whole sequence
 * is repeated again (for NOTIFYs only).
 **************************************************************************/

/**
 * next_advertisement - Build next message and advance the state machine
 * @a: Advertisement state
 * @islast: Buffer for indicating whether this is the last message (= 1)
 * Returns: The new message (caller is responsible for freeing this)
 *
 * Note: next_advertisement is shared code with msearchreply_* functions
 */
static struct wpabuf *
next_advertisement(struct upnp_wps_device_sm *sm,
		   struct advertisement_state_machine *a, int *islast)
{
	struct wpabuf *msg;
	char *NTString = "";
	char uuid_string[80];
	struct upnp_wps_device_interface *iface;

	*islast = 0;
	iface = dl_list_first(&sm->interfaces,
			      struct upnp_wps_device_interface, list);
	uuid_bin2str(iface->wps->uuid, uuid_string, sizeof(uuid_string));
	msg = wpabuf_alloc(800); /* more than big enough */
	if (msg == NULL)
		goto fail;
	switch (a->type) {
	case ADVERTISE_UP:
	case ADVERTISE_DOWN:
		NTString = "NT";
		wpabuf_put_str(msg, "NOTIFY * HTTP/1.1\r\n");
		wpabuf_printf(msg, "HOST: %s:%d\r\n",
			      UPNP_MULTICAST_ADDRESS, UPNP_MULTICAST_PORT);
		wpabuf_printf(msg, "CACHE-CONTROL: max-age=%d\r\n",
			      UPNP_CACHE_SEC);
		wpabuf_printf(msg, "NTS: %s\r\n",
			      (a->type == ADVERTISE_UP ?
			       "ssdp:alive" : "ssdp:byebye"));
		break;
	case MSEARCH_REPLY:
		NTString = "ST";
		wpabuf_put_str(msg, "HTTP/1.1 200 OK\r\n");
		wpabuf_printf(msg, "CACHE-CONTROL: max-age=%d\r\n",
			      UPNP_CACHE_SEC);

		wpabuf_put_str(msg, "DATE: ");
		format_date(msg);
		wpabuf_put_str(msg, "\r\n");

		wpabuf_put_str(msg, "EXT:\r\n");
		break;
	}

	if (a->type != ADVERTISE_DOWN) {
		/* Where others may get our XML files from */
		wpabuf_printf(msg, "LOCATION: http://%s:%d/%s\r\n",
			      sm->ip_addr_text, sm->web_port,
			      UPNP_WPS_DEVICE_XML_FILE);
	}

	/* The SERVER line has three comma-separated fields:
	 *      operating system / version
	 *      upnp version
	 *      software package / version
	 * However, only the UPnP version is really required, the
	 * others can be place holders... for security reasons
	 * it is better to NOT provide extra information.
	 */
	wpabuf_put_str(msg, "SERVER: Unspecified, UPnP/1.0, Unspecified\r\n");

	switch (a->state / UPNP_ADVERTISE_REPEAT) {
	case 0:
		wpabuf_printf(msg, "%s: upnp:rootdevice\r\n", NTString);
		wpabuf_printf(msg, "USN: uuid:%s::upnp:rootdevice\r\n",
			      uuid_string);
		break;
	case 1:
		wpabuf_printf(msg, "%s: uuid:%s\r\n", NTString, uuid_string);
		wpabuf_printf(msg, "USN: uuid:%s\r\n", uuid_string);
		break;
	case 2:
		wpabuf_printf(msg, "%s: urn:schemas-wifialliance-org:device:"
			      "WFADevice:1\r\n", NTString);
		wpabuf_printf(msg, "USN: uuid:%s::urn:schemas-wifialliance-"
			      "org:device:WFADevice:1\r\n", uuid_string);
		break;
	case 3:
		wpabuf_printf(msg, "%s: urn:schemas-wifialliance-org:service:"
			      "WFAWLANConfig:1\r\n", NTString);
		wpabuf_printf(msg, "USN: uuid:%s::urn:schemas-wifialliance-"
			      "org:service:WFAWLANConfig:1\r\n", uuid_string);
		break;
	}
	wpabuf_put_str(msg, "\r\n");

	if (a->state + 1 >= 4 * UPNP_ADVERTISE_REPEAT)
		*islast = 1;

	return msg;

fail:
	wpabuf_free(msg);
	return NULL;
}


static void advertisement_state_machine_handler(void *eloop_data,
						void *user_ctx);


/**
 * advertisement_state_machine_stop - Stop SSDP advertisements
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 * @send_byebye: Send byebye advertisement messages immediately
 */
void advertisement_state_machine_stop(struct upnp_wps_device_sm *sm,
				      int send_byebye)
{
	struct advertisement_state_machine *a = &sm->advertisement;
	int islast = 0;
	struct wpabuf *msg;
	struct sockaddr_in dest;

	eloop_cancel_timeout(advertisement_state_machine_handler, NULL, sm);
	if (!send_byebye || sm->multicast_sd < 0)
		return;

	a->type = ADVERTISE_DOWN;
	a->state = 0;

	os_memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = inet_addr(UPNP_MULTICAST_ADDRESS);
	dest.sin_port = htons(UPNP_MULTICAST_PORT);

	while (!islast) {
		msg = next_advertisement(sm, a, &islast);
		if (msg == NULL)
			break;
		if (sendto(sm->multicast_sd, wpabuf_head(msg), wpabuf_len(msg),
			   0, (struct sockaddr *) &dest, sizeof(dest)) < 0) {
			wpa_printf(MSG_INFO, "WPS UPnP: Advertisement sendto "
				   "failed: %d (%s)", errno, strerror(errno));
		}
		wpabuf_free(msg);
		a->state++;
	}
}


static void advertisement_state_machine_handler(void *eloop_data,
						void *user_ctx)
{
	struct upnp_wps_device_sm *sm = user_ctx;
	struct advertisement_state_machine *a = &sm->advertisement;
	struct wpabuf *msg;
	int next_timeout_msec = 100;
	int next_timeout_sec = 0;
	struct sockaddr_in dest;
	int islast = 0;

	/*
	 * Each is sent twice (in case lost) w/ 100 msec delay between;
	 * spec says no more than 3 times.
	 * One pair for rootdevice, one pair for uuid, and a pair each for
	 * each of the two urns.
	 * The entire sequence must be repeated before cache control timeout
	 * (which  is min  1800 seconds),
	 * recommend random portion of half of the advertised cache control age
	 * to ensure against loss... perhaps 1800/4 + rand*1800/4 ?
	 * Delay random interval < 100 msec prior to initial sending.
	 * TTL of 4
	 */

	wpa_printf(MSG_MSGDUMP, "WPS UPnP: Advertisement state=%d", a->state);
	msg = next_advertisement(sm, a, &islast);
	if (msg == NULL)
		return;

	os_memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = inet_addr(UPNP_MULTICAST_ADDRESS);
	dest.sin_port = htons(UPNP_MULTICAST_PORT);

	if (sendto(sm->multicast_sd, wpabuf_head(msg), wpabuf_len(msg), 0,
		   (struct sockaddr *) &dest, sizeof(dest)) == -1) {
		wpa_printf(MSG_ERROR, "WPS UPnP: Advertisement sendto failed:"
			   "%d (%s)", errno, strerror(errno));
		next_timeout_msec = 0;
		next_timeout_sec = 10; /* ... later */
	} else if (islast) {
		a->state = 0; /* wrap around */
		if (a->type == ADVERTISE_DOWN) {
			wpa_printf(MSG_DEBUG, "WPS UPnP: ADVERTISE_DOWN->UP");
			a->type = ADVERTISE_UP;
			/* do it all over again right away */
		} else {
			u16 r;
			/*
			 * Start over again after a long timeout
			 * (see notes above)
			 */
			next_timeout_msec = 0;
			os_get_random((void *) &r, sizeof(r));
			next_timeout_sec = UPNP_CACHE_SEC / 4 +
				(((UPNP_CACHE_SEC / 4) * r) >> 16);
			sm->advertise_count++;
			wpa_printf(MSG_DEBUG, "WPS UPnP: ADVERTISE_UP (#%u); "
				   "next in %d sec",
				   sm->advertise_count, next_timeout_sec);
		}
	} else {
		a->state++;
	}

	wpabuf_free(msg);

	eloop_register_timeout(next_timeout_sec, next_timeout_msec,
			       advertisement_state_machine_handler, NULL, sm);
}


/**
 * advertisement_state_machine_start - Start SSDP advertisements
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 * Returns: 0 on success, -1 on failure
 */
int advertisement_state_machine_start(struct upnp_wps_device_sm *sm)
{
	struct advertisement_state_machine *a = &sm->advertisement;
	int next_timeout_msec;

	advertisement_state_machine_stop(sm, 0);

	/*
	 * Start out advertising down, this automatically switches
	 * to advertising up which signals our restart.
	 */
	a->type = ADVERTISE_DOWN;
	a->state = 0;
	/* (other fields not used here) */

	/* First timeout should be random interval < 100 msec */
	next_timeout_msec = (100 * (os_random() & 0xFF)) >> 8;
	return eloop_register_timeout(0, next_timeout_msec,
				      advertisement_state_machine_handler,
				      NULL, sm);
}


/***************************************************************************
 * M-SEARCH replies
 * These are very similar to the multicast advertisements, with some
 * small changes in data content; and they are sent (UDP) to a specific
 * unicast address instead of multicast.
 * They are sent in response to a UDP M-SEARCH packet.
 **************************************************************************/

/**
 * msearchreply_state_machine_stop - Stop M-SEARCH reply state machine
 * @a: Selected advertisement/reply state
 */
void msearchreply_state_machine_stop(struct advertisement_state_machine *a)
{
	wpa_printf(MSG_DEBUG, "WPS UPnP: M-SEARCH stop");
	dl_list_del(&a->list);
	os_free(a);
}


static void msearchreply_state_machine_handler(void *eloop_data,
					       void *user_ctx)
{
	struct advertisement_state_machine *a = user_ctx;
	struct upnp_wps_device_sm *sm = eloop_data;
	struct wpabuf *msg;
	int next_timeout_msec = 100;
	int next_timeout_sec = 0;
	int islast = 0;

	/*
	 * Each response is sent twice (in case lost) w/ 100 msec delay
	 * between; spec says no more than 3 times.
	 * One pair for rootdevice, one pair for uuid, and a pair each for
	 * each of the two urns.
	 */

	/* TODO: should only send the requested response types */

	wpa_printf(MSG_MSGDUMP, "WPS UPnP: M-SEARCH reply state=%d (%s:%d)",
		   a->state, inet_ntoa(a->client.sin_addr),
		   ntohs(a->client.sin_port));
	msg = next_advertisement(sm, a, &islast);
	if (msg == NULL)
		return;

	/*
	 * Send it on the multicast socket to avoid having to set up another
	 * socket.
	 */
	if (sendto(sm->multicast_sd, wpabuf_head(msg), wpabuf_len(msg), 0,
		   (struct sockaddr *) &a->client, sizeof(a->client)) < 0) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: M-SEARCH reply sendto "
			   "errno %d (%s) for %s:%d",
			   errno, strerror(errno),
			   inet_ntoa(a->client.sin_addr),
			   ntohs(a->client.sin_port));
		/* Ignore error and hope for the best */
	}
	wpabuf_free(msg);
	if (islast) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: M-SEARCH reply done");
		msearchreply_state_machine_stop(a);
		return;
	}
	a->state++;

	wpa_printf(MSG_MSGDUMP, "WPS UPnP: M-SEARCH reply in %d.%03d sec",
		   next_timeout_sec, next_timeout_msec);
	eloop_register_timeout(next_timeout_sec, next_timeout_msec,
			       msearchreply_state_machine_handler, sm, a);
}


/**
 * msearchreply_state_machine_start - Reply to M-SEARCH discovery request
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 * @client: Client address
 * @mx: Maximum delay in seconds
 *
 * Use TTL of 4 (this was done when socket set up).
 * A response should be given in randomized portion of min(MX,120) seconds
 *
 * UPnP-arch-DeviceArchitecture, 1.2.3:
 * To be found, a device must send a UDP response to the source IP address and
 * port that sent the request to the multicast channel. Devices respond if the
 * ST header of the M-SEARCH request is "ssdp:all", "upnp:rootdevice", "uuid:"
 * followed by a UUID that exactly matches one advertised by the device.
 */
static void msearchreply_state_machine_start(struct upnp_wps_device_sm *sm,
					     struct sockaddr_in *client,
					     int mx)
{
	struct advertisement_state_machine *a;
	int next_timeout_sec;
	int next_timeout_msec;
	int replies;

	replies = dl_list_len(&sm->msearch_replies);
	wpa_printf(MSG_DEBUG, "WPS UPnP: M-SEARCH reply start (%d "
		   "outstanding)", replies);
	if (replies >= MAX_MSEARCH) {
		wpa_printf(MSG_INFO, "WPS UPnP: Too many outstanding "
			   "M-SEARCH replies");
		return;
	}

	a = os_zalloc(sizeof(*a));
	if (a == NULL)
		return;
	a->type = MSEARCH_REPLY;
	a->state = 0;
	os_memcpy(&a->client, client, sizeof(*client));
	/* Wait time depending on MX value */
	next_timeout_msec = (1000 * mx * (os_random() & 0xFF)) >> 8;
	next_timeout_sec = next_timeout_msec / 1000;
	next_timeout_msec = next_timeout_msec % 1000;
	if (eloop_register_timeout(next_timeout_sec, next_timeout_msec,
				   msearchreply_state_machine_handler, sm,
				   a)) {
		/* No way to recover (from malloc failure) */
		goto fail;
	}
	/* Remember for future cleanup */
	dl_list_add(&sm->msearch_replies, &a->list);
	return;

fail:
	wpa_printf(MSG_INFO, "WPS UPnP: M-SEARCH reply failure!");
	eloop_cancel_timeout(msearchreply_state_machine_handler, sm, a);
	os_free(a);
}


/**
 * ssdp_parse_msearch - Process a received M-SEARCH
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 * @client: Client address
 * @data: NULL terminated M-SEARCH message
 *
 * Given that we have received a header w/ M-SEARCH, act upon it
 *
 * Format of M-SEARCH (case insensitive!):
 *
 * First line must be:
 *      M-SEARCH * HTTP/1.1
 * Other lines in arbitrary order:
 *      HOST:239.255.255.250:1900
 *      ST:<varies -- must match>
 *      MAN:"ssdp:discover"
 *      MX:<varies>
 *
 * It should be noted that when Microsoft Vista is still learning its IP
 * address, it sends out host lines like: HOST:[FF02::C]:1900
 */
static void ssdp_parse_msearch(struct upnp_wps_device_sm *sm,
			       struct sockaddr_in *client, const char *data)
{
#ifndef CONFIG_NO_STDOUT_DEBUG
	const char *start = data;
#endif /* CONFIG_NO_STDOUT_DEBUG */
	const char *end;
	int got_host = 0;
	int got_st = 0, st_match = 0;
	int got_man = 0;
	int got_mx = 0;
	int mx = 0;

	/*
	 * Skip first line M-SEARCH * HTTP/1.1
	 * (perhaps we should check remainder of the line for syntax)
	 */
	data += line_length(data);

	/* Parse remaining lines */
	for (; *data != '\0'; data += line_length(data)) {
		end = data + line_length_stripped(data);
		if (token_eq(data, "host")) {
			/* The host line indicates who the packet
			 * is addressed to... but do we really care?
			 * Note that Microsoft sometimes does funny
			 * stuff with the HOST: line.
			 */
#if 0   /* could be */
			data += token_length(data);
			data += word_separation_length(data);
			if (*data != ':')
				goto bad;
			data++;
			data += word_separation_length(data);
			/* UPNP_MULTICAST_ADDRESS */
			if (!str_starts(data, "239.255.255.250"))
				goto bad;
			data += os_strlen("239.255.255.250");
			if (*data == ':') {
				if (!str_starts(data, ":1900"))
					goto bad;
			}
#endif  /* could be */
			got_host = 1;
			continue;
		} else if (token_eq(data, "st")) {
			/* There are a number of forms; we look
			 * for one that matches our case.
			 */
			got_st = 1;
			data += token_length(data);
			data += word_separation_length(data);
			if (*data != ':')
				continue;
			data++;
			data += word_separation_length(data);
			if (str_starts(data, "ssdp:all")) {
				st_match = 1;
				continue;
			}
			if (str_starts(data, "upnp:rootdevice")) {
				st_match = 1;
				continue;
			}
			if (str_starts(data, "uuid:")) {
				char uuid_string[80];
				struct upnp_wps_device_interface *iface;
				iface = dl_list_first(
					&sm->interfaces,
					struct upnp_wps_device_interface,
					list);
				data += os_strlen("uuid:");
				uuid_bin2str(iface->wps->uuid, uuid_string,
					     sizeof(uuid_string));
				if (str_starts(data, uuid_string))
					st_match = 1;
				continue;
			}
#if 0
			/* FIX: should we really reply to IGD string? */
			if (str_starts(data, "urn:schemas-upnp-org:device:"
				       "InternetGatewayDevice:1")) {
				st_match = 1;
				continue;
			}
#endif
			if (str_starts(data, "urn:schemas-wifialliance-org:"
				       "service:WFAWLANConfig:1")) {
				st_match = 1;
				continue;
			}
			if (str_starts(data, "urn:schemas-wifialliance-org:"
				       "device:WFADevice:1")) {
				st_match = 1;
				continue;
			}
			continue;
		} else if (token_eq(data, "man")) {
			data += token_length(data);
			data += word_separation_length(data);
			if (*data != ':')
				continue;
			data++;
			data += word_separation_length(data);
			if (!str_starts(data, "\"ssdp:discover\"")) {
				wpa_printf(MSG_DEBUG, "WPS UPnP: Unexpected "
					   "M-SEARCH man-field");
				goto bad;
			}
			got_man = 1;
			continue;
		} else if (token_eq(data, "mx")) {
			data += token_length(data);
			data += word_separation_length(data);
			if (*data != ':')
				continue;
			data++;
			data += word_separation_length(data);
			mx = atol(data);
			got_mx = 1;
			continue;
		}
		/* ignore anything else */
	}
	if (!got_host || !got_st || !got_man || !got_mx || mx < 0) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: Invalid M-SEARCH: %d %d %d "
			   "%d mx=%d", got_host, got_st, got_man, got_mx, mx);
		goto bad;
	}
	if (!st_match) {
		wpa_printf(MSG_DEBUG, "WPS UPnP: Ignored M-SEARCH (no ST "
			   "match)");
		return;
	}
	if (mx > 120)
		mx = 120; /* UPnP-arch-DeviceArchitecture, 1.2.3 */
	msearchreply_state_machine_start(sm, client, mx);
	return;

bad:
	wpa_printf(MSG_INFO, "WPS UPnP: Failed to parse M-SEARCH");
	wpa_printf(MSG_MSGDUMP, "WPS UPnP: M-SEARCH data:\n%s", start);
}


/* Listening for (UDP) discovery (M-SEARCH) packets */

/**
 * ssdp_listener_stop - Stop SSDP listered
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 *
 * This function stops the SSDP listener that was started by calling
 * ssdp_listener_start().
 */
void ssdp_listener_stop(struct upnp_wps_device_sm *sm)
{
	if (sm->ssdp_sd_registered) {
		eloop_unregister_sock(sm->ssdp_sd, EVENT_TYPE_READ);
		sm->ssdp_sd_registered = 0;
	}

	if (sm->ssdp_sd != -1) {
		close(sm->ssdp_sd);
		sm->ssdp_sd = -1;
	}

	eloop_cancel_timeout(msearchreply_state_machine_handler, sm,
			     ELOOP_ALL_CTX);
}


static void ssdp_listener_handler(int sd, void *eloop_ctx, void *sock_ctx)
{
	struct upnp_wps_device_sm *sm = sock_ctx;
	struct sockaddr_in addr; /* client address */
	socklen_t addr_len;
	int nread;
	char buf[MULTICAST_MAX_READ], *pos;

	addr_len = sizeof(addr);
	nread = recvfrom(sm->ssdp_sd, buf, sizeof(buf) - 1, 0,
			 (struct sockaddr *) &addr, &addr_len);
	if (nread <= 0)
		return;
	buf[nread] = '\0'; /* need null termination for algorithm */

	if (str_starts(buf, "NOTIFY ")) {
		/*
		 * Silently ignore NOTIFYs to avoid filling debug log with
		 * unwanted messages.
		 */
		return;
	}

	pos = os_strchr(buf, '\n');
	if (pos)
		*pos = '\0';
	wpa_printf(MSG_MSGDUMP, "WPS UPnP: Received SSDP packet from %s:%d: "
		   "%s", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), buf);
	if (pos)
		*pos = '\n';

	/* Parse first line */
	if (os_strncasecmp(buf, "M-SEARCH", os_strlen("M-SEARCH")) == 0 &&
	    !isgraph(buf[strlen("M-SEARCH")])) {
		ssdp_parse_msearch(sm, &addr, buf);
		return;
	}

	/* Ignore anything else */
}


int ssdp_listener_open(void)
{
	struct sockaddr_in addr;
	struct ip_mreq mcast_addr;
	int on = 1;
	/* per UPnP spec, keep IP packet time to live (TTL) small */
	unsigned char ttl = 4;
	int sd;

	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sd < 0)
		goto fail;
	if (fcntl(sd, F_SETFL, O_NONBLOCK) != 0)
		goto fail;
	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)))
		goto fail;
	os_memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(UPNP_MULTICAST_PORT);
	if (bind(sd, (struct sockaddr *) &addr, sizeof(addr)))
		goto fail;
	os_memset(&mcast_addr, 0, sizeof(mcast_addr));
	mcast_addr.imr_interface.s_addr = htonl(INADDR_ANY);
	mcast_addr.imr_multiaddr.s_addr = inet_addr(UPNP_MULTICAST_ADDRESS);
	if (setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		       (char *) &mcast_addr, sizeof(mcast_addr)))
		goto fail;
	if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_TTL,
		       &ttl, sizeof(ttl)))
		goto fail;

	return sd;

fail:
	if (sd >= 0)
		close(sd);
	return -1;
}


/**
 * ssdp_listener_start - Set up for receiving discovery (UDP) packets
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 * Returns: 0 on success, -1 on failure
 *
 * The SSDP listener is stopped by calling ssdp_listener_stop().
 */
int ssdp_listener_start(struct upnp_wps_device_sm *sm)
{
	sm->ssdp_sd = ssdp_listener_open();

	if (eloop_register_sock(sm->ssdp_sd, EVENT_TYPE_READ,
				ssdp_listener_handler, NULL, sm))
		goto fail;
	sm->ssdp_sd_registered = 1;
	return 0;

fail:
	/* Error */
	wpa_printf(MSG_ERROR, "WPS UPnP: ssdp_listener_start failed");
	ssdp_listener_stop(sm);
	return -1;
}


/**
 * add_ssdp_network - Add routing entry for SSDP
 * @net_if: Selected network interface name
 * Returns: 0 on success, -1 on failure
 *
 * This function assures that the multicast address will be properly
 * handled by Linux networking code (by a modification to routing tables).
 * This must be done per network interface. It really only needs to be done
 * once after booting up, but it does not hurt to call this more frequently
 * "to be safe".
 */
int add_ssdp_network(const char *net_if)
{
#ifdef __linux__
	int ret = -1;
	int sock = -1;
	struct rtentry rt;
	struct sockaddr_in *sin;

	if (!net_if)
		goto fail;

	os_memset(&rt, 0, sizeof(rt));
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		goto fail;

	rt.rt_dev = (char *) net_if;
	sin = aliasing_hide_typecast(&rt.rt_dst, struct sockaddr_in);
	sin->sin_family = AF_INET;
	sin->sin_port = 0;
	sin->sin_addr.s_addr = inet_addr(SSDP_TARGET);
	sin = aliasing_hide_typecast(&rt.rt_genmask, struct sockaddr_in);
	sin->sin_family = AF_INET;
	sin->sin_port = 0;
	sin->sin_addr.s_addr = inet_addr(SSDP_NETMASK);
	rt.rt_flags = RTF_UP;
	if (ioctl(sock, SIOCADDRT, &rt) < 0) {
		if (errno == EPERM) {
			wpa_printf(MSG_DEBUG, "add_ssdp_network: No "
				   "permissions to add routing table entry");
			/* Continue to allow testing as non-root */
		} else if (errno != EEXIST) {
			wpa_printf(MSG_INFO, "add_ssdp_network() ioctl errno "
				   "%d (%s)", errno, strerror(errno));
			goto fail;
		}
	}

	ret = 0;

fail:
	if (sock >= 0)
		close(sock);

	return ret;
#else /* __linux__ */
	return 0;
#endif /* __linux__ */
}


int ssdp_open_multicast_sock(u32 ip_addr)
{
	int sd;
	 /* per UPnP-arch-DeviceArchitecture, 1. Discovery, keep IP packet
	  * time to live (TTL) small */
	unsigned char ttl = 4;

	sd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sd < 0)
		return -1;

#if 0   /* maybe ok if we sometimes block on writes */
	if (fcntl(sd, F_SETFL, O_NONBLOCK) != 0)
		return -1;
#endif

	if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_IF,
		       &ip_addr, sizeof(ip_addr))) {
		wpa_printf(MSG_DEBUG, "WPS: setsockopt(IP_MULTICAST_IF) %x: "
			   "%d (%s)", ip_addr, errno, strerror(errno));
		return -1;
	}
	if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_TTL,
		       &ttl, sizeof(ttl))) {
		wpa_printf(MSG_DEBUG, "WPS: setsockopt(IP_MULTICAST_TTL): "
			   "%d (%s)", errno, strerror(errno));
		return -1;
	}

#if 0   /* not needed, because we don't receive using multicast_sd */
	{
		struct ip_mreq mreq;
		mreq.imr_multiaddr.s_addr = inet_addr(UPNP_MULTICAST_ADDRESS);
		mreq.imr_interface.s_addr = ip_addr;
		wpa_printf(MSG_DEBUG, "WPS UPnP: Multicast addr 0x%x if addr "
			   "0x%x",
			   mreq.imr_multiaddr.s_addr,
			   mreq.imr_interface.s_addr);
		if (setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
				sizeof(mreq))) {
			wpa_printf(MSG_ERROR,
				   "WPS UPnP: setsockopt "
				   "IP_ADD_MEMBERSHIP errno %d (%s)",
				   errno, strerror(errno));
			return -1;
		}
	}
#endif  /* not needed */

	/*
	 * TODO: What about IP_MULTICAST_LOOP? It seems to be on by default?
	 * which aids debugging I suppose but isn't really necessary?
	 */

	return sd;
}


/**
 * ssdp_open_multicast - Open socket for sending multicast SSDP messages
 * @sm: WPS UPnP state machine from upnp_wps_device_init()
 * Returns: 0 on success, -1 on failure
 */
int ssdp_open_multicast(struct upnp_wps_device_sm *sm)
{
	sm->multicast_sd = ssdp_open_multicast_sock(sm->ip_addr);
	if (sm->multicast_sd < 0)
		return -1;
	return 0;
}
