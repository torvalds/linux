/*
 * user-mode-linux networking multicast transport
 * Copyright (C) 2001 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Copyright (C) 2001 by Harald Welte <laforge@gnumonks.org>
 *
 * based on the existing uml-networking code, which is
 * Copyright (C) 2001 Lennert Buytenhek (buytenh@gnu.org) and
 * James Leu (jleu@mindspring.net).
 * Copyright (C) 2001 by various other people who didn't put their name here.
 *
 * Licensed under the GPL.
 *
 */

#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include "umcast.h"
#include <net_user.h>
#include <um_malloc.h>

static struct sockaddr_in *new_addr(char *addr, unsigned short port)
{
	struct sockaddr_in *sin;

	sin = uml_kmalloc(sizeof(struct sockaddr_in), UM_GFP_KERNEL);
	if (sin == NULL) {
		printk(UM_KERN_ERR "new_addr: allocation of sockaddr_in "
		       "failed\n");
		return NULL;
	}
	sin->sin_family = AF_INET;
	if (addr)
		sin->sin_addr.s_addr = in_aton(addr);
	else
		sin->sin_addr.s_addr = INADDR_ANY;
	sin->sin_port = htons(port);
	return sin;
}

static int umcast_user_init(void *data, void *dev)
{
	struct umcast_data *pri = data;

	pri->remote_addr = new_addr(pri->addr, pri->rport);
	if (pri->unicast)
		pri->listen_addr = new_addr(NULL, pri->lport);
	else
		pri->listen_addr = pri->remote_addr;
	pri->dev = dev;
	return 0;
}

static void umcast_remove(void *data)
{
	struct umcast_data *pri = data;

	kfree(pri->listen_addr);
	if (pri->unicast)
		kfree(pri->remote_addr);
	pri->listen_addr = pri->remote_addr = NULL;
}

static int umcast_open(void *data)
{
	struct umcast_data *pri = data;
	struct sockaddr_in *lsin = pri->listen_addr;
	struct sockaddr_in *rsin = pri->remote_addr;
	struct ip_mreq mreq;
	int fd, yes = 1, err = -EINVAL;


	if ((!pri->unicast && lsin->sin_addr.s_addr == 0) ||
	    (rsin->sin_addr.s_addr == 0) ||
	    (lsin->sin_port == 0) || (rsin->sin_port == 0))
		goto out;

	fd = socket(AF_INET, SOCK_DGRAM, 0);

	if (fd < 0) {
		err = -errno;
		printk(UM_KERN_ERR "umcast_open : data socket failed, "
		       "errno = %d\n", errno);
		goto out;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
		err = -errno;
		printk(UM_KERN_ERR "umcast_open: SO_REUSEADDR failed, "
		       "errno = %d\n", errno);
		goto out_close;
	}

	if (!pri->unicast) {
		/* set ttl according to config */
		if (setsockopt(fd, SOL_IP, IP_MULTICAST_TTL, &pri->ttl,
			       sizeof(pri->ttl)) < 0) {
			err = -errno;
			printk(UM_KERN_ERR "umcast_open: IP_MULTICAST_TTL "
			       "failed, error = %d\n", errno);
			goto out_close;
		}

		/* set LOOP, so data does get fed back to local sockets */
		if (setsockopt(fd, SOL_IP, IP_MULTICAST_LOOP,
			       &yes, sizeof(yes)) < 0) {
			err = -errno;
			printk(UM_KERN_ERR "umcast_open: IP_MULTICAST_LOOP "
			       "failed, error = %d\n", errno);
			goto out_close;
		}
	}

	/* bind socket to the address */
	if (bind(fd, (struct sockaddr *) lsin, sizeof(*lsin)) < 0) {
		err = -errno;
		printk(UM_KERN_ERR "umcast_open : data bind failed, "
		       "errno = %d\n", errno);
		goto out_close;
	}

	if (!pri->unicast) {
		/* subscribe to the multicast group */
		mreq.imr_multiaddr.s_addr = lsin->sin_addr.s_addr;
		mreq.imr_interface.s_addr = 0;
		if (setsockopt(fd, SOL_IP, IP_ADD_MEMBERSHIP,
			       &mreq, sizeof(mreq)) < 0) {
			err = -errno;
			printk(UM_KERN_ERR "umcast_open: IP_ADD_MEMBERSHIP "
			       "failed, error = %d\n", errno);
			printk(UM_KERN_ERR "There appears not to be a "
			       "multicast-capable network interface on the "
			       "host.\n");
			printk(UM_KERN_ERR "eth0 should be configured in order "
			       "to use the multicast transport.\n");
			goto out_close;
		}
	}

	return fd;

 out_close:
	close(fd);
 out:
	return err;
}

static void umcast_close(int fd, void *data)
{
	struct umcast_data *pri = data;

	if (!pri->unicast) {
		struct ip_mreq mreq;
		struct sockaddr_in *lsin = pri->listen_addr;

		mreq.imr_multiaddr.s_addr = lsin->sin_addr.s_addr;
		mreq.imr_interface.s_addr = 0;
		if (setsockopt(fd, SOL_IP, IP_DROP_MEMBERSHIP,
			       &mreq, sizeof(mreq)) < 0) {
			printk(UM_KERN_ERR "umcast_close: IP_DROP_MEMBERSHIP "
			       "failed, error = %d\n", errno);
		}
	}

	close(fd);
}

int umcast_user_write(int fd, void *buf, int len, struct umcast_data *pri)
{
	struct sockaddr_in *data_addr = pri->remote_addr;

	return net_sendto(fd, buf, len, data_addr, sizeof(*data_addr));
}

const struct net_user_info umcast_user_info = {
	.init	= umcast_user_init,
	.open	= umcast_open,
	.close	= umcast_close,
	.remove	= umcast_remove,
	.add_address	= NULL,
	.delete_address = NULL,
	.mtu	= ETH_MAX_PACKET,
	.max_packet	= ETH_MAX_PACKET + ETH_HEADER_OTHER,
};
