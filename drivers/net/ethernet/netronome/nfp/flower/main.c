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
#include <linux/lockdep.h>
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

	return NFP_FLOWER_CMSG_PORT_TYPE_UNSPEC;
}

static struct net_device *
nfp_flower_repr_get(struct nfp_app *app, u32 port_id)
{
	enum nfp_repr_type repr_type;
	struct nfp_reprs *reprs;
	u8 port = 0;

	repr_type = nfp_flower_repr_get_type_and_port(app, port_id, &port);

	reprs = rcu_dereference(app->reprs[repr_type]);
	if (!reprs)
		return NULL;

	if (port >= reprs->num_reprs)
		return NULL;

	return rcu_dereference(reprs->reprs[port]);
}

static int
nfp_flower_reprs_reify(struct nfp_app *app, enum nfp_repr_type type,
		       bool exists)
{
	struct nfp_reprs *reprs;
	int i, err, count = 0;

	reprs = rcu_dereference_protected(app->reprs[type],
					  lockdep_is_held(&app->pf->lock));
	if (!reprs)
		return 0;

	for (i = 0; i < reprs->num_reprs; i++) {
		struct net_device *netdev;

		netdev = nfp_repr_get_locked(app, reprs, i);
		if (netdev) {
			struct nfp_repr *repr = netdev_priv(netdev);

			err = nfp_flower_cmsg_portreify(repr, exists);
			if (err)
				return err;
			count++;
		}
	}

	return count;
}

static int
nfp_flower_wait_repr_reify(struct nfp_app *app, atomic_t *replies, int tot_repl)
{
	struct nfp_flower_priv *priv = app->priv;
	int err;

	if (!tot_repl)
		return 0;

	lockdep_assert_held(&app->pf->lock);
	err = wait_event_interruptible_timeout(priv->reify_wait_queue,
					       atomic_read(replies) >= tot_repl,
					       msecs_to_jiffies(10));
	if (err <= 0) {
		nfp_warn(app->cpp, "Not all reprs responded to reify\n");
		return -EIO;
	}

	return 0;
}

static int
nfp_flower_repr_netdev_open(struct nfp_app *app, struct nfp_repr *repr)
{
	int err;

	err = nfp_flower_cmsg_portmod(repr, true, repr->netdev->mtu, false);
	if (err)
		return err;

	netif_tx_wake_all_queues(repr->netdev);

	return 0;
}

static int
nfp_flower_repr_netdev_stop(struct nfp_app *app, struct nfp_repr *repr)
{
	netif_tx_disable(repr->netdev);

	return nfp_flower_cmsg_portmod(repr, false, repr->netdev->mtu, false);
}

static int
nfp_flower_repr_netdev_init(struct nfp_app *app, struct net_device *netdev)
{
	return tc_setup_cb_egdev_register(netdev,
					  nfp_flower_setup_tc_egress_cb,
					  netdev_priv(netdev));
}

static void
nfp_flower_repr_netdev_clean(struct nfp_app *app, struct net_device *netdev)
{
	struct nfp_repr *repr = netdev_priv(netdev);

	kfree(repr->app_priv);

	tc_setup_cb_egdev_unregister(netdev, nfp_flower_setup_tc_egress_cb,
				     netdev_priv(netdev));
}

static void
nfp_flower_repr_netdev_preclean(struct nfp_app *app, struct net_device *netdev)
{
	struct nfp_repr *repr = netdev_priv(netdev);
	struct nfp_flower_priv *priv = app->priv;
	atomic_t *replies = &priv->reify_replies;
	int err;

	atomic_set(replies, 0);
	err = nfp_flower_cmsg_portreify(repr, false);
	if (err) {
		nfp_warn(app->cpp, "Failed to notify firmware about repr destruction\n");
		return;
	}

	nfp_flower_wait_repr_reify(app, replies, 1);
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
	atomic_t *replies = &priv->reify_replies;
	struct nfp_flower_repr_priv *repr_priv;
	enum nfp_port_type port_type;
	struct nfp_repr *nfp_repr;
	struct nfp_reprs *reprs;
	int i, err, reify_cnt;
	const u8 queue = 0;

	port_type = repr_type == NFP_REPR_TYPE_PF ? NFP_PORT_PF_PORT :
						    NFP_PORT_VF_PORT;

	reprs = nfp_reprs_alloc(cnt);
	if (!reprs)
		return -ENOMEM;

	for (i = 0; i < cnt; i++) {
		struct net_device *repr;
		struct nfp_port *port;
		u32 port_id;

		repr = nfp_repr_alloc(app);
		if (!repr) {
			err = -ENOMEM;
			goto err_reprs_clean;
		}

		repr_priv = kzalloc(sizeof(*repr_priv), GFP_KERNEL);
		if (!repr_priv) {
			err = -ENOMEM;
			goto err_reprs_clean;
		}

		nfp_repr = netdev_priv(repr);
		nfp_repr->app_priv = repr_priv;

		/* For now we only support 1 PF */
		WARN_ON(repr_type == NFP_REPR_TYPE_PF && i);

		port = nfp_port_alloc(app, port_type, repr);
		if (IS_ERR(port)) {
			err = PTR_ERR(port);
			nfp_repr_free(repr);
			goto err_reprs_clean;
		}
		if (repr_type == NFP_REPR_TYPE_PF) {
			port->pf_id = i;
			port->vnic = priv->nn->dp.ctrl_bar;
		} else {
			port->pf_id = 0;
			port->vf_id = i;
			port->vnic =
				app->pf->vf_cfg_mem + i * NFP_NET_CFG_BAR_SZ;
		}

		eth_hw_addr_random(repr);

		port_id = nfp_flower_cmsg_pcie_port(nfp_pcie, vnic_type,
						    i, queue);
		err = nfp_repr_init(app, repr,
				    port_id, port, priv->nn->dp.netdev);
		if (err) {
			nfp_port_free(port);
			nfp_repr_free(repr);
			goto err_reprs_clean;
		}

		RCU_INIT_POINTER(reprs->reprs[i], repr);
		nfp_info(app->cpp, "%s%d Representor(%s) created\n",
			 repr_type == NFP_REPR_TYPE_PF ? "PF" : "VF", i,
			 repr->name);
	}

	nfp_app_reprs_set(app, repr_type, reprs);

	atomic_set(replies, 0);
	reify_cnt = nfp_flower_reprs_reify(app, repr_type, true);
	if (reify_cnt < 0) {
		err = reify_cnt;
		nfp_warn(app->cpp, "Failed to notify firmware about repr creation\n");
		goto err_reprs_remove;
	}

	err = nfp_flower_wait_repr_reify(app, replies, reify_cnt);
	if (err)
		goto err_reprs_remove;

	return 0;
err_reprs_remove:
	reprs = nfp_app_reprs_set(app, repr_type, NULL);
err_reprs_clean:
	nfp_reprs_clean_and_free(app, reprs);
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
	atomic_t *replies = &priv->reify_replies;
	struct nfp_flower_repr_priv *repr_priv;
	struct nfp_repr *nfp_repr;
	struct sk_buff *ctrl_skb;
	struct nfp_reprs *reprs;
	int err, reify_cnt;
	unsigned int i;

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
		struct net_device *repr;
		struct nfp_port *port;
		u32 cmsg_port_id;

		repr = nfp_repr_alloc(app);
		if (!repr) {
			err = -ENOMEM;
			goto err_reprs_clean;
		}

		repr_priv = kzalloc(sizeof(*repr_priv), GFP_KERNEL);
		if (!repr_priv) {
			err = -ENOMEM;
			goto err_reprs_clean;
		}

		nfp_repr = netdev_priv(repr);
		nfp_repr->app_priv = repr_priv;

		port = nfp_port_alloc(app, NFP_PORT_PHYS_PORT, repr);
		if (IS_ERR(port)) {
			err = PTR_ERR(port);
			nfp_repr_free(repr);
			goto err_reprs_clean;
		}
		err = nfp_port_init_phy_port(app->pf, app, port, i);
		if (err) {
			nfp_port_free(port);
			nfp_repr_free(repr);
			goto err_reprs_clean;
		}

		SET_NETDEV_DEV(repr, &priv->nn->pdev->dev);
		nfp_net_get_mac_addr(app->pf, repr, port);

		cmsg_port_id = nfp_flower_cmsg_phys_port(phys_port);
		err = nfp_repr_init(app, repr,
				    cmsg_port_id, port, priv->nn->dp.netdev);
		if (err) {
			nfp_port_free(port);
			nfp_repr_free(repr);
			goto err_reprs_clean;
		}

		nfp_flower_cmsg_mac_repr_add(ctrl_skb, i,
					     eth_tbl->ports[i].nbi,
					     eth_tbl->ports[i].base,
					     phys_port);

		RCU_INIT_POINTER(reprs->reprs[phys_port], repr);
		nfp_info(app->cpp, "Phys Port %d Representor(%s) created\n",
			 phys_port, repr->name);
	}

	nfp_app_reprs_set(app, NFP_REPR_TYPE_PHYS_PORT, reprs);

	/* The REIFY/MAC_REPR control messages should be sent after the MAC
	 * representors are registered using nfp_app_reprs_set().  This is
	 * because the firmware may respond with control messages for the
	 * MAC representors, f.e. to provide the driver with information
	 * about their state, and without registration the driver will drop
	 * any such messages.
	 */
	atomic_set(replies, 0);
	reify_cnt = nfp_flower_reprs_reify(app, NFP_REPR_TYPE_PHYS_PORT, true);
	if (reify_cnt < 0) {
		err = reify_cnt;
		nfp_warn(app->cpp, "Failed to notify firmware about repr creation\n");
		goto err_reprs_remove;
	}

	err = nfp_flower_wait_repr_reify(app, replies, reify_cnt);
	if (err)
		goto err_reprs_remove;

	nfp_ctrl_tx(app->ctrl, ctrl_skb);

	return 0;
err_reprs_remove:
	reprs = nfp_app_reprs_set(app, NFP_REPR_TYPE_PHYS_PORT, NULL);
err_reprs_clean:
	nfp_reprs_clean_and_free(app, reprs);
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
	nn->vnic_no_name = true;

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
	u64 version, features;
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
	skb_queue_head_init(&app_priv->cmsg_skbs_high);
	skb_queue_head_init(&app_priv->cmsg_skbs_low);
	INIT_WORK(&app_priv->cmsg_work, nfp_flower_cmsg_process_rx);
	init_waitqueue_head(&app_priv->reify_wait_queue);

	init_waitqueue_head(&app_priv->mtu_conf.wait_q);
	spin_lock_init(&app_priv->mtu_conf.lock);

	err = nfp_flower_metadata_init(app);
	if (err)
		goto err_free_app_priv;

	/* Extract the extra features supported by the firmware. */
	features = nfp_rtsym_read_le(app->pf->rtbl,
				     "_abi_flower_extra_features", &err);
	if (err)
		app_priv->flower_ext_feats = 0;
	else
		app_priv->flower_ext_feats = features;

	/* Tell the firmware that the driver supports lag. */
	err = nfp_rtsym_write_le(app->pf->rtbl,
				 "_abi_flower_balance_sync_enable", 1);
	if (!err) {
		app_priv->flower_ext_feats |= NFP_FL_FEATS_LAG;
		nfp_flower_lag_init(&app_priv->nfp_lag);
	} else if (err == -ENOENT) {
		nfp_warn(app->cpp, "LAG not supported by FW.\n");
	} else {
		goto err_cleanup_metadata;
	}

	return 0;

err_cleanup_metadata:
	nfp_flower_metadata_cleanup(app);
err_free_app_priv:
	vfree(app->priv);
	return err;
}

static void nfp_flower_clean(struct nfp_app *app)
{
	struct nfp_flower_priv *app_priv = app->priv;

	skb_queue_purge(&app_priv->cmsg_skbs_high);
	skb_queue_purge(&app_priv->cmsg_skbs_low);
	flush_work(&app_priv->cmsg_work);

	if (app_priv->flower_ext_feats & NFP_FL_FEATS_LAG)
		nfp_flower_lag_cleanup(&app_priv->nfp_lag);

	nfp_flower_metadata_cleanup(app);
	vfree(app->priv);
	app->priv = NULL;
}

static bool nfp_flower_check_ack(struct nfp_flower_priv *app_priv)
{
	bool ret;

	spin_lock_bh(&app_priv->mtu_conf.lock);
	ret = app_priv->mtu_conf.ack;
	spin_unlock_bh(&app_priv->mtu_conf.lock);

	return ret;
}

static int
nfp_flower_repr_change_mtu(struct nfp_app *app, struct net_device *netdev,
			   int new_mtu)
{
	struct nfp_flower_priv *app_priv = app->priv;
	struct nfp_repr *repr = netdev_priv(netdev);
	int err, ack;

	/* Only need to config FW for physical port MTU change. */
	if (repr->port->type != NFP_PORT_PHYS_PORT)
		return 0;

	if (!(app_priv->flower_ext_feats & NFP_FL_NBI_MTU_SETTING)) {
		nfp_err(app->cpp, "Physical port MTU setting not supported\n");
		return -EINVAL;
	}

	spin_lock_bh(&app_priv->mtu_conf.lock);
	app_priv->mtu_conf.ack = false;
	app_priv->mtu_conf.requested_val = new_mtu;
	app_priv->mtu_conf.portnum = repr->dst->u.port_info.port_id;
	spin_unlock_bh(&app_priv->mtu_conf.lock);

	err = nfp_flower_cmsg_portmod(repr, netif_carrier_ok(netdev), new_mtu,
				      true);
	if (err) {
		spin_lock_bh(&app_priv->mtu_conf.lock);
		app_priv->mtu_conf.requested_val = 0;
		spin_unlock_bh(&app_priv->mtu_conf.lock);
		return err;
	}

	/* Wait for fw to ack the change. */
	ack = wait_event_timeout(app_priv->mtu_conf.wait_q,
				 nfp_flower_check_ack(app_priv),
				 msecs_to_jiffies(10));

	if (!ack) {
		spin_lock_bh(&app_priv->mtu_conf.lock);
		app_priv->mtu_conf.requested_val = 0;
		spin_unlock_bh(&app_priv->mtu_conf.lock);
		nfp_warn(app->cpp, "MTU change not verified with fw\n");
		return -EIO;
	}

	return 0;
}

static int nfp_flower_start(struct nfp_app *app)
{
	struct nfp_flower_priv *app_priv = app->priv;
	int err;

	if (app_priv->flower_ext_feats & NFP_FL_FEATS_LAG) {
		err = nfp_flower_lag_reset(&app_priv->nfp_lag);
		if (err)
			return err;

		err = register_netdevice_notifier(&app_priv->nfp_lag.lag_nb);
		if (err)
			return err;
	}

	return nfp_tunnel_config_start(app);
}

static void nfp_flower_stop(struct nfp_app *app)
{
	struct nfp_flower_priv *app_priv = app->priv;

	if (app_priv->flower_ext_feats & NFP_FL_FEATS_LAG)
		unregister_netdevice_notifier(&app_priv->nfp_lag.lag_nb);

	nfp_tunnel_config_stop(app);
}

const struct nfp_app_type app_flower = {
	.id		= NFP_APP_FLOWER_NIC,
	.name		= "flower",

	.ctrl_cap_mask	= ~0U,
	.ctrl_has_meta	= true,

	.extra_cap	= nfp_flower_extra_cap,

	.init		= nfp_flower_init,
	.clean		= nfp_flower_clean,

	.repr_change_mtu  = nfp_flower_repr_change_mtu,

	.vnic_alloc	= nfp_flower_vnic_alloc,
	.vnic_init	= nfp_flower_vnic_init,
	.vnic_clean	= nfp_flower_vnic_clean,

	.repr_init	= nfp_flower_repr_netdev_init,
	.repr_preclean	= nfp_flower_repr_netdev_preclean,
	.repr_clean	= nfp_flower_repr_netdev_clean,

	.repr_open	= nfp_flower_repr_netdev_open,
	.repr_stop	= nfp_flower_repr_netdev_stop,

	.start		= nfp_flower_start,
	.stop		= nfp_flower_stop,

	.ctrl_msg_rx	= nfp_flower_cmsg_rx,

	.sriov_enable	= nfp_flower_sriov_enable,
	.sriov_disable	= nfp_flower_sriov_disable,

	.eswitch_mode_get  = eswitch_mode_get,
	.repr_get	= nfp_flower_repr_get,

	.setup_tc	= nfp_flower_setup_tc,
};
