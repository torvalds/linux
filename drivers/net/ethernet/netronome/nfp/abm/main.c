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

#include <linux/bitfield.h>
#include <linux/etherdevice.h>
#include <linux/lockdep.h>
#include <linux/netdevice.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>
#include <net/red.h>

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
__nfp_abm_reset_root(struct net_device *netdev, struct nfp_abm_link *alink,
		     u32 handle, unsigned int qs, u32 init_val)
{
	struct nfp_port *port = nfp_port_from_netdev(netdev);
	int ret;

	ret = nfp_abm_ctrl_set_all_q_lvls(alink, init_val);
	memset(alink->qdiscs, 0, sizeof(*alink->qdiscs) * alink->num_qdiscs);

	alink->parent = handle;
	alink->num_qdiscs = qs;
	port->tc_offload_cnt = qs;

	return ret;
}

static void
nfp_abm_reset_root(struct net_device *netdev, struct nfp_abm_link *alink,
		   u32 handle, unsigned int qs)
{
	__nfp_abm_reset_root(netdev, alink, handle, qs, ~0);
}

static int
nfp_abm_red_find(struct nfp_abm_link *alink, struct tc_red_qopt_offload *opt)
{
	unsigned int i = TC_H_MIN(opt->parent) - 1;

	if (opt->parent == TC_H_ROOT)
		i = 0;
	else if (TC_H_MAJ(alink->parent) == TC_H_MAJ(opt->parent))
		i = TC_H_MIN(opt->parent) - 1;
	else
		return -EOPNOTSUPP;

	if (i >= alink->num_qdiscs || opt->handle != alink->qdiscs[i].handle)
		return -EOPNOTSUPP;

	return i;
}

static void
nfp_abm_red_destroy(struct net_device *netdev, struct nfp_abm_link *alink,
		    u32 handle)
{
	unsigned int i;

	for (i = 0; i < alink->num_qdiscs; i++)
		if (handle == alink->qdiscs[i].handle)
			break;
	if (i == alink->num_qdiscs)
		return;

	if (alink->parent == TC_H_ROOT) {
		nfp_abm_reset_root(netdev, alink, TC_H_ROOT, 0);
	} else {
		nfp_abm_ctrl_set_q_lvl(alink, i, ~0);
		memset(&alink->qdiscs[i], 0, sizeof(*alink->qdiscs));
	}
}

static int
nfp_abm_red_replace(struct net_device *netdev, struct nfp_abm_link *alink,
		    struct tc_red_qopt_offload *opt)
{
	bool existing;
	int i, err;

	i = nfp_abm_red_find(alink, opt);
	existing = i >= 0;

	if (opt->set.min != opt->set.max || !opt->set.is_ecn) {
		nfp_warn(alink->abm->app->cpp,
			 "RED offload failed - unsupported parameters\n");
		err = -EINVAL;
		goto err_destroy;
	}

	if (existing) {
		if (alink->parent == TC_H_ROOT)
			err = nfp_abm_ctrl_set_all_q_lvls(alink, opt->set.min);
		else
			err = nfp_abm_ctrl_set_q_lvl(alink, i, opt->set.min);
		if (err)
			goto err_destroy;
		return 0;
	}

	if (opt->parent == TC_H_ROOT) {
		i = 0;
		err = __nfp_abm_reset_root(netdev, alink, TC_H_ROOT, 1,
					   opt->set.min);
	} else if (TC_H_MAJ(alink->parent) == TC_H_MAJ(opt->parent)) {
		i = TC_H_MIN(opt->parent) - 1;
		err = nfp_abm_ctrl_set_q_lvl(alink, i, opt->set.min);
	} else {
		return -EINVAL;
	}
	/* Set the handle to try full clean up, in case IO failed */
	alink->qdiscs[i].handle = opt->handle;
	if (err)
		goto err_destroy;

	if (opt->parent == TC_H_ROOT)
		err = nfp_abm_ctrl_read_stats(alink, &alink->qdiscs[i].stats);
	else
		err = nfp_abm_ctrl_read_q_stats(alink, i,
						&alink->qdiscs[i].stats);
	if (err)
		goto err_destroy;

	if (opt->parent == TC_H_ROOT)
		err = nfp_abm_ctrl_read_xstats(alink,
					       &alink->qdiscs[i].xstats);
	else
		err = nfp_abm_ctrl_read_q_xstats(alink, i,
						 &alink->qdiscs[i].xstats);
	if (err)
		goto err_destroy;

	alink->qdiscs[i].stats.backlog_pkts = 0;
	alink->qdiscs[i].stats.backlog_bytes = 0;

	return 0;
err_destroy:
	/* If the qdisc keeps on living, but we can't offload undo changes */
	if (existing) {
		opt->set.qstats->qlen -= alink->qdiscs[i].stats.backlog_pkts;
		opt->set.qstats->backlog -=
			alink->qdiscs[i].stats.backlog_bytes;
	}
	nfp_abm_red_destroy(netdev, alink, opt->handle);

	return err;
}

static void
nfp_abm_update_stats(struct nfp_alink_stats *new, struct nfp_alink_stats *old,
		     struct tc_qopt_offload_stats *stats)
{
	_bstats_update(stats->bstats, new->tx_bytes - old->tx_bytes,
		       new->tx_pkts - old->tx_pkts);
	stats->qstats->qlen += new->backlog_pkts - old->backlog_pkts;
	stats->qstats->backlog += new->backlog_bytes - old->backlog_bytes;
	stats->qstats->overlimits += new->overlimits - old->overlimits;
	stats->qstats->drops += new->drops - old->drops;
}

static int
nfp_abm_red_stats(struct nfp_abm_link *alink, struct tc_red_qopt_offload *opt)
{
	struct nfp_alink_stats *prev_stats;
	struct nfp_alink_stats stats;
	int i, err;

	i = nfp_abm_red_find(alink, opt);
	if (i < 0)
		return i;
	prev_stats = &alink->qdiscs[i].stats;

	if (alink->parent == TC_H_ROOT)
		err = nfp_abm_ctrl_read_stats(alink, &stats);
	else
		err = nfp_abm_ctrl_read_q_stats(alink, i, &stats);
	if (err)
		return err;

	nfp_abm_update_stats(&stats, prev_stats, &opt->stats);

	*prev_stats = stats;

	return 0;
}

static int
nfp_abm_red_xstats(struct nfp_abm_link *alink, struct tc_red_qopt_offload *opt)
{
	struct nfp_alink_xstats *prev_xstats;
	struct nfp_alink_xstats xstats;
	int i, err;

	i = nfp_abm_red_find(alink, opt);
	if (i < 0)
		return i;
	prev_xstats = &alink->qdiscs[i].xstats;

	if (alink->parent == TC_H_ROOT)
		err = nfp_abm_ctrl_read_xstats(alink, &xstats);
	else
		err = nfp_abm_ctrl_read_q_xstats(alink, i, &xstats);
	if (err)
		return err;

	opt->xstats->forced_mark += xstats.ecn_marked - prev_xstats->ecn_marked;
	opt->xstats->pdrop += xstats.pdrop - prev_xstats->pdrop;

	*prev_xstats = xstats;

	return 0;
}

static int
nfp_abm_setup_tc_red(struct net_device *netdev, struct nfp_abm_link *alink,
		     struct tc_red_qopt_offload *opt)
{
	switch (opt->command) {
	case TC_RED_REPLACE:
		return nfp_abm_red_replace(netdev, alink, opt);
	case TC_RED_DESTROY:
		nfp_abm_red_destroy(netdev, alink, opt->handle);
		return 0;
	case TC_RED_STATS:
		return nfp_abm_red_stats(alink, opt);
	case TC_RED_XSTATS:
		return nfp_abm_red_xstats(alink, opt);
	default:
		return -EOPNOTSUPP;
	}
}

static int
nfp_abm_mq_stats(struct nfp_abm_link *alink, struct tc_mq_qopt_offload *opt)
{
	struct nfp_alink_stats stats;
	unsigned int i;
	int err;

	for (i = 0; i < alink->num_qdiscs; i++) {
		if (alink->qdiscs[i].handle == TC_H_UNSPEC)
			continue;

		err = nfp_abm_ctrl_read_q_stats(alink, i, &stats);
		if (err)
			return err;

		nfp_abm_update_stats(&stats, &alink->qdiscs[i].stats,
				     &opt->stats);
	}

	return 0;
}

static int
nfp_abm_setup_tc_mq(struct net_device *netdev, struct nfp_abm_link *alink,
		    struct tc_mq_qopt_offload *opt)
{
	switch (opt->command) {
	case TC_MQ_CREATE:
		nfp_abm_reset_root(netdev, alink, opt->handle,
				   alink->total_queues);
		return 0;
	case TC_MQ_DESTROY:
		if (opt->handle == alink->parent)
			nfp_abm_reset_root(netdev, alink, TC_H_ROOT, 0);
		return 0;
	case TC_MQ_STATS:
		return nfp_abm_mq_stats(alink, opt);
	default:
		return -EOPNOTSUPP;
	}
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
	case TC_SETUP_QDISC_MQ:
		return nfp_abm_setup_tc_mq(netdev, repr->app_priv, type_data);
	case TC_SETUP_QDISC_RED:
		return nfp_abm_setup_tc_red(netdev, repr->app_priv, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static struct net_device *nfp_abm_repr_get(struct nfp_app *app, u32 port_id)
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
	rcu_assign_pointer(reprs->reprs[alink->id], netdev);

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
	rcu_assign_pointer(reprs->reprs[alink->id], NULL);
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
	alink->parent = TC_H_ROOT;
	alink->total_queues = alink->vnic->max_rx_rings;
	alink->qdiscs = kvcalloc(alink->total_queues, sizeof(*alink->qdiscs),
				 GFP_KERNEL);
	if (!alink->qdiscs) {
		err = -ENOMEM;
		goto err_free_alink;
	}

	/* This is a multi-host app, make sure MAC/PHY is up, but don't
	 * make the MAC/PHY state follow the state of any of the ports.
	 */
	err = nfp_eth_set_configured(app->cpp, eth_port->index, true);
	if (err < 0)
		goto err_free_qdiscs;

	netif_keep_dst(nn->dp.netdev);

	nfp_abm_vnic_set_mac(app->pf, abm, nn, id);
	nfp_abm_ctrl_read_params(alink);

	return 0;

err_free_qdiscs:
	kvfree(alink->qdiscs);
err_free_alink:
	kfree(alink);
	return err;
}

static void nfp_abm_vnic_free(struct nfp_app *app, struct nfp_net *nn)
{
	struct nfp_abm_link *alink = nn->app_priv;

	nfp_abm_kill_reprs(alink->abm, alink);
	kvfree(alink->qdiscs);
	kfree(alink);
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

	/* We start in legacy mode, make sure advanced queuing is disabled */
	err = nfp_abm_ctrl_qm_disable(abm);
	if (err)
		goto err_free_abm;

	err = -ENOMEM;
	reprs = nfp_reprs_alloc(pf->max_data_vnics);
	if (!reprs)
		goto err_free_abm;
	RCU_INIT_POINTER(app->reprs[NFP_REPR_TYPE_PHYS_PORT], reprs);

	reprs = nfp_reprs_alloc(pf->max_data_vnics);
	if (!reprs)
		goto err_free_phys;
	RCU_INIT_POINTER(app->reprs[NFP_REPR_TYPE_PF], reprs);

	return 0;

err_free_phys:
	nfp_reprs_clean_and_free_by_type(app, NFP_REPR_TYPE_PHYS_PORT);
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

	.port_get_stats		= nfp_abm_port_get_stats,
	.port_get_stats_count	= nfp_abm_port_get_stats_count,
	.port_get_stats_strings	= nfp_abm_port_get_stats_strings,

	.setup_tc	= nfp_abm_setup_tc,

	.eswitch_mode_get	= nfp_abm_eswitch_mode_get,
	.eswitch_mode_set	= nfp_abm_eswitch_mode_set,

	.repr_get	= nfp_abm_repr_get,
};
