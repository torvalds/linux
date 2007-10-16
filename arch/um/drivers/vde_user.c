/*
 * Copyright (C) 2007 Luca Bigliardi (shammash@artha.org).
 * Licensed under the GPL.
 */

#include <errno.h>
#include <unistd.h>
#include <libvdeplug.h>
#include "net_user.h"
#include "kern_util.h"
#include "kern_constants.h"
#include "user.h"
#include "os.h"
#include "um_malloc.h"
#include "vde.h"

#define MAX_PACKET (ETH_MAX_PACKET + ETH_HEADER_OTHER)

static int vde_user_init(void *data, void *dev)
{
	struct vde_data *pri = data;
	VDECONN *conn = NULL;
	int err = -EINVAL;

	pri->dev = dev;

	conn = vde_open(pri->vde_switch, pri->descr, pri->args);

	if (conn == NULL) {
		err = -errno;
		printk(UM_KERN_ERR "vde_user_init: vde_open failed, "
		       "errno = %d\n", errno);
		return err;
	}

	printk(UM_KERN_INFO "vde backend - connection opened\n");

	pri->conn = conn;

	return 0;
}

static int vde_user_open(void *data)
{
	struct vde_data *pri = data;

	if (pri->conn != NULL)
		return vde_datafd(pri->conn);

	printk(UM_KERN_WARNING "vde_open - we have no VDECONN to open");
	return -EINVAL;
}

static void vde_remove(void *data)
{
	struct vde_data *pri = data;

	if (pri->conn != NULL) {
		printk(UM_KERN_INFO "vde backend - closing connection\n");
		vde_close(pri->conn);
		pri->conn = NULL;
		kfree(pri->args);
		pri->args = NULL;
		return;
	}

	printk(UM_KERN_WARNING "vde_remove - we have no VDECONN to remove");
}

static int vde_set_mtu(int mtu, void *data)
{
	return mtu;
}

const struct net_user_info vde_user_info = {
	.init		= vde_user_init,
	.open		= vde_user_open,
	.close	 	= NULL,
	.remove	 	= vde_remove,
	.set_mtu	= vde_set_mtu,
	.add_address	= NULL,
	.delete_address = NULL,
	.max_packet	= MAX_PACKET - ETH_HEADER_OTHER
};

void vde_init_libstuff(struct vde_data *vpri, struct vde_init *init)
{
	struct vde_open_args *args;

	vpri->args = kmalloc(sizeof(struct vde_open_args), UM_GFP_KERNEL);
	if (vpri->args == NULL) {
		printk(UM_KERN_ERR "vde_init_libstuff - vde_open_args"
		       "allocation failed");
		return;
	}

	args = vpri->args;

	args->port = init->port;
	args->group = init->group;
	args->mode = init->mode ? init->mode : 0700;

	args->port ?  printk(UM_KERN_INFO "port %d", args->port) :
		printk(UM_KERN_INFO "undefined port");
}

int vde_user_read(void *conn, void *buf, int len)
{
	VDECONN *vconn = conn;
	int rv;

	if (vconn == NULL)
		return 0;

	rv = vde_recv(vconn, buf, len, 0);
	if (rv < 0) {
		if (errno == EAGAIN)
			return 0;
		return -errno;
	}
	else if (rv == 0)
		return -ENOTCONN;

	return rv;
}

int vde_user_write(void *conn, void *buf, int len)
{
	VDECONN *vconn = conn;

	if (vconn == NULL)
		return 0;

	return vde_send(vconn, buf, len, 0);
}

