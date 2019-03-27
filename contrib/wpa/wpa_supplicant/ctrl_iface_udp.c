/*
 * WPA Supplicant / UDP socket -based control interface
 * Copyright (c) 2004-2016, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eloop.h"
#include "config.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "wpa_supplicant_i.h"
#include "ctrl_iface.h"
#include "common/wpa_ctrl.h"


#define COOKIE_LEN 8

/* Per-interface ctrl_iface */

/**
 * struct wpa_ctrl_dst - Internal data structure of control interface monitors
 *
 * This structure is used to store information about registered control
 * interface monitors into struct wpa_supplicant. This data is private to
 * ctrl_iface_udp.c and should not be touched directly from other files.
 */
struct wpa_ctrl_dst {
	struct wpa_ctrl_dst *next;
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
	struct sockaddr_in6 addr;
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
	struct sockaddr_in addr;
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
	socklen_t addrlen;
	int debug_level;
	int errors;
};


struct ctrl_iface_priv {
	struct wpa_supplicant *wpa_s;
	int sock;
	struct wpa_ctrl_dst *ctrl_dst;
	u8 cookie[COOKIE_LEN];
};

struct ctrl_iface_global_priv {
	int sock;
	struct wpa_ctrl_dst *ctrl_dst;
	u8 cookie[COOKIE_LEN];
};


static void wpa_supplicant_ctrl_iface_send(struct wpa_supplicant *wpa_s,
					   const char *ifname, int sock,
					   struct wpa_ctrl_dst **head,
					   int level, const char *buf,
					   size_t len);


static void wpas_ctrl_iface_free_dst(struct wpa_ctrl_dst *dst)
{
	struct wpa_ctrl_dst *prev;

	while (dst) {
		prev = dst;
		dst = dst->next;
		os_free(prev);
	}
}


static int wpa_supplicant_ctrl_iface_attach(struct wpa_ctrl_dst **head,
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
					    struct sockaddr_in6 *from,
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
					    struct sockaddr_in *from,
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
					    socklen_t fromlen)
{
	struct wpa_ctrl_dst *dst;
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
	char addr[INET6_ADDRSTRLEN];
#endif /* CONFIG_UDP_IPV6 */

	dst = os_zalloc(sizeof(*dst));
	if (dst == NULL)
		return -1;
	os_memcpy(&dst->addr, from, sizeof(*from));
	dst->addrlen = fromlen;
	dst->debug_level = MSG_INFO;
	dst->next = *head;
	*head = dst;
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
	wpa_printf(MSG_DEBUG, "CTRL_IFACE monitor attached %s:%d",
		   inet_ntop(AF_INET6, &from->sin6_addr, addr, sizeof(*from)),
		   ntohs(from->sin6_port));
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
	wpa_printf(MSG_DEBUG, "CTRL_IFACE monitor attached %s:%d",
		   inet_ntoa(from->sin_addr), ntohs(from->sin_port));
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
	return 0;
}


static int wpa_supplicant_ctrl_iface_detach(struct wpa_ctrl_dst **head,
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
					    struct sockaddr_in6 *from,
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
					    struct sockaddr_in *from,
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
					    socklen_t fromlen)
{
	struct wpa_ctrl_dst *dst, *prev = NULL;
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
	char addr[INET6_ADDRSTRLEN];
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */

	dst = *head;
	while (dst) {
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
		if (from->sin6_port == dst->addr.sin6_port &&
		    !os_memcmp(&from->sin6_addr, &dst->addr.sin6_addr,
			       sizeof(from->sin6_addr))) {
			wpa_printf(MSG_DEBUG, "CTRL_IFACE monitor detached %s:%d",
				   inet_ntop(AF_INET6, &from->sin6_addr, addr,
					     sizeof(*from)),
				   ntohs(from->sin6_port));
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
		if (from->sin_addr.s_addr == dst->addr.sin_addr.s_addr &&
		    from->sin_port == dst->addr.sin_port) {
			wpa_printf(MSG_DEBUG, "CTRL_IFACE monitor detached "
				   "%s:%d", inet_ntoa(from->sin_addr),
				   ntohs(from->sin_port));
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
			if (prev == NULL)
				*head = dst->next;
			else
				prev->next = dst->next;
			os_free(dst);
			return 0;
		}
		prev = dst;
		dst = dst->next;
	}
	return -1;
}


static int wpa_supplicant_ctrl_iface_level(struct ctrl_iface_priv *priv,
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
					   struct sockaddr_in6 *from,
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
					   struct sockaddr_in *from,
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
					   socklen_t fromlen,
					   char *level)
{
	struct wpa_ctrl_dst *dst;
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
	char addr[INET6_ADDRSTRLEN];
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */

	wpa_printf(MSG_DEBUG, "CTRL_IFACE LEVEL %s", level);

	dst = priv->ctrl_dst;
	while (dst) {
#if CONFIG_CTRL_IFACE_UDP_IPV6
		if (from->sin6_port == dst->addr.sin6_port &&
		    !os_memcmp(&from->sin6_addr, &dst->addr.sin6_addr,
			       sizeof(from->sin6_addr))) {
			wpa_printf(MSG_DEBUG, "CTRL_IFACE changed monitor level %s:%d",
				   inet_ntop(AF_INET6, &from->sin6_addr, addr,
					     sizeof(*from)),
				   ntohs(from->sin6_port));
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
		if (from->sin_addr.s_addr == dst->addr.sin_addr.s_addr &&
		    from->sin_port == dst->addr.sin_port) {
			wpa_printf(MSG_DEBUG, "CTRL_IFACE changed monitor "
				   "level %s:%d", inet_ntoa(from->sin_addr),
				   ntohs(from->sin_port));
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
			dst->debug_level = atoi(level);
			return 0;
		}
		dst = dst->next;
	}

	return -1;
}


static char *
wpa_supplicant_ctrl_iface_get_cookie(struct ctrl_iface_priv *priv,
				     size_t *reply_len)
{
	char *reply;
	reply = os_malloc(7 + 2 * COOKIE_LEN + 1);
	if (reply == NULL) {
		*reply_len = 1;
		return NULL;
	}

	os_memcpy(reply, "COOKIE=", 7);
	wpa_snprintf_hex(reply + 7, 2 * COOKIE_LEN + 1,
			 priv->cookie, COOKIE_LEN);

	*reply_len = 7 + 2 * COOKIE_LEN;
	return reply;
}


static void wpa_supplicant_ctrl_iface_receive(int sock, void *eloop_ctx,
					      void *sock_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	struct ctrl_iface_priv *priv = sock_ctx;
	char buf[4096], *pos;
	int res;
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
	struct sockaddr_in6 from;
#ifndef CONFIG_CTRL_IFACE_UDP_REMOTE
	char addr[INET6_ADDRSTRLEN];
#endif /* CONFIG_CTRL_IFACE_UDP_REMOTE */
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
	struct sockaddr_in from;
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
	socklen_t fromlen = sizeof(from);
	char *reply = NULL;
	size_t reply_len = 0;
	int new_attached = 0;
	u8 cookie[COOKIE_LEN];

	res = recvfrom(sock, buf, sizeof(buf) - 1, 0,
		       (struct sockaddr *) &from, &fromlen);
	if (res < 0) {
		wpa_printf(MSG_ERROR, "recvfrom(ctrl_iface): %s",
			   strerror(errno));
		return;
	}

#ifndef CONFIG_CTRL_IFACE_UDP_REMOTE
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
	inet_ntop(AF_INET6, &from.sin6_addr, addr, sizeof(from));
	if (os_strcmp(addr, "::1")) {
		wpa_printf(MSG_DEBUG, "CTRL: Drop packet from unexpected source %s",
			   addr);
	}
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
	if (from.sin_addr.s_addr != htonl((127 << 24) | 1)) {
		/*
		 * The OS networking stack is expected to drop this kind of
		 * frames since the socket is bound to only localhost address.
		 * Just in case, drop the frame if it is coming from any other
		 * address.
		 */
		wpa_printf(MSG_DEBUG, "CTRL: Drop packet from unexpected "
			   "source %s", inet_ntoa(from.sin_addr));
		return;
	}
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
#endif /* CONFIG_CTRL_IFACE_UDP_REMOTE */

	buf[res] = '\0';

	if (os_strcmp(buf, "GET_COOKIE") == 0) {
		reply = wpa_supplicant_ctrl_iface_get_cookie(priv, &reply_len);
		goto done;
	}

	/*
	 * Require that the client includes a prefix with the 'cookie' value
	 * fetched with GET_COOKIE command. This is used to verify that the
	 * client has access to a bidirectional link over UDP in order to
	 * avoid attacks using forged localhost IP address even if the OS does
	 * not block such frames from remote destinations.
	 */
	if (os_strncmp(buf, "COOKIE=", 7) != 0) {
		wpa_printf(MSG_DEBUG, "CTLR: No cookie in the request - "
			   "drop request");
		return;
	}

	if (hexstr2bin(buf + 7, cookie, COOKIE_LEN) < 0) {
		wpa_printf(MSG_DEBUG, "CTLR: Invalid cookie format in the "
			   "request - drop request");
		return;
	}

	if (os_memcmp(cookie, priv->cookie, COOKIE_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "CTLR: Invalid cookie in the request - "
			   "drop request");
		return;
	}

	pos = buf + 7 + 2 * COOKIE_LEN;
	while (*pos == ' ')
		pos++;

	if (os_strcmp(pos, "ATTACH") == 0) {
		if (wpa_supplicant_ctrl_iface_attach(&priv->ctrl_dst,
						     &from, fromlen))
			reply_len = 1;
		else {
			new_attached = 1;
			reply_len = 2;
		}
	} else if (os_strcmp(pos, "DETACH") == 0) {
		if (wpa_supplicant_ctrl_iface_detach(&priv->ctrl_dst,
						     &from, fromlen))
			reply_len = 1;
		else
			reply_len = 2;
	} else if (os_strncmp(pos, "LEVEL ", 6) == 0) {
		if (wpa_supplicant_ctrl_iface_level(priv, &from, fromlen,
						    pos + 6))
			reply_len = 1;
		else
			reply_len = 2;
	} else {
		reply = wpa_supplicant_ctrl_iface_process(wpa_s, pos,
							  &reply_len);
	}

 done:
	if (reply) {
		sendto(sock, reply, reply_len, 0, (struct sockaddr *) &from,
		       fromlen);
		os_free(reply);
	} else if (reply_len == 1) {
		sendto(sock, "FAIL\n", 5, 0, (struct sockaddr *) &from,
		       fromlen);
	} else if (reply_len == 2) {
		sendto(sock, "OK\n", 3, 0, (struct sockaddr *) &from,
		       fromlen);
	}

	if (new_attached)
		eapol_sm_notify_ctrl_attached(wpa_s->eapol);
}


static void wpa_supplicant_ctrl_iface_msg_cb(void *ctx, int level,
					     enum wpa_msg_type type,
					     const char *txt, size_t len)
{
	struct wpa_supplicant *wpa_s = ctx;

	if (!wpa_s)
		return;

	if (type != WPA_MSG_NO_GLOBAL && wpa_s->global->ctrl_iface) {
		struct ctrl_iface_global_priv *priv = wpa_s->global->ctrl_iface;

		if (priv->ctrl_dst) {
			wpa_supplicant_ctrl_iface_send(
				wpa_s,
				type != WPA_MSG_PER_INTERFACE ?
				NULL : wpa_s->ifname,
				priv->sock, &priv->ctrl_dst, level, txt, len);
		}
	}

	if (type == WPA_MSG_ONLY_GLOBAL || !wpa_s->ctrl_iface)
		return;

	wpa_supplicant_ctrl_iface_send(wpa_s, NULL, wpa_s->ctrl_iface->sock,
				       &wpa_s->ctrl_iface->ctrl_dst,
				       level, txt, len);
}


struct ctrl_iface_priv *
wpa_supplicant_ctrl_iface_init(struct wpa_supplicant *wpa_s)
{
	struct ctrl_iface_priv *priv;
	char port_str[40];
	int port = WPA_CTRL_IFACE_PORT;
	char *pos;
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
	struct sockaddr_in6 addr;
	int domain = PF_INET6;
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
	struct sockaddr_in addr;
	int domain = PF_INET;
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */

	priv = os_zalloc(sizeof(*priv));
	if (priv == NULL)
		return NULL;
	priv->wpa_s = wpa_s;
	priv->sock = -1;
	os_get_random(priv->cookie, COOKIE_LEN);

	if (wpa_s->conf->ctrl_interface == NULL)
		return priv;

	pos = os_strstr(wpa_s->conf->ctrl_interface, "udp:");
	if (pos) {
		pos += 4;
		port = atoi(pos);
		if (port <= 0) {
			wpa_printf(MSG_ERROR, "Invalid ctrl_iface UDP port: %s",
				   wpa_s->conf->ctrl_interface);
			goto fail;
		}
	}

	priv->sock = socket(domain, SOCK_DGRAM, 0);
	if (priv->sock < 0) {
		wpa_printf(MSG_ERROR, "socket(PF_INET): %s", strerror(errno));
		goto fail;
	}

	os_memset(&addr, 0, sizeof(addr));
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
	addr.sin6_family = AF_INET6;
#ifdef CONFIG_CTRL_IFACE_UDP_REMOTE
	addr.sin6_addr = in6addr_any;
#else /* CONFIG_CTRL_IFACE_UDP_REMOTE */
	inet_pton(AF_INET6, "::1", &addr.sin6_addr);
#endif /* CONFIG_CTRL_IFACE_UDP_REMOTE */
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
	addr.sin_family = AF_INET;
#ifdef CONFIG_CTRL_IFACE_UDP_REMOTE
	addr.sin_addr.s_addr = INADDR_ANY;
#else /* CONFIG_CTRL_IFACE_UDP_REMOTE */
	addr.sin_addr.s_addr = htonl((127 << 24) | 1);
#endif /* CONFIG_CTRL_IFACE_UDP_REMOTE */
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
try_again:
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
	addr.sin6_port = htons(port);
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
	addr.sin_port = htons(port);
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
	if (bind(priv->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		port--;
		if ((WPA_CTRL_IFACE_PORT - port) < WPA_CTRL_IFACE_PORT_LIMIT)
			goto try_again;
		wpa_printf(MSG_ERROR, "bind(AF_INET): %s", strerror(errno));
		goto fail;
	}

	/* Update the ctrl_interface value to match the selected port */
	os_snprintf(port_str, sizeof(port_str), "udp:%d", port);
	os_free(wpa_s->conf->ctrl_interface);
	wpa_s->conf->ctrl_interface = os_strdup(port_str);
	if (!wpa_s->conf->ctrl_interface) {
		wpa_msg(wpa_s, MSG_ERROR, "Failed to malloc ctrl_interface");
		goto fail;
	}

#ifdef CONFIG_CTRL_IFACE_UDP_REMOTE
	wpa_msg(wpa_s, MSG_DEBUG, "ctrl_iface_init UDP port: %d", port);
#endif /* CONFIG_CTRL_IFACE_UDP_REMOTE */

	eloop_register_read_sock(priv->sock, wpa_supplicant_ctrl_iface_receive,
				 wpa_s, priv);
	wpa_msg_register_cb(wpa_supplicant_ctrl_iface_msg_cb);

	return priv;

fail:
	if (priv->sock >= 0)
		close(priv->sock);
	os_free(priv);
	return NULL;
}


void wpa_supplicant_ctrl_iface_deinit(struct ctrl_iface_priv *priv)
{
	if (priv->sock > -1) {
		eloop_unregister_read_sock(priv->sock);
		if (priv->ctrl_dst) {
			/*
			 * Wait before closing the control socket if
			 * there are any attached monitors in order to allow
			 * them to receive any pending messages.
			 */
			wpa_printf(MSG_DEBUG, "CTRL_IFACE wait for attached "
				   "monitors to receive messages");
			os_sleep(0, 100000);
		}
		close(priv->sock);
		priv->sock = -1;
	}

	wpas_ctrl_iface_free_dst(priv->ctrl_dst);
	os_free(priv);
}


static void wpa_supplicant_ctrl_iface_send(struct wpa_supplicant *wpa_s,
					   const char *ifname, int sock,
					   struct wpa_ctrl_dst **head,
					   int level, const char *buf,
					   size_t len)
{
	struct wpa_ctrl_dst *dst, *next;
	char levelstr[64];
	int idx;
	char *sbuf;
	int llen;
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
	char addr[INET6_ADDRSTRLEN];
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */

	dst = *head;
	if (sock < 0 || dst == NULL)
		return;

	if (ifname)
		os_snprintf(levelstr, sizeof(levelstr), "IFACE=%s <%d>",
			    ifname, level);
	else
		os_snprintf(levelstr, sizeof(levelstr), "<%d>", level);

	llen = os_strlen(levelstr);
	sbuf = os_malloc(llen + len);
	if (sbuf == NULL)
		return;

	os_memcpy(sbuf, levelstr, llen);
	os_memcpy(sbuf + llen, buf, len);

	idx = 0;
	while (dst) {
		next = dst->next;
		if (level >= dst->debug_level) {
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
			wpa_printf(MSG_DEBUG, "CTRL_IFACE monitor send %s:%d",
				   inet_ntop(AF_INET6, &dst->addr.sin6_addr,
					     addr, sizeof(dst->addr)),
				   ntohs(dst->addr.sin6_port));
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
			wpa_printf(MSG_DEBUG, "CTRL_IFACE monitor send %s:%d",
				   inet_ntoa(dst->addr.sin_addr),
				   ntohs(dst->addr.sin_port));
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
			if (sendto(sock, sbuf, llen + len, 0,
				   (struct sockaddr *) &dst->addr,
				   sizeof(dst->addr)) < 0) {
				wpa_printf(MSG_ERROR,
					   "sendto(CTRL_IFACE monitor): %s",
					   strerror(errno));
				dst->errors++;
				if (dst->errors > 10) {
					wpa_supplicant_ctrl_iface_detach(
						head, &dst->addr,
						dst->addrlen);
				}
			} else
				dst->errors = 0;
		}
		idx++;
		dst = next;
	}
	os_free(sbuf);
}


void wpa_supplicant_ctrl_iface_wait(struct ctrl_iface_priv *priv)
{
	wpa_printf(MSG_DEBUG, "CTRL_IFACE - %s - wait for monitor",
		   priv->wpa_s->ifname);
	eloop_wait_for_read_sock(priv->sock);
}


/* Global ctrl_iface */

static char *
wpa_supplicant_global_get_cookie(struct ctrl_iface_global_priv *priv,
				 size_t *reply_len)
{
	char *reply;
	reply = os_malloc(7 + 2 * COOKIE_LEN + 1);
	if (reply == NULL) {
		*reply_len = 1;
		return NULL;
	}

	os_memcpy(reply, "COOKIE=", 7);
	wpa_snprintf_hex(reply + 7, 2 * COOKIE_LEN + 1,
			 priv->cookie, COOKIE_LEN);

	*reply_len = 7 + 2 * COOKIE_LEN;
	return reply;
}


static void wpa_supplicant_global_ctrl_iface_receive(int sock, void *eloop_ctx,
						     void *sock_ctx)
{
	struct wpa_global *global = eloop_ctx;
	struct ctrl_iface_global_priv *priv = sock_ctx;
	char buf[4096], *pos;
	int res;
#ifdef CONFIG_CTRL_IFACE_UDP_IPV6
	struct sockaddr_in6 from;
#else /* CONFIG_CTRL_IFACE_UDP_IPV6 */
	struct sockaddr_in from;
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
	socklen_t fromlen = sizeof(from);
	char *reply = NULL;
	size_t reply_len;
	u8 cookie[COOKIE_LEN];

	res = recvfrom(sock, buf, sizeof(buf) - 1, 0,
		       (struct sockaddr *) &from, &fromlen);
	if (res < 0) {
		wpa_printf(MSG_ERROR, "recvfrom(ctrl_iface): %s",
			   strerror(errno));
		return;
	}

#ifndef CONFIG_CTRL_IFACE_UDP_REMOTE
#ifndef CONFIG_CTRL_IFACE_UDP_IPV6
	if (from.sin_addr.s_addr != htonl((127 << 24) | 1)) {
		/*
		 * The OS networking stack is expected to drop this kind of
		 * frames since the socket is bound to only localhost address.
		 * Just in case, drop the frame if it is coming from any other
		 * address.
		 */
		wpa_printf(MSG_DEBUG, "CTRL: Drop packet from unexpected "
			   "source %s", inet_ntoa(from.sin_addr));
		return;
	}
#endif /* CONFIG_CTRL_IFACE_UDP_IPV6 */
#endif /* CONFIG_CTRL_IFACE_UDP_REMOTE */

	buf[res] = '\0';

	if (os_strcmp(buf, "GET_COOKIE") == 0) {
		reply = wpa_supplicant_global_get_cookie(priv, &reply_len);
		goto done;
	}

	if (os_strncmp(buf, "COOKIE=", 7) != 0) {
		wpa_printf(MSG_DEBUG, "CTLR: No cookie in the request - "
			   "drop request");
		return;
	}

	if (hexstr2bin(buf + 7, cookie, COOKIE_LEN) < 0) {
		wpa_printf(MSG_DEBUG, "CTLR: Invalid cookie format in the "
			   "request - drop request");
		return;
	}

	if (os_memcmp(cookie, priv->cookie, COOKIE_LEN) != 0) {
		wpa_printf(MSG_DEBUG, "CTLR: Invalid cookie in the request - "
			   "drop request");
		return;
	}

	pos = buf + 7 + 2 * COOKIE_LEN;
	while (*pos == ' ')
		pos++;

	if (os_strcmp(pos, "ATTACH") == 0) {
		if (wpa_supplicant_ctrl_iface_attach(&priv->ctrl_dst,
						     &from, fromlen))
			reply_len = 1;
		else
			reply_len = 2;
	} else if (os_strcmp(pos, "DETACH") == 0) {
		if (wpa_supplicant_ctrl_iface_detach(&priv->ctrl_dst,
						     &from, fromlen))
			reply_len = 1;
		else
			reply_len = 2;
	} else {
		reply = wpa_supplicant_global_ctrl_iface_process(global, pos,
								 &reply_len);
	}

 done:
	if (reply) {
		sendto(sock, reply, reply_len, 0, (struct sockaddr *) &from,
		       fromlen);
		os_free(reply);
	} else if (reply_len == 1) {
		sendto(sock, "FAIL\n", 5, 0, (struct sockaddr *) &from,
		       fromlen);
	} else if (reply_len == 2) {
		sendto(sock, "OK\n", 3, 0, (struct sockaddr *) &from,
		       fromlen);
	}
}


struct ctrl_iface_global_priv *
wpa_supplicant_global_ctrl_iface_init(struct wpa_global *global)
{
	struct ctrl_iface_global_priv *priv;
	struct sockaddr_in addr;
	char *pos;
	int port = WPA_GLOBAL_CTRL_IFACE_PORT;

	priv = os_zalloc(sizeof(*priv));
	if (priv == NULL)
		return NULL;
	priv->sock = -1;
	os_get_random(priv->cookie, COOKIE_LEN);

	if (global->params.ctrl_interface == NULL)
		return priv;

	wpa_printf(MSG_DEBUG, "Global control interface '%s'",
		   global->params.ctrl_interface);

	pos = os_strstr(global->params.ctrl_interface, "udp:");
	if (pos) {
		pos += 4;
		port = atoi(pos);
		if (port <= 0) {
			wpa_printf(MSG_ERROR, "Invalid global ctrl UDP port %s",
				   global->params.ctrl_interface);
			goto fail;
		}
	}

	priv->sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (priv->sock < 0) {
		wpa_printf(MSG_ERROR, "socket(PF_INET): %s", strerror(errno));
		goto fail;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
#ifdef CONFIG_CTRL_IFACE_UDP_REMOTE
	addr.sin_addr.s_addr = INADDR_ANY;
#else /* CONFIG_CTRL_IFACE_UDP_REMOTE */
	addr.sin_addr.s_addr = htonl((127 << 24) | 1);
#endif /* CONFIG_CTRL_IFACE_UDP_REMOTE */
try_again:
	addr.sin_port = htons(port);
	if (bind(priv->sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		port++;
		if ((port - WPA_GLOBAL_CTRL_IFACE_PORT) <
		    WPA_GLOBAL_CTRL_IFACE_PORT_LIMIT && !pos)
			goto try_again;
		wpa_printf(MSG_ERROR, "bind(AF_INET): %s", strerror(errno));
		goto fail;
	}

#ifdef CONFIG_CTRL_IFACE_UDP_REMOTE
	wpa_printf(MSG_DEBUG, "global_ctrl_iface_init UDP port: %d", port);
#endif /* CONFIG_CTRL_IFACE_UDP_REMOTE */

	eloop_register_read_sock(priv->sock,
				 wpa_supplicant_global_ctrl_iface_receive,
				 global, priv);
	wpa_msg_register_cb(wpa_supplicant_ctrl_iface_msg_cb);

	return priv;

fail:
	if (priv->sock >= 0)
		close(priv->sock);
	os_free(priv);
	return NULL;
}


void
wpa_supplicant_global_ctrl_iface_deinit(struct ctrl_iface_global_priv *priv)
{
	if (priv->sock >= 0) {
		eloop_unregister_read_sock(priv->sock);
		close(priv->sock);
	}

	wpas_ctrl_iface_free_dst(priv->ctrl_dst);
	os_free(priv);
}
