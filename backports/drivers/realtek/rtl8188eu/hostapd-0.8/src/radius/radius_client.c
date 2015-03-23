/*
 * RADIUS client
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
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

#include "includes.h"

#include "common.h"
#include "radius.h"
#include "radius_client.h"
#include "eloop.h"

/* Defaults for RADIUS retransmit values (exponential backoff) */

/**
 * RADIUS_CLIENT_FIRST_WAIT - RADIUS client timeout for first retry in seconds
 */
#define RADIUS_CLIENT_FIRST_WAIT 3

/**
 * RADIUS_CLIENT_MAX_WAIT - RADIUS client maximum retry timeout in seconds
 */
#define RADIUS_CLIENT_MAX_WAIT 120

/**
 * RADIUS_CLIENT_MAX_RETRIES - RADIUS client maximum retries
 *
 * Maximum number of retransmit attempts before the entry is removed from
 * retransmit list.
 */
#define RADIUS_CLIENT_MAX_RETRIES 10

/**
 * RADIUS_CLIENT_MAX_ENTRIES - RADIUS client maximum pending messages
 *
 * Maximum number of entries in retransmit list (oldest entries will be
 * removed, if this limit is exceeded).
 */
#define RADIUS_CLIENT_MAX_ENTRIES 30

/**
 * RADIUS_CLIENT_NUM_FAILOVER - RADIUS client failover point
 *
 * The number of failed retry attempts after which the RADIUS server will be
 * changed (if one of more backup servers are configured).
 */
#define RADIUS_CLIENT_NUM_FAILOVER 4


/**
 * struct radius_rx_handler - RADIUS client RX handler
 *
 * This data structure is used internally inside the RADIUS client module to
 * store registered RX handlers. These handlers are registered by calls to
 * radius_client_register() and unregistered when the RADIUS client is
 * deinitialized with a call to radius_client_deinit().
 */
struct radius_rx_handler {
	/**
	 * handler - Received RADIUS message handler
	 */
	RadiusRxResult (*handler)(struct radius_msg *msg,
				  struct radius_msg *req,
				  const u8 *shared_secret,
				  size_t shared_secret_len,
				  void *data);

	/**
	 * data - Context data for the handler
	 */
	void *data;
};


/**
 * struct radius_msg_list - RADIUS client message retransmit list
 *
 * This data structure is used internally inside the RADIUS client module to
 * store pending RADIUS requests that may still need to be retransmitted.
 */
struct radius_msg_list {
	/**
	 * addr - STA/client address
	 *
	 * This is used to find RADIUS messages for the same STA.
	 */
	u8 addr[ETH_ALEN];

	/**
	 * msg - RADIUS message
	 */
	struct radius_msg *msg;

	/**
	 * msg_type - Message type
	 */
	RadiusType msg_type;

	/**
	 * first_try - Time of the first transmission attempt
	 */
	os_time_t first_try;

	/**
	 * next_try - Time for the next transmission attempt
	 */
	os_time_t next_try;

	/**
	 * attempts - Number of transmission attempts
	 */
	int attempts;

	/**
	 * next_wait - Next retransmission wait time in seconds
	 */
	int next_wait;

	/**
	 * last_attempt - Time of the last transmission attempt
	 */
	struct os_time last_attempt;

	/**
	 * shared_secret - Shared secret with the target RADIUS server
	 */
	const u8 *shared_secret;

	/**
	 * shared_secret_len - shared_secret length in octets
	 */
	size_t shared_secret_len;

	/* TODO: server config with failover to backup server(s) */

	/**
	 * next - Next message in the list
	 */
	struct radius_msg_list *next;
};


/**
 * struct radius_client_data - Internal RADIUS client data
 *
 * This data structure is used internally inside the RADIUS client module.
 * External users allocate this by calling radius_client_init() and free it by
 * calling radius_client_deinit(). The pointer to this opaque data is used in
 * calls to other functions as an identifier for the RADIUS client instance.
 */
struct radius_client_data {
	/**
	 * ctx - Context pointer for hostapd_logger() callbacks
	 */
	void *ctx;

	/**
	 * conf - RADIUS client configuration (list of RADIUS servers to use)
	 */
	struct hostapd_radius_servers *conf;

	/**
	 * auth_serv_sock - IPv4 socket for RADIUS authentication messages
	 */
	int auth_serv_sock;

	/**
	 * acct_serv_sock - IPv4 socket for RADIUS accounting messages
	 */
	int acct_serv_sock;

	/**
	 * auth_serv_sock6 - IPv6 socket for RADIUS authentication messages
	 */
	int auth_serv_sock6;

	/**
	 * acct_serv_sock6 - IPv6 socket for RADIUS accounting messages
	 */
	int acct_serv_sock6;

	/**
	 * auth_sock - Currently used socket for RADIUS authentication server
	 */
	int auth_sock;

	/**
	 * acct_sock - Currently used socket for RADIUS accounting server
	 */
	int acct_sock;

	/**
	 * auth_handlers - Authentication message handlers
	 */
	struct radius_rx_handler *auth_handlers;

	/**
	 * num_auth_handlers - Number of handlers in auth_handlers
	 */
	size_t num_auth_handlers;

	/**
	 * acct_handlers - Accounting message handlers
	 */
	struct radius_rx_handler *acct_handlers;

	/**
	 * num_acct_handlers - Number of handlers in acct_handlers
	 */
	size_t num_acct_handlers;

	/**
	 * msgs - Pending outgoing RADIUS messages
	 */
	struct radius_msg_list *msgs;

	/**
	 * num_msgs - Number of pending messages in the msgs list
	 */
	size_t num_msgs;

	/**
	 * next_radius_identifier - Next RADIUS message identifier to use
	 */
	u8 next_radius_identifier;
};


static int
radius_change_server(struct radius_client_data *radius,
		     struct hostapd_radius_server *nserv,
		     struct hostapd_radius_server *oserv,
		     int sock, int sock6, int auth);
static int radius_client_init_acct(struct radius_client_data *radius);
static int radius_client_init_auth(struct radius_client_data *radius);


static void radius_client_msg_free(struct radius_msg_list *req)
{
	radius_msg_free(req->msg);
	os_free(req);
}


/**
 * radius_client_register - Register a RADIUS client RX handler
 * @radius: RADIUS client context from radius_client_init()
 * @msg_type: RADIUS client type (RADIUS_AUTH or RADIUS_ACCT)
 * @handler: Handler for received RADIUS messages
 * @data: Context pointer for handler callbacks
 * Returns: 0 on success, -1 on failure
 *
 * This function is used to register a handler for processing received RADIUS
 * authentication and accounting messages. The handler() callback function will
 * be called whenever a RADIUS message is received from the active server.
 *
 * There can be multiple registered RADIUS message handlers. The handlers will
 * be called in order until one of them indicates that it has processed or
 * queued the message.
 */
int radius_client_register(struct radius_client_data *radius,
			   RadiusType msg_type,
			   RadiusRxResult (*handler)(struct radius_msg *msg,
						     struct radius_msg *req,
						     const u8 *shared_secret,
						     size_t shared_secret_len,
						     void *data),
			   void *data)
{
	struct radius_rx_handler **handlers, *newh;
	size_t *num;

	if (msg_type == RADIUS_ACCT) {
		handlers = &radius->acct_handlers;
		num = &radius->num_acct_handlers;
	} else {
		handlers = &radius->auth_handlers;
		num = &radius->num_auth_handlers;
	}

	newh = os_realloc(*handlers,
			  (*num + 1) * sizeof(struct radius_rx_handler));
	if (newh == NULL)
		return -1;

	newh[*num].handler = handler;
	newh[*num].data = data;
	(*num)++;
	*handlers = newh;

	return 0;
}


static void radius_client_handle_send_error(struct radius_client_data *radius,
					    int s, RadiusType msg_type)
{
#ifndef CONFIG_NATIVE_WINDOWS
	int _errno = errno;
	perror("send[RADIUS]");
	if (_errno == ENOTCONN || _errno == EDESTADDRREQ || _errno == EINVAL ||
	    _errno == EBADF) {
		hostapd_logger(radius->ctx, NULL, HOSTAPD_MODULE_RADIUS,
			       HOSTAPD_LEVEL_INFO,
			       "Send failed - maybe interface status changed -"
			       " try to connect again");
		eloop_unregister_read_sock(s);
		close(s);
		if (msg_type == RADIUS_ACCT || msg_type == RADIUS_ACCT_INTERIM)
			radius_client_init_acct(radius);
		else
			radius_client_init_auth(radius);
	}
#endif /* CONFIG_NATIVE_WINDOWS */
}


static int radius_client_retransmit(struct radius_client_data *radius,
				    struct radius_msg_list *entry,
				    os_time_t now)
{
	struct hostapd_radius_servers *conf = radius->conf;
	int s;
	struct wpabuf *buf;

	if (entry->msg_type == RADIUS_ACCT ||
	    entry->msg_type == RADIUS_ACCT_INTERIM) {
		s = radius->acct_sock;
		if (entry->attempts == 0)
			conf->acct_server->requests++;
		else {
			conf->acct_server->timeouts++;
			conf->acct_server->retransmissions++;
		}
	} else {
		s = radius->auth_sock;
		if (entry->attempts == 0)
			conf->auth_server->requests++;
		else {
			conf->auth_server->timeouts++;
			conf->auth_server->retransmissions++;
		}
	}

	/* retransmit; remove entry if too many attempts */
	entry->attempts++;
	hostapd_logger(radius->ctx, entry->addr, HOSTAPD_MODULE_RADIUS,
		       HOSTAPD_LEVEL_DEBUG, "Resending RADIUS message (id=%d)",
		       radius_msg_get_hdr(entry->msg)->identifier);

	os_get_time(&entry->last_attempt);
	buf = radius_msg_get_buf(entry->msg);
	if (send(s, wpabuf_head(buf), wpabuf_len(buf), 0) < 0)
		radius_client_handle_send_error(radius, s, entry->msg_type);

	entry->next_try = now + entry->next_wait;
	entry->next_wait *= 2;
	if (entry->next_wait > RADIUS_CLIENT_MAX_WAIT)
		entry->next_wait = RADIUS_CLIENT_MAX_WAIT;
	if (entry->attempts >= RADIUS_CLIENT_MAX_RETRIES) {
		printf("Removing un-ACKed RADIUS message due to too many "
		       "failed retransmit attempts\n");
		return 1;
	}

	return 0;
}


static void radius_client_timer(void *eloop_ctx, void *timeout_ctx)
{
	struct radius_client_data *radius = eloop_ctx;
	struct hostapd_radius_servers *conf = radius->conf;
	struct os_time now;
	os_time_t first;
	struct radius_msg_list *entry, *prev, *tmp;
	int auth_failover = 0, acct_failover = 0;
	char abuf[50];

	entry = radius->msgs;
	if (!entry)
		return;

	os_get_time(&now);
	first = 0;

	prev = NULL;
	while (entry) {
		if (now.sec >= entry->next_try &&
		    radius_client_retransmit(radius, entry, now.sec)) {
			if (prev)
				prev->next = entry->next;
			else
				radius->msgs = entry->next;

			tmp = entry;
			entry = entry->next;
			radius_client_msg_free(tmp);
			radius->num_msgs--;
			continue;
		}

		if (entry->attempts > RADIUS_CLIENT_NUM_FAILOVER) {
			if (entry->msg_type == RADIUS_ACCT ||
			    entry->msg_type == RADIUS_ACCT_INTERIM)
				acct_failover++;
			else
				auth_failover++;
		}

		if (first == 0 || entry->next_try < first)
			first = entry->next_try;

		prev = entry;
		entry = entry->next;
	}

	if (radius->msgs) {
		if (first < now.sec)
			first = now.sec;
		eloop_register_timeout(first - now.sec, 0,
				       radius_client_timer, radius, NULL);
		hostapd_logger(radius->ctx, NULL, HOSTAPD_MODULE_RADIUS,
			       HOSTAPD_LEVEL_DEBUG, "Next RADIUS client "
			       "retransmit in %ld seconds",
			       (long int) (first - now.sec));
	}

	if (auth_failover && conf->num_auth_servers > 1) {
		struct hostapd_radius_server *next, *old;
		old = conf->auth_server;
		hostapd_logger(radius->ctx, NULL, HOSTAPD_MODULE_RADIUS,
			       HOSTAPD_LEVEL_NOTICE,
			       "No response from Authentication server "
			       "%s:%d - failover",
			       hostapd_ip_txt(&old->addr, abuf, sizeof(abuf)),
			       old->port);

		for (entry = radius->msgs; entry; entry = entry->next) {
			if (entry->msg_type == RADIUS_AUTH)
				old->timeouts++;
		}

		next = old + 1;
		if (next > &(conf->auth_servers[conf->num_auth_servers - 1]))
			next = conf->auth_servers;
		conf->auth_server = next;
		radius_change_server(radius, next, old,
				     radius->auth_serv_sock,
				     radius->auth_serv_sock6, 1);
	}

	if (acct_failover && conf->num_acct_servers > 1) {
		struct hostapd_radius_server *next, *old;
		old = conf->acct_server;
		hostapd_logger(radius->ctx, NULL, HOSTAPD_MODULE_RADIUS,
			       HOSTAPD_LEVEL_NOTICE,
			       "No response from Accounting server "
			       "%s:%d - failover",
			       hostapd_ip_txt(&old->addr, abuf, sizeof(abuf)),
			       old->port);

		for (entry = radius->msgs; entry; entry = entry->next) {
			if (entry->msg_type == RADIUS_ACCT ||
			    entry->msg_type == RADIUS_ACCT_INTERIM)
				old->timeouts++;
		}

		next = old + 1;
		if (next > &conf->acct_servers[conf->num_acct_servers - 1])
			next = conf->acct_servers;
		conf->acct_server = next;
		radius_change_server(radius, next, old,
				     radius->acct_serv_sock,
				     radius->acct_serv_sock6, 0);
	}
}


static void radius_client_update_timeout(struct radius_client_data *radius)
{
	struct os_time now;
	os_time_t first;
	struct radius_msg_list *entry;

	eloop_cancel_timeout(radius_client_timer, radius, NULL);

	if (radius->msgs == NULL) {
		return;
	}

	first = 0;
	for (entry = radius->msgs; entry; entry = entry->next) {
		if (first == 0 || entry->next_try < first)
			first = entry->next_try;
	}

	os_get_time(&now);
	if (first < now.sec)
		first = now.sec;
	eloop_register_timeout(first - now.sec, 0, radius_client_timer, radius,
			       NULL);
	hostapd_logger(radius->ctx, NULL, HOSTAPD_MODULE_RADIUS,
		       HOSTAPD_LEVEL_DEBUG, "Next RADIUS client retransmit in"
		       " %ld seconds\n", (long int) (first - now.sec));
}


static void radius_client_list_add(struct radius_client_data *radius,
				   struct radius_msg *msg,
				   RadiusType msg_type,
				   const u8 *shared_secret,
				   size_t shared_secret_len, const u8 *addr)
{
	struct radius_msg_list *entry, *prev;

	if (eloop_terminated()) {
		/* No point in adding entries to retransmit queue since event
		 * loop has already been terminated. */
		radius_msg_free(msg);
		return;
	}

	entry = os_zalloc(sizeof(*entry));
	if (entry == NULL) {
		printf("Failed to add RADIUS packet into retransmit list\n");
		radius_msg_free(msg);
		return;
	}

	if (addr)
		os_memcpy(entry->addr, addr, ETH_ALEN);
	entry->msg = msg;
	entry->msg_type = msg_type;
	entry->shared_secret = shared_secret;
	entry->shared_secret_len = shared_secret_len;
	os_get_time(&entry->last_attempt);
	entry->first_try = entry->last_attempt.sec;
	entry->next_try = entry->first_try + RADIUS_CLIENT_FIRST_WAIT;
	entry->attempts = 1;
	entry->next_wait = RADIUS_CLIENT_FIRST_WAIT * 2;
	entry->next = radius->msgs;
	radius->msgs = entry;
	radius_client_update_timeout(radius);

	if (radius->num_msgs >= RADIUS_CLIENT_MAX_ENTRIES) {
		printf("Removing the oldest un-ACKed RADIUS packet due to "
		       "retransmit list limits.\n");
		prev = NULL;
		while (entry->next) {
			prev = entry;
			entry = entry->next;
		}
		if (prev) {
			prev->next = NULL;
			radius_client_msg_free(entry);
		}
	} else
		radius->num_msgs++;
}


static void radius_client_list_del(struct radius_client_data *radius,
				   RadiusType msg_type, const u8 *addr)
{
	struct radius_msg_list *entry, *prev, *tmp;

	if (addr == NULL)
		return;

	entry = radius->msgs;
	prev = NULL;
	while (entry) {
		if (entry->msg_type == msg_type &&
		    os_memcmp(entry->addr, addr, ETH_ALEN) == 0) {
			if (prev)
				prev->next = entry->next;
			else
				radius->msgs = entry->next;
			tmp = entry;
			entry = entry->next;
			hostapd_logger(radius->ctx, addr,
				       HOSTAPD_MODULE_RADIUS,
				       HOSTAPD_LEVEL_DEBUG,
				       "Removing matching RADIUS message");
			radius_client_msg_free(tmp);
			radius->num_msgs--;
			continue;
		}
		prev = entry;
		entry = entry->next;
	}
}


/**
 * radius_client_send - Send a RADIUS request
 * @radius: RADIUS client context from radius_client_init()
 * @msg: RADIUS message to be sent
 * @msg_type: Message type (RADIUS_AUTH, RADIUS_ACCT, RADIUS_ACCT_INTERIM)
 * @addr: MAC address of the device related to this message or %NULL
 * Returns: 0 on success, -1 on failure
 *
 * This function is used to transmit a RADIUS authentication (RADIUS_AUTH) or
 * accounting request (RADIUS_ACCT or RADIUS_ACCT_INTERIM). The only difference
 * between accounting and interim accounting messages is that the interim
 * message will override any pending interim accounting updates while a new
 * accounting message does not remove any pending messages.
 *
 * The message is added on the retransmission queue and will be retransmitted
 * automatically until a response is received or maximum number of retries
 * (RADIUS_CLIENT_MAX_RETRIES) is reached.
 *
 * The related device MAC address can be used to identify pending messages that
 * can be removed with radius_client_flush_auth() or with interim accounting
 * updates.
 */
int radius_client_send(struct radius_client_data *radius,
		       struct radius_msg *msg, RadiusType msg_type,
		       const u8 *addr)
{
	struct hostapd_radius_servers *conf = radius->conf;
	const u8 *shared_secret;
	size_t shared_secret_len;
	char *name;
	int s, res;
	struct wpabuf *buf;

	if (msg_type == RADIUS_ACCT_INTERIM) {
		/* Remove any pending interim acct update for the same STA. */
		radius_client_list_del(radius, msg_type, addr);
	}

	if (msg_type == RADIUS_ACCT || msg_type == RADIUS_ACCT_INTERIM) {
		if (conf->acct_server == NULL) {
			hostapd_logger(radius->ctx, NULL,
				       HOSTAPD_MODULE_RADIUS,
				       HOSTAPD_LEVEL_INFO,
				       "No accounting server configured");
			return -1;
		}
		shared_secret = conf->acct_server->shared_secret;
		shared_secret_len = conf->acct_server->shared_secret_len;
		radius_msg_finish_acct(msg, shared_secret, shared_secret_len);
		name = "accounting";
		s = radius->acct_sock;
		conf->acct_server->requests++;
	} else {
		if (conf->auth_server == NULL) {
			hostapd_logger(radius->ctx, NULL,
				       HOSTAPD_MODULE_RADIUS,
				       HOSTAPD_LEVEL_INFO,
				       "No authentication server configured");
			return -1;
		}
		shared_secret = conf->auth_server->shared_secret;
		shared_secret_len = conf->auth_server->shared_secret_len;
		radius_msg_finish(msg, shared_secret, shared_secret_len);
		name = "authentication";
		s = radius->auth_sock;
		conf->auth_server->requests++;
	}

	hostapd_logger(radius->ctx, NULL, HOSTAPD_MODULE_RADIUS,
		       HOSTAPD_LEVEL_DEBUG, "Sending RADIUS message to %s "
		       "server", name);
	if (conf->msg_dumps)
		radius_msg_dump(msg);

	buf = radius_msg_get_buf(msg);
	res = send(s, wpabuf_head(buf), wpabuf_len(buf), 0);
	if (res < 0)
		radius_client_handle_send_error(radius, s, msg_type);

	radius_client_list_add(radius, msg, msg_type, shared_secret,
			       shared_secret_len, addr);

	return res;
}


static void radius_client_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct radius_client_data *radius = eloop_ctx;
	struct hostapd_radius_servers *conf = radius->conf;
	RadiusType msg_type = (RadiusType) sock_ctx;
	int len, roundtrip;
	unsigned char buf[3000];
	struct radius_msg *msg;
	struct radius_hdr *hdr;
	struct radius_rx_handler *handlers;
	size_t num_handlers, i;
	struct radius_msg_list *req, *prev_req;
	struct os_time now;
	struct hostapd_radius_server *rconf;
	int invalid_authenticator = 0;

	if (msg_type == RADIUS_ACCT) {
		handlers = radius->acct_handlers;
		num_handlers = radius->num_acct_handlers;
		rconf = conf->acct_server;
	} else {
		handlers = radius->auth_handlers;
		num_handlers = radius->num_auth_handlers;
		rconf = conf->auth_server;
	}

	len = recv(sock, buf, sizeof(buf), MSG_DONTWAIT);
	if (len < 0) {
		perror("recv[RADIUS]");
		return;
	}
	hostapd_logger(radius->ctx, NULL, HOSTAPD_MODULE_RADIUS,
		       HOSTAPD_LEVEL_DEBUG, "Received %d bytes from RADIUS "
		       "server", len);
	if (len == sizeof(buf)) {
		printf("Possibly too long UDP frame for our buffer - "
		       "dropping it\n");
		return;
	}

	msg = radius_msg_parse(buf, len);
	if (msg == NULL) {
		printf("Parsing incoming RADIUS frame failed\n");
		rconf->malformed_responses++;
		return;
	}
	hdr = radius_msg_get_hdr(msg);

	hostapd_logger(radius->ctx, NULL, HOSTAPD_MODULE_RADIUS,
		       HOSTAPD_LEVEL_DEBUG, "Received RADIUS message");
	if (conf->msg_dumps)
		radius_msg_dump(msg);

	switch (hdr->code) {
	case RADIUS_CODE_ACCESS_ACCEPT:
		rconf->access_accepts++;
		break;
	case RADIUS_CODE_ACCESS_REJECT:
		rconf->access_rejects++;
		break;
	case RADIUS_CODE_ACCESS_CHALLENGE:
		rconf->access_challenges++;
		break;
	case RADIUS_CODE_ACCOUNTING_RESPONSE:
		rconf->responses++;
		break;
	}

	prev_req = NULL;
	req = radius->msgs;
	while (req) {
		/* TODO: also match by src addr:port of the packet when using
		 * alternative RADIUS servers (?) */
		if ((req->msg_type == msg_type ||
		     (req->msg_type == RADIUS_ACCT_INTERIM &&
		      msg_type == RADIUS_ACCT)) &&
		    radius_msg_get_hdr(req->msg)->identifier ==
		    hdr->identifier)
			break;

		prev_req = req;
		req = req->next;
	}

	if (req == NULL) {
		hostapd_logger(radius->ctx, NULL, HOSTAPD_MODULE_RADIUS,
			       HOSTAPD_LEVEL_DEBUG,
			       "No matching RADIUS request found (type=%d "
			       "id=%d) - dropping packet",
			       msg_type, hdr->identifier);
		goto fail;
	}

	os_get_time(&now);
	roundtrip = (now.sec - req->last_attempt.sec) * 100 +
		(now.usec - req->last_attempt.usec) / 10000;
	hostapd_logger(radius->ctx, req->addr, HOSTAPD_MODULE_RADIUS,
		       HOSTAPD_LEVEL_DEBUG,
		       "Received RADIUS packet matched with a pending "
		       "request, round trip time %d.%02d sec",
		       roundtrip / 100, roundtrip % 100);
	rconf->round_trip_time = roundtrip;

	/* Remove ACKed RADIUS packet from retransmit list */
	if (prev_req)
		prev_req->next = req->next;
	else
		radius->msgs = req->next;
	radius->num_msgs--;

	for (i = 0; i < num_handlers; i++) {
		RadiusRxResult res;
		res = handlers[i].handler(msg, req->msg, req->shared_secret,
					  req->shared_secret_len,
					  handlers[i].data);
		switch (res) {
		case RADIUS_RX_PROCESSED:
			radius_msg_free(msg);
			/* continue */
		case RADIUS_RX_QUEUED:
			radius_client_msg_free(req);
			return;
		case RADIUS_RX_INVALID_AUTHENTICATOR:
			invalid_authenticator++;
			/* continue */
		case RADIUS_RX_UNKNOWN:
			/* continue with next handler */
			break;
		}
	}

	if (invalid_authenticator)
		rconf->bad_authenticators++;
	else
		rconf->unknown_types++;
	hostapd_logger(radius->ctx, req->addr, HOSTAPD_MODULE_RADIUS,
		       HOSTAPD_LEVEL_DEBUG, "No RADIUS RX handler found "
		       "(type=%d code=%d id=%d)%s - dropping packet",
		       msg_type, hdr->code, hdr->identifier,
		       invalid_authenticator ? " [INVALID AUTHENTICATOR]" :
		       "");
	radius_client_msg_free(req);

 fail:
	radius_msg_free(msg);
}


/**
 * radius_client_get_id - Get an identifier for a new RADIUS message
 * @radius: RADIUS client context from radius_client_init()
 * Returns: Allocated identifier
 *
 * This function is used to fetch a unique (among pending requests) identifier
 * for a new RADIUS message.
 */
u8 radius_client_get_id(struct radius_client_data *radius)
{
	struct radius_msg_list *entry, *prev, *_remove;
	u8 id = radius->next_radius_identifier++;

	/* remove entries with matching id from retransmit list to avoid
	 * using new reply from the RADIUS server with an old request */
	entry = radius->msgs;
	prev = NULL;
	while (entry) {
		if (radius_msg_get_hdr(entry->msg)->identifier == id) {
			hostapd_logger(radius->ctx, entry->addr,
				       HOSTAPD_MODULE_RADIUS,
				       HOSTAPD_LEVEL_DEBUG,
				       "Removing pending RADIUS message, "
				       "since its id (%d) is reused", id);
			if (prev)
				prev->next = entry->next;
			else
				radius->msgs = entry->next;
			_remove = entry;
		} else {
			_remove = NULL;
			prev = entry;
		}
		entry = entry->next;

		if (_remove)
			radius_client_msg_free(_remove);
	}

	return id;
}


/**
 * radius_client_flush - Flush all pending RADIUS client messages
 * @radius: RADIUS client context from radius_client_init()
 * @only_auth: Whether only authentication messages are removed
 */
void radius_client_flush(struct radius_client_data *radius, int only_auth)
{
	struct radius_msg_list *entry, *prev, *tmp;

	if (!radius)
		return;

	prev = NULL;
	entry = radius->msgs;

	while (entry) {
		if (!only_auth || entry->msg_type == RADIUS_AUTH) {
			if (prev)
				prev->next = entry->next;
			else
				radius->msgs = entry->next;

			tmp = entry;
			entry = entry->next;
			radius_client_msg_free(tmp);
			radius->num_msgs--;
		} else {
			prev = entry;
			entry = entry->next;
		}
	}

	if (radius->msgs == NULL)
		eloop_cancel_timeout(radius_client_timer, radius, NULL);
}


static void radius_client_update_acct_msgs(struct radius_client_data *radius,
					   const u8 *shared_secret,
					   size_t shared_secret_len)
{
	struct radius_msg_list *entry;

	if (!radius)
		return;

	for (entry = radius->msgs; entry; entry = entry->next) {
		if (entry->msg_type == RADIUS_ACCT) {
			entry->shared_secret = shared_secret;
			entry->shared_secret_len = shared_secret_len;
			radius_msg_finish_acct(entry->msg, shared_secret,
					       shared_secret_len);
		}
	}
}


static int
radius_change_server(struct radius_client_data *radius,
		     struct hostapd_radius_server *nserv,
		     struct hostapd_radius_server *oserv,
		     int sock, int sock6, int auth)
{
	struct sockaddr_in serv, claddr;
#ifdef CONFIG_IPV6
	struct sockaddr_in6 serv6, claddr6;
#endif /* CONFIG_IPV6 */
	struct sockaddr *addr, *cl_addr;
	socklen_t addrlen, claddrlen;
	char abuf[50];
	int sel_sock;
	struct radius_msg_list *entry;
	struct hostapd_radius_servers *conf = radius->conf;

	hostapd_logger(radius->ctx, NULL, HOSTAPD_MODULE_RADIUS,
		       HOSTAPD_LEVEL_INFO,
		       "%s server %s:%d",
		       auth ? "Authentication" : "Accounting",
		       hostapd_ip_txt(&nserv->addr, abuf, sizeof(abuf)),
		       nserv->port);

	if (!oserv || nserv->shared_secret_len != oserv->shared_secret_len ||
	    os_memcmp(nserv->shared_secret, oserv->shared_secret,
		      nserv->shared_secret_len) != 0) {
		/* Pending RADIUS packets used different shared secret, so
		 * they need to be modified. Update accounting message
		 * authenticators here. Authentication messages are removed
		 * since they would require more changes and the new RADIUS
		 * server may not be prepared to receive them anyway due to
		 * missing state information. Client will likely retry
		 * authentication, so this should not be an issue. */
		if (auth)
			radius_client_flush(radius, 1);
		else {
			radius_client_update_acct_msgs(
				radius, nserv->shared_secret,
				nserv->shared_secret_len);
		}
	}

	/* Reset retry counters for the new server */
	for (entry = radius->msgs; entry; entry = entry->next) {
		if ((auth && entry->msg_type != RADIUS_AUTH) ||
		    (!auth && entry->msg_type != RADIUS_ACCT))
			continue;
		entry->next_try = entry->first_try + RADIUS_CLIENT_FIRST_WAIT;
		entry->attempts = 0;
		entry->next_wait = RADIUS_CLIENT_FIRST_WAIT * 2;
	}

	if (radius->msgs) {
		eloop_cancel_timeout(radius_client_timer, radius, NULL);
		eloop_register_timeout(RADIUS_CLIENT_FIRST_WAIT, 0,
				       radius_client_timer, radius, NULL);
	}

	switch (nserv->addr.af) {
	case AF_INET:
		os_memset(&serv, 0, sizeof(serv));
		serv.sin_family = AF_INET;
		serv.sin_addr.s_addr = nserv->addr.u.v4.s_addr;
		serv.sin_port = htons(nserv->port);
		addr = (struct sockaddr *) &serv;
		addrlen = sizeof(serv);
		sel_sock = sock;
		break;
#ifdef CONFIG_IPV6
	case AF_INET6:
		os_memset(&serv6, 0, sizeof(serv6));
		serv6.sin6_family = AF_INET6;
		os_memcpy(&serv6.sin6_addr, &nserv->addr.u.v6,
			  sizeof(struct in6_addr));
		serv6.sin6_port = htons(nserv->port);
		addr = (struct sockaddr *) &serv6;
		addrlen = sizeof(serv6);
		sel_sock = sock6;
		break;
#endif /* CONFIG_IPV6 */
	default:
		return -1;
	}

	if (conf->force_client_addr) {
		switch (conf->client_addr.af) {
		case AF_INET:
			os_memset(&claddr, 0, sizeof(claddr));
			claddr.sin_family = AF_INET;
			claddr.sin_addr.s_addr = conf->client_addr.u.v4.s_addr;
			claddr.sin_port = htons(0);
			cl_addr = (struct sockaddr *) &claddr;
			claddrlen = sizeof(claddr);
			break;
#ifdef CONFIG_IPV6
		case AF_INET6:
			os_memset(&claddr6, 0, sizeof(claddr6));
			claddr6.sin6_family = AF_INET6;
			os_memcpy(&claddr6.sin6_addr, &conf->client_addr.u.v6,
				  sizeof(struct in6_addr));
			claddr6.sin6_port = htons(0);
			cl_addr = (struct sockaddr *) &claddr6;
			claddrlen = sizeof(claddr6);
			break;
#endif /* CONFIG_IPV6 */
		default:
			return -1;
		}

		if (bind(sel_sock, cl_addr, claddrlen) < 0) {
			perror("bind[radius]");
			return -1;
		}
	}

	if (connect(sel_sock, addr, addrlen) < 0) {
		perror("connect[radius]");
		return -1;
	}

#ifndef CONFIG_NATIVE_WINDOWS
	switch (nserv->addr.af) {
	case AF_INET:
		claddrlen = sizeof(claddr);
		getsockname(sel_sock, (struct sockaddr *) &claddr, &claddrlen);
		wpa_printf(MSG_DEBUG, "RADIUS local address: %s:%u",
			   inet_ntoa(claddr.sin_addr), ntohs(claddr.sin_port));
		break;
#ifdef CONFIG_IPV6
	case AF_INET6: {
		claddrlen = sizeof(claddr6);
		getsockname(sel_sock, (struct sockaddr *) &claddr6,
			    &claddrlen);
		wpa_printf(MSG_DEBUG, "RADIUS local address: %s:%u",
			   inet_ntop(AF_INET6, &claddr6.sin6_addr,
				     abuf, sizeof(abuf)),
			   ntohs(claddr6.sin6_port));
		break;
	}
#endif /* CONFIG_IPV6 */
	}
#endif /* CONFIG_NATIVE_WINDOWS */

	if (auth)
		radius->auth_sock = sel_sock;
	else
		radius->acct_sock = sel_sock;

	return 0;
}


static void radius_retry_primary_timer(void *eloop_ctx, void *timeout_ctx)
{
	struct radius_client_data *radius = eloop_ctx;
	struct hostapd_radius_servers *conf = radius->conf;
	struct hostapd_radius_server *oserv;

	if (radius->auth_sock >= 0 && conf->auth_servers &&
	    conf->auth_server != conf->auth_servers) {
		oserv = conf->auth_server;
		conf->auth_server = conf->auth_servers;
		radius_change_server(radius, conf->auth_server, oserv,
				     radius->auth_serv_sock,
				     radius->auth_serv_sock6, 1);
	}

	if (radius->acct_sock >= 0 && conf->acct_servers &&
	    conf->acct_server != conf->acct_servers) {
		oserv = conf->acct_server;
		conf->acct_server = conf->acct_servers;
		radius_change_server(radius, conf->acct_server, oserv,
				     radius->acct_serv_sock,
				     radius->acct_serv_sock6, 0);
	}

	if (conf->retry_primary_interval)
		eloop_register_timeout(conf->retry_primary_interval, 0,
				       radius_retry_primary_timer, radius,
				       NULL);
}


static int radius_client_disable_pmtu_discovery(int s)
{
	int r = -1;
#if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DONT)
	/* Turn off Path MTU discovery on IPv4/UDP sockets. */
	int action = IP_PMTUDISC_DONT;
	r = setsockopt(s, IPPROTO_IP, IP_MTU_DISCOVER, &action,
		       sizeof(action));
	if (r == -1)
		wpa_printf(MSG_ERROR, "Failed to set IP_MTU_DISCOVER: "
			   "%s", strerror(errno));
#endif
	return r;
}


static int radius_client_init_auth(struct radius_client_data *radius)
{
	struct hostapd_radius_servers *conf = radius->conf;
	int ok = 0;

	radius->auth_serv_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (radius->auth_serv_sock < 0)
		perror("socket[PF_INET,SOCK_DGRAM]");
	else {
		radius_client_disable_pmtu_discovery(radius->auth_serv_sock);
		ok++;
	}

#ifdef CONFIG_IPV6
	radius->auth_serv_sock6 = socket(PF_INET6, SOCK_DGRAM, 0);
	if (radius->auth_serv_sock6 < 0)
		perror("socket[PF_INET6,SOCK_DGRAM]");
	else
		ok++;
#endif /* CONFIG_IPV6 */

	if (ok == 0)
		return -1;

	radius_change_server(radius, conf->auth_server, NULL,
			     radius->auth_serv_sock, radius->auth_serv_sock6,
			     1);

	if (radius->auth_serv_sock >= 0 &&
	    eloop_register_read_sock(radius->auth_serv_sock,
				     radius_client_receive, radius,
				     (void *) RADIUS_AUTH)) {
		printf("Could not register read socket for authentication "
		       "server\n");
		return -1;
	}

#ifdef CONFIG_IPV6
	if (radius->auth_serv_sock6 >= 0 &&
	    eloop_register_read_sock(radius->auth_serv_sock6,
				     radius_client_receive, radius,
				     (void *) RADIUS_AUTH)) {
		printf("Could not register read socket for authentication "
		       "server\n");
		return -1;
	}
#endif /* CONFIG_IPV6 */

	return 0;
}


static int radius_client_init_acct(struct radius_client_data *radius)
{
	struct hostapd_radius_servers *conf = radius->conf;
	int ok = 0;

	radius->acct_serv_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (radius->acct_serv_sock < 0)
		perror("socket[PF_INET,SOCK_DGRAM]");
	else {
		radius_client_disable_pmtu_discovery(radius->acct_serv_sock);
		ok++;
	}

#ifdef CONFIG_IPV6
	radius->acct_serv_sock6 = socket(PF_INET6, SOCK_DGRAM, 0);
	if (radius->acct_serv_sock6 < 0)
		perror("socket[PF_INET6,SOCK_DGRAM]");
	else
		ok++;
#endif /* CONFIG_IPV6 */

	if (ok == 0)
		return -1;

	radius_change_server(radius, conf->acct_server, NULL,
			     radius->acct_serv_sock, radius->acct_serv_sock6,
			     0);

	if (radius->acct_serv_sock >= 0 &&
	    eloop_register_read_sock(radius->acct_serv_sock,
				     radius_client_receive, radius,
				     (void *) RADIUS_ACCT)) {
		printf("Could not register read socket for accounting "
		       "server\n");
		return -1;
	}

#ifdef CONFIG_IPV6
	if (radius->acct_serv_sock6 >= 0 &&
	    eloop_register_read_sock(radius->acct_serv_sock6,
				     radius_client_receive, radius,
				     (void *) RADIUS_ACCT)) {
		printf("Could not register read socket for accounting "
		       "server\n");
		return -1;
	}
#endif /* CONFIG_IPV6 */

	return 0;
}


/**
 * radius_client_init - Initialize RADIUS client
 * @ctx: Callback context to be used in hostapd_logger() calls
 * @conf: RADIUS client configuration (RADIUS servers)
 * Returns: Pointer to private RADIUS client context or %NULL on failure
 *
 * The caller is responsible for keeping the configuration data available for
 * the lifetime of the RADIUS client, i.e., until radius_client_deinit() is
 * called for the returned context pointer.
 */
struct radius_client_data *
radius_client_init(void *ctx, struct hostapd_radius_servers *conf)
{
	struct radius_client_data *radius;

	radius = os_zalloc(sizeof(struct radius_client_data));
	if (radius == NULL)
		return NULL;

	radius->ctx = ctx;
	radius->conf = conf;
	radius->auth_serv_sock = radius->acct_serv_sock =
		radius->auth_serv_sock6 = radius->acct_serv_sock6 =
		radius->auth_sock = radius->acct_sock = -1;

	if (conf->auth_server && radius_client_init_auth(radius)) {
		radius_client_deinit(radius);
		return NULL;
	}

	if (conf->acct_server && radius_client_init_acct(radius)) {
		radius_client_deinit(radius);
		return NULL;
	}

	if (conf->retry_primary_interval)
		eloop_register_timeout(conf->retry_primary_interval, 0,
				       radius_retry_primary_timer, radius,
				       NULL);

	return radius;
}


/**
 * radius_client_deinit - Deinitialize RADIUS client
 * @radius: RADIUS client context from radius_client_init()
 */
void radius_client_deinit(struct radius_client_data *radius)
{
	if (!radius)
		return;

	if (radius->auth_serv_sock >= 0)
		eloop_unregister_read_sock(radius->auth_serv_sock);
	if (radius->acct_serv_sock >= 0)
		eloop_unregister_read_sock(radius->acct_serv_sock);
#ifdef CONFIG_IPV6
	if (radius->auth_serv_sock6 >= 0)
		eloop_unregister_read_sock(radius->auth_serv_sock6);
	if (radius->acct_serv_sock6 >= 0)
		eloop_unregister_read_sock(radius->acct_serv_sock6);
#endif /* CONFIG_IPV6 */

	eloop_cancel_timeout(radius_retry_primary_timer, radius, NULL);

	radius_client_flush(radius, 0);
	os_free(radius->auth_handlers);
	os_free(radius->acct_handlers);
	os_free(radius);
}


/**
 * radius_client_flush_auth - Flush pending RADIUS messages for an address
 * @radius: RADIUS client context from radius_client_init()
 * @addr: MAC address of the related device
 *
 * This function can be used to remove pending RADIUS authentication messages
 * that are related to a specific device. The addr parameter is matched with
 * the one used in radius_client_send() call that was used to transmit the
 * authentication request.
 */
void radius_client_flush_auth(struct radius_client_data *radius,
			      const u8 *addr)
{
	struct radius_msg_list *entry, *prev, *tmp;

	prev = NULL;
	entry = radius->msgs;
	while (entry) {
		if (entry->msg_type == RADIUS_AUTH &&
		    os_memcmp(entry->addr, addr, ETH_ALEN) == 0) {
			hostapd_logger(radius->ctx, addr,
				       HOSTAPD_MODULE_RADIUS,
				       HOSTAPD_LEVEL_DEBUG,
				       "Removing pending RADIUS authentication"
				       " message for removed client");

			if (prev)
				prev->next = entry->next;
			else
				radius->msgs = entry->next;

			tmp = entry;
			entry = entry->next;
			radius_client_msg_free(tmp);
			radius->num_msgs--;
			continue;
		}

		prev = entry;
		entry = entry->next;
	}
}


static int radius_client_dump_auth_server(char *buf, size_t buflen,
					  struct hostapd_radius_server *serv,
					  struct radius_client_data *cli)
{
	int pending = 0;
	struct radius_msg_list *msg;
	char abuf[50];

	if (cli) {
		for (msg = cli->msgs; msg; msg = msg->next) {
			if (msg->msg_type == RADIUS_AUTH)
				pending++;
		}
	}

	return os_snprintf(buf, buflen,
			   "radiusAuthServerIndex=%d\n"
			   "radiusAuthServerAddress=%s\n"
			   "radiusAuthClientServerPortNumber=%d\n"
			   "radiusAuthClientRoundTripTime=%d\n"
			   "radiusAuthClientAccessRequests=%u\n"
			   "radiusAuthClientAccessRetransmissions=%u\n"
			   "radiusAuthClientAccessAccepts=%u\n"
			   "radiusAuthClientAccessRejects=%u\n"
			   "radiusAuthClientAccessChallenges=%u\n"
			   "radiusAuthClientMalformedAccessResponses=%u\n"
			   "radiusAuthClientBadAuthenticators=%u\n"
			   "radiusAuthClientPendingRequests=%u\n"
			   "radiusAuthClientTimeouts=%u\n"
			   "radiusAuthClientUnknownTypes=%u\n"
			   "radiusAuthClientPacketsDropped=%u\n",
			   serv->index,
			   hostapd_ip_txt(&serv->addr, abuf, sizeof(abuf)),
			   serv->port,
			   serv->round_trip_time,
			   serv->requests,
			   serv->retransmissions,
			   serv->access_accepts,
			   serv->access_rejects,
			   serv->access_challenges,
			   serv->malformed_responses,
			   serv->bad_authenticators,
			   pending,
			   serv->timeouts,
			   serv->unknown_types,
			   serv->packets_dropped);
}


static int radius_client_dump_acct_server(char *buf, size_t buflen,
					  struct hostapd_radius_server *serv,
					  struct radius_client_data *cli)
{
	int pending = 0;
	struct radius_msg_list *msg;
	char abuf[50];

	if (cli) {
		for (msg = cli->msgs; msg; msg = msg->next) {
			if (msg->msg_type == RADIUS_ACCT ||
			    msg->msg_type == RADIUS_ACCT_INTERIM)
				pending++;
		}
	}

	return os_snprintf(buf, buflen,
			   "radiusAccServerIndex=%d\n"
			   "radiusAccServerAddress=%s\n"
			   "radiusAccClientServerPortNumber=%d\n"
			   "radiusAccClientRoundTripTime=%d\n"
			   "radiusAccClientRequests=%u\n"
			   "radiusAccClientRetransmissions=%u\n"
			   "radiusAccClientResponses=%u\n"
			   "radiusAccClientMalformedResponses=%u\n"
			   "radiusAccClientBadAuthenticators=%u\n"
			   "radiusAccClientPendingRequests=%u\n"
			   "radiusAccClientTimeouts=%u\n"
			   "radiusAccClientUnknownTypes=%u\n"
			   "radiusAccClientPacketsDropped=%u\n",
			   serv->index,
			   hostapd_ip_txt(&serv->addr, abuf, sizeof(abuf)),
			   serv->port,
			   serv->round_trip_time,
			   serv->requests,
			   serv->retransmissions,
			   serv->responses,
			   serv->malformed_responses,
			   serv->bad_authenticators,
			   pending,
			   serv->timeouts,
			   serv->unknown_types,
			   serv->packets_dropped);
}


/**
 * radius_client_get_mib - Get RADIUS client MIB information
 * @radius: RADIUS client context from radius_client_init()
 * @buf: Buffer for returning MIB data in text format
 * @buflen: Maximum buf length in octets
 * Returns: Number of octets written into the buffer
 */
int radius_client_get_mib(struct radius_client_data *radius, char *buf,
			  size_t buflen)
{
	struct hostapd_radius_servers *conf = radius->conf;
	int i;
	struct hostapd_radius_server *serv;
	int count = 0;

	if (conf->auth_servers) {
		for (i = 0; i < conf->num_auth_servers; i++) {
			serv = &conf->auth_servers[i];
			count += radius_client_dump_auth_server(
				buf + count, buflen - count, serv,
				serv == conf->auth_server ?
				radius : NULL);
		}
	}

	if (conf->acct_servers) {
		for (i = 0; i < conf->num_acct_servers; i++) {
			serv = &conf->acct_servers[i];
			count += radius_client_dump_acct_server(
				buf + count, buflen - count, serv,
				serv == conf->acct_server ?
				radius : NULL);
		}
	}

	return count;
}


void radius_client_reconfig(struct radius_client_data *radius,
			    struct hostapd_radius_servers *conf)
{
	if (radius)
		radius->conf = conf;
}
