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

static void rxe_set_port_state(struct net_device *ndev)
{
	struct rxe_dev *rxe = net_to_rxe(ndev);
	bool is_up = netif_running(ndev) && netif_carrier_ok(ndev);

	if (!rxe)
		goto out;

	if (is_up)
		rxe_port_up(rxe);
	else
		rxe_port_down(rxe); /* down for unknown state */
out:
	return;
}

static int rxe_param_set_add(const char *val, const struct kernel_param *kp)
{
	int len;
	int err = 0;
	char intf[32];
	struct net_device *ndev = NULL;
	struct rxe_dev *rxe;

	len = sanitize_arg(val, intf, sizeof(intf));
	if (!len) {
		pr_err("add: invalid interface name\n");
		err = -EINVAL;
		goto err;
	}

	ndev = dev_get_by_name(&init_net, intf);
	if (!ndev) {
		pr_err("interface %s not found\n", intf);
		err = -EINVAL;
		goto err;
	}

	if (net_to_rxe(ndev)) {
		pr_err("already configured on %s\n", intf);
		err = -EINVAL;
		goto err;
	}

	rxe = rxe_net_add(ndev);
	if (!rxe) {
		pr_err("failed to add %s\n", intf);
		err = -EINVAL;
		goto err;
	}

	rxe_set_port_state(ndev);
	dev_info(&rxe->ib_dev.dev, "added %s\n", intf);
err:
	if (ndev)
		dev_put(ndev);
	return err;
}

static int rxe_param_set_remove(const char *val, const struct kernel_param *kp)
{
	int len;
	char intf[32];
	struct rxe_dev *rxe;

	len = sanitize_arg(val, intf, sizeof(intf));
	if (!len) {
		pr_err("add: invalid interface name\n");
		return -EINVAL;
	}

	if (strncmp("all", intf, len) == 0) {
		pr_info("rxe_sys: remove all");
		rxe_remove_all();
		return 0;
	}

	rxe = get_rxe_by_name(intf);

	if (!rxe) {
		pr_err("not configured on %s\n", intf);
		return -EINVAL;
	}

	list_del(&rxe->list);
	rxe_remove(rxe);

	return 0;
}

static const struct kernel_param_ops rxe_add_ops = {
	.set = rxe_param_set_add,
};

static const struct kernel_param_ops rxe_remove_ops = {
	.set = rxe_param_set_remove,
};

module_param_cb(add, &rxe_add_ops, NULL, 0200);
MODULE_PARM_DESC(add, "Create RXE device over network interface");
module_param_cb(remove, &rxe_remove_ops, NULL, 0200);
MODULE_PARM_DESC(remove, "Remove RXE device over network interface");
