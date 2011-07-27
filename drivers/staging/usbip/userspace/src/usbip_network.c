/*
 *
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include "usbip_network.h"

void pack_uint32_t(int pack, uint32_t *num)
{
	uint32_t i;

	if (pack)
		i = htonl(*num);
	else
		i = ntohl(*num);

	*num = i;
}

void pack_uint16_t(int pack, uint16_t *num)
{
	uint16_t i;

	if (pack)
		i = htons(*num);
	else
		i = ntohs(*num);

	*num = i;
}

void pack_usb_device(int pack, struct usb_device *udev)
{
	pack_uint32_t(pack, &udev->busnum);
	pack_uint32_t(pack, &udev->devnum);
	pack_uint32_t(pack, &udev->speed );

	pack_uint16_t(pack, &udev->idVendor );
	pack_uint16_t(pack, &udev->idProduct);
	pack_uint16_t(pack, &udev->bcdDevice);
}

void pack_usb_interface(int pack __attribute__((unused)),
			struct usb_interface *udev __attribute__((unused)))
{
	/* uint8_t members need nothing */
}


static ssize_t usbip_xmit(int sockfd, void *buff, size_t bufflen, int sending)
{
	ssize_t total = 0;

	if (!bufflen)
		return 0;

	do {
		ssize_t nbytes;

		if (sending)
			nbytes = send(sockfd, buff, bufflen, 0);
		else
			nbytes = recv(sockfd, buff, bufflen, MSG_WAITALL);

		if (nbytes <= 0)
			return -1;

		buff	= (void *) ((intptr_t) buff + nbytes);
		bufflen	-= nbytes;
		total	+= nbytes;

	} while (bufflen > 0);


	return total;
}

ssize_t usbip_recv(int sockfd, void *buff, size_t bufflen)
{
	return usbip_xmit(sockfd, buff, bufflen, 0);
}

ssize_t usbip_send(int sockfd, void *buff, size_t bufflen)
{
	return usbip_xmit(sockfd, buff, bufflen, 1);
}

int usbip_send_op_common(int sockfd, uint32_t code, uint32_t status)
{
	int ret;
	struct op_common op_common;

	bzero(&op_common, sizeof(op_common));

	op_common.version	= USBIP_VERSION;
	op_common.code		= code;
	op_common.status	= status;

	PACK_OP_COMMON(1, &op_common);

	ret = usbip_send(sockfd, (void *) &op_common, sizeof(op_common));
	if (ret < 0) {
		err("send op_common");
		return -1;
	}

	return 0;
}

int usbip_recv_op_common(int sockfd, uint16_t *code)
{
	int ret;
	struct op_common op_common;

	bzero(&op_common, sizeof(op_common));

	ret = usbip_recv(sockfd, (void *) &op_common, sizeof(op_common));
	if (ret < 0) {
		err("recv op_common, %d", ret);
		goto err;
	}

	PACK_OP_COMMON(0, &op_common);

	if (op_common.version != USBIP_VERSION) {
		err("version mismatch, %d %d", op_common.version, USBIP_VERSION);
		goto err;
	}

	switch(*code) {
		case OP_UNSPEC:
			break;
		default:
			if (op_common.code != *code) {
				info("unexpected pdu %d for %d", op_common.code, *code);
				goto err;
			}
	}

	if (op_common.status != ST_OK) {
		info("request failed at peer, %d", op_common.status);
		goto err;
	}

	*code = op_common.code;

	return 0;
err:
	return -1;
}


int usbip_set_reuseaddr(int sockfd)
{
	const int val = 1;
	int ret;

	ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	if (ret < 0)
		err("setsockopt SO_REUSEADDR");

	return ret;
}

int usbip_set_nodelay(int sockfd)
{
	const int val = 1;
	int ret;

	ret = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
	if (ret < 0)
		err("setsockopt TCP_NODELAY");

	return ret;
}

int usbip_set_keepalive(int sockfd)
{
	const int val = 1;
	int ret;

	ret = setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));
	if (ret < 0)
		err("setsockopt SO_KEEPALIVE");

	return ret;
}

/* IPv6 Ready */
/*
 * moved here from vhci_attach.c
 */
int tcp_connect(char *hostname, char *service)
{
	struct addrinfo hints, *res, *res0;
	int sockfd;
	int err;


	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;

	/* get all possible addresses */
	err = getaddrinfo(hostname, service, &hints, &res0);
	if (err) {
		err("%s %s: %s", hostname, service, gai_strerror(err));
		return -1;
	}

	/* try all the addresses */
	for (res = res0; res; res = res->ai_next) {
		char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

		err = getnameinfo(res->ai_addr, res->ai_addrlen,
				hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
		if (err) {
			err("%s %s: %s", hostname, service, gai_strerror(err));
			continue;
		}

		dbg("trying %s port %s\n", hbuf, sbuf);

		sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (sockfd < 0) {
			err("socket");
			continue;
		}

		/* should set TCP_NODELAY for usbip */
		usbip_set_nodelay(sockfd);
		/* TODO: write code for heatbeat */
		usbip_set_keepalive(sockfd);

		err = connect(sockfd, res->ai_addr, res->ai_addrlen);
		if (err < 0) {
			close(sockfd);
			continue;
		}

		/* connected */
		dbg("connected to %s:%s", hbuf, sbuf);
		freeaddrinfo(res0);
		return sockfd;
	}


	dbg("%s:%s, %s", hostname, service, "no destination to connect to");
	freeaddrinfo(res0);

	return -1;
}
