// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#include <linux/rtnetlink.h>
#include <net/devlink.h>

#include "nfpcore/nfp_nsp.h"
#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_port.h"

static int
nfp_devlink_fill_eth_port(struct nfp_port *port,
			  struct nfp_eth_table_port *copy)
{
	struct nfp_eth_table_port *eth_port;

	eth_port = __nfp_port_get_eth_port(port);
	if (!eth_port)
		return -EINVAL;

	memcpy(copy, eth_port, sizeof(*eth_port));

	return 0;
}

static int
nfp_devlink_fill_eth_port_from_id(struct nfp_pf *pf, unsigned int port_index,
				  struct nfp_eth_table_port *copy)
{
	struct nfp_port *port;

	port = nfp_port_from_id(pf, NFP_PORT_PHYS_PORT, port_index);

	return nfp_devlink_fill_eth_port(port, copy);
}

static int
nfp_devlink_set_lanes(struct nfp_pf *pf, unsigned int idx, unsigned int lanes)
{
	struct nfp_nsp *nsp;
	int ret;

	nsp = nfp_eth_config_start(pf->cpp, idx);
	if (IS_ERR(nsp))
		return PTR_ERR(nsp);

	ret = __nfp_eth_set_split(nsp, lanes);
	if (ret) {
		nfp_eth_config_cleanup_end(nsp);
		return ret;
	}

	ret = nfp_eth_config_commit_end(nsp);
	if (ret < 0)
		return ret;
	if (ret) /* no change */
		return 0;

	return nfp_net_refresh_port_table_sync(pf);
}

static int
nfp_devlink_port_split(struct devlink *devlink, unsigned int port_index,
		       unsigned int count, struct netlink_ext_ack *extack)
{
	struct nfp_pf *pf = devlink_priv(devlink);
	struct nfp_eth_table_port eth_port;
	unsigned int lanes;
	int ret;

	if (count < 2)
		return -EINVAL;

	mutex_lock(&pf->lock);

	rtnl_lock();
	ret = nfp_devlink_fill_eth_port_from_id(pf, port_index, &eth_port);
	rtnl_unlock();
	if (ret)
		goto out;

	if (eth_port.is_split || eth_port.port_lanes % count) {
		ret = -EINVAL;
		goto out;
	}

	/* Special case the 100G CXP -> 2x40G split */
	lanes = eth_port.port_lanes / count;
	if (eth_port.lanes == 10 && count == 2)
		lanes = 8 / count;

	ret = nfp_devlink_set_lanes(pf, eth_port.index, lanes);
out:
	mutex_unlock(&pf->lock);

	return ret;
}

static int
nfp_devlink_port_unsplit(struct devlink *devlink, unsigned int port_index,
			 struct netlink_ext_ack *extack)
{
	struct nfp_pf *pf = devlink_priv(devlink);
	struct nfp_eth_table_port eth_port;
	unsigned int lanes;
	int ret;

	mutex_lock(&pf->lock);

	rtnl_lock();
	ret = nfp_devlink_fill_eth_port_from_id(pf, port_index, &eth_port);
	rtnl_unlock();
	if (ret)
		goto out;

	if (!eth_port.is_split) {
		ret = -EINVAL;
		goto out;
	}

	/* Special case the 100G CXP -> 2x40G unsplit */
	lanes = eth_port.port_lanes;
	if (eth_port.port_lanes == 8)
		lanes = 10;

	ret = nfp_devlink_set_lanes(pf, eth_port.index, lanes);
out:
	mutex_unlock(&pf->lock);

	return ret;
}

static int
nfp_devlink_sb_pool_get(struct devlink *devlink, unsigned int sb_index,
			u16 pool_index, struct devlink_sb_pool_info *pool_info)
{
	struct nfp_pf *pf = devlink_priv(devlink);

	return nfp_shared_buf_pool_get(pf, sb_index, pool_index, pool_info);
}

static int
nfp_devlink_sb_pool_set(struct devlink *devlink, unsigned int sb_index,
			u16 pool_index,
			u32 size, enum devlink_sb_threshold_type threshold_type)
{
	struct nfp_pf *pf = devlink_priv(devlink);

	return nfp_shared_buf_pool_set(pf, sb_index, pool_index,
				       size, threshold_type);
}

static int nfp_devlink_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	struct nfp_pf *pf = devlink_priv(devlink);

	return nfp_app_eswitch_mode_get(pf->app, mode);
}

static int nfp_devlink_eswitch_mode_set(struct devlink *devlink, u16 mode,
					struct netlink_ext_ack *extack)
{
	struct nfp_pf *pf = devlink_priv(devlink);
	int ret;

	mutex_lock(&pf->lock);
	ret = nfp_app_eswitch_mode_set(pf->app, mode);
	mutex_unlock(&pf->lock);

	return ret;
}

const struct devlink_ops nfp_devlink_ops = {
	.port_split		= nfp_devlink_port_split,
	.port_unsplit		= nfp_devlink_port_unsplit,
	.sb_pool_get		= nfp_devlink_sb_pool_get,
	.sb_pool_set		= nfp_devlink_sb_pool_set,
	.eswitch_mode_get	= nfp_devlink_eswitch_mode_get,
	.eswitch_mode_set	= nfp_devlink_eswitch_mode_set,
};

int nfp_devlink_port_register(struct nfp_app *app, struct nfp_port *port)
{
	struct nfp_eth_table_port eth_port;
	struct devlink *devlink;
	int ret;

	rtnl_lock();
	ret = nfp_devlink_fill_eth_port(port, &eth_port);
	rtnl_unlock();
	if (ret)
		return ret;

	devlink_port_type_eth_set(&port->dl_port, port->netdev);
	devlink_port_attrs_set(&port->dl_port, DEVLINK_PORT_FLAVOUR_PHYSICAL,
			       eth_port.label_port, eth_port.is_split,
			       eth_port.label_subport);

	devlink = priv_to_devlink(app->pf);

	return devlink_port_register(devlink, &port->dl_port, port->eth_id);
}

void nfp_devlink_port_unregister(struct nfp_port *port)
{
	devlink_port_unregister(&port->dl_port);
}
