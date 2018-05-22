// SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)
/*
 * Copyright (C) 2018 Netronome Systems, Inc.
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

#include <linux/etherdevice.h>

#include "../nfpcore/nfp.h"
#include "../nfpcore/nfp_cpp.h"
#include "../nfpcore/nfp_nsp.h"
#include "../nfp_app.h"
#include "../nfp_main.h"
#include "../nfp_net.h"
#include "../nfp_port.h"
#include "main.h"

static void
nfp_abm_vnic_set_mac(struct nfp_pf *pf, struct nfp_abm *abm, struct nfp_net *nn,
		     unsigned int id)
{
	struct nfp_eth_table_port *eth_port = &pf->eth_tbl->ports[id];
	u8 mac_addr[ETH_ALEN];
	const char *mac_str;
	char name[32];

	if (id > pf->eth_tbl->count) {
		nfp_warn(pf->cpp, "No entry for persistent MAC address\n");
		eth_hw_addr_random(nn->dp.netdev);
		return;
	}

	snprintf(name, sizeof(name), "eth%u.mac.pf%u",
		 eth_port->eth_index, abm->pf_id);

	mac_str = nfp_hwinfo_lookup(pf->hwinfo, name);
	if (!mac_str) {
		nfp_warn(pf->cpp, "Can't lookup persistent MAC address (%s)\n",
			 name);
		eth_hw_addr_random(nn->dp.netdev);
		return;
	}

	if (sscanf(mac_str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		   &mac_addr[0], &mac_addr[1], &mac_addr[2],
		   &mac_addr[3], &mac_addr[4], &mac_addr[5]) != 6) {
		nfp_warn(pf->cpp, "Can't parse persistent MAC address (%s)\n",
			 mac_str);
		eth_hw_addr_random(nn->dp.netdev);
		return;
	}

	ether_addr_copy(nn->dp.netdev->dev_addr, mac_addr);
	ether_addr_copy(nn->dp.netdev->perm_addr, mac_addr);
}

static int
nfp_abm_vnic_alloc(struct nfp_app *app, struct nfp_net *nn, unsigned int id)
{
	struct nfp_abm *abm = app->priv;
	struct nfp_abm_link *alink;
	int err;

	alink = kzalloc(sizeof(*alink), GFP_KERNEL);
	if (!alink)
		return -ENOMEM;
	nn->app_priv = alink;
	alink->abm = abm;
	alink->vnic = nn;
	alink->id = id;

	nn->port = nfp_port_alloc(app, NFP_PORT_PHYS_PORT, nn->dp.netdev);
	if (IS_ERR(nn->port)) {
		err = PTR_ERR(nn->port);
		goto err_free_alink;
	}

	err = nfp_app_nic_vnic_init_phy_port(app->pf, app, nn, id);
	if (err < 0)
		goto err_free_port;
	if (nn->port->type == NFP_PORT_INVALID)
		/* core will kill this vNIC */
		return 0;

	nfp_abm_vnic_set_mac(app->pf, abm, nn, id);
	nfp_abm_ctrl_read_params(alink);

	return 0;

err_free_port:
	nfp_port_free(nn->port);
err_free_alink:
	kfree(alink);
	return err;
}

static void nfp_abm_vnic_free(struct nfp_app *app, struct nfp_net *nn)
{
	struct nfp_abm_link *alink = nn->app_priv;

	kfree(alink);
}

static int nfp_abm_init(struct nfp_app *app)
{
	struct nfp_pf *pf = app->pf;
	struct nfp_abm *abm;
	int err;

	if (!pf->eth_tbl) {
		nfp_err(pf->cpp, "ABM NIC requires ETH table\n");
		return -EINVAL;
	}
	if (pf->max_data_vnics != pf->eth_tbl->count) {
		nfp_err(pf->cpp, "ETH entries don't match vNICs (%d vs %d)\n",
			pf->max_data_vnics, pf->eth_tbl->count);
		return -EINVAL;
	}
	if (!pf->mac_stats_bar) {
		nfp_warn(app->cpp, "ABM NIC requires mac_stats symbol\n");
		return -EINVAL;
	}

	abm = kzalloc(sizeof(*abm), GFP_KERNEL);
	if (!abm)
		return -ENOMEM;
	app->priv = abm;
	abm->app = app;

	err = nfp_abm_ctrl_find_addrs(abm);
	if (err)
		goto err_free_abm;

	return 0;

err_free_abm:
	kfree(abm);
	app->priv = NULL;
	return err;
}

static void nfp_abm_clean(struct nfp_app *app)
{
	struct nfp_abm *abm = app->priv;

	kfree(abm);
	app->priv = NULL;
}

const struct nfp_app_type app_abm = {
	.id		= NFP_APP_ACTIVE_BUFFER_MGMT_NIC,
	.name		= "abm",

	.init		= nfp_abm_init,
	.clean		= nfp_abm_clean,

	.vnic_alloc	= nfp_abm_vnic_alloc,
	.vnic_free	= nfp_abm_vnic_free,
};
