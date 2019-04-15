// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc. */

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/etherdevice.h>
#include <linux/lockdep.h>
#include <linux/netdevice.h>
#include <linux/rcupdate.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>

#include "../nfpcore/nfp.h"
#include "../nfpcore/nfp_cpp.h"
#include "../nfpcore/nfp_nsp.h"
#include "../nfp_app.h"
#include "../nfp_main.h"
#include "../nfp_net.h"
#include "../nfp_net_repr.h"
#include "../nfp_port.h"
#include "main.h"

static u32 nfp_abm_portid(enum nfp_repr_type rtype, unsigned int id)
{
	return FIELD_PREP(NFP_ABM_PORTID_TYPE, rtype) |
	       FIELD_PREP(NFP_ABM_PORTID_ID, id);
}

static int
nfp_abm_setup_tc(struct nfp_app *app, struct net_device *netdev,
		 enum tc_setup_type type, void *type_data)
{
	struct nfp_repr *repr = netdev_priv(netdev);
	struct nfp_port *port;

	port = nfp_port_from_netdev(netdev);
	if (!port || port->type != NFP_PORT_PF_PORT)
		return -EOPNOTSUPP;

	switch (type) {
	case TC_SETUP_ROOT_QDISC:
		return nfp_abm_setup_root(netdev, repr->app_priv, type_data);
	case TC_SETUP_QDISC_MQ:
		return nfp_abm_setup_tc_mq(netdev, repr->app_priv, type_data);
	case TC_SETUP_QDISC_RED:
		return nfp_abm_setup_tc_red(netdev, repr->app_priv, type_data);
	case TC_SETUP_QDISC_GRED:
		return nfp_abm_setup_tc_gred(netdev, repr->app_priv, type_data);
	case TC_SETUP_BLOCK:
		return nfp_abm_setup_cls_block(netdev, repr, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static struct net_device *
nfp_abm_repr_get(struct nfp_app *app, u32 port_id, bool *redir_egress)
{
	enum nfp_repr_type rtype;
	struct nfp_reprs *reprs;
	u8 port;

	rtype = FIELD_GET(NFP_ABM_PORTID_TYPE, port_id);
	port = FIELD_GET(NFP_ABM_PORTID_ID, port_id);

	reprs = rcu_dereference(app->reprs[rtype]);
	if (!reprs)
		return NULL;

	if (port >= reprs->num_reprs)
		return NULL;

	return rcu_dereference(reprs->reprs[port]);
}

static int
nfp_abm_spawn_repr(struct nfp_app *app, struct nfp_abm_link *alink,
		   enum nfp_port_type ptype)
{
	struct net_device *netdev;
	enum nfp_repr_type rtype;
	struct nfp_reprs *reprs;
	struct nfp_repr *repr;
	struct nfp_port *port;
	unsigned int txqs;
	int err;

	if (ptype == NFP_PORT_PHYS_PORT) {
		rtype = NFP_REPR_TYPE_PHYS_PORT;
		txqs = 1;
	} else {
		rtype = NFP_REPR_TYPE_PF;
		txqs = alink->vnic->max_rx_rings;
	}

	netdev = nfp_repr_alloc_mqs(app, txqs, 1);
	if (!netdev)
		return -ENOMEM;
	repr = netdev_priv(netdev);
	repr->app_priv = alink;

	port = nfp_port_alloc(app, ptype, netdev);
	if (IS_ERR(port)) {
		err = PTR_ERR(port);
		goto err_free_repr;
	}

	if (ptype == NFP_PORT_PHYS_PORT) {
		port->eth_forced = true;
		err = nfp_port_init_phy_port(app->pf, app, port, alink->id);
		if (err)
			goto err_free_port;
	} else {
		port->pf_id = alink->abm->pf_id;
		port->pf_split = app->pf->max_data_vnics > 1;
		port->pf_split_id = alink->id;
		port->vnic = alink->vnic->dp.ctrl_bar;
	}

	SET_NETDEV_DEV(netdev, &alink->vnic->pdev->dev);
	eth_hw_addr_random(netdev);

	err = nfp_repr_init(app, netdev, nfp_abm_portid(rtype, alink->id),
			    port, alink->vnic->dp.netdev);
	if (err)
		goto err_free_port;

	reprs = nfp_reprs_get_locked(app, rtype);
	WARN(nfp_repr_get_locked(app, reprs, alink->id), "duplicate repr");
	rtnl_lock();
	rcu_assign_pointer(reprs->reprs[alink->id], netdev);
	rtnl_unlock();

	nfp_info(app->cpp, "%s Port %d Representor(%s) created\n",
		 ptype == NFP_PORT_PF_PORT ? "PCIe" : "Phys",
		 alink->id, netdev->name);

	return 0;

err_free_port:
	nfp_port_free(port);
err_free_repr:
	nfp_repr_free(netdev);
	return err;
}

static void
nfp_abm_kill_repr(struct nfp_app *app, struct nfp_abm_link *alink,
		  enum nfp_repr_type rtype)
{
	struct net_device *netdev;
	struct nfp_reprs *reprs;

	reprs = nfp_reprs_get_locked(app, rtype);
	netdev = nfp_repr_get_locked(app, reprs, alink->id);
	if (!netdev)
		return;
	rtnl_lock();
	rcu_assign_pointer(reprs->reprs[alink->id], NULL);
	rtnl_unlock();
	synchronize_rcu();
	/* Cast to make sure nfp_repr_clean_and_free() takes a nfp_repr */
	nfp_repr_clean_and_free((struct nfp_repr *)netdev_priv(netdev));
}

static void
nfp_abm_kill_reprs(struct nfp_abm *abm, struct nfp_abm_link *alink)
{
	nfp_abm_kill_repr(abm->app, alink, NFP_REPR_TYPE_PF);
	nfp_abm_kill_repr(abm->app, alink, NFP_REPR_TYPE_PHYS_PORT);
}

static void nfp_abm_kill_reprs_all(struct nfp_abm *abm)
{
	struct nfp_pf *pf = abm->app->pf;
	struct nfp_net *nn;

	list_for_each_entry(nn, &pf->vnics, vnic_list)
		nfp_abm_kill_reprs(abm, (struct nfp_abm_link *)nn->app_priv);
}

static enum devlink_eswitch_mode nfp_abm_eswitch_mode_get(struct nfp_app *app)
{
	struct nfp_abm *abm = app->priv;

	return abm->eswitch_mode;
}

static int nfp_abm_eswitch_set_legacy(struct nfp_abm *abm)
{
	nfp_abm_kill_reprs_all(abm);
	nfp_abm_ctrl_qm_disable(abm);

	abm->eswitch_mode = DEVLINK_ESWITCH_MODE_LEGACY;
	return 0;
}

static void nfp_abm_eswitch_clean_up(struct nfp_abm *abm)
{
	if (abm->eswitch_mode != DEVLINK_ESWITCH_MODE_LEGACY)
		WARN_ON(nfp_abm_eswitch_set_legacy(abm));
}

static int nfp_abm_eswitch_set_switchdev(struct nfp_abm *abm)
{
	struct nfp_app *app = abm->app;
	struct nfp_pf *pf = app->pf;
	struct nfp_net *nn;
	int err;

	if (!abm->red_support)
		return -EOPNOTSUPP;

	err = nfp_abm_ctrl_qm_enable(abm);
	if (err)
		return err;

	list_for_each_entry(nn, &pf->vnics, vnic_list) {
		struct nfp_abm_link *alink = nn->app_priv;

		err = nfp_abm_spawn_repr(app, alink, NFP_PORT_PHYS_PORT);
		if (err)
			goto err_kill_all_reprs;

		err = nfp_abm_spawn_repr(app, alink, NFP_PORT_PF_PORT);
		if (err)
			goto err_kill_all_reprs;
	}

	abm->eswitch_mode = DEVLINK_ESWITCH_MODE_SWITCHDEV;
	return 0;

err_kill_all_reprs:
	nfp_abm_kill_reprs_all(abm);
	nfp_abm_ctrl_qm_disable(abm);
	return err;
}

static int nfp_abm_eswitch_mode_set(struct nfp_app *app, u16 mode)
{
	struct nfp_abm *abm = app->priv;

	if (abm->eswitch_mode == mode)
		return 0;

	switch (mode) {
	case DEVLINK_ESWITCH_MODE_LEGACY:
		return nfp_abm_eswitch_set_legacy(abm);
	case DEVLINK_ESWITCH_MODE_SWITCHDEV:
		return nfp_abm_eswitch_set_switchdev(abm);
	default:
		return -EINVAL;
	}
}

static void
nfp_abm_vnic_set_mac(struct nfp_pf *pf, struct nfp_abm *abm, struct nfp_net *nn,
		     unsigned int id)
{
	struct nfp_eth_table_port *eth_port = &pf->eth_tbl->ports[id];
	u8 mac_addr[ETH_ALEN];
	struct nfp_nsp *nsp;
	char hwinfo[32];
	int err;

	if (id > pf->eth_tbl->count) {
		nfp_warn(pf->cpp, "No entry for persistent MAC address\n");
		eth_hw_addr_random(nn->dp.netdev);
		return;
	}

	snprintf(hwinfo, sizeof(hwinfo), "eth%u.mac.pf%u",
		 eth_port->eth_index, abm->pf_id);

	nsp = nfp_nsp_open(pf->cpp);
	if (IS_ERR(nsp)) {
		nfp_warn(pf->cpp, "Failed to access the NSP for persistent MAC address: %ld\n",
			 PTR_ERR(nsp));
		eth_hw_addr_random(nn->dp.netdev);
		return;
	}

	if (!nfp_nsp_has_hwinfo_lookup(nsp)) {
		nfp_warn(pf->cpp, "NSP doesn't support PF MAC generation\n");
		eth_hw_addr_random(nn->dp.netdev);
		return;
	}

	err = nfp_nsp_hwinfo_lookup(nsp, hwinfo, sizeof(hwinfo));
	nfp_nsp_close(nsp);
	if (err) {
		nfp_warn(pf->cpp, "Reading persistent MAC address failed: %d\n",
			 err);
		eth_hw_addr_random(nn->dp.netdev);
		return;
	}

	if (sscanf(hwinfo, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		   &mac_addr[0], &mac_addr[1], &mac_addr[2],
		   &mac_addr[3], &mac_addr[4], &mac_addr[5]) != 6) {
		nfp_warn(pf->cpp, "Can't parse persistent MAC address (%s)\n",
			 hwinfo);
		eth_hw_addr_random(nn->dp.netdev);
		return;
	}

	ether_addr_copy(nn->dp.netdev->dev_addr, mac_addr);
	ether_addr_copy(nn->dp.netdev->perm_addr, mac_addr);
}

static int
nfp_abm_vnic_alloc(struct nfp_app *app, struct nfp_net *nn, unsigned int id)
{
	struct nfp_eth_table_port *eth_port = &app->pf->eth_tbl->ports[id];
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
	alink->total_queues = alink->vnic->max_rx_rings;

	INIT_LIST_HEAD(&alink->dscp_map);

	err = nfp_abm_ctrl_read_params(alink);
	if (err)
		goto err_free_alink;

	alink->prio_map = kzalloc(abm->prio_map_len, GFP_KERNEL);
	if (!alink->prio_map)
		goto err_free_alink;

	/* This is a multi-host app, make sure MAC/PHY is up, but don't
	 * make the MAC/PHY state follow the state of any of the ports.
	 */
	err = nfp_eth_set_configured(app->cpp, eth_port->index, true);
	if (err < 0)
		goto err_free_priomap;

	netif_keep_dst(nn->dp.netdev);

	nfp_abm_vnic_set_mac(app->pf, abm, nn, id);
	INIT_RADIX_TREE(&alink->qdiscs, GFP_KERNEL);

	return 0;

err_free_priomap:
	kfree(alink->prio_map);
err_free_alink:
	kfree(alink);
	return err;
}

static void nfp_abm_vnic_free(struct nfp_app *app, struct nfp_net *nn)
{
	struct nfp_abm_link *alink = nn->app_priv;

	nfp_abm_kill_reprs(alink->abm, alink);
	WARN(!radix_tree_empty(&alink->qdiscs), "left over qdiscs\n");
	kfree(alink->prio_map);
	kfree(alink);
}

static int nfp_abm_vnic_init(struct nfp_app *app, struct nfp_net *nn)
{
	struct nfp_abm_link *alink = nn->app_priv;

	if (nfp_abm_has_prio(alink->abm))
		return nfp_abm_ctrl_prio_map_update(alink, alink->prio_map);
	return 0;
}

static u64 *
nfp_abm_port_get_stats(struct nfp_app *app, struct nfp_port *port, u64 *data)
{
	struct nfp_repr *repr = netdev_priv(port->netdev);
	struct nfp_abm_link *alink;
	unsigned int i;

	if (port->type != NFP_PORT_PF_PORT)
		return data;
	alink = repr->app_priv;
	for (i = 0; i < alink->vnic->dp.num_r_vecs; i++) {
		*data++ = nfp_abm_ctrl_stat_non_sto(alink, i);
		*data++ = nfp_abm_ctrl_stat_sto(alink, i);
	}
	return data;
}

static int
nfp_abm_port_get_stats_count(struct nfp_app *app, struct nfp_port *port)
{
	struct nfp_repr *repr = netdev_priv(port->netdev);
	struct nfp_abm_link *alink;

	if (port->type != NFP_PORT_PF_PORT)
		return 0;
	alink = repr->app_priv;
	return alink->vnic->dp.num_r_vecs * 2;
}

static u8 *
nfp_abm_port_get_stats_strings(struct nfp_app *app, struct nfp_port *port,
			       u8 *data)
{
	struct nfp_repr *repr = netdev_priv(port->netdev);
	struct nfp_abm_link *alink;
	unsigned int i;

	if (port->type != NFP_PORT_PF_PORT)
		return data;
	alink = repr->app_priv;
	for (i = 0; i < alink->vnic->dp.num_r_vecs; i++) {
		data = nfp_pr_et(data, "q%u_no_wait", i);
		data = nfp_pr_et(data, "q%u_delayed", i);
	}
	return data;
}

static int nfp_abm_fw_init_reset(struct nfp_abm *abm)
{
	unsigned int i;

	if (!abm->red_support)
		return 0;

	for (i = 0; i < abm->num_bands * NFP_NET_MAX_RX_RINGS; i++) {
		__nfp_abm_ctrl_set_q_lvl(abm, i, NFP_ABM_LVL_INFINITY);
		__nfp_abm_ctrl_set_q_act(abm, i, NFP_ABM_ACT_DROP);
	}

	return nfp_abm_ctrl_qm_disable(abm);
}

static int nfp_abm_init(struct nfp_app *app)
{
	struct nfp_pf *pf = app->pf;
	struct nfp_reprs *reprs;
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

	err = -ENOMEM;
	abm->num_thresholds = array_size(abm->num_bands, NFP_NET_MAX_RX_RINGS);
	abm->threshold_undef = bitmap_zalloc(abm->num_thresholds, GFP_KERNEL);
	if (!abm->threshold_undef)
		goto err_free_abm;

	abm->thresholds = kvcalloc(abm->num_thresholds,
				   sizeof(*abm->thresholds), GFP_KERNEL);
	if (!abm->thresholds)
		goto err_free_thresh_umap;

	abm->actions = kvcalloc(abm->num_thresholds, sizeof(*abm->actions),
				GFP_KERNEL);
	if (!abm->actions)
		goto err_free_thresh;

	/* We start in legacy mode, make sure advanced queuing is disabled */
	err = nfp_abm_fw_init_reset(abm);
	if (err)
		goto err_free_act;

	err = -ENOMEM;
	reprs = nfp_reprs_alloc(pf->max_data_vnics);
	if (!reprs)
		goto err_free_act;
	RCU_INIT_POINTER(app->reprs[NFP_REPR_TYPE_PHYS_PORT], reprs);

	reprs = nfp_reprs_alloc(pf->max_data_vnics);
	if (!reprs)
		goto err_free_phys;
	RCU_INIT_POINTER(app->reprs[NFP_REPR_TYPE_PF], reprs);

	return 0;

err_free_phys:
	nfp_reprs_clean_and_free_by_type(app, NFP_REPR_TYPE_PHYS_PORT);
err_free_act:
	kvfree(abm->actions);
err_free_thresh:
	kvfree(abm->thresholds);
err_free_thresh_umap:
	bitmap_free(abm->threshold_undef);
err_free_abm:
	kfree(abm);
	app->priv = NULL;
	return err;
}

static void nfp_abm_clean(struct nfp_app *app)
{
	struct nfp_abm *abm = app->priv;

	nfp_abm_eswitch_clean_up(abm);
	nfp_reprs_clean_and_free_by_type(app, NFP_REPR_TYPE_PF);
	nfp_reprs_clean_and_free_by_type(app, NFP_REPR_TYPE_PHYS_PORT);
	bitmap_free(abm->threshold_undef);
	kvfree(abm->actions);
	kvfree(abm->thresholds);
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
	.vnic_init	= nfp_abm_vnic_init,

	.port_get_stats		= nfp_abm_port_get_stats,
	.port_get_stats_count	= nfp_abm_port_get_stats_count,
	.port_get_stats_strings	= nfp_abm_port_get_stats_strings,

	.setup_tc	= nfp_abm_setup_tc,

	.eswitch_mode_get	= nfp_abm_eswitch_mode_get,
	.eswitch_mode_set	= nfp_abm_eswitch_mode_set,

	.dev_get	= nfp_abm_repr_get,
};
