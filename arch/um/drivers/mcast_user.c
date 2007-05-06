/*
 * user-mode-linux networking multicast transport
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

#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include "net_user.h"
#include "mcast.h"
#include "kern_util.h"
#include "user_util.h"
#include "user.h"
#include "os.h"
#include "um_malloc.h"

#define MAX_PACKET (ETH_MAX_PACKET + ETH_HEADER_OTHER)

static struct sockaddr_in *new_addr(char *addr, unsigned short port)
{
	struct sockaddr_in *sin;

	sin = um_kmalloc(sizeof(struct sockaddr_in));
	if(sin == NULL){
		printk("new_addr: allocation of sockaddr_in failed\n");
		return NULL;
	}
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = in_aton(addr);
	sin->sin_port = htons(port);
	return sin;
}

static int mcast_user_init(void *data, void *dev)
{
	struct mcast_data *pri = data;

	pri->mcast_addr = new_addr(pri->addr, pri->port);
	pri->dev = dev;
	return 0;
}

static void mcast_remove(void *data)
{
	struct mcast_data *pri = data;

	kfree(pri->mcast_addr);
	pri->mcast_addr = NULL;
}

static int mcast_open(void *data)
{
	struct mcast_data *pri = data;
	struct sockaddr_in *sin = pri->mcast_addr;
	struct ip_mreq mreq;
	int fd, yes = 1, err = -EINVAL;


	if ((sin->sin_addr.s_addr == 0) || (sin->sin_port == 0))
		goto out;

	fd = socket(AF_INET, SOCK_DGRAM, 0);

	if (fd < 0){
		err = -errno;
		printk("mcast_open : data socket failed, errno = %d\n", 
		       errno);
		goto out;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
		err = -errno;
		printk("mcast_open: SO_REUSEADDR failed, errno = %d\n",
			errno);
		goto out_close;
	}

	/* set ttl according to config */
	if (setsockopt(fd, SOL_IP, IP_MULTICAST_TTL, &pri->ttl,
		       sizeof(pri->ttl)) < 0) {
		err = -errno;
		printk("mcast_open: IP_MULTICAST_TTL failed, error = %d\n",
			errno);
		goto out_close;
	}

	/* set LOOP, so data does get fed back to local sockets */
	if (setsockopt(fd, SOL_IP, IP_MULTICAST_LOOP, &yes, sizeof(yes)) < 0) {
		err = -errno;
		printk("mcast_open: IP_MULTICAST_LOOP failed, error = %d\n",
			errno);
		goto out_close;
	}

	/* bind socket to mcast address */
	if (bind(fd, (struct sockaddr *) sin, sizeof(*sin)) < 0) {
		err = -errno;
		printk("mcast_open : data bind failed, errno = %d\n", errno);
		goto out_close;
	}

	/* subscribe to the multicast group */
	mreq.imr_multiaddr.s_addr = sin->sin_addr.s_addr;
	mreq.imr_interface.s_addr = 0;
	if (setsockopt(fd, SOL_IP, IP_ADD_MEMBERSHIP, 
		       &mreq, sizeof(mreq)) < 0) {
		err = -errno;
		printk("mcast_open: IP_ADD_MEMBERSHIP failed, error = %d\n",
			errno);
		printk("There appears not to be a multicast-capable network "
		       "interface on the host.\n");
		printk("eth0 should be configured in order to use the "
		       "multicast transport.\n");
		goto out_close;
	}

	return fd;

 out_close:
	os_close_file(fd);
 out:
	return err;
}

static void mcast_close(int fd, void *data)
{
	struct ip_mreq mreq;
	struct mcast_data *pri = data;
	struct sockaddr_in *sin = pri->mcast_addr;

	mreq.imr_multiaddr.s_addr = sin->sin_addr.s_addr;
	mreq.imr_interface.s_addr = 0;
	if (setsockopt(fd, SOL_IP, IP_DROP_MEMBERSHIP,
		       &mreq, sizeof(mreq)) < 0) {
		printk("mcast_open: IP_DROP_MEMBERSHIP failed, error = %d\n",
			errno);
	}

	os_close_file(fd);
}

int mcast_user_write(int fd, void *buf, int len, struct mcast_data *pri)
{
	struct sockaddr_in *data_addr = pri->mcast_addr;

	return net_sendto(fd, buf, len, data_addr, sizeof(*data_addr));
}

static int mcast_set_mtu(int mtu, void *data)
{
	return mtu;
}

const struct net_user_info mcast_user_info = {
	.init		= mcast_user_init,
	.open		= mcast_open,
	.close	 	= mcast_close,
	.remove	 	= mcast_remove,
	.set_mtu	= mcast_set_mtu,
	.add_address	= NULL,
	.delete_address = NULL,
	.max_packet	= MAX_PACKET - ETH_HEADER_OTHER
};
