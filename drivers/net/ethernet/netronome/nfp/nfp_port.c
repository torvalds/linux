/*
 * Copyright (C) 2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
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

#include <linux/lockdep.h>
#include <net/switchdev.h>

#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_nsp.h"
#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_net.h"
#include "nfp_port.h"

struct nfp_port *nfp_port_from_netdev(struct net_device *netdev)
{
	if (nfp_netdev_is_nfp_net(netdev)) {
		struct nfp_net *nn = netdev_priv(netdev);

		return nn->port;
	}

	if (nfp_netdev_is_nfp_repr(netdev)) {
		struct nfp_repr *repr = netdev_priv(netdev);

		return repr->port;
	}

	WARN(1, "Unknown netdev type for nfp_port\n");

	return NULL;
}

static int
nfp_port_attr_get(struct net_device *netdev, struct switchdev_attr *attr)
{
	struct nfp_port *port;

	port = nfp_port_from_netdev(netdev);
	if (!port)
		return -EOPNOTSUPP;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PARENT_ID: {
		const u8 *serial;
		/* N.B: attr->u.ppid.id is binary data */
		attr->u.ppid.id_len = nfp_cpp_serial(port->app->cpp, &serial);
		memcpy(&attr->u.ppid.id, serial, attr->u.ppid.id_len);
		break;
	}
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

const struct switchdev_ops nfp_port_switchdev_ops = {
	.switchdev_port_attr_get	= nfp_port_attr_get,
};

int nfp_port_setup_tc(struct net_device *netdev, u32 handle, u32 chain_index,
		      __be16 proto, struct tc_to_netdev *tc)
{
	struct nfp_port *port;

	if (chain_index)
		return -EOPNOTSUPP;

	port = nfp_port_from_netdev(netdev);
	if (!port)
		return -EOPNOTSUPP;

	return nfp_app_setup_tc(port->app, netdev, handle, proto, tc);
}

struct nfp_port *
nfp_port_from_id(struct nfp_pf *pf, enum nfp_port_type type, unsigned int id)
{
	struct nfp_port *port;

	lockdep_assert_held(&pf->lock);

	if (type != NFP_PORT_PHYS_PORT)
		return NULL;

	list_for_each_entry(port, &pf->ports, port_list)
		if (port->eth_id == id)
			return port;

	return NULL;
}

struct nfp_eth_table_port *__nfp_port_get_eth_port(struct nfp_port *port)
{
	if (!port)
		return NULL;
	if (port->type != NFP_PORT_PHYS_PORT)
		return NULL;

	return port->eth_port;
}

struct nfp_eth_table_port *nfp_port_get_eth_port(struct nfp_port *port)
{
	if (!__nfp_port_get_eth_port(port))
		return NULL;

	if (test_bit(NFP_PORT_CHANGED, &port->flags))
		if (nfp_net_refresh_eth_port(port))
			return NULL;

	return __nfp_port_get_eth_port(port);
}

int
nfp_port_get_phys_port_name(struct net_device *netdev, char *name, size_t len)
{
	struct nfp_eth_table_port *eth_port;
	struct nfp_port *port;
	int n;

	port = nfp_port_from_netdev(netdev);
	if (!port)
		return -EOPNOTSUPP;

	switch (port->type) {
	case NFP_PORT_PHYS_PORT:
		eth_port = __nfp_port_get_eth_port(port);
		if (!eth_port)
			return -EOPNOTSUPP;

		if (!eth_port->is_split)
			n = snprintf(name, len, "p%d", eth_port->label_port);
		else
			n = snprintf(name, len, "p%ds%d", eth_port->label_port,
				     eth_port->label_subport);
		break;
	case NFP_PORT_PF_PORT:
		n = snprintf(name, len, "pf%d", port->pf_id);
		break;
	case NFP_PORT_VF_PORT:
		n = snprintf(name, len, "pf%dvf%d", port->pf_id, port->vf_id);
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (n >= len)
		return -EINVAL;

	return 0;
}

int nfp_port_init_phy_port(struct nfp_pf *pf, struct nfp_app *app,
			   struct nfp_port *port, unsigned int id)
{
	/* Check if vNIC has external port associated and cfg is OK */
	if (!pf->eth_tbl || id >= pf->eth_tbl->count) {
		nfp_err(app->cpp,
			"NSP port entries don't match vNICs (no entry %d)\n",
			id);
		return -EINVAL;
	}
	if (pf->eth_tbl->ports[id].override_changed) {
		nfp_warn(app->cpp,
			 "Config changed for port #%d, reboot required before port will be operational\n",
			 pf->eth_tbl->ports[id].index);
		port->type = NFP_PORT_INVALID;
		return 0;
	}

	port->eth_port = &pf->eth_tbl->ports[id];
	port->eth_id = pf->eth_tbl->ports[id].index;

	return 0;
}

struct nfp_port *
nfp_port_alloc(struct nfp_app *app, enum nfp_port_type type,
	       struct net_device *netdev)
{
	struct nfp_port *port;

	port = kzalloc(sizeof(*port), GFP_KERNEL);
	if (!port)
		return ERR_PTR(-ENOMEM);

	port->netdev = netdev;
	port->type = type;
	port->app = app;

	list_add_tail(&port->port_list, &app->pf->ports);

	return port;
}

void nfp_port_free(struct nfp_port *port)
{
	if (!port)
		return;
	list_del(&port->port_list);
	kfree(port);
}
