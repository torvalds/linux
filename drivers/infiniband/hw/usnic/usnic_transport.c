/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/bitmap.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <net/inet_sock.h>

#include "usnic_transport.h"
#include "usnic_log.h"

/* ROCE */
static unsigned long *roce_bitmap;
static u16 roce_next_port = 1;
#define ROCE_BITMAP_SZ ((1 << (8 /*CHAR_BIT*/ * sizeof(u16)))/8 /*CHAR BIT*/)
static DEFINE_SPINLOCK(roce_bitmap_lock);

const char *usnic_transport_to_str(enum usnic_transport_type type)
{
	switch (type) {
	case USNIC_TRANSPORT_UNKNOWN:
		return "Unknown";
	case USNIC_TRANSPORT_ROCE_CUSTOM:
		return "roce custom";
	case USNIC_TRANSPORT_IPV4_UDP:
		return "IPv4 UDP";
	case USNIC_TRANSPORT_MAX:
		return "Max?";
	default:
		return "Not known";
	}
}

int usnic_transport_sock_to_str(char *buf, int buf_sz,
					struct socket *sock)
{
	int err;
	uint32_t addr;
	uint16_t port;
	int proto;

	memset(buf, 0, buf_sz);
	err = usnic_transport_sock_get_addr(sock, &proto, &addr, &port);
	if (err)
		return 0;

	return scnprintf(buf, buf_sz, "Proto:%u Addr:%pI4h Port:%hu",
			proto, &addr, port);
}

/*
 * reserve a port number.  if "0" specified, we will try to pick one
 * starting at roce_next_port.  roce_next_port will take on the values
 * 1..4096
 */
u16 usnic_transport_rsrv_port(enum usnic_transport_type type, u16 port_num)
{
	if (type == USNIC_TRANSPORT_ROCE_CUSTOM) {
		spin_lock(&roce_bitmap_lock);
		if (!port_num) {
			port_num = bitmap_find_next_zero_area(roce_bitmap,
						ROCE_BITMAP_SZ,
						roce_next_port /* start */,
						1 /* nr */,
						0 /* align */);
			roce_next_port = (port_num & 4095) + 1;
		} else if (test_bit(port_num, roce_bitmap)) {
			usnic_err("Failed to allocate port for %s\n",
					usnic_transport_to_str(type));
			spin_unlock(&roce_bitmap_lock);
			goto out_fail;
		}
		bitmap_set(roce_bitmap, port_num, 1);
		spin_unlock(&roce_bitmap_lock);
	} else {
		usnic_err("Failed to allocate port - transport %s unsupported\n",
				usnic_transport_to_str(type));
		goto out_fail;
	}

	usnic_dbg("Allocating port %hu for %s\n", port_num,
			usnic_transport_to_str(type));
	return port_num;

out_fail:
	return 0;
}

void usnic_transport_unrsrv_port(enum usnic_transport_type type, u16 port_num)
{
	if (type == USNIC_TRANSPORT_ROCE_CUSTOM) {
		spin_lock(&roce_bitmap_lock);
		if (!port_num) {
			usnic_err("Unreserved invalid port num 0 for %s\n",
					usnic_transport_to_str(type));
			goto out_roce_custom;
		}

		if (!test_bit(port_num, roce_bitmap)) {
			usnic_err("Unreserving invalid %hu for %s\n",
					port_num,
					usnic_transport_to_str(type));
			goto out_roce_custom;
		}
		bitmap_clear(roce_bitmap, port_num, 1);
		usnic_dbg("Freeing port %hu for %s\n", port_num,
				usnic_transport_to_str(type));
out_roce_custom:
		spin_unlock(&roce_bitmap_lock);
	} else {
		usnic_err("Freeing invalid port %hu for %d\n", port_num, type);
	}
}

struct socket *usnic_transport_get_socket(int sock_fd)
{
	struct socket *sock;
	int err;
	char buf[25];

	/* sockfd_lookup will internally do a fget */
	sock = sockfd_lookup(sock_fd, &err);
	if (!sock) {
		usnic_err("Unable to lookup socket for fd %d with err %d\n",
				sock_fd, err);
		return ERR_PTR(-ENOENT);
	}

	usnic_transport_sock_to_str(buf, sizeof(buf), sock);
	usnic_dbg("Get sock %s\n", buf);

	return sock;
}

void usnic_transport_put_socket(struct socket *sock)
{
	char buf[100];

	usnic_transport_sock_to_str(buf, sizeof(buf), sock);
	usnic_dbg("Put sock %s\n", buf);
	sockfd_put(sock);
}

int usnic_transport_sock_get_addr(struct socket *sock, int *proto,
					uint32_t *addr, uint16_t *port)
{
	int err;
	struct sockaddr_in sock_addr;

	err = sock->ops->getname(sock,
				(struct sockaddr *)&sock_addr,
				0);
	if (err < 0)
		return err;

	if (sock_addr.sin_family != AF_INET)
		return -EINVAL;

	if (proto)
		*proto = sock->sk->sk_protocol;
	if (port)
		*port = ntohs(((struct sockaddr_in *)&sock_addr)->sin_port);
	if (addr)
		*addr = ntohl(((struct sockaddr_in *)
					&sock_addr)->sin_addr.s_addr);

	return 0;
}

int usnic_transport_init(void)
{
	roce_bitmap = kzalloc(ROCE_BITMAP_SZ, GFP_KERNEL);
	if (!roce_bitmap)
		return -ENOMEM;

	/* Do not ever allocate bit 0, hence set it here */
	bitmap_set(roce_bitmap, 0, 1);
	return 0;
}

void usnic_transport_fini(void)
{
	kfree(roce_bitmap);
}
