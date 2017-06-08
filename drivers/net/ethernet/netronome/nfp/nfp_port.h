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

#ifndef _NFP_PORT_H_
#define _NFP_PORT_H_

#include <net/devlink.h>

struct net_device;
struct nfp_app;
struct nfp_pf;
struct nfp_port;

/**
 * enum nfp_port_type - type of port NFP can switch traffic to
 * @NFP_PORT_INVALID:	port is invalid, %NFP_PORT_PHYS_PORT transitions to this
 *			state when port disappears because of FW fault or config
 *			change
 * @NFP_PORT_PHYS_PORT:	external NIC port
 */
enum nfp_port_type {
	NFP_PORT_INVALID,
	NFP_PORT_PHYS_PORT,
};

/**
 * enum nfp_port_flags - port flags (can be type-specific)
 * @NFP_PORT_CHANGED:	port state has changed since last eth table refresh;
 *			for NFP_PORT_PHYS_PORT, never set otherwise; must hold
 *			rtnl_lock to clear
 */
enum nfp_port_flags {
	NFP_PORT_CHANGED = 0,
};

/**
 * struct nfp_port - structure representing NFP port
 * @netdev:	backpointer to associated netdev
 * @type:	what port type does the entity represent
 * @flags:	port flags
 * @app:	backpointer to the app structure
 * @dl_port:	devlink port structure
 * @eth_id:	for %NFP_PORT_PHYS_PORT port ID in NFP enumeration scheme
 * @eth_port:	for %NFP_PORT_PHYS_PORT translated ETH Table port entry
 * @port_list:	entry on pf's list of ports
 */
struct nfp_port {
	struct net_device *netdev;
	enum nfp_port_type type;

	unsigned long flags;

	struct nfp_app *app;

	struct devlink_port dl_port;

	unsigned int eth_id;
	struct nfp_eth_table_port *eth_port;

	struct list_head port_list;
};

struct nfp_port *nfp_port_from_netdev(struct net_device *netdev);
struct nfp_port *
nfp_port_from_id(struct nfp_pf *pf, enum nfp_port_type type, unsigned int id);
struct nfp_eth_table_port *__nfp_port_get_eth_port(struct nfp_port *port);
struct nfp_eth_table_port *nfp_port_get_eth_port(struct nfp_port *port);

int
nfp_port_get_phys_port_name(struct net_device *netdev, char *name, size_t len);

struct nfp_port *
nfp_port_alloc(struct nfp_app *app, enum nfp_port_type type,
	       struct net_device *netdev);
void nfp_port_free(struct nfp_port *port);

int nfp_net_refresh_eth_port(struct nfp_port *port);
void nfp_net_refresh_port_table(struct nfp_port *port);
int nfp_net_refresh_port_table_sync(struct nfp_pf *pf);

int nfp_devlink_port_register(struct nfp_app *app, struct nfp_port *port);
void nfp_devlink_port_unregister(struct nfp_port *port);

#endif
