// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020 Facebook Inc.

#include <linux/debugfs.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <net/udp_tunnel.h>

#include "netdevsim.h"

static int
nsim_udp_tunnel_set_port(struct net_device *dev, unsigned int table,
			 unsigned int entry, struct udp_tunnel_info *ti)
{
	struct netdevsim *ns = netdev_priv(dev);
	int ret;

	ret = -ns->udp_ports.inject_error;
	ns->udp_ports.inject_error = 0;

	if (ns->udp_ports.sleep)
		msleep(ns->udp_ports.sleep);

	if (!ret) {
		if (ns->udp_ports.ports[table][entry]) {
			WARN(1, "entry already in use\n");
			ret = -EBUSY;
		} else {
			ns->udp_ports.ports[table][entry] =
				be16_to_cpu(ti->port) << 16 | ti->type;
		}
	}

	netdev_info(dev, "set [%d, %d] type %d family %d port %d - %d\n",
		    table, entry, ti->type, ti->sa_family, ntohs(ti->port),
		    ret);
	return ret;
}

static int
nsim_udp_tunnel_unset_port(struct net_device *dev, unsigned int table,
			   unsigned int entry, struct udp_tunnel_info *ti)
{
	struct netdevsim *ns = netdev_priv(dev);
	int ret;

	ret = -ns->udp_ports.inject_error;
	ns->udp_ports.inject_error = 0;

	if (ns->udp_ports.sleep)
		msleep(ns->udp_ports.sleep);
	if (!ret) {
		u32 val = be16_to_cpu(ti->port) << 16 | ti->type;

		if (val == ns->udp_ports.ports[table][entry]) {
			ns->udp_ports.ports[table][entry] = 0;
		} else {
			WARN(1, "entry not installed %x vs %x\n",
			     val, ns->udp_ports.ports[table][entry]);
			ret = -ENOENT;
		}
	}

	netdev_info(dev, "unset [%d, %d] type %d family %d port %d - %d\n",
		    table, entry, ti->type, ti->sa_family, ntohs(ti->port),
		    ret);
	return ret;
}

static int
nsim_udp_tunnel_sync_table(struct net_device *dev, unsigned int table)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct udp_tunnel_info ti;
	unsigned int i;
	int ret;

	ret = -ns->udp_ports.inject_error;
	ns->udp_ports.inject_error = 0;

	for (i = 0; i < NSIM_UDP_TUNNEL_N_PORTS; i++) {
		udp_tunnel_nic_get_port(dev, table, i, &ti);
		ns->udp_ports.ports[table][i] =
			be16_to_cpu(ti.port) << 16 | ti.type;
	}

	return ret;
}

static const struct udp_tunnel_nic_info nsim_udp_tunnel_info = {
	.set_port	= nsim_udp_tunnel_set_port,
	.unset_port	= nsim_udp_tunnel_unset_port,
	.sync_table	= nsim_udp_tunnel_sync_table,

	.tables = {
		{
			.n_entries	= NSIM_UDP_TUNNEL_N_PORTS,
			.tunnel_types	= UDP_TUNNEL_TYPE_VXLAN,
		},
		{
			.n_entries	= NSIM_UDP_TUNNEL_N_PORTS,
			.tunnel_types	= UDP_TUNNEL_TYPE_GENEVE |
					  UDP_TUNNEL_TYPE_VXLAN_GPE,
		},
	},
};

static ssize_t
nsim_udp_tunnels_info_reset_write(struct file *file, const char __user *data,
				  size_t count, loff_t *ppos)
{
	struct net_device *dev = file->private_data;
	struct netdevsim *ns = netdev_priv(dev);

	memset(ns->udp_ports.ports, 0, sizeof(ns->udp_ports.__ports));
	rtnl_lock();
	udp_tunnel_nic_reset_ntf(dev);
	rtnl_unlock();

	return count;
}

static const struct file_operations nsim_udp_tunnels_info_reset_fops = {
	.open = simple_open,
	.write = nsim_udp_tunnels_info_reset_write,
	.llseek = generic_file_llseek,
};

int nsim_udp_tunnels_info_create(struct nsim_dev *nsim_dev,
				 struct net_device *dev)
{
	struct netdevsim *ns = netdev_priv(dev);
	struct udp_tunnel_nic_info *info;

	if (nsim_dev->udp_ports.shared && nsim_dev->udp_ports.open_only) {
		dev_err(&nsim_dev->nsim_bus_dev->dev,
			"shared can't be used in conjunction with open_only\n");
		return -EINVAL;
	}

	if (!nsim_dev->udp_ports.shared)
		ns->udp_ports.ports = ns->udp_ports.__ports;
	else
		ns->udp_ports.ports = nsim_dev->udp_ports.__ports;

	debugfs_create_u32("udp_ports_inject_error", 0600,
			   ns->nsim_dev_port->ddir,
			   &ns->udp_ports.inject_error);

	ns->udp_ports.dfs_ports[0].array = ns->udp_ports.ports[0];
	ns->udp_ports.dfs_ports[0].n_elements = NSIM_UDP_TUNNEL_N_PORTS;
	debugfs_create_u32_array("udp_ports_table0", 0400,
				 ns->nsim_dev_port->ddir,
				 &ns->udp_ports.dfs_ports[0]);

	ns->udp_ports.dfs_ports[1].array = ns->udp_ports.ports[1];
	ns->udp_ports.dfs_ports[1].n_elements = NSIM_UDP_TUNNEL_N_PORTS;
	debugfs_create_u32_array("udp_ports_table1", 0400,
				 ns->nsim_dev_port->ddir,
				 &ns->udp_ports.dfs_ports[1]);

	debugfs_create_file("udp_ports_reset", 0200, ns->nsim_dev_port->ddir,
			    dev, &nsim_udp_tunnels_info_reset_fops);

	/* Note: it's not normal to allocate the info struct like this!
	 * Drivers are expected to use a static const one, here we're testing.
	 */
	info = kmemdup(&nsim_udp_tunnel_info, sizeof(nsim_udp_tunnel_info),
		       GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	ns->udp_ports.sleep = nsim_dev->udp_ports.sleep;

	if (nsim_dev->udp_ports.sync_all) {
		info->set_port = NULL;
		info->unset_port = NULL;
	} else {
		info->sync_table = NULL;
	}

	if (ns->udp_ports.sleep)
		info->flags |= UDP_TUNNEL_NIC_INFO_MAY_SLEEP;
	if (nsim_dev->udp_ports.open_only)
		info->flags |= UDP_TUNNEL_NIC_INFO_OPEN_ONLY;
	if (nsim_dev->udp_ports.ipv4_only)
		info->flags |= UDP_TUNNEL_NIC_INFO_IPV4_ONLY;
	if (nsim_dev->udp_ports.shared)
		info->shared = &nsim_dev->udp_ports.utn_shared;

	dev->udp_tunnel_nic_info = info;
	return 0;
}

void nsim_udp_tunnels_info_destroy(struct net_device *dev)
{
	kfree(dev->udp_tunnel_nic_info);
	dev->udp_tunnel_nic_info = NULL;
}

void nsim_udp_tunnels_debugfs_create(struct nsim_dev *nsim_dev)
{
	debugfs_create_bool("udp_ports_sync_all", 0600, nsim_dev->ddir,
			    &nsim_dev->udp_ports.sync_all);
	debugfs_create_bool("udp_ports_open_only", 0600, nsim_dev->ddir,
			    &nsim_dev->udp_ports.open_only);
	debugfs_create_bool("udp_ports_ipv4_only", 0600, nsim_dev->ddir,
			    &nsim_dev->udp_ports.ipv4_only);
	debugfs_create_bool("udp_ports_shared", 0600, nsim_dev->ddir,
			    &nsim_dev->udp_ports.shared);
	debugfs_create_u32("udp_ports_sleep", 0600, nsim_dev->ddir,
			   &nsim_dev->udp_ports.sleep);
}
