/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "rxe.h"
#include "rxe_net.h"

/* Copy argument and remove trailing CR. Return the new length. */
static int sanitize_arg(const char *val, char *intf, int intf_len)
{
	int len;

	if (!val)
		return 0;

	/* Remove newline. */
	for (len = 0; len < intf_len - 1 && val[len] && val[len] != '\n'; len++)
		intf[len] = val[len];
	intf[len] = 0;

	if (len == 0 || (val[len] != 0 && val[len] != '\n'))
		return 0;

	return len;
}

static int rxe_param_set_add(const char *val, const struct kernel_param *kp)
{
	int len;
	int err = 0;
	char intf[32];
	struct net_device *ndev;
	struct rxe_dev *exists;

	if (!rxe_initialized) {
		pr_err("Module parameters are not supported, use rdma link add or rxe_cfg\n");
		return -EAGAIN;
	}

	len = sanitize_arg(val, intf, sizeof(intf));
	if (!len) {
		pr_err("add: invalid interface name\n");
		return -EINVAL;
	}

	ndev = dev_get_by_name(&init_net, intf);
	if (!ndev) {
		pr_err("interface %s not found\n", intf);
		return -EINVAL;
	}

	exists = rxe_get_dev_from_net(ndev);
	if (exists) {
		ib_device_put(&exists->ib_dev);
		pr_err("already configured on %s\n", intf);
		err = -EINVAL;
		goto err;
	}

	err = rxe_net_add("rxe%d", ndev);
	if (err) {
		pr_err("failed to add %s\n", intf);
		goto err;
	}

err:
	dev_put(ndev);
	return err;
}

static int rxe_param_set_remove(const char *val, const struct kernel_param *kp)
{
	int len;
	char intf[32];
	struct ib_device *ib_dev;

	len = sanitize_arg(val, intf, sizeof(intf));
	if (!len) {
		pr_err("add: invalid interface name\n");
		return -EINVAL;
	}

	if (strncmp("all", intf, len) == 0) {
		pr_info("rxe_sys: remove all");
		ib_unregister_driver(RDMA_DRIVER_RXE);
		return 0;
	}

	ib_dev = ib_device_get_by_name(intf, RDMA_DRIVER_RXE);
	if (!ib_dev) {
		pr_err("not configured on %s\n", intf);
		return -EINVAL;
	}

	ib_unregister_device_and_put(ib_dev);

	return 0;
}

static const struct kernel_param_ops rxe_add_ops = {
	.set = rxe_param_set_add,
};

static const struct kernel_param_ops rxe_remove_ops = {
	.set = rxe_param_set_remove,
};

module_param_cb(add, &rxe_add_ops, NULL, 0200);
MODULE_PARM_DESC(add, "DEPRECATED.  Create RXE device over network interface");
module_param_cb(remove, &rxe_remove_ops, NULL, 0200);
MODULE_PARM_DESC(remove, "DEPRECATED.  Remove RXE device over network interface");
