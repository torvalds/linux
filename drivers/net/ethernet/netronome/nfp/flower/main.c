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

#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <net/devlink.h>
#include <net/dst_metadata.h>

#include "main.h"
#include "../nfpcore/nfp_cpp.h"
#include "../nfpcore/nfp_nffw.h"
#include "../nfpcore/nfp_nsp.h"
#include "../nfp_app.h"
#include "../nfp_main.h"
#include "../nfp_net.h"
#include "../nfp_net_repr.h"
#include "../nfp_port.h"
#include "./cmsg.h"

#define NFP_FLOWER_ALLOWED_VER 0x0001000000010000UL

static const char *nfp_flower_extra_cap(struct nfp_app *app, struct nfp_net *nn)
{
	return "FLOWER";
}

static enum devlink_eswitch_mode eswitch_mode_get(struct nfp_app *app)
{
	return DEVLINK_ESWITCH_MODE_SWITCHDEV;
}

static enum nfp_repr_type
nfp_flower_repr_get_type_and_port(struct nfp_app *app, u32 port_id, u8 *port)
{
	switch (FIELD_GET(NFP_FLOWER_CMSG_PORT_TYPE, port_id)) {
	case NFP_FLOWER_CMSG_PORT_TYPE_PHYS_PORT:
		*port = FIELD_GET(NFP_FLOWER_CMSG_PORT_PHYS_PORT_NUM,
				  port_id);
		return NFP_REPR_TYPE_PHYS_PORT;

	case NFP_FLOWER_CMSG_PORT_TYPE_PCIE_PORT:
		*port = FIELD_GET(NFP_FLOWER_CMSG_PORT_VNIC, port_id);
		if (FIELD_GET(NFP_FLOWER_CMSG_PORT_VNIC_TYPE, port_id) ==
		    NFP_FLOWER_CMSG_PORT_VNIC_TYPE_PF)
			return NFP_REPR_TYPE_PF;
		else
			return NFP_REPR_TYPE_VF;
	}

	return __NFP_REPR_TYPE_MAX;
}

static struct net_device *
nfp_flower_repr_get(struct nfp_app *app, u32 port_id)
{
	enum nfp_repr_type repr_type;
	struct nfp_reprs *reprs;
	u8 port = 0;

	repr_type = nfp_flower_repr_get_type_and_port(app, port_id, &port);
	if (repr_type > NFP_REPR_TYPE_MAX)
		return NULL;

	reprs = rcu_dereference(app->reprs[repr_type]);
	if (!reprs)
		return NULL;

	if (port >= reprs->num_reprs)
		return NULL;

	return reprs->reprs[port];
}

static int
nfp_flower_repr_netdev_open(struct nfp_app *app, struct nfp_repr *repr)
{
	int err;

	err = nfp_flower_cmsg_portmod(repr, true);
	if (err)
		return err;

	netif_carrier_on(repr->netdev);
	netif_tx_wake_all_queues(repr->netdev);

	return 0;
}

static int
nfp_flower_repr_netdev_stop(struct nfp_app *app, struct nfp_repr *repr)
{
	netif_carrier_off(repr->netdev);
	netif_tx_disable(repr->netdev);

	return nfp_flower_cmsg_portmod(repr, false);
}

static void nfp_flower_sriov_disable(struct nfp_app *app)
{
	struct nfp_flower_priv *priv = app->priv;

	if (!priv->nn)
		return;

	nfp_reprs_clean_and_free_by_type(app, NFP_REPR_TYPE_VF);
}

static int
nfp_flower_spawn_vnic_reprs(struct nfp_app *app,
			    enum nfp_flower_cmsg_port_vnic_type vnic_type,
			    enum nfp_repr_type repr_type, unsigned int cnt)
{
	u8 nfp_pcie = nfp_cppcore_pcie_unit(app->pf->cpp);
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_reprs *reprs, *old_reprs;
	enum nfp_port_type port_type;
	const u8 queue = 0;
	int i, err;

	port_type = repr_type == NFP_REPR_TYPE_PF ? NFP_PORT_PF_PORT :
						    NFP_PORT_VF_PORT;

	reprs = nfp_reprs_alloc(cnt);
	if (!reprs)
		return -ENOMEM;

	for (i = 0; i < cnt; i++) {
		struct nfp_port *port;
		u32 port_id;

		reprs->reprs[i] = nfp_repr_alloc(app);
		if (!reprs->reprs[i]) {
			err = -ENOMEM;
			goto err_reprs_clean;
		}

		/* For now we only support 1 PF */
		WARN_ON(repr_type == NFP_REPR_TYPE_PF && i);

		port = nfp_port_alloc(app, port_type, reprs->reprs[i]);
		if (repr_type == NFP_REPR_TYPE_PF) {
			port->pf_id = i;
			port->vnic = priv->nn->dp.ctrl_bar;
		} else {
			port->pf_id = 0;
			port->vf_id = i;
			port->vnic =
				app->pf->vf_cfg_mem + i * NFP_NET_CFG_BAR_SZ;
		}

		eth_hw_addr_random(reprs->reprs[i]);

		port_id = nfp_flower_cmsg_pcie_port(nfp_pcie, vnic_type,
						    i, queue);
		err = nfp_repr_init(app, reprs->reprs[i],
				    port_id, port, priv->nn->dp.netdev);
		if (err) {
			nfp_port_free(port);
			goto err_reprs_clean;
		}

		nfp_info(app->cpp, "%s%d Representor(%s) created\n",
			 repr_type == NFP_REPR_TYPE_PF ? "PF" : "VF", i,
			 reprs->reprs[i]->name);
	}

	old_reprs = nfp_app_reprs_set(app, repr_type, reprs);
	if (IS_ERR(old_reprs)) {
		err = PTR_ERR(old_reprs);
		goto err_reprs_clean;
	}

	return 0;
err_reprs_clean:
	nfp_reprs_clean_and_free(reprs);
	return err;
}

static int nfp_flower_sriov_enable(struct nfp_app *app, int num_vfs)
{
	struct nfp_flower_priv *priv = app->priv;

	if (!priv->nn)
		return 0;

	return nfp_flower_spawn_vnic_reprs(app,
					   NFP_FLOWER_CMSG_PORT_VNIC_TYPE_VF,
					   NFP_REPR_TYPE_VF, num_vfs);
}

static int
nfp_flower_spawn_phy_reprs(struct nfp_app *app, struct nfp_flower_priv *priv)
{
	struct nfp_eth_table *eth_tbl = app->pf->eth_tbl;
	struct nfp_reprs *reprs, *old_reprs;
	struct sk_buff *ctrl_skb;
	unsigned int i;
	int err;

	ctrl_skb = nfp_flower_cmsg_mac_repr_start(app, eth_tbl->count);
	if (!ctrl_skb)
		return -ENOMEM;

	reprs = nfp_reprs_alloc(eth_tbl->max_index + 1);
	if (!reprs) {
		err = -ENOMEM;
		goto err_free_ctrl_skb;
	}

	for (i = 0; i < eth_tbl->count; i++) {
		unsigned int phys_port = eth_tbl->ports[i].index;
		struct nfp_port *port;
		u32 cmsg_port_id;

		reprs->reprs[phys_port] = nfp_repr_alloc(app);
		if (!reprs->reprs[phys_port]) {
			err = -ENOMEM;
			goto err_reprs_clean;
		}

		port = nfp_port_alloc(app, NFP_PORT_PHYS_PORT,
				      reprs->reprs[phys_port]);
		if (IS_ERR(port)) {
			err = PTR_ERR(port);
			goto err_reprs_clean;
		}
		err = nfp_port_init_phy_port(app->pf, app, port, i);
		if (err) {
			nfp_port_free(port);
			goto err_reprs_clean;
		}

		SET_NETDEV_DEV(reprs->reprs[phys_port], &priv->nn->pdev->dev);
		nfp_net_get_mac_addr(app->pf, port);

		cmsg_port_id = nfp_flower_cmsg_phys_port(phys_port);
		err = nfp_repr_init(app, reprs->reprs[phys_port],
				    cmsg_port_id, port, priv->nn->dp.netdev);
		if (err) {
			nfp_port_free(port);
			goto err_reprs_clean;
		}

		nfp_flower_cmsg_mac_repr_add(ctrl_skb, i,
					     eth_tbl->ports[i].nbi,
					     eth_tbl->ports[i].base,
					     phys_port);

		nfp_info(app->cpp, "Phys Port %d Representor(%s) created\n",
			 phys_port, reprs->reprs[phys_port]->name);
	}

	old_reprs = nfp_app_reprs_set(app, NFP_REPR_TYPE_PHYS_PORT, reprs);
	if (IS_ERR(old_reprs)) {
		err = PTR_ERR(old_reprs);
		goto err_reprs_clean;
	}

	/* The MAC_REPR control message should be sent after the MAC
	 * representors are registered using nfp_app_reprs_set().  This is
	 * because the firmware may respond with control messages for the
	 * MAC representors, f.e. to provide the driver with information
	 * about their state, and without registration the driver will drop
	 * any such messages.
	 */
	nfp_ctrl_tx(app->ctrl, ctrl_skb);

	return 0;
err_reprs_clean:
	nfp_reprs_clean_and_free(reprs);
err_free_ctrl_skb:
	kfree_skb(ctrl_skb);
	return err;
}

static int nfp_flower_vnic_alloc(struct nfp_app *app, struct nfp_net *nn,
				 unsigned int id)
{
	if (id > 0) {
		nfp_warn(app->cpp, "FlowerNIC doesn't support more than one data vNIC\n");
		goto err_invalid_port;
	}

	eth_hw_addr_random(nn->dp.netdev);
	netif_keep_dst(nn->dp.netdev);

	return 0;

err_invalid_port:
	nn->port = nfp_port_alloc(app, NFP_PORT_INVALID, nn->dp.netdev);
	return PTR_ERR_OR_ZERO(nn->port);
}

static void nfp_flower_vnic_clean(struct nfp_app *app, struct nfp_net *nn)
{
	struct nfp_flower_priv *priv = app->priv;

	if (app->pf->num_vfs)
		nfp_reprs_clean_and_free_by_type(app, NFP_REPR_TYPE_VF);
	nfp_reprs_clean_and_free_by_type(app, NFP_REPR_TYPE_PF);
	nfp_reprs_clean_and_free_by_type(app, NFP_REPR_TYPE_PHYS_PORT);

	priv->nn = NULL;
}

static int nfp_flower_vnic_init(struct nfp_app *app, struct nfp_net *nn)
{
	struct nfp_flower_priv *priv = app->priv;
	int err;

	priv->nn = nn;

	err = nfp_flower_spawn_phy_reprs(app, app->priv);
	if (err)
		goto err_clear_nn;

	err = nfp_flower_spawn_vnic_reprs(app,
					  NFP_FLOWER_CMSG_PORT_VNIC_TYPE_PF,
					  NFP_REPR_TYPE_PF, 1);
	if (err)
		goto err_destroy_reprs_phy;

	if (app->pf->num_vfs) {
		err = nfp_flower_spawn_vnic_reprs(app,
						  NFP_FLOWER_CMSG_PORT_VNIC_TYPE_VF,
						  NFP_REPR_TYPE_VF,
						  app->pf->num_vfs);
		if (err)
			goto err_destroy_reprs_pf;
	}

	return 0;

err_destroy_reprs_pf:
	nfp_reprs_clean_and_free_by_type(app, NFP_REPR_TYPE_PF);
err_destroy_reprs_phy:
	nfp_reprs_clean_and_free_by_type(app, NFP_REPR_TYPE_PHYS_PORT);
err_clear_nn:
	priv->nn = NULL;
	return err;
}

static int nfp_flower_init(struct nfp_app *app)
{
	const struct nfp_pf *pf = app->pf;
	struct nfp_flower_priv *app_priv;
	u64 version;
	int err;

	if (!pf->eth_tbl) {
		nfp_warn(app->cpp, "FlowerNIC requires eth table\n");
		return -EINVAL;
	}

	if (!pf->mac_stats_bar) {
		nfp_warn(app->cpp, "FlowerNIC requires mac_stats BAR\n");
		return -EINVAL;
	}

	if (!pf->vf_cfg_bar) {
		nfp_warn(app->cpp, "FlowerNIC requires vf_cfg BAR\n");
		return -EINVAL;
	}

	version = nfp_rtsym_read_le(app->pf->rtbl, "hw_flower_version", &err);
	if (err) {
		nfp_warn(app->cpp, "FlowerNIC requires hw_flower_version memory symbol\n");
		return err;
	}

	/* We need to ensure hardware has enough flower capabilities. */
	if (version != NFP_FLOWER_ALLOWED_VER) {
		nfp_warn(app->cpp, "FlowerNIC: unsupported firmware version\n");
		return -EINVAL;
	}

	app_priv = vzalloc(sizeof(struct nfp_flower_priv));
	if (!app_priv)
		return -ENOMEM;

	app->priv = app_priv;
	app_priv->app = app;
	skb_queue_head_init(&app_priv->cmsg_skbs);
	INIT_WORK(&app_priv->cmsg_work, nfp_flower_cmsg_process_rx);

	err = nfp_flower_metadata_init(app);
	if (err)
		goto err_free_app_priv;

	return 0;

err_free_app_priv:
	vfree(app->priv);
	return err;
}

static void nfp_flower_clean(struct nfp_app *app)
{
	struct nfp_flower_priv *app_priv = app->priv;

	skb_queue_purge(&app_priv->cmsg_skbs);
	flush_work(&app_priv->cmsg_work);

	nfp_flower_metadata_cleanup(app);
	vfree(app->priv);
	app->priv = NULL;
}

const struct nfp_app_type app_flower = {
	.id		= NFP_APP_FLOWER_NIC,
	.name		= "flower",
	.ctrl_has_meta	= true,

	.extra_cap	= nfp_flower_extra_cap,

	.init		= nfp_flower_init,
	.clean		= nfp_flower_clean,

	.vnic_alloc	= nfp_flower_vnic_alloc,
	.vnic_init	= nfp_flower_vnic_init,
	.vnic_clean	= nfp_flower_vnic_clean,

	.repr_open	= nfp_flower_repr_netdev_open,
	.repr_stop	= nfp_flower_repr_netdev_stop,

	.ctrl_msg_rx	= nfp_flower_cmsg_rx,

	.sriov_enable	= nfp_flower_sriov_enable,
	.sriov_disable	= nfp_flower_sriov_disable,

	.eswitch_mode_get  = eswitch_mode_get,
	.repr_get	= nfp_flower_repr_get,

	.setup_tc	= nfp_flower_setup_tc,
};
