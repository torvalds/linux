/* vi: set sw=4 ts=4: */
/*
 * Port to Busybox Copyright (C) 2006 Jesse Dutton <jessedutton@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 *
 * DHCP Relay for 'DHCPv4 Configuration of IPSec Tunnel Mode' support
 * Copyright (C) 2002 Mario Strasser <mast@gmx.net>,
 *                   Zuercher Hochschule Winterthur,
 *                   Netbeat AG
 * Upstream has GPL v2 or later
 */
//applet:IF_DHCPRELAY(APPLET(dhcprelay, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_DHCPRELAY) += dhcprelay.o

//usage:#define dhcprelay_trivial_usage
//usage:       "CLIENT_IFACE[,CLIENT_IFACE2]... SERVER_IFACE [SERVER_IP]"
//usage:#define dhcprelay_full_usage "\n\n"
//usage:       "Relay DHCP requests between clients and server"

#include "common.h"

#define SERVER_PORT    67

/* lifetime of an xid entry in sec. */
#define MAX_LIFETIME   2*60
/* select timeout in sec. */
#define SELECT_TIMEOUT (MAX_LIFETIME / 8)

/* This list holds information about clients. The xid_* functions manipulate this list. */
struct xid_item {
	unsigned timestamp;
	int client;
	uint32_t xid;
	struct sockaddr_in ip;
	struct xid_item *next;
} FIX_ALIASING;

#define dhcprelay_xid_list (*(struct xid_item*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)

static struct xid_item *xid_add(uint32_t xid, struct sockaddr_in *ip, int client)
{
	struct xid_item *item;

	/* create new xid entry */
	item = xmalloc(sizeof(struct xid_item));

	/* add xid entry */
	item->ip = *ip;
	item->xid = xid;
	item->client = client;
	item->timestamp = monotonic_sec();
	item->next = dhcprelay_xid_list.next;
	dhcprelay_xid_list.next = item;

	return item;
}

static void xid_expire(void)
{
	struct xid_item *item = dhcprelay_xid_list.next;
	struct xid_item *last = &dhcprelay_xid_list;
	unsigned current_time = monotonic_sec();

	while (item != NULL) {
		if ((current_time - item->timestamp) > MAX_LIFETIME) {
			last->next = item->next;
			free(item);
			item = last->next;
		} else {
			last = item;
			item = item->next;
		}
	}
}

static struct xid_item *xid_find(uint32_t xid)
{
	struct xid_item *item = dhcprelay_xid_list.next;
	while (item != NULL) {
		if (item->xid == xid) {
			break;
		}
		item = item->next;
	}
	return item;
}

static void xid_del(uint32_t xid)
{
	struct xid_item *item = dhcprelay_xid_list.next;
	struct xid_item *last = &dhcprelay_xid_list;
	while (item != NULL) {
		if (item->xid == xid) {
			last->next = item->next;
			free(item);
			item = last->next;
		} else {
			last = item;
			item = item->next;
		}
	}
}

/**
 * get_dhcp_packet_type - gets the message type of a dhcp packet
 * p - pointer to the dhcp packet
 * returns the message type on success, -1 otherwise
 */
static int get_dhcp_packet_type(struct dhcp_packet *p)
{
	uint8_t *op;

	/* it must be either a BOOTREQUEST or a BOOTREPLY */
	if (p->op != BOOTREQUEST && p->op != BOOTREPLY)
		return -1;
	/* get message type option */
	op = udhcp_get_option(p, DHCP_MESSAGE_TYPE);
	if (op != NULL)
		return op[0];
	return -1;
}

/**
 * make_iface_list - parses client/server interface names
 * returns array
 */
static char **make_iface_list(char **client_and_server_ifaces, int *client_number)
{
	char *s, **iface_list;
	int i, cn;

	/* get number of items */
	cn = 2; /* 1 server iface + at least 1 client one */
	s = client_and_server_ifaces[0]; /* list of client ifaces */
	while (*s) {
		if (*s == ',')
			cn++;
		s++;
	}
	*client_number = cn;

	/* create vector of pointers */
	iface_list = xzalloc(cn * sizeof(iface_list[0]));

	iface_list[0] = client_and_server_ifaces[1]; /* server iface */

	i = 1;
	s = xstrdup(client_and_server_ifaces[0]); /* list of client ifaces */
	goto store_client_iface_name;

	while (i < cn) {
		if (*s++ == ',') {
			s[-1] = '\0';
 store_client_iface_name:
			iface_list[i++] = s;
		}
	}

	return iface_list;
}

/* Creates listen sockets (in fds) bound to client and server ifaces,
 * and returns numerically max fd.
 */
static int init_sockets(char **iface_list, int num_clients, int *fds)
{
	int i, n;

	n = 0;
	for (i = 0; i < num_clients; i++) {
		fds[i] = udhcp_listen_socket(/*INADDR_ANY,*/ SERVER_PORT, iface_list[i]);
		if (n < fds[i])
			n = fds[i];
	}
	return n;
}

static int sendto_ip4(int sock, const void *msg, int msg_len, struct sockaddr_in *to)
{
	int err;

	errno = 0;
	err = sendto(sock, msg, msg_len, 0, (struct sockaddr*) to, sizeof(*to));
	err -= msg_len;
	if (err)
		bb_perror_msg("sendto");
	return err;
}

/**
 * pass_to_server() - forwards dhcp packets from client to server
 * p - packet to send
 * client - number of the client
 */
static void pass_to_server(struct dhcp_packet *p, int packet_len, int client, int *fds,
			struct sockaddr_in *client_addr, struct sockaddr_in *server_addr)
{
	int type;

	/* check packet_type */
	type = get_dhcp_packet_type(p);
	if (type != DHCPDISCOVER && type != DHCPREQUEST
	 && type != DHCPDECLINE && type != DHCPRELEASE
	 && type != DHCPINFORM
	) {
		return;
	}

	/* create new xid entry */
	xid_add(p->xid, client_addr, client);

	/* forward request to server */
	/* note that we send from fds[0] which is bound to SERVER_PORT (67).
	 * IOW: we send _from_ SERVER_PORT! Although this may look strange,
	 * RFC 1542 not only allows, but prescribes this for BOOTP relays.
	 */
	sendto_ip4(fds[0], p, packet_len, server_addr);
}

/**
 * pass_to_client() - forwards dhcp packets from server to client
 * p - packet to send
 */
static void pass_to_client(struct dhcp_packet *p, int packet_len, int *fds)
{
	int type;
	struct xid_item *item;

	/* check xid */
	item = xid_find(p->xid);
	if (!item) {
		return;
	}

	/* check packet type */
	type = get_dhcp_packet_type(p);
	if (type != DHCPOFFER && type != DHCPACK && type != DHCPNAK) {
		return;
	}

//TODO: also do it if (p->flags & htons(BROADCAST_FLAG)) is set!
	if (item->ip.sin_addr.s_addr == htonl(INADDR_ANY))
		item->ip.sin_addr.s_addr = htonl(INADDR_BROADCAST);

	if (sendto_ip4(fds[item->client], p, packet_len, &item->ip) != 0) {
		return; /* send error occurred */
	}

	/* remove xid entry */
	xid_del(p->xid);
}

int dhcprelay_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int dhcprelay_main(int argc UNUSED_PARAM, char **argv)
{
	struct sockaddr_in server_addr;
	char **iface_list;
	int *fds;
	int num_sockets, max_socket;
	uint32_t our_nip;

	INIT_G();

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	server_addr.sin_port = htons(SERVER_PORT);

	/* dhcprelay CLIENT_IFACE1[,CLIENT_IFACE2...] SERVER_IFACE [SERVER_IP] */
	if (!argv[1] || !argv[2])
		bb_show_usage();
	if (argv[3]) {
		if (!inet_aton(argv[3], &server_addr.sin_addr))
			bb_perror_msg_and_die("bad server IP");
	}

	iface_list = make_iface_list(argv + 1, &num_sockets);

	fds = xmalloc(num_sockets * sizeof(fds[0]));

	/* Create sockets and bind one to every iface */
	max_socket = init_sockets(iface_list, num_sockets, fds);

	/* Get our IP on server_iface */
	if (udhcp_read_interface(argv[2], NULL, &our_nip, NULL))
		return 1;

	/* Main loop */
	while (1) {
// reinit stuff from time to time? go back to make_iface_list
// every N minutes?
		fd_set rfds;
		struct timeval tv;
		int i;

		FD_ZERO(&rfds);
		for (i = 0; i < num_sockets; i++)
			FD_SET(fds[i], &rfds);
		tv.tv_sec = SELECT_TIMEOUT;
		tv.tv_usec = 0;
		if (select(max_socket + 1, &rfds, NULL, NULL, &tv) > 0) {
			int packlen;
			struct dhcp_packet dhcp_msg;

			/* server */
			if (FD_ISSET(fds[0], &rfds)) {
				packlen = udhcp_recv_kernel_packet(&dhcp_msg, fds[0]);
				if (packlen > 0) {
					pass_to_client(&dhcp_msg, packlen, fds);
				}
			}

			/* clients */
			for (i = 1; i < num_sockets; i++) {
				struct sockaddr_in client_addr;
				socklen_t addr_size;

				if (!FD_ISSET(fds[i], &rfds))
					continue;

				addr_size = sizeof(client_addr);
				packlen = recvfrom(fds[i], &dhcp_msg, sizeof(dhcp_msg), 0,
						(struct sockaddr *)(&client_addr), &addr_size);
				if (packlen <= 0)
					continue;

				/* Get our IP on corresponding client_iface */
// RFC 1542
// 4.1 General BOOTP Processing for Relay Agents
// 4.1.1 BOOTREQUEST Messages
//   If the relay agent does decide to relay the request, it MUST examine
//   the 'giaddr' ("gateway" IP address) field.  If this field is zero,
//   the relay agent MUST fill this field with the IP address of the
//   interface on which the request was received.  If the interface has
//   more than one IP address logically associated with it, the relay
//   agent SHOULD choose one IP address associated with that interface and
//   use it consistently for all BOOTP messages it relays.  If the
//   'giaddr' field contains some non-zero value, the 'giaddr' field MUST
//   NOT be modified.  The relay agent MUST NOT, under any circumstances,
//   fill the 'giaddr' field with a broadcast address as is suggested in
//   [1] (Section 8, sixth paragraph).

// but why? what if server can't route such IP? Client ifaces may be, say, NATed!

// 4.1.2 BOOTREPLY Messages
//   BOOTP relay agents relay BOOTREPLY messages only to BOOTP clients.
//   It is the responsibility of BOOTP servers to send BOOTREPLY messages
//   directly to the relay agent identified in the 'giaddr' field.
// (yeah right, unless it is impossible... see comment above)
//   Therefore, a relay agent may assume that all BOOTREPLY messages it
//   receives are intended for BOOTP clients on its directly-connected
//   networks.
//
//   When a relay agent receives a BOOTREPLY message, it should examine
//   the BOOTP 'giaddr', 'yiaddr', 'chaddr', 'htype', and 'hlen' fields.
//   These fields should provide adequate information for the relay agent
//   to deliver the BOOTREPLY message to the client.
//
//   The 'giaddr' field can be used to identify the logical interface from
//   which the reply must be sent (i.e., the host or router interface
//   connected to the same network as the BOOTP client).  If the content
//   of the 'giaddr' field does not match one of the relay agent's
//   directly-connected logical interfaces, the BOOTREPLY message MUST be
//   silently discarded.
				if (udhcp_read_interface(iface_list[i], NULL, &dhcp_msg.gateway_nip, NULL)) {
					/* Fall back to our IP on server iface */
// this makes more sense!
					dhcp_msg.gateway_nip = our_nip;
				}
// maybe dhcp_msg.hops++? drop packets with too many hops (RFC 1542 says 4 or 16)?
				pass_to_server(&dhcp_msg, packlen, i, fds, &client_addr, &server_addr);
			}
		}
		xid_expire();
	} /* while (1) */

	/* return 0; - not reached */
}
