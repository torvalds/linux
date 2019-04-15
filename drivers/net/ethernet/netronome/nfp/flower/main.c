// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

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

#define NFP_MIN_INT_PORT_ID	1
#define NFP_MAX_INT_PORT_ID	256

static const char *nfp_flower_extra_cap(struct nfp_app *app, struct nfp_net *nn)
{
	return "FLOWER";
}

static enum devlink_eswitch_mode eswitch_mode_get(struct nfp_app *app)
{
	return DEVLINK_ESWITCH_MODE_SWITCHDEV;
}

static int
nfp_flower_lookup_internal_port_id(struct nfp_flower_priv *priv,
				   struct net_device *netdev)
{
	struct net_device *entry;
	int i, id = 0;

	rcu_read_lock();
	idr_for_each_entry(&priv->internal_ports.port_ids, entry, i)
		if (entry == netdev) {
			id = i;
			break;
		}
	rcu_read_unlock();

	return id;
}

static int
nfp_flower_get_internal_port_id(struct nfp_app *app, struct net_device *netdev)
{
	struct nfp_flower_priv *priv = app->priv;
	int id;

	id = nfp_flower_lookup_internal_port_id(priv, netdev);
	if (id > 0)
		return id;

	idr_preload(GFP_ATOMIC);
	spin_lock_bh(&priv->internal_ports.lock);
	id = idr_alloc(&priv->internal_ports.port_ids, netdev,
		       NFP_MIN_INT_PORT_ID, NFP_MAX_INT_PORT_ID, GFP_ATOMIC);
	spin_unlock_bh(&priv->internal_ports.lock);
	idr_preload_end();

	return id;
}

u32 nfp_flower_get_port_id_from_netdev(struct nfp_app *app,
				       struct net_device *netdev)
{
	int ext_port;

	if (nfp_netdev_is_nfp_repr(netdev)) {
		return nfp_repr_get_port_id(netdev);
	} else if (nfp_flower_internal_port_can_offload(app, netdev)) {
		ext_port = nfp_flower_get_internal_port_id(app, netdev);
		if (ext_port < 0)
			return 0;

		return nfp_flower_internal_port_get_port_id(ext_port);
	}

	return 0;
}

static struct net_device *
nfp_flower_get_netdev_from_internal_port_id(struct nfp_app *app, int port_id)
{
	struct nfp_flower_priv *priv = app->priv;
	struct net_device *netdev;

	rcu_read_lock();
	netdev = idr_find(&priv->internal_ports.port_ids, port_id);
	rcu_read_unlock();

	return netdev;
}

static void
nfp_flower_free_internal_port_id(struct nfp_app *app, struct net_device *netdev)
{
	struct nfp_flower_priv *priv = app->priv;
	int id;

	id = nfp_flower_lookup_internal_port_id(priv, netdev);
	if (!id)
		return;

	spin_lock_bh(&priv->internal_ports.lock);
	idr_remove(&priv->internal_ports.port_ids, id);
	spin_unlock_bh(&priv->internal_ports.lock);
}

static int
nfp_flower_internal_port_event_handler(struct nfp_app *app,
				       struct net_device *netdev,
				       unsigned long event)
{
	if (event == NETDEV_UNREGISTER &&
	    nfp_flower_internal_port_can_offload(app, netdev))
		nfp_flower_free_internal_port_id(app, netdev);

	return NOTIFY_OK;
}

static void nfp_flower_internal_port_init(struct nfp_flower_priv *priv)
{
	spin_lock_init(&priv->internal_ports.lock);
	idr_init(&priv->internal_ports.port_ids);
}

static void nfp_flower_internal_port_cleanup(struct nfp_flower_priv *priv)
{
	idr_destroy(&priv->internal_ports.port_ids);
}

static struct nfp_flower_non_repr_priv *
nfp_flower_non_repr_priv_lookup(struct nfp_app *app, struct net_device *netdev)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_flower_non_repr_priv *entry;

	ASSERT_RTNL();

	list_for_each_entry(entry, &priv->non_repr_priv, list)
		if (entry->netdev == netdev)
			return entry;

	return NULL;
}

void
__nfp_flower_non_repr_priv_get(struct nfp_flower_non_repr_priv *non_repr_priv)
{
	non_repr_priv->ref_count++;
}

struct nfp_flower_non_repr_priv *
nfp_flower_non_repr_priv_get(struct nfp_app *app, struct net_device *netdev)
{
	struct nfp_flower_priv *priv = app->priv;
	struct nfp_flower_non_repr_priv *entry;

	entry = nfp_flower_non_repr_priv_lookup(app, netdev);
	if (entry)
		goto inc_ref;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return NULL;

	entry->netdev = netdev;
	list_add(&entry->list, &priv->non_repr_priv);

inc_ref:
	__nfp_flower_non_repr_priv_get(entry);
	return entry;
}

void
__nfp_flower_non_repr_priv_put(struct nfp_flower_non_repr_priv *non_repr_priv)
{
	if (--non_repr_priv->ref_count)
		return;

	list_del(&non_repr_priv->list);
	kfree(non_repr_priv);
}

void
nfp_flower_non_repr_priv_put(struct nfp_app *app, struct net_device *netdev)
{
	struct nfp_flower_non_repr_priv *entry;

	entry = nfp_flower_non_repr_priv_lookup(app, netdev);
	if (!entry)
		return;

	__nfp_flower_non_repr_priv_put(entry);
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
nfp_flower_dev_get(struct nfp_app *app, u32 port_id, bool *redir_egress)
{
	enum nfp_repr_type repr_type;
	struct nfp_reprs *reprs;
	u8 port = 0;

	/* Check if the port is internal. */
	if (FIELD_GET(NFP_FLOWER_CMSG_PORT_TYPE, port_id) ==
	    NFP_FLOWER_CMSG_PORT_TYPE_OTHER_PORT) {
		if (redir_egress)
			*redir_egress = true;
		port = FIELD_GET(NFP_FLOWER_CMSG_PORT_PHYS_PORT_NUM, port_id);
		return nfp_flower_get_netdev_from_internal_port_id(app, port);
	}

	repr_type = nfp_flower_repr_get_type_and_port(app, port_id, &port);
	if (repr_type > NFP_REPR_TYPE_MAX)
		return NULL;

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

	if (!tot_repl)
		return 0;

	lockdep_assert_held(&app->pf->lock);
	if (!wait_event_timeout(priv->reify_wait_queue,
				atomic_read(replies) >= tot_repl,
				NFP_FL_REPLY_TIMEOUT)) {
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

static void
nfp_flower_repr_netdev_clean(struct nfp_app *app, struct net_device *netdev)
{
	struct nfp_repr *repr = netdev_priv(netdev);

	kfree(repr->app_priv);
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
		repr_priv->nfp_repr = nfp_repr;

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
		repr_priv->nfp_repr = nfp_repr;

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
	u64 version, features, ctx_count, num_mems;
	const struct nfp_pf *pf = app->pf;
	struct nfp_flower_priv *app_priv;
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

	num_mems = nfp_rtsym_read_le(app->pf->rtbl, "CONFIG_FC_HOST_CTX_SPLIT",
				     &err);
	if (err) {
		nfp_warn(app->cpp,
			 "FlowerNIC: unsupported host context memory: %d\n",
			 err);
		err = 0;
		num_mems = 1;
	}

	if (!FIELD_FIT(NFP_FL_STAT_ID_MU_NUM, num_mems) || !num_mems) {
		nfp_warn(app->cpp,
			 "FlowerNIC: invalid host context memory: %llu\n",
			 num_mems);
		return -EINVAL;
	}

	ctx_count = nfp_rtsym_read_le(app->pf->rtbl, "CONFIG_FC_HOST_CTX_COUNT",
				      &err);
	if (err) {
		nfp_warn(app->cpp,
			 "FlowerNIC: unsupported host context count: %d\n",
			 err);
		err = 0;
		ctx_count = BIT(17);
	}

	/* We need to ensure hardware has enough flower capabilities. */
	if (version != NFP_FLOWER_ALLOWED_VER) {
		nfp_warn(app->cpp, "FlowerNIC: unsupported firmware version\n");
		return -EINVAL;
	}

	app_priv = vzalloc(sizeof(struct nfp_flower_priv));
	if (!app_priv)
		return -ENOMEM;

	app_priv->total_mem_units = num_mems;
	app_priv->active_mem_unit = 0;
	app_priv->stats_ring_size = roundup_pow_of_two(ctx_count);
	app->priv = app_priv;
	app_priv->app = app;
	skb_queue_head_init(&app_priv->cmsg_skbs_high);
	skb_queue_head_init(&app_priv->cmsg_skbs_low);
	INIT_WORK(&app_priv->cmsg_work, nfp_flower_cmsg_process_rx);
	init_waitqueue_head(&app_priv->reify_wait_queue);

	init_waitqueue_head(&app_priv->mtu_conf.wait_q);
	spin_lock_init(&app_priv->mtu_conf.lock);

	err = nfp_flower_metadata_init(app, ctx_count, num_mems);
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

	if (app_priv->flower_ext_feats & NFP_FL_FEATS_FLOW_MOD) {
		/* Tell the firmware that the driver supports flow merging. */
		err = nfp_rtsym_write_le(app->pf->rtbl,
					 "_abi_flower_merge_hint_enable", 1);
		if (!err) {
			app_priv->flower_ext_feats |= NFP_FL_FEATS_FLOW_MERGE;
			nfp_flower_internal_port_init(app_priv);
		} else if (err == -ENOENT) {
			nfp_warn(app->cpp, "Flow merge not supported by FW.\n");
		} else {
			goto err_lag_clean;
		}
	} else {
		nfp_warn(app->cpp, "Flow mod/merge not supported by FW.\n");
	}

	INIT_LIST_HEAD(&app_priv->indr_block_cb_priv);
	INIT_LIST_HEAD(&app_priv->non_repr_priv);

	return 0;

err_lag_clean:
	if (app_priv->flower_ext_feats & NFP_FL_FEATS_LAG)
		nfp_flower_lag_cleanup(&app_priv->nfp_lag);
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

	if (app_priv->flower_ext_feats & NFP_FL_FEATS_FLOW_MERGE)
		nfp_flower_internal_port_cleanup(app_priv);

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
	int err;

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
	if (!wait_event_timeout(app_priv->mtu_conf.wait_q,
				nfp_flower_check_ack(app_priv),
				NFP_FL_REPLY_TIMEOUT)) {
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
	}

	return nfp_tunnel_config_start(app);
}

static void nfp_flower_stop(struct nfp_app *app)
{
	nfp_tunnel_config_stop(app);
}

static int
nfp_flower_netdev_event(struct nfp_app *app, struct net_device *netdev,
			unsigned long event, void *ptr)
{
	struct nfp_flower_priv *app_priv = app->priv;
	int ret;

	if (app_priv->flower_ext_feats & NFP_FL_FEATS_LAG) {
		ret = nfp_flower_lag_netdev_event(app_priv, netdev, event, ptr);
		if (ret & NOTIFY_STOP_MASK)
			return ret;
	}

	ret = nfp_flower_reg_indir_block_handler(app, netdev, event);
	if (ret & NOTIFY_STOP_MASK)
		return ret;

	ret = nfp_flower_internal_port_event_handler(app, netdev, event);
	if (ret & NOTIFY_STOP_MASK)
		return ret;

	return nfp_tunnel_mac_event_handler(app, netdev, event, ptr);
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

	.repr_preclean	= nfp_flower_repr_netdev_preclean,
	.repr_clean	= nfp_flower_repr_netdev_clean,

	.repr_open	= nfp_flower_repr_netdev_open,
	.repr_stop	= nfp_flower_repr_netdev_stop,

	.start		= nfp_flower_start,
	.stop		= nfp_flower_stop,

	.netdev_event	= nfp_flower_netdev_event,

	.ctrl_msg_rx	= nfp_flower_cmsg_rx,

	.sriov_enable	= nfp_flower_sriov_enable,
	.sriov_disable	= nfp_flower_sriov_disable,

	.eswitch_mode_get  = eswitch_mode_get,
	.dev_get	= nfp_flower_dev_get,

	.setup_tc	= nfp_flower_setup_tc,
};
