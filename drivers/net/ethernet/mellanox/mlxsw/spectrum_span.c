// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/if_bridge.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/rtnetlink.h>
#include <linux/workqueue.h>
#include <net/arp.h>
#include <net/gre.h>
#include <net/lag.h>
#include <net/ndisc.h>
#include <net/ip6_tunnel.h>

#include "spectrum.h"
#include "spectrum_ipip.h"
#include "spectrum_span.h"
#include "spectrum_switchdev.h"

struct mlxsw_sp_span {
	struct work_struct work;
	struct mlxsw_sp *mlxsw_sp;
	const struct mlxsw_sp_span_trigger_ops **span_trigger_ops_arr;
	const struct mlxsw_sp_span_entry_ops **span_entry_ops_arr;
	size_t span_entry_ops_arr_size;
	struct list_head analyzed_ports_list;
	struct mutex analyzed_ports_lock; /* Protects analyzed_ports_list */
	struct list_head trigger_entries_list;
	u16 policer_id_base;
	refcount_t policer_id_base_ref_count;
	atomic_t active_entries_count;
	int entries_count;
	struct mlxsw_sp_span_entry entries[];
};

struct mlxsw_sp_span_analyzed_port {
	struct list_head list; /* Member of analyzed_ports_list */
	refcount_t ref_count;
	u16 local_port;
	bool ingress;
};

struct mlxsw_sp_span_trigger_entry {
	struct list_head list; /* Member of trigger_entries_list */
	struct mlxsw_sp_span *span;
	const struct mlxsw_sp_span_trigger_ops *ops;
	refcount_t ref_count;
	u16 local_port;
	enum mlxsw_sp_span_trigger trigger;
	struct mlxsw_sp_span_trigger_parms parms;
};

enum mlxsw_sp_span_trigger_type {
	MLXSW_SP_SPAN_TRIGGER_TYPE_PORT,
	MLXSW_SP_SPAN_TRIGGER_TYPE_GLOBAL,
};

struct mlxsw_sp_span_trigger_ops {
	int (*bind)(struct mlxsw_sp_span_trigger_entry *trigger_entry);
	void (*unbind)(struct mlxsw_sp_span_trigger_entry *trigger_entry);
	bool (*matches)(struct mlxsw_sp_span_trigger_entry *trigger_entry,
			enum mlxsw_sp_span_trigger trigger,
			struct mlxsw_sp_port *mlxsw_sp_port);
	int (*enable)(struct mlxsw_sp_span_trigger_entry *trigger_entry,
		      struct mlxsw_sp_port *mlxsw_sp_port, u8 tc);
	void (*disable)(struct mlxsw_sp_span_trigger_entry *trigger_entry,
			struct mlxsw_sp_port *mlxsw_sp_port, u8 tc);
};

static void mlxsw_sp_span_respin_work(struct work_struct *work);

static u64 mlxsw_sp_span_occ_get(void *priv)
{
	const struct mlxsw_sp *mlxsw_sp = priv;

	return atomic_read(&mlxsw_sp->span->active_entries_count);
}

int mlxsw_sp_span_init(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);
	struct mlxsw_sp_span *span;
	int i, entries_count, err;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_SPAN))
		return -EIO;

	entries_count = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_SPAN);
	span = kzalloc(struct_size(span, entries, entries_count), GFP_KERNEL);
	if (!span)
		return -ENOMEM;
	refcount_set(&span->policer_id_base_ref_count, 0);
	span->entries_count = entries_count;
	atomic_set(&span->active_entries_count, 0);
	mutex_init(&span->analyzed_ports_lock);
	INIT_LIST_HEAD(&span->analyzed_ports_list);
	INIT_LIST_HEAD(&span->trigger_entries_list);
	span->mlxsw_sp = mlxsw_sp;
	mlxsw_sp->span = span;

	for (i = 0; i < mlxsw_sp->span->entries_count; i++)
		mlxsw_sp->span->entries[i].id = i;

	err = mlxsw_sp->span_ops->init(mlxsw_sp);
	if (err)
		goto err_init;

	devl_resource_occ_get_register(devlink, MLXSW_SP_RESOURCE_SPAN,
				       mlxsw_sp_span_occ_get, mlxsw_sp);
	INIT_WORK(&span->work, mlxsw_sp_span_respin_work);

	return 0;

err_init:
	mutex_destroy(&mlxsw_sp->span->analyzed_ports_lock);
	kfree(mlxsw_sp->span);
	return err;
}

void mlxsw_sp_span_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct devlink *devlink = priv_to_devlink(mlxsw_sp->core);

	cancel_work_sync(&mlxsw_sp->span->work);
	devl_resource_occ_get_unregister(devlink, MLXSW_SP_RESOURCE_SPAN);

	WARN_ON_ONCE(!list_empty(&mlxsw_sp->span->trigger_entries_list));
	WARN_ON_ONCE(!list_empty(&mlxsw_sp->span->analyzed_ports_list));
	mutex_destroy(&mlxsw_sp->span->analyzed_ports_lock);
	kfree(mlxsw_sp->span);
}

static bool mlxsw_sp1_span_cpu_can_handle(const struct net_device *dev)
{
	return !dev;
}

static int mlxsw_sp1_span_entry_cpu_parms(struct mlxsw_sp *mlxsw_sp,
					  const struct net_device *to_dev,
					  struct mlxsw_sp_span_parms *sparmsp)
{
	return -EOPNOTSUPP;
}

static int
mlxsw_sp1_span_entry_cpu_configure(struct mlxsw_sp_span_entry *span_entry,
				   struct mlxsw_sp_span_parms sparms)
{
	return -EOPNOTSUPP;
}

static void
mlxsw_sp1_span_entry_cpu_deconfigure(struct mlxsw_sp_span_entry *span_entry)
{
}

static const
struct mlxsw_sp_span_entry_ops mlxsw_sp1_span_entry_ops_cpu = {
	.is_static = true,
	.can_handle = mlxsw_sp1_span_cpu_can_handle,
	.parms_set = mlxsw_sp1_span_entry_cpu_parms,
	.configure = mlxsw_sp1_span_entry_cpu_configure,
	.deconfigure = mlxsw_sp1_span_entry_cpu_deconfigure,
};

static int
mlxsw_sp_span_entry_phys_parms(struct mlxsw_sp *mlxsw_sp,
			       const struct net_device *to_dev,
			       struct mlxsw_sp_span_parms *sparmsp)
{
	sparmsp->dest_port = netdev_priv(to_dev);
	return 0;
}

static int
mlxsw_sp_span_entry_phys_configure(struct mlxsw_sp_span_entry *span_entry,
				   struct mlxsw_sp_span_parms sparms)
{
	struct mlxsw_sp_port *dest_port = sparms.dest_port;
	struct mlxsw_sp *mlxsw_sp = dest_port->mlxsw_sp;
	u16 local_port = dest_port->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	/* Create a new port analayzer entry for local_port. */
	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, true,
			    MLXSW_REG_MPAT_SPAN_TYPE_LOCAL_ETH);
	mlxsw_reg_mpat_session_id_set(mpat_pl, sparms.session_id);
	mlxsw_reg_mpat_pide_set(mpat_pl, sparms.policer_enable);
	mlxsw_reg_mpat_pid_set(mpat_pl, sparms.policer_id);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpat), mpat_pl);
}

static void
mlxsw_sp_span_entry_deconfigure_common(struct mlxsw_sp_span_entry *span_entry,
				       enum mlxsw_reg_mpat_span_type span_type)
{
	struct mlxsw_sp_port *dest_port = span_entry->parms.dest_port;
	struct mlxsw_sp *mlxsw_sp = dest_port->mlxsw_sp;
	u16 local_port = dest_port->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, false, span_type);
	mlxsw_reg_mpat_session_id_set(mpat_pl, span_entry->parms.session_id);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpat), mpat_pl);
}

static void
mlxsw_sp_span_entry_phys_deconfigure(struct mlxsw_sp_span_entry *span_entry)
{
	mlxsw_sp_span_entry_deconfigure_common(span_entry,
					    MLXSW_REG_MPAT_SPAN_TYPE_LOCAL_ETH);
}

static const
struct mlxsw_sp_span_entry_ops mlxsw_sp_span_entry_ops_phys = {
	.is_static = true,
	.can_handle = mlxsw_sp_port_dev_check,
	.parms_set = mlxsw_sp_span_entry_phys_parms,
	.configure = mlxsw_sp_span_entry_phys_configure,
	.deconfigure = mlxsw_sp_span_entry_phys_deconfigure,
};

static int mlxsw_sp_span_dmac(struct neigh_table *tbl,
			      const void *pkey,
			      struct net_device *dev,
			      unsigned char dmac[ETH_ALEN])
{
	struct neighbour *neigh = neigh_lookup(tbl, pkey, dev);
	int err = 0;

	if (!neigh) {
		neigh = neigh_create(tbl, pkey, dev);
		if (IS_ERR(neigh))
			return PTR_ERR(neigh);
	}

	neigh_event_send(neigh, NULL);

	read_lock_bh(&neigh->lock);
	if ((neigh->nud_state & NUD_VALID) && !neigh->dead)
		memcpy(dmac, neigh->ha, ETH_ALEN);
	else
		err = -ENOENT;
	read_unlock_bh(&neigh->lock);

	neigh_release(neigh);
	return err;
}

static int
mlxsw_sp_span_entry_unoffloadable(struct mlxsw_sp_span_parms *sparmsp)
{
	sparmsp->dest_port = NULL;
	return 0;
}

static struct net_device *
mlxsw_sp_span_entry_bridge_8021q(const struct net_device *br_dev,
				 unsigned char *dmac,
				 u16 *p_vid)
{
	struct bridge_vlan_info vinfo;
	struct net_device *edev;
	u16 vid = *p_vid;

	if (!vid && WARN_ON(br_vlan_get_pvid(br_dev, &vid)))
		return NULL;
	if (!vid || br_vlan_get_info(br_dev, vid, &vinfo) ||
	    !(vinfo.flags & BRIDGE_VLAN_INFO_BRENTRY))
		return NULL;

	edev = br_fdb_find_port(br_dev, dmac, vid);
	if (!edev)
		return NULL;

	if (br_vlan_get_info(edev, vid, &vinfo))
		return NULL;
	if (vinfo.flags & BRIDGE_VLAN_INFO_UNTAGGED)
		*p_vid = 0;
	else
		*p_vid = vid;
	return edev;
}

static struct net_device *
mlxsw_sp_span_entry_bridge_8021d(const struct net_device *br_dev,
				 unsigned char *dmac)
{
	return br_fdb_find_port(br_dev, dmac, 0);
}

static struct net_device *
mlxsw_sp_span_entry_bridge(const struct net_device *br_dev,
			   unsigned char dmac[ETH_ALEN],
			   u16 *p_vid)
{
	struct mlxsw_sp_bridge_port *bridge_port;
	enum mlxsw_reg_spms_state spms_state;
	struct net_device *dev = NULL;
	struct mlxsw_sp_port *port;
	u8 stp_state;

	if (br_vlan_enabled(br_dev))
		dev = mlxsw_sp_span_entry_bridge_8021q(br_dev, dmac, p_vid);
	else if (!*p_vid)
		dev = mlxsw_sp_span_entry_bridge_8021d(br_dev, dmac);
	if (!dev)
		return NULL;

	port = mlxsw_sp_port_dev_lower_find(dev);
	if (!port)
		return NULL;

	bridge_port = mlxsw_sp_bridge_port_find(port->mlxsw_sp->bridge, dev);
	if (!bridge_port)
		return NULL;

	stp_state = mlxsw_sp_bridge_port_stp_state(bridge_port);
	spms_state = mlxsw_sp_stp_spms_state(stp_state);
	if (spms_state != MLXSW_REG_SPMS_STATE_FORWARDING)
		return NULL;

	return dev;
}

static struct net_device *
mlxsw_sp_span_entry_vlan(const struct net_device *vlan_dev,
			 u16 *p_vid)
{
	*p_vid = vlan_dev_vlan_id(vlan_dev);
	return vlan_dev_real_dev(vlan_dev);
}

static struct net_device *
mlxsw_sp_span_entry_lag(struct net_device *lag_dev)
{
	struct net_device *dev;
	struct list_head *iter;

	netdev_for_each_lower_dev(lag_dev, dev, iter)
		if (netif_carrier_ok(dev) &&
		    net_lag_port_dev_txable(dev) &&
		    mlxsw_sp_port_dev_check(dev))
			return dev;

	return NULL;
}

static __maybe_unused int
mlxsw_sp_span_entry_tunnel_parms_common(struct net_device *edev,
					union mlxsw_sp_l3addr saddr,
					union mlxsw_sp_l3addr daddr,
					union mlxsw_sp_l3addr gw,
					__u8 ttl,
					struct neigh_table *tbl,
					struct mlxsw_sp_span_parms *sparmsp)
{
	unsigned char dmac[ETH_ALEN];
	u16 vid = 0;

	if (mlxsw_sp_l3addr_is_zero(gw))
		gw = daddr;

	if (!edev || mlxsw_sp_span_dmac(tbl, &gw, edev, dmac))
		goto unoffloadable;

	if (is_vlan_dev(edev))
		edev = mlxsw_sp_span_entry_vlan(edev, &vid);

	if (netif_is_bridge_master(edev)) {
		edev = mlxsw_sp_span_entry_bridge(edev, dmac, &vid);
		if (!edev)
			goto unoffloadable;
	}

	if (is_vlan_dev(edev)) {
		if (vid || !(edev->flags & IFF_UP))
			goto unoffloadable;
		edev = mlxsw_sp_span_entry_vlan(edev, &vid);
	}

	if (netif_is_lag_master(edev)) {
		if (!(edev->flags & IFF_UP))
			goto unoffloadable;
		edev = mlxsw_sp_span_entry_lag(edev);
		if (!edev)
			goto unoffloadable;
	}

	if (!mlxsw_sp_port_dev_check(edev))
		goto unoffloadable;

	sparmsp->dest_port = netdev_priv(edev);
	sparmsp->ttl = ttl;
	memcpy(sparmsp->dmac, dmac, ETH_ALEN);
	memcpy(sparmsp->smac, edev->dev_addr, ETH_ALEN);
	sparmsp->saddr = saddr;
	sparmsp->daddr = daddr;
	sparmsp->vid = vid;
	return 0;

unoffloadable:
	return mlxsw_sp_span_entry_unoffloadable(sparmsp);
}

#if IS_ENABLED(CONFIG_NET_IPGRE)
static struct net_device *
mlxsw_sp_span_gretap4_route(const struct net_device *to_dev,
			    __be32 *saddrp, __be32 *daddrp)
{
	struct ip_tunnel *tun = netdev_priv(to_dev);
	struct net_device *dev = NULL;
	struct ip_tunnel_parm parms;
	struct rtable *rt = NULL;
	struct flowi4 fl4;

	/* We assume "dev" stays valid after rt is put. */
	ASSERT_RTNL();

	parms = mlxsw_sp_ipip_netdev_parms4(to_dev);
	ip_tunnel_init_flow(&fl4, parms.iph.protocol, *daddrp, *saddrp,
			    0, 0, dev_net(to_dev), parms.link, tun->fwmark, 0);

	rt = ip_route_output_key(tun->net, &fl4);
	if (IS_ERR(rt))
		return NULL;

	if (rt->rt_type != RTN_UNICAST)
		goto out;

	dev = rt->dst.dev;
	*saddrp = fl4.saddr;
	if (rt->rt_gw_family == AF_INET)
		*daddrp = rt->rt_gw4;
	/* can not offload if route has an IPv6 gateway */
	else if (rt->rt_gw_family == AF_INET6)
		dev = NULL;

out:
	ip_rt_put(rt);
	return dev;
}

static int
mlxsw_sp_span_entry_gretap4_parms(struct mlxsw_sp *mlxsw_sp,
				  const struct net_device *to_dev,
				  struct mlxsw_sp_span_parms *sparmsp)
{
	struct ip_tunnel_parm tparm = mlxsw_sp_ipip_netdev_parms4(to_dev);
	union mlxsw_sp_l3addr saddr = { .addr4 = tparm.iph.saddr };
	union mlxsw_sp_l3addr daddr = { .addr4 = tparm.iph.daddr };
	bool inherit_tos = tparm.iph.tos & 0x1;
	bool inherit_ttl = !tparm.iph.ttl;
	union mlxsw_sp_l3addr gw = daddr;
	struct net_device *l3edev;

	if (!(to_dev->flags & IFF_UP) ||
	    /* Reject tunnels with GRE keys, checksums, etc. */
	    tparm.i_flags || tparm.o_flags ||
	    /* Require a fixed TTL and a TOS copied from the mirrored packet. */
	    inherit_ttl || !inherit_tos ||
	    /* A destination address may not be "any". */
	    mlxsw_sp_l3addr_is_zero(daddr))
		return mlxsw_sp_span_entry_unoffloadable(sparmsp);

	l3edev = mlxsw_sp_span_gretap4_route(to_dev, &saddr.addr4, &gw.addr4);
	return mlxsw_sp_span_entry_tunnel_parms_common(l3edev, saddr, daddr, gw,
						       tparm.iph.ttl,
						       &arp_tbl, sparmsp);
}

static int
mlxsw_sp_span_entry_gretap4_configure(struct mlxsw_sp_span_entry *span_entry,
				      struct mlxsw_sp_span_parms sparms)
{
	struct mlxsw_sp_port *dest_port = sparms.dest_port;
	struct mlxsw_sp *mlxsw_sp = dest_port->mlxsw_sp;
	u16 local_port = dest_port->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	/* Create a new port analayzer entry for local_port. */
	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, true,
			    MLXSW_REG_MPAT_SPAN_TYPE_REMOTE_ETH_L3);
	mlxsw_reg_mpat_pide_set(mpat_pl, sparms.policer_enable);
	mlxsw_reg_mpat_pid_set(mpat_pl, sparms.policer_id);
	mlxsw_reg_mpat_eth_rspan_pack(mpat_pl, sparms.vid);
	mlxsw_reg_mpat_eth_rspan_l2_pack(mpat_pl,
				    MLXSW_REG_MPAT_ETH_RSPAN_VERSION_NO_HEADER,
				    sparms.dmac, !!sparms.vid);
	mlxsw_reg_mpat_eth_rspan_l3_ipv4_pack(mpat_pl,
					      sparms.ttl, sparms.smac,
					      be32_to_cpu(sparms.saddr.addr4),
					      be32_to_cpu(sparms.daddr.addr4));

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpat), mpat_pl);
}

static void
mlxsw_sp_span_entry_gretap4_deconfigure(struct mlxsw_sp_span_entry *span_entry)
{
	mlxsw_sp_span_entry_deconfigure_common(span_entry,
					MLXSW_REG_MPAT_SPAN_TYPE_REMOTE_ETH_L3);
}

static const struct mlxsw_sp_span_entry_ops mlxsw_sp_span_entry_ops_gretap4 = {
	.can_handle = netif_is_gretap,
	.parms_set = mlxsw_sp_span_entry_gretap4_parms,
	.configure = mlxsw_sp_span_entry_gretap4_configure,
	.deconfigure = mlxsw_sp_span_entry_gretap4_deconfigure,
};
#endif

#if IS_ENABLED(CONFIG_IPV6_GRE)
static struct net_device *
mlxsw_sp_span_gretap6_route(const struct net_device *to_dev,
			    struct in6_addr *saddrp,
			    struct in6_addr *daddrp)
{
	struct ip6_tnl *t = netdev_priv(to_dev);
	struct flowi6 fl6 = t->fl.u.ip6;
	struct net_device *dev = NULL;
	struct dst_entry *dst;
	struct rt6_info *rt6;

	/* We assume "dev" stays valid after dst is released. */
	ASSERT_RTNL();

	fl6.flowi6_mark = t->parms.fwmark;
	if (!ip6_tnl_xmit_ctl(t, &fl6.saddr, &fl6.daddr))
		return NULL;

	dst = ip6_route_output(t->net, NULL, &fl6);
	if (!dst || dst->error)
		goto out;

	rt6 = container_of(dst, struct rt6_info, dst);

	dev = dst->dev;
	*saddrp = fl6.saddr;
	*daddrp = rt6->rt6i_gateway;

out:
	dst_release(dst);
	return dev;
}

static int
mlxsw_sp_span_entry_gretap6_parms(struct mlxsw_sp *mlxsw_sp,
				  const struct net_device *to_dev,
				  struct mlxsw_sp_span_parms *sparmsp)
{
	struct __ip6_tnl_parm tparm = mlxsw_sp_ipip_netdev_parms6(to_dev);
	bool inherit_tos = tparm.flags & IP6_TNL_F_USE_ORIG_TCLASS;
	union mlxsw_sp_l3addr saddr = { .addr6 = tparm.laddr };
	union mlxsw_sp_l3addr daddr = { .addr6 = tparm.raddr };
	bool inherit_ttl = !tparm.hop_limit;
	union mlxsw_sp_l3addr gw = daddr;
	struct net_device *l3edev;

	if (!(to_dev->flags & IFF_UP) ||
	    /* Reject tunnels with GRE keys, checksums, etc. */
	    tparm.i_flags || tparm.o_flags ||
	    /* Require a fixed TTL and a TOS copied from the mirrored packet. */
	    inherit_ttl || !inherit_tos ||
	    /* A destination address may not be "any". */
	    mlxsw_sp_l3addr_is_zero(daddr))
		return mlxsw_sp_span_entry_unoffloadable(sparmsp);

	l3edev = mlxsw_sp_span_gretap6_route(to_dev, &saddr.addr6, &gw.addr6);
	return mlxsw_sp_span_entry_tunnel_parms_common(l3edev, saddr, daddr, gw,
						       tparm.hop_limit,
						       &nd_tbl, sparmsp);
}

static int
mlxsw_sp_span_entry_gretap6_configure(struct mlxsw_sp_span_entry *span_entry,
				      struct mlxsw_sp_span_parms sparms)
{
	struct mlxsw_sp_port *dest_port = sparms.dest_port;
	struct mlxsw_sp *mlxsw_sp = dest_port->mlxsw_sp;
	u16 local_port = dest_port->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	/* Create a new port analayzer entry for local_port. */
	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, true,
			    MLXSW_REG_MPAT_SPAN_TYPE_REMOTE_ETH_L3);
	mlxsw_reg_mpat_pide_set(mpat_pl, sparms.policer_enable);
	mlxsw_reg_mpat_pid_set(mpat_pl, sparms.policer_id);
	mlxsw_reg_mpat_eth_rspan_pack(mpat_pl, sparms.vid);
	mlxsw_reg_mpat_eth_rspan_l2_pack(mpat_pl,
				    MLXSW_REG_MPAT_ETH_RSPAN_VERSION_NO_HEADER,
				    sparms.dmac, !!sparms.vid);
	mlxsw_reg_mpat_eth_rspan_l3_ipv6_pack(mpat_pl, sparms.ttl, sparms.smac,
					      sparms.saddr.addr6,
					      sparms.daddr.addr6);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpat), mpat_pl);
}

static void
mlxsw_sp_span_entry_gretap6_deconfigure(struct mlxsw_sp_span_entry *span_entry)
{
	mlxsw_sp_span_entry_deconfigure_common(span_entry,
					MLXSW_REG_MPAT_SPAN_TYPE_REMOTE_ETH_L3);
}

static const
struct mlxsw_sp_span_entry_ops mlxsw_sp_span_entry_ops_gretap6 = {
	.can_handle = netif_is_ip6gretap,
	.parms_set = mlxsw_sp_span_entry_gretap6_parms,
	.configure = mlxsw_sp_span_entry_gretap6_configure,
	.deconfigure = mlxsw_sp_span_entry_gretap6_deconfigure,
};
#endif

static bool
mlxsw_sp_span_vlan_can_handle(const struct net_device *dev)
{
	return is_vlan_dev(dev) &&
	       mlxsw_sp_port_dev_check(vlan_dev_real_dev(dev));
}

static int
mlxsw_sp_span_entry_vlan_parms(struct mlxsw_sp *mlxsw_sp,
			       const struct net_device *to_dev,
			       struct mlxsw_sp_span_parms *sparmsp)
{
	struct net_device *real_dev;
	u16 vid;

	if (!(to_dev->flags & IFF_UP))
		return mlxsw_sp_span_entry_unoffloadable(sparmsp);

	real_dev = mlxsw_sp_span_entry_vlan(to_dev, &vid);
	sparmsp->dest_port = netdev_priv(real_dev);
	sparmsp->vid = vid;
	return 0;
}

static int
mlxsw_sp_span_entry_vlan_configure(struct mlxsw_sp_span_entry *span_entry,
				   struct mlxsw_sp_span_parms sparms)
{
	struct mlxsw_sp_port *dest_port = sparms.dest_port;
	struct mlxsw_sp *mlxsw_sp = dest_port->mlxsw_sp;
	u16 local_port = dest_port->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, true,
			    MLXSW_REG_MPAT_SPAN_TYPE_REMOTE_ETH);
	mlxsw_reg_mpat_pide_set(mpat_pl, sparms.policer_enable);
	mlxsw_reg_mpat_pid_set(mpat_pl, sparms.policer_id);
	mlxsw_reg_mpat_eth_rspan_pack(mpat_pl, sparms.vid);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpat), mpat_pl);
}

static void
mlxsw_sp_span_entry_vlan_deconfigure(struct mlxsw_sp_span_entry *span_entry)
{
	mlxsw_sp_span_entry_deconfigure_common(span_entry,
					MLXSW_REG_MPAT_SPAN_TYPE_REMOTE_ETH);
}

static const
struct mlxsw_sp_span_entry_ops mlxsw_sp_span_entry_ops_vlan = {
	.can_handle = mlxsw_sp_span_vlan_can_handle,
	.parms_set = mlxsw_sp_span_entry_vlan_parms,
	.configure = mlxsw_sp_span_entry_vlan_configure,
	.deconfigure = mlxsw_sp_span_entry_vlan_deconfigure,
};

static const
struct mlxsw_sp_span_entry_ops *mlxsw_sp1_span_entry_ops_arr[] = {
	&mlxsw_sp1_span_entry_ops_cpu,
	&mlxsw_sp_span_entry_ops_phys,
#if IS_ENABLED(CONFIG_NET_IPGRE)
	&mlxsw_sp_span_entry_ops_gretap4,
#endif
#if IS_ENABLED(CONFIG_IPV6_GRE)
	&mlxsw_sp_span_entry_ops_gretap6,
#endif
	&mlxsw_sp_span_entry_ops_vlan,
};

static bool mlxsw_sp2_span_cpu_can_handle(const struct net_device *dev)
{
	return !dev;
}

static int mlxsw_sp2_span_entry_cpu_parms(struct mlxsw_sp *mlxsw_sp,
					  const struct net_device *to_dev,
					  struct mlxsw_sp_span_parms *sparmsp)
{
	sparmsp->dest_port = mlxsw_sp->ports[MLXSW_PORT_CPU_PORT];
	return 0;
}

static int
mlxsw_sp2_span_entry_cpu_configure(struct mlxsw_sp_span_entry *span_entry,
				   struct mlxsw_sp_span_parms sparms)
{
	/* Mirroring to the CPU port is like mirroring to any other physical
	 * port. Its local port is used instead of that of the physical port.
	 */
	return mlxsw_sp_span_entry_phys_configure(span_entry, sparms);
}

static void
mlxsw_sp2_span_entry_cpu_deconfigure(struct mlxsw_sp_span_entry *span_entry)
{
	enum mlxsw_reg_mpat_span_type span_type;

	span_type = MLXSW_REG_MPAT_SPAN_TYPE_LOCAL_ETH;
	mlxsw_sp_span_entry_deconfigure_common(span_entry, span_type);
}

static const
struct mlxsw_sp_span_entry_ops mlxsw_sp2_span_entry_ops_cpu = {
	.is_static = true,
	.can_handle = mlxsw_sp2_span_cpu_can_handle,
	.parms_set = mlxsw_sp2_span_entry_cpu_parms,
	.configure = mlxsw_sp2_span_entry_cpu_configure,
	.deconfigure = mlxsw_sp2_span_entry_cpu_deconfigure,
};

static const
struct mlxsw_sp_span_entry_ops *mlxsw_sp2_span_entry_ops_arr[] = {
	&mlxsw_sp2_span_entry_ops_cpu,
	&mlxsw_sp_span_entry_ops_phys,
#if IS_ENABLED(CONFIG_NET_IPGRE)
	&mlxsw_sp_span_entry_ops_gretap4,
#endif
#if IS_ENABLED(CONFIG_IPV6_GRE)
	&mlxsw_sp_span_entry_ops_gretap6,
#endif
	&mlxsw_sp_span_entry_ops_vlan,
};

static int
mlxsw_sp_span_entry_nop_parms(struct mlxsw_sp *mlxsw_sp,
			      const struct net_device *to_dev,
			      struct mlxsw_sp_span_parms *sparmsp)
{
	return mlxsw_sp_span_entry_unoffloadable(sparmsp);
}

static int
mlxsw_sp_span_entry_nop_configure(struct mlxsw_sp_span_entry *span_entry,
				  struct mlxsw_sp_span_parms sparms)
{
	return 0;
}

static void
mlxsw_sp_span_entry_nop_deconfigure(struct mlxsw_sp_span_entry *span_entry)
{
}

static const struct mlxsw_sp_span_entry_ops mlxsw_sp_span_entry_ops_nop = {
	.parms_set = mlxsw_sp_span_entry_nop_parms,
	.configure = mlxsw_sp_span_entry_nop_configure,
	.deconfigure = mlxsw_sp_span_entry_nop_deconfigure,
};

static void
mlxsw_sp_span_entry_configure(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_span_entry *span_entry,
			      struct mlxsw_sp_span_parms sparms)
{
	int err;

	if (!sparms.dest_port)
		goto set_parms;

	if (sparms.dest_port->mlxsw_sp != mlxsw_sp) {
		dev_err(mlxsw_sp->bus_info->dev,
			"Cannot mirror to a port which belongs to a different mlxsw instance\n");
		sparms.dest_port = NULL;
		goto set_parms;
	}

	err = span_entry->ops->configure(span_entry, sparms);
	if (err) {
		dev_err(mlxsw_sp->bus_info->dev, "Failed to offload mirror\n");
		sparms.dest_port = NULL;
		goto set_parms;
	}

set_parms:
	span_entry->parms = sparms;
}

static void
mlxsw_sp_span_entry_deconfigure(struct mlxsw_sp_span_entry *span_entry)
{
	if (span_entry->parms.dest_port)
		span_entry->ops->deconfigure(span_entry);
}

static int mlxsw_sp_span_policer_id_base_set(struct mlxsw_sp_span *span,
					     u16 policer_id)
{
	struct mlxsw_sp *mlxsw_sp = span->mlxsw_sp;
	u16 policer_id_base;
	int err;

	/* Policers set on SPAN agents must be in the range of
	 * `policer_id_base .. policer_id_base + max_span_agents - 1`. If the
	 * base is set and the new policer is not within the range, then we
	 * must error out.
	 */
	if (refcount_read(&span->policer_id_base_ref_count)) {
		if (policer_id < span->policer_id_base ||
		    policer_id >= span->policer_id_base + span->entries_count)
			return -EINVAL;

		refcount_inc(&span->policer_id_base_ref_count);
		return 0;
	}

	/* Base must be even. */
	policer_id_base = policer_id % 2 == 0 ? policer_id : policer_id - 1;
	err = mlxsw_sp->span_ops->policer_id_base_set(mlxsw_sp,
						      policer_id_base);
	if (err)
		return err;

	span->policer_id_base = policer_id_base;
	refcount_set(&span->policer_id_base_ref_count, 1);

	return 0;
}

static void mlxsw_sp_span_policer_id_base_unset(struct mlxsw_sp_span *span)
{
	if (refcount_dec_and_test(&span->policer_id_base_ref_count))
		span->policer_id_base = 0;
}

static struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_create(struct mlxsw_sp *mlxsw_sp,
			   const struct net_device *to_dev,
			   const struct mlxsw_sp_span_entry_ops *ops,
			   struct mlxsw_sp_span_parms sparms)
{
	struct mlxsw_sp_span_entry *span_entry = NULL;
	int i;

	/* find a free entry to use */
	for (i = 0; i < mlxsw_sp->span->entries_count; i++) {
		if (!refcount_read(&mlxsw_sp->span->entries[i].ref_count)) {
			span_entry = &mlxsw_sp->span->entries[i];
			break;
		}
	}
	if (!span_entry)
		return NULL;

	if (sparms.policer_enable) {
		int err;

		err = mlxsw_sp_span_policer_id_base_set(mlxsw_sp->span,
							sparms.policer_id);
		if (err)
			return NULL;
	}

	atomic_inc(&mlxsw_sp->span->active_entries_count);
	span_entry->ops = ops;
	refcount_set(&span_entry->ref_count, 1);
	span_entry->to_dev = to_dev;
	mlxsw_sp_span_entry_configure(mlxsw_sp, span_entry, sparms);

	return span_entry;
}

static void mlxsw_sp_span_entry_destroy(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_span_entry *span_entry)
{
	mlxsw_sp_span_entry_deconfigure(span_entry);
	atomic_dec(&mlxsw_sp->span->active_entries_count);
	if (span_entry->parms.policer_enable)
		mlxsw_sp_span_policer_id_base_unset(mlxsw_sp->span);
}

struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_find_by_port(struct mlxsw_sp *mlxsw_sp,
				 const struct net_device *to_dev)
{
	int i;

	for (i = 0; i < mlxsw_sp->span->entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span->entries[i];

		if (refcount_read(&curr->ref_count) && curr->to_dev == to_dev)
			return curr;
	}
	return NULL;
}

void mlxsw_sp_span_entry_invalidate(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_span_entry *span_entry)
{
	mlxsw_sp_span_entry_deconfigure(span_entry);
	span_entry->ops = &mlxsw_sp_span_entry_ops_nop;
}

static struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_find_by_id(struct mlxsw_sp *mlxsw_sp, int span_id)
{
	int i;

	for (i = 0; i < mlxsw_sp->span->entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span->entries[i];

		if (refcount_read(&curr->ref_count) && curr->id == span_id)
			return curr;
	}
	return NULL;
}

static struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_find_by_parms(struct mlxsw_sp *mlxsw_sp,
				  const struct net_device *to_dev,
				  const struct mlxsw_sp_span_parms *sparms)
{
	int i;

	for (i = 0; i < mlxsw_sp->span->entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span->entries[i];

		if (refcount_read(&curr->ref_count) && curr->to_dev == to_dev &&
		    curr->parms.policer_enable == sparms->policer_enable &&
		    curr->parms.policer_id == sparms->policer_id &&
		    curr->parms.session_id == sparms->session_id)
			return curr;
	}
	return NULL;
}

static struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_get(struct mlxsw_sp *mlxsw_sp,
			const struct net_device *to_dev,
			const struct mlxsw_sp_span_entry_ops *ops,
			struct mlxsw_sp_span_parms sparms)
{
	struct mlxsw_sp_span_entry *span_entry;

	span_entry = mlxsw_sp_span_entry_find_by_parms(mlxsw_sp, to_dev,
						       &sparms);
	if (span_entry) {
		/* Already exists, just take a reference */
		refcount_inc(&span_entry->ref_count);
		return span_entry;
	}

	return mlxsw_sp_span_entry_create(mlxsw_sp, to_dev, ops, sparms);
}

static int mlxsw_sp_span_entry_put(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_span_entry *span_entry)
{
	if (refcount_dec_and_test(&span_entry->ref_count))
		mlxsw_sp_span_entry_destroy(mlxsw_sp, span_entry);
	return 0;
}

static int mlxsw_sp_span_port_buffer_update(struct mlxsw_sp_port *mlxsw_sp_port, bool enable)
{
	struct mlxsw_sp_hdroom hdroom;

	hdroom = *mlxsw_sp_port->hdroom;
	hdroom.int_buf.enable = enable;
	mlxsw_sp_hdroom_bufs_reset_sizes(mlxsw_sp_port, &hdroom);

	return mlxsw_sp_hdroom_configure(mlxsw_sp_port, &hdroom);
}

static int
mlxsw_sp_span_port_buffer_enable(struct mlxsw_sp_port *mlxsw_sp_port)
{
	return mlxsw_sp_span_port_buffer_update(mlxsw_sp_port, true);
}

static void mlxsw_sp_span_port_buffer_disable(struct mlxsw_sp_port *mlxsw_sp_port)
{
	mlxsw_sp_span_port_buffer_update(mlxsw_sp_port, false);
}

static struct mlxsw_sp_span_analyzed_port *
mlxsw_sp_span_analyzed_port_find(struct mlxsw_sp_span *span, u16 local_port,
				 bool ingress)
{
	struct mlxsw_sp_span_analyzed_port *analyzed_port;

	list_for_each_entry(analyzed_port, &span->analyzed_ports_list, list) {
		if (analyzed_port->local_port == local_port &&
		    analyzed_port->ingress == ingress)
			return analyzed_port;
	}

	return NULL;
}

static const struct mlxsw_sp_span_entry_ops *
mlxsw_sp_span_entry_ops(struct mlxsw_sp *mlxsw_sp,
			const struct net_device *to_dev)
{
	struct mlxsw_sp_span *span = mlxsw_sp->span;
	size_t i;

	for (i = 0; i < span->span_entry_ops_arr_size; ++i)
		if (span->span_entry_ops_arr[i]->can_handle(to_dev))
			return span->span_entry_ops_arr[i];

	return NULL;
}

static void mlxsw_sp_span_respin_work(struct work_struct *work)
{
	struct mlxsw_sp_span *span;
	struct mlxsw_sp *mlxsw_sp;
	int i, err;

	span = container_of(work, struct mlxsw_sp_span, work);
	mlxsw_sp = span->mlxsw_sp;

	rtnl_lock();
	for (i = 0; i < mlxsw_sp->span->entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span->entries[i];
		struct mlxsw_sp_span_parms sparms = {NULL};

		if (!refcount_read(&curr->ref_count))
			continue;

		if (curr->ops->is_static)
			continue;

		err = curr->ops->parms_set(mlxsw_sp, curr->to_dev, &sparms);
		if (err)
			continue;

		if (memcmp(&sparms, &curr->parms, sizeof(sparms))) {
			mlxsw_sp_span_entry_deconfigure(curr);
			mlxsw_sp_span_entry_configure(mlxsw_sp, curr, sparms);
		}
	}
	rtnl_unlock();
}

void mlxsw_sp_span_respin(struct mlxsw_sp *mlxsw_sp)
{
	if (atomic_read(&mlxsw_sp->span->active_entries_count) == 0)
		return;
	mlxsw_core_schedule_work(&mlxsw_sp->span->work);
}

int mlxsw_sp_span_agent_get(struct mlxsw_sp *mlxsw_sp, int *p_span_id,
			    const struct mlxsw_sp_span_agent_parms *parms)
{
	const struct net_device *to_dev = parms->to_dev;
	const struct mlxsw_sp_span_entry_ops *ops;
	struct mlxsw_sp_span_entry *span_entry;
	struct mlxsw_sp_span_parms sparms;
	int err;

	ASSERT_RTNL();

	ops = mlxsw_sp_span_entry_ops(mlxsw_sp, to_dev);
	if (!ops) {
		dev_err(mlxsw_sp->bus_info->dev, "Cannot mirror to requested destination\n");
		return -EOPNOTSUPP;
	}

	memset(&sparms, 0, sizeof(sparms));
	err = ops->parms_set(mlxsw_sp, to_dev, &sparms);
	if (err)
		return err;

	sparms.policer_id = parms->policer_id;
	sparms.policer_enable = parms->policer_enable;
	sparms.session_id = parms->session_id;
	span_entry = mlxsw_sp_span_entry_get(mlxsw_sp, to_dev, ops, sparms);
	if (!span_entry)
		return -ENOBUFS;

	*p_span_id = span_entry->id;

	return 0;
}

void mlxsw_sp_span_agent_put(struct mlxsw_sp *mlxsw_sp, int span_id)
{
	struct mlxsw_sp_span_entry *span_entry;

	ASSERT_RTNL();

	span_entry = mlxsw_sp_span_entry_find_by_id(mlxsw_sp, span_id);
	if (WARN_ON_ONCE(!span_entry))
		return;

	mlxsw_sp_span_entry_put(mlxsw_sp, span_entry);
}

static struct mlxsw_sp_span_analyzed_port *
mlxsw_sp_span_analyzed_port_create(struct mlxsw_sp_span *span,
				   struct mlxsw_sp_port *mlxsw_sp_port,
				   bool ingress)
{
	struct mlxsw_sp_span_analyzed_port *analyzed_port;
	int err;

	analyzed_port = kzalloc(sizeof(*analyzed_port), GFP_KERNEL);
	if (!analyzed_port)
		return ERR_PTR(-ENOMEM);

	refcount_set(&analyzed_port->ref_count, 1);
	analyzed_port->local_port = mlxsw_sp_port->local_port;
	analyzed_port->ingress = ingress;
	list_add_tail(&analyzed_port->list, &span->analyzed_ports_list);

	/* An egress mirror buffer should be allocated on the egress port which
	 * does the mirroring.
	 */
	if (!ingress) {
		err = mlxsw_sp_span_port_buffer_enable(mlxsw_sp_port);
		if (err)
			goto err_buffer_update;
	}

	return analyzed_port;

err_buffer_update:
	list_del(&analyzed_port->list);
	kfree(analyzed_port);
	return ERR_PTR(err);
}

static void
mlxsw_sp_span_analyzed_port_destroy(struct mlxsw_sp_port *mlxsw_sp_port,
				    struct mlxsw_sp_span_analyzed_port *
				    analyzed_port)
{
	/* Remove egress mirror buffer now that port is no longer analyzed
	 * at egress.
	 */
	if (!analyzed_port->ingress)
		mlxsw_sp_span_port_buffer_disable(mlxsw_sp_port);

	list_del(&analyzed_port->list);
	kfree(analyzed_port);
}

int mlxsw_sp_span_analyzed_port_get(struct mlxsw_sp_port *mlxsw_sp_port,
				    bool ingress)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_span_analyzed_port *analyzed_port;
	u16 local_port = mlxsw_sp_port->local_port;
	int err = 0;

	mutex_lock(&mlxsw_sp->span->analyzed_ports_lock);

	analyzed_port = mlxsw_sp_span_analyzed_port_find(mlxsw_sp->span,
							 local_port, ingress);
	if (analyzed_port) {
		refcount_inc(&analyzed_port->ref_count);
		goto out_unlock;
	}

	analyzed_port = mlxsw_sp_span_analyzed_port_create(mlxsw_sp->span,
							   mlxsw_sp_port,
							   ingress);
	if (IS_ERR(analyzed_port))
		err = PTR_ERR(analyzed_port);

out_unlock:
	mutex_unlock(&mlxsw_sp->span->analyzed_ports_lock);
	return err;
}

void mlxsw_sp_span_analyzed_port_put(struct mlxsw_sp_port *mlxsw_sp_port,
				     bool ingress)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_span_analyzed_port *analyzed_port;
	u16 local_port = mlxsw_sp_port->local_port;

	mutex_lock(&mlxsw_sp->span->analyzed_ports_lock);

	analyzed_port = mlxsw_sp_span_analyzed_port_find(mlxsw_sp->span,
							 local_port, ingress);
	if (WARN_ON_ONCE(!analyzed_port))
		goto out_unlock;

	if (!refcount_dec_and_test(&analyzed_port->ref_count))
		goto out_unlock;

	mlxsw_sp_span_analyzed_port_destroy(mlxsw_sp_port, analyzed_port);

out_unlock:
	mutex_unlock(&mlxsw_sp->span->analyzed_ports_lock);
}

static int
__mlxsw_sp_span_trigger_port_bind(struct mlxsw_sp_span *span,
				  struct mlxsw_sp_span_trigger_entry *
				  trigger_entry, bool enable)
{
	char mpar_pl[MLXSW_REG_MPAR_LEN];
	enum mlxsw_reg_mpar_i_e i_e;

	switch (trigger_entry->trigger) {
	case MLXSW_SP_SPAN_TRIGGER_INGRESS:
		i_e = MLXSW_REG_MPAR_TYPE_INGRESS;
		break;
	case MLXSW_SP_SPAN_TRIGGER_EGRESS:
		i_e = MLXSW_REG_MPAR_TYPE_EGRESS;
		break;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	if (trigger_entry->parms.probability_rate > MLXSW_REG_MPAR_RATE_MAX)
		return -EINVAL;

	mlxsw_reg_mpar_pack(mpar_pl, trigger_entry->local_port, i_e, enable,
			    trigger_entry->parms.span_id,
			    trigger_entry->parms.probability_rate);
	return mlxsw_reg_write(span->mlxsw_sp->core, MLXSW_REG(mpar), mpar_pl);
}

static int
mlxsw_sp_span_trigger_port_bind(struct mlxsw_sp_span_trigger_entry *
				trigger_entry)
{
	return __mlxsw_sp_span_trigger_port_bind(trigger_entry->span,
						 trigger_entry, true);
}

static void
mlxsw_sp_span_trigger_port_unbind(struct mlxsw_sp_span_trigger_entry *
				  trigger_entry)
{
	__mlxsw_sp_span_trigger_port_bind(trigger_entry->span, trigger_entry,
					  false);
}

static bool
mlxsw_sp_span_trigger_port_matches(struct mlxsw_sp_span_trigger_entry *
				   trigger_entry,
				   enum mlxsw_sp_span_trigger trigger,
				   struct mlxsw_sp_port *mlxsw_sp_port)
{
	return trigger_entry->trigger == trigger &&
	       trigger_entry->local_port == mlxsw_sp_port->local_port;
}

static int
mlxsw_sp_span_trigger_port_enable(struct mlxsw_sp_span_trigger_entry *
				  trigger_entry,
				  struct mlxsw_sp_port *mlxsw_sp_port, u8 tc)
{
	/* Port trigger are enabled during binding. */
	return 0;
}

static void
mlxsw_sp_span_trigger_port_disable(struct mlxsw_sp_span_trigger_entry *
				   trigger_entry,
				   struct mlxsw_sp_port *mlxsw_sp_port, u8 tc)
{
}

static const struct mlxsw_sp_span_trigger_ops
mlxsw_sp_span_trigger_port_ops = {
	.bind = mlxsw_sp_span_trigger_port_bind,
	.unbind = mlxsw_sp_span_trigger_port_unbind,
	.matches = mlxsw_sp_span_trigger_port_matches,
	.enable = mlxsw_sp_span_trigger_port_enable,
	.disable = mlxsw_sp_span_trigger_port_disable,
};

static int
mlxsw_sp1_span_trigger_global_bind(struct mlxsw_sp_span_trigger_entry *
				   trigger_entry)
{
	return -EOPNOTSUPP;
}

static void
mlxsw_sp1_span_trigger_global_unbind(struct mlxsw_sp_span_trigger_entry *
				     trigger_entry)
{
}

static bool
mlxsw_sp1_span_trigger_global_matches(struct mlxsw_sp_span_trigger_entry *
				      trigger_entry,
				      enum mlxsw_sp_span_trigger trigger,
				      struct mlxsw_sp_port *mlxsw_sp_port)
{
	WARN_ON_ONCE(1);
	return false;
}

static int
mlxsw_sp1_span_trigger_global_enable(struct mlxsw_sp_span_trigger_entry *
				     trigger_entry,
				     struct mlxsw_sp_port *mlxsw_sp_port,
				     u8 tc)
{
	return -EOPNOTSUPP;
}

static void
mlxsw_sp1_span_trigger_global_disable(struct mlxsw_sp_span_trigger_entry *
				      trigger_entry,
				      struct mlxsw_sp_port *mlxsw_sp_port,
				      u8 tc)
{
}

static const struct mlxsw_sp_span_trigger_ops
mlxsw_sp1_span_trigger_global_ops = {
	.bind = mlxsw_sp1_span_trigger_global_bind,
	.unbind = mlxsw_sp1_span_trigger_global_unbind,
	.matches = mlxsw_sp1_span_trigger_global_matches,
	.enable = mlxsw_sp1_span_trigger_global_enable,
	.disable = mlxsw_sp1_span_trigger_global_disable,
};

static const struct mlxsw_sp_span_trigger_ops *
mlxsw_sp1_span_trigger_ops_arr[] = {
	[MLXSW_SP_SPAN_TRIGGER_TYPE_PORT] = &mlxsw_sp_span_trigger_port_ops,
	[MLXSW_SP_SPAN_TRIGGER_TYPE_GLOBAL] =
		&mlxsw_sp1_span_trigger_global_ops,
};

static int
mlxsw_sp2_span_trigger_global_bind(struct mlxsw_sp_span_trigger_entry *
				   trigger_entry)
{
	struct mlxsw_sp *mlxsw_sp = trigger_entry->span->mlxsw_sp;
	enum mlxsw_reg_mpagr_trigger trigger;
	char mpagr_pl[MLXSW_REG_MPAGR_LEN];

	switch (trigger_entry->trigger) {
	case MLXSW_SP_SPAN_TRIGGER_TAIL_DROP:
		trigger = MLXSW_REG_MPAGR_TRIGGER_INGRESS_SHARED_BUFFER;
		break;
	case MLXSW_SP_SPAN_TRIGGER_EARLY_DROP:
		trigger = MLXSW_REG_MPAGR_TRIGGER_INGRESS_WRED;
		break;
	case MLXSW_SP_SPAN_TRIGGER_ECN:
		trigger = MLXSW_REG_MPAGR_TRIGGER_EGRESS_ECN;
		break;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	if (trigger_entry->parms.probability_rate > MLXSW_REG_MPAGR_RATE_MAX)
		return -EINVAL;

	mlxsw_reg_mpagr_pack(mpagr_pl, trigger, trigger_entry->parms.span_id,
			     trigger_entry->parms.probability_rate);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpagr), mpagr_pl);
}

static void
mlxsw_sp2_span_trigger_global_unbind(struct mlxsw_sp_span_trigger_entry *
				     trigger_entry)
{
	/* There is no unbinding for global triggers. The trigger should be
	 * disabled on all ports by now.
	 */
}

static bool
mlxsw_sp2_span_trigger_global_matches(struct mlxsw_sp_span_trigger_entry *
				      trigger_entry,
				      enum mlxsw_sp_span_trigger trigger,
				      struct mlxsw_sp_port *mlxsw_sp_port)
{
	return trigger_entry->trigger == trigger;
}

static int
__mlxsw_sp2_span_trigger_global_enable(struct mlxsw_sp_span_trigger_entry *
				       trigger_entry,
				       struct mlxsw_sp_port *mlxsw_sp_port,
				       u8 tc, bool enable)
{
	struct mlxsw_sp *mlxsw_sp = trigger_entry->span->mlxsw_sp;
	char momte_pl[MLXSW_REG_MOMTE_LEN];
	enum mlxsw_reg_momte_type type;
	int err;

	switch (trigger_entry->trigger) {
	case MLXSW_SP_SPAN_TRIGGER_TAIL_DROP:
		type = MLXSW_REG_MOMTE_TYPE_SHARED_BUFFER_TCLASS;
		break;
	case MLXSW_SP_SPAN_TRIGGER_EARLY_DROP:
		type = MLXSW_REG_MOMTE_TYPE_WRED;
		break;
	case MLXSW_SP_SPAN_TRIGGER_ECN:
		type = MLXSW_REG_MOMTE_TYPE_ECN;
		break;
	default:
		WARN_ON_ONCE(1);
		return -EINVAL;
	}

	/* Query existing configuration in order to only change the state of
	 * the specified traffic class.
	 */
	mlxsw_reg_momte_pack(momte_pl, mlxsw_sp_port->local_port, type);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(momte), momte_pl);
	if (err)
		return err;

	mlxsw_reg_momte_tclass_en_set(momte_pl, tc, enable);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(momte), momte_pl);
}

static int
mlxsw_sp2_span_trigger_global_enable(struct mlxsw_sp_span_trigger_entry *
				     trigger_entry,
				     struct mlxsw_sp_port *mlxsw_sp_port,
				     u8 tc)
{
	return __mlxsw_sp2_span_trigger_global_enable(trigger_entry,
						      mlxsw_sp_port, tc, true);
}

static void
mlxsw_sp2_span_trigger_global_disable(struct mlxsw_sp_span_trigger_entry *
				      trigger_entry,
				      struct mlxsw_sp_port *mlxsw_sp_port,
				      u8 tc)
{
	__mlxsw_sp2_span_trigger_global_enable(trigger_entry, mlxsw_sp_port, tc,
					       false);
}

static const struct mlxsw_sp_span_trigger_ops
mlxsw_sp2_span_trigger_global_ops = {
	.bind = mlxsw_sp2_span_trigger_global_bind,
	.unbind = mlxsw_sp2_span_trigger_global_unbind,
	.matches = mlxsw_sp2_span_trigger_global_matches,
	.enable = mlxsw_sp2_span_trigger_global_enable,
	.disable = mlxsw_sp2_span_trigger_global_disable,
};

static const struct mlxsw_sp_span_trigger_ops *
mlxsw_sp2_span_trigger_ops_arr[] = {
	[MLXSW_SP_SPAN_TRIGGER_TYPE_PORT] = &mlxsw_sp_span_trigger_port_ops,
	[MLXSW_SP_SPAN_TRIGGER_TYPE_GLOBAL] =
		&mlxsw_sp2_span_trigger_global_ops,
};

static void
mlxsw_sp_span_trigger_ops_set(struct mlxsw_sp_span_trigger_entry *trigger_entry)
{
	struct mlxsw_sp_span *span = trigger_entry->span;
	enum mlxsw_sp_span_trigger_type type;

	switch (trigger_entry->trigger) {
	case MLXSW_SP_SPAN_TRIGGER_INGRESS:
	case MLXSW_SP_SPAN_TRIGGER_EGRESS:
		type = MLXSW_SP_SPAN_TRIGGER_TYPE_PORT;
		break;
	case MLXSW_SP_SPAN_TRIGGER_TAIL_DROP:
	case MLXSW_SP_SPAN_TRIGGER_EARLY_DROP:
	case MLXSW_SP_SPAN_TRIGGER_ECN:
		type = MLXSW_SP_SPAN_TRIGGER_TYPE_GLOBAL;
		break;
	default:
		WARN_ON_ONCE(1);
		return;
	}

	trigger_entry->ops = span->span_trigger_ops_arr[type];
}

static struct mlxsw_sp_span_trigger_entry *
mlxsw_sp_span_trigger_entry_create(struct mlxsw_sp_span *span,
				   enum mlxsw_sp_span_trigger trigger,
				   struct mlxsw_sp_port *mlxsw_sp_port,
				   const struct mlxsw_sp_span_trigger_parms
				   *parms)
{
	struct mlxsw_sp_span_trigger_entry *trigger_entry;
	int err;

	trigger_entry = kzalloc(sizeof(*trigger_entry), GFP_KERNEL);
	if (!trigger_entry)
		return ERR_PTR(-ENOMEM);

	refcount_set(&trigger_entry->ref_count, 1);
	trigger_entry->local_port = mlxsw_sp_port ? mlxsw_sp_port->local_port :
						    0;
	trigger_entry->trigger = trigger;
	memcpy(&trigger_entry->parms, parms, sizeof(trigger_entry->parms));
	trigger_entry->span = span;
	mlxsw_sp_span_trigger_ops_set(trigger_entry);
	list_add_tail(&trigger_entry->list, &span->trigger_entries_list);

	err = trigger_entry->ops->bind(trigger_entry);
	if (err)
		goto err_trigger_entry_bind;

	return trigger_entry;

err_trigger_entry_bind:
	list_del(&trigger_entry->list);
	kfree(trigger_entry);
	return ERR_PTR(err);
}

static void
mlxsw_sp_span_trigger_entry_destroy(struct mlxsw_sp_span *span,
				    struct mlxsw_sp_span_trigger_entry *
				    trigger_entry)
{
	trigger_entry->ops->unbind(trigger_entry);
	list_del(&trigger_entry->list);
	kfree(trigger_entry);
}

static struct mlxsw_sp_span_trigger_entry *
mlxsw_sp_span_trigger_entry_find(struct mlxsw_sp_span *span,
				 enum mlxsw_sp_span_trigger trigger,
				 struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp_span_trigger_entry *trigger_entry;

	list_for_each_entry(trigger_entry, &span->trigger_entries_list, list) {
		if (trigger_entry->ops->matches(trigger_entry, trigger,
						mlxsw_sp_port))
			return trigger_entry;
	}

	return NULL;
}

int mlxsw_sp_span_agent_bind(struct mlxsw_sp *mlxsw_sp,
			     enum mlxsw_sp_span_trigger trigger,
			     struct mlxsw_sp_port *mlxsw_sp_port,
			     const struct mlxsw_sp_span_trigger_parms *parms)
{
	struct mlxsw_sp_span_trigger_entry *trigger_entry;
	int err = 0;

	ASSERT_RTNL();

	if (!mlxsw_sp_span_entry_find_by_id(mlxsw_sp, parms->span_id))
		return -EINVAL;

	trigger_entry = mlxsw_sp_span_trigger_entry_find(mlxsw_sp->span,
							 trigger,
							 mlxsw_sp_port);
	if (trigger_entry) {
		if (trigger_entry->parms.span_id != parms->span_id ||
		    trigger_entry->parms.probability_rate !=
		    parms->probability_rate)
			return -EINVAL;
		refcount_inc(&trigger_entry->ref_count);
		goto out;
	}

	trigger_entry = mlxsw_sp_span_trigger_entry_create(mlxsw_sp->span,
							   trigger,
							   mlxsw_sp_port,
							   parms);
	if (IS_ERR(trigger_entry))
		err = PTR_ERR(trigger_entry);

out:
	return err;
}

void mlxsw_sp_span_agent_unbind(struct mlxsw_sp *mlxsw_sp,
				enum mlxsw_sp_span_trigger trigger,
				struct mlxsw_sp_port *mlxsw_sp_port,
				const struct mlxsw_sp_span_trigger_parms *parms)
{
	struct mlxsw_sp_span_trigger_entry *trigger_entry;

	ASSERT_RTNL();

	if (WARN_ON_ONCE(!mlxsw_sp_span_entry_find_by_id(mlxsw_sp,
							 parms->span_id)))
		return;

	trigger_entry = mlxsw_sp_span_trigger_entry_find(mlxsw_sp->span,
							 trigger,
							 mlxsw_sp_port);
	if (WARN_ON_ONCE(!trigger_entry))
		return;

	if (!refcount_dec_and_test(&trigger_entry->ref_count))
		return;

	mlxsw_sp_span_trigger_entry_destroy(mlxsw_sp->span, trigger_entry);
}

int mlxsw_sp_span_trigger_enable(struct mlxsw_sp_port *mlxsw_sp_port,
				 enum mlxsw_sp_span_trigger trigger, u8 tc)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_span_trigger_entry *trigger_entry;

	ASSERT_RTNL();

	trigger_entry = mlxsw_sp_span_trigger_entry_find(mlxsw_sp->span,
							 trigger,
							 mlxsw_sp_port);
	if (WARN_ON_ONCE(!trigger_entry))
		return -EINVAL;

	return trigger_entry->ops->enable(trigger_entry, mlxsw_sp_port, tc);
}

void mlxsw_sp_span_trigger_disable(struct mlxsw_sp_port *mlxsw_sp_port,
				   enum mlxsw_sp_span_trigger trigger, u8 tc)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_span_trigger_entry *trigger_entry;

	ASSERT_RTNL();

	trigger_entry = mlxsw_sp_span_trigger_entry_find(mlxsw_sp->span,
							 trigger,
							 mlxsw_sp_port);
	if (WARN_ON_ONCE(!trigger_entry))
		return;

	return trigger_entry->ops->disable(trigger_entry, mlxsw_sp_port, tc);
}

bool mlxsw_sp_span_trigger_is_ingress(enum mlxsw_sp_span_trigger trigger)
{
	switch (trigger) {
	case MLXSW_SP_SPAN_TRIGGER_INGRESS:
	case MLXSW_SP_SPAN_TRIGGER_EARLY_DROP:
	case MLXSW_SP_SPAN_TRIGGER_TAIL_DROP:
		return true;
	case MLXSW_SP_SPAN_TRIGGER_EGRESS:
	case MLXSW_SP_SPAN_TRIGGER_ECN:
		return false;
	}

	WARN_ON_ONCE(1);
	return false;
}

static int mlxsw_sp1_span_init(struct mlxsw_sp *mlxsw_sp)
{
	size_t arr_size = ARRAY_SIZE(mlxsw_sp1_span_entry_ops_arr);

	/* Must be first to avoid NULL pointer dereference by subsequent
	 * can_handle() callbacks.
	 */
	if (WARN_ON(mlxsw_sp1_span_entry_ops_arr[0] !=
		    &mlxsw_sp1_span_entry_ops_cpu))
		return -EINVAL;

	mlxsw_sp->span->span_trigger_ops_arr = mlxsw_sp1_span_trigger_ops_arr;
	mlxsw_sp->span->span_entry_ops_arr = mlxsw_sp1_span_entry_ops_arr;
	mlxsw_sp->span->span_entry_ops_arr_size = arr_size;

	return 0;
}

static int mlxsw_sp1_span_policer_id_base_set(struct mlxsw_sp *mlxsw_sp,
					      u16 policer_id_base)
{
	return -EOPNOTSUPP;
}

const struct mlxsw_sp_span_ops mlxsw_sp1_span_ops = {
	.init = mlxsw_sp1_span_init,
	.policer_id_base_set = mlxsw_sp1_span_policer_id_base_set,
};

static int mlxsw_sp2_span_init(struct mlxsw_sp *mlxsw_sp)
{
	size_t arr_size = ARRAY_SIZE(mlxsw_sp2_span_entry_ops_arr);

	/* Must be first to avoid NULL pointer dereference by subsequent
	 * can_handle() callbacks.
	 */
	if (WARN_ON(mlxsw_sp2_span_entry_ops_arr[0] !=
		    &mlxsw_sp2_span_entry_ops_cpu))
		return -EINVAL;

	mlxsw_sp->span->span_trigger_ops_arr = mlxsw_sp2_span_trigger_ops_arr;
	mlxsw_sp->span->span_entry_ops_arr = mlxsw_sp2_span_entry_ops_arr;
	mlxsw_sp->span->span_entry_ops_arr_size = arr_size;

	return 0;
}

#define MLXSW_SP2_SPAN_EG_MIRROR_BUFFER_FACTOR 38
#define MLXSW_SP3_SPAN_EG_MIRROR_BUFFER_FACTOR 50

static int mlxsw_sp2_span_policer_id_base_set(struct mlxsw_sp *mlxsw_sp,
					      u16 policer_id_base)
{
	char mogcr_pl[MLXSW_REG_MOGCR_LEN];
	int err;

	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(mogcr), mogcr_pl);
	if (err)
		return err;

	mlxsw_reg_mogcr_mirroring_pid_base_set(mogcr_pl, policer_id_base);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mogcr), mogcr_pl);
}

const struct mlxsw_sp_span_ops mlxsw_sp2_span_ops = {
	.init = mlxsw_sp2_span_init,
	.policer_id_base_set = mlxsw_sp2_span_policer_id_base_set,
};

const struct mlxsw_sp_span_ops mlxsw_sp3_span_ops = {
	.init = mlxsw_sp2_span_init,
	.policer_id_base_set = mlxsw_sp2_span_policer_id_base_set,
};
