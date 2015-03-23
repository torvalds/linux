/*
 * WPA Supplicant - Layer2 packet handling with privilege separation
 * Copyright (c) 2007, Jouni Malinen <j@w1.fi>
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
#include <sys/un.h>

#include "common.h"
#include "eloop.h"
#include "l2_packet.h"
#include "common/privsep_commands.h"


struct l2_packet_data {
	int fd; /* UNIX domain socket for privsep access */
	void (*rx_callback)(void *ctx, const u8 *src_addr,
			    const u8 *buf, size_t len);
	void *rx_callback_ctx;
	u8 own_addr[ETH_ALEN];
	char *own_socket_path;
	struct sockaddr_un priv_addr;
};


static int wpa_priv_cmd(struct l2_packet_data *l2, int cmd,
			const void *data, size_t data_len)
{
	struct msghdr msg;
	struct iovec io[2];

	io[0].iov_base = &cmd;
	io[0].iov_len = sizeof(cmd);
	io[1].iov_base = (u8 *) data;
	io[1].iov_len = data_len;

	os_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = io;
	msg.msg_iovlen = data ? 2 : 1;
	msg.msg_name = &l2->priv_addr;
	msg.msg_namelen = sizeof(l2->priv_addr);

	if (sendmsg(l2->fd, &msg, 0) < 0) {
		perror("L2: sendmsg(cmd)");
		return -1;
	}

	return 0;
}

			     
int l2_packet_get_own_addr(struct l2_packet_data *l2, u8 *addr)
{
	os_memcpy(addr, l2->own_addr, ETH_ALEN);
	return 0;
}


int l2_packet_send(struct l2_packet_data *l2, const u8 *dst_addr, u16 proto,
		   const u8 *buf, size_t len)
{
	struct msghdr msg;
	struct iovec io[4];
	int cmd = PRIVSEP_CMD_L2_SEND;

	io[0].iov_base = &cmd;
	io[0].iov_len = sizeof(cmd);
	io[1].iov_base = &dst_addr;
	io[1].iov_len = ETH_ALEN;
	io[2].iov_base = &proto;
	io[2].iov_len = 2;
	io[3].iov_base = (u8 *) buf;
	io[3].iov_len = len;

	os_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = io;
	msg.msg_iovlen = 4;
	msg.msg_name = &l2->priv_addr;
	msg.msg_namelen = sizeof(l2->priv_addr);

	if (sendmsg(l2->fd, &msg, 0) < 0) {
		perror("L2: sendmsg(packet_send)");
		return -1;
	}

	return 0;
}


static void l2_packet_receive(int sock, void *eloop_ctx, void *sock_ctx)
{
	struct l2_packet_data *l2 = eloop_ctx;
	u8 buf[2300];
	int res;
	struct sockaddr_un from;
	socklen_t fromlen = sizeof(from);

	os_memset(&from, 0, sizeof(from));
	res = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *) &from,
		       &fromlen);
	if (res < 0) {
		perror("l2_packet_receive - recvfrom");
		return;
	}
	if (res < ETH_ALEN) {
		wpa_printf(MSG_DEBUG, "L2: Too show packet received");
		return;
	}

	if (from.sun_family != AF_UNIX ||
	    os_strncmp(from.sun_path, l2->priv_addr.sun_path,
		       sizeof(from.sun_path)) != 0) {
		wpa_printf(MSG_DEBUG, "L2: Received message from unexpected "
			   "source");
		return;
	}

	l2->rx_callback(l2->rx_callback_ctx, buf, buf + ETH_ALEN,
			res - ETH_ALEN);
}


struct l2_packet_data * l2_packet_init(
	const char *ifname, const u8 *own_addr, unsigned short protocol,
	void (*rx_callback)(void *ctx, const u8 *src_addr,
			    const u8 *buf, size_t len),
	void *rx_callback_ctx, int l2_hdr)
{
	struct l2_packet_data *l2;
	char *own_dir = "/tmp";
	char *priv_dir = "/var/run/wpa_priv";
	size_t len;
	static unsigned int counter = 0;
	struct sockaddr_un addr;
	fd_set rfds;
	struct timeval tv;
	int res;
	u8 reply[ETH_ALEN + 1];
	int reg_cmd[2];

	l2 = os_zalloc(sizeof(struct l2_packet_data));
	if (l2 == NULL)
		return NULL;
	l2->rx_callback = rx_callback;
	l2->rx_callback_ctx = rx_callback_ctx;

	len = os_strlen(own_dir) + 50;
	l2->own_socket_path = os_malloc(len);
	if (l2->own_socket_path == NULL) {
		os_free(l2);
		return NULL;
	}
	os_snprintf(l2->own_socket_path, len, "%s/wpa_privsep-l2-%d-%d",
		    own_dir, getpid(), counter++);

	l2->priv_addr.sun_family = AF_UNIX;
	os_snprintf(l2->priv_addr.sun_path, sizeof(l2->priv_addr.sun_path),
		    "%s/%s", priv_dir, ifname);

	l2->fd = socket(PF_UNIX, SOCK_DGRAM, 0);
	if (l2->fd < 0) {
		perror("socket(PF_UNIX)");
		os_free(l2->own_socket_path);
		l2->own_socket_path = NULL;
		os_free(l2);
		return NULL;
	}

	os_memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	os_strlcpy(addr.sun_path, l2->own_socket_path, sizeof(addr.sun_path));
	if (bind(l2->fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		perror("bind(PF_UNIX)");
		goto fail;
	}

	reg_cmd[0] = protocol;
	reg_cmd[1] = l2_hdr;
	if (wpa_priv_cmd(l2, PRIVSEP_CMD_L2_REGISTER, reg_cmd, sizeof(reg_cmd))
	    < 0) {
		wpa_printf(MSG_ERROR, "L2: Failed to register with wpa_priv");
		goto fail;
	}

	FD_ZERO(&rfds);
	FD_SET(l2->fd, &rfds);
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	res = select(l2->fd + 1, &rfds, NULL, NULL, &tv);
	if (res < 0 && errno != EINTR) {
		perror("select");
		goto fail;
	}

	if (FD_ISSET(l2->fd, &rfds)) {
		res = recv(l2->fd, reply, sizeof(reply), 0);
		if (res < 0) {
			perror("recv");
			goto fail;
		}
	} else {
		wpa_printf(MSG_DEBUG, "L2: Timeout while waiting for "
			   "registration reply");
		goto fail;
	}

	if (res != ETH_ALEN) {
		wpa_printf(MSG_DEBUG, "L2: Unexpected registration reply "
			   "(len=%d)", res);
	}
	os_memcpy(l2->own_addr, reply, ETH_ALEN);

	eloop_register_read_sock(l2->fd, l2_packet_receive, l2, NULL);

	return l2;

fail:
	close(l2->fd);
	l2->fd = -1;
	unlink(l2->own_socket_path);
	os_free(l2->own_socket_path);
	l2->own_socket_path = NULL;
	os_free(l2);
	return NULL;
}


void l2_packet_deinit(struct l2_packet_data *l2)
{
	if (l2 == NULL)
		return;

	if (l2->fd >= 0) {
		wpa_priv_cmd(l2, PRIVSEP_CMD_L2_UNREGISTER, NULL, 0);
		eloop_unregister_read_sock(l2->fd);
		close(l2->fd);
	}

	if (l2->own_socket_path) {
		unlink(l2->own_socket_path);
		os_free(l2->own_socket_path);
	}
		
	os_free(l2);
}


int l2_packet_get_ip_addr(struct l2_packet_data *l2, char *buf, size_t len)
{
	/* TODO */
	return -1;
}


void l2_packet_notify_auth_start(struct l2_packet_data *l2)
{
	wpa_priv_cmd(l2, PRIVSEP_CMD_L2_NOTIFY_AUTH_START, NULL, 0);
}
