// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/if_bridge.h>
#include <linux/list.h>
#include <net/arp.h>
#include <net/gre.h>
#include <net/lag.h>
#include <net/ndisc.h>
#include <net/ip6_tunnel.h>

#include "spectrum.h"
#include "spectrum_ipip.h"
#include "spectrum_span.h"
#include "spectrum_switchdev.h"

int mlxsw_sp_span_init(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_SPAN))
		return -EIO;

	mlxsw_sp->span.entries_count = MLXSW_CORE_RES_GET(mlxsw_sp->core,
							  MAX_SPAN);
	mlxsw_sp->span.entries = kcalloc(mlxsw_sp->span.entries_count,
					 sizeof(struct mlxsw_sp_span_entry),
					 GFP_KERNEL);
	if (!mlxsw_sp->span.entries)
		return -ENOMEM;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		INIT_LIST_HEAD(&curr->bound_ports_list);
		curr->id = i;
	}

	return 0;
}

void mlxsw_sp_span_fini(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		WARN_ON_ONCE(!list_empty(&curr->bound_ports_list));
	}
	kfree(mlxsw_sp->span.entries);
}

static int
mlxsw_sp_span_entry_phys_parms(const struct net_device *to_dev,
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
	u8 local_port = dest_port->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	/* Create a new port analayzer entry for local_port. */
	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, true,
			    MLXSW_REG_MPAT_SPAN_TYPE_LOCAL_ETH);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpat), mpat_pl);
}

static void
mlxsw_sp_span_entry_deconfigure_common(struct mlxsw_sp_span_entry *span_entry,
				       enum mlxsw_reg_mpat_span_type span_type)
{
	struct mlxsw_sp_port *dest_port = span_entry->parms.dest_port;
	struct mlxsw_sp *mlxsw_sp = dest_port->mlxsw_sp;
	u8 local_port = dest_port->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, false, span_type);
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
	.can_handle = mlxsw_sp_port_dev_check,
	.parms = mlxsw_sp_span_entry_phys_parms,
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
	if (!vid ||
	    br_vlan_get_info(br_dev, vid, &vinfo) ||
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
			    0, 0, parms.link, tun->fwmark, 0);

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
mlxsw_sp_span_entry_gretap4_parms(const struct net_device *to_dev,
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
	u8 local_port = dest_port->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	/* Create a new port analayzer entry for local_port. */
	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, true,
			    MLXSW_REG_MPAT_SPAN_TYPE_REMOTE_ETH_L3);
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
	.parms = mlxsw_sp_span_entry_gretap4_parms,
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
mlxsw_sp_span_entry_gretap6_parms(const struct net_device *to_dev,
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
	u8 local_port = dest_port->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	/* Create a new port analayzer entry for local_port. */
	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, true,
			    MLXSW_REG_MPAT_SPAN_TYPE_REMOTE_ETH_L3);
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
	.parms = mlxsw_sp_span_entry_gretap6_parms,
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
mlxsw_sp_span_entry_vlan_parms(const struct net_device *to_dev,
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
	u8 local_port = dest_port->local_port;
	char mpat_pl[MLXSW_REG_MPAT_LEN];
	int pa_id = span_entry->id;

	mlxsw_reg_mpat_pack(mpat_pl, pa_id, local_port, true,
			    MLXSW_REG_MPAT_SPAN_TYPE_REMOTE_ETH);
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
	.parms = mlxsw_sp_span_entry_vlan_parms,
	.configure = mlxsw_sp_span_entry_vlan_configure,
	.deconfigure = mlxsw_sp_span_entry_vlan_deconfigure,
};

static const
struct mlxsw_sp_span_entry_ops *const mlxsw_sp_span_entry_types[] = {
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
mlxsw_sp_span_entry_nop_parms(const struct net_device *to_dev,
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
	.parms = mlxsw_sp_span_entry_nop_parms,
	.configure = mlxsw_sp_span_entry_nop_configure,
	.deconfigure = mlxsw_sp_span_entry_nop_deconfigure,
};

static void
mlxsw_sp_span_entry_configure(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_span_entry *span_entry,
			      struct mlxsw_sp_span_parms sparms)
{
	if (sparms.dest_port) {
		if (sparms.dest_port->mlxsw_sp != mlxsw_sp) {
			netdev_err(span_entry->to_dev, "Cannot mirror to %s, which belongs to a different mlxsw instance",
				   sparms.dest_port->dev->name);
			sparms.dest_port = NULL;
		} else if (span_entry->ops->configure(span_entry, sparms)) {
			netdev_err(span_entry->to_dev, "Failed to offload mirror to %s",
				   sparms.dest_port->dev->name);
			sparms.dest_port = NULL;
		}
	}

	span_entry->parms = sparms;
}

static void
mlxsw_sp_span_entry_deconfigure(struct mlxsw_sp_span_entry *span_entry)
{
	if (span_entry->parms.dest_port)
		span_entry->ops->deconfigure(span_entry);
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
	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		if (!mlxsw_sp->span.entries[i].ref_count) {
			span_entry = &mlxsw_sp->span.entries[i];
			break;
		}
	}
	if (!span_entry)
		return NULL;

	span_entry->ops = ops;
	span_entry->ref_count = 1;
	span_entry->to_dev = to_dev;
	mlxsw_sp_span_entry_configure(mlxsw_sp, span_entry, sparms);

	return span_entry;
}

static void mlxsw_sp_span_entry_destroy(struct mlxsw_sp_span_entry *span_entry)
{
	mlxsw_sp_span_entry_deconfigure(span_entry);
}

struct mlxsw_sp_span_entry *
mlxsw_sp_span_entry_find_by_port(struct mlxsw_sp *mlxsw_sp,
				 const struct net_device *to_dev)
{
	int i;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		if (curr->ref_count && curr->to_dev == to_dev)
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

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		if (curr->ref_count && curr->id == span_id)
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

	span_entry = mlxsw_sp_span_entry_find_by_port(mlxsw_sp, to_dev);
	if (span_entry) {
		/* Already exists, just take a reference */
		span_entry->ref_count++;
		return span_entry;
	}

	return mlxsw_sp_span_entry_create(mlxsw_sp, to_dev, ops, sparms);
}

static int mlxsw_sp_span_entry_put(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_span_entry *span_entry)
{
	WARN_ON(!span_entry->ref_count);
	if (--span_entry->ref_count == 0)
		mlxsw_sp_span_entry_destroy(span_entry);
	return 0;
}

static bool mlxsw_sp_span_is_egress_mirror(struct mlxsw_sp_port *port)
{
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	struct mlxsw_sp_span_inspected_port *p;
	int i;

	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];

		list_for_each_entry(p, &curr->bound_ports_list, list)
			if (p->local_port == port->local_port &&
			    p->type == MLXSW_SP_SPAN_EGRESS)
				return true;
	}

	return false;
}

static int mlxsw_sp_span_mtu_to_buffsize(const struct mlxsw_sp *mlxsw_sp,
					 int mtu)
{
	return mlxsw_sp_bytes_cells(mlxsw_sp, mtu * 5 / 2) + 1;
}

int mlxsw_sp_span_port_mtu_update(struct mlxsw_sp_port *port, u16 mtu)
{
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	char sbib_pl[MLXSW_REG_SBIB_LEN];
	int err;

	/* If port is egress mirrored, the shared buffer size should be
	 * updated according to the mtu value
	 */
	if (mlxsw_sp_span_is_egress_mirror(port)) {
		u32 buffsize = mlxsw_sp_span_mtu_to_buffsize(mlxsw_sp, mtu);

		mlxsw_reg_sbib_pack(sbib_pl, port->local_port, buffsize);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbib), sbib_pl);
		if (err) {
			netdev_err(port->dev, "Could not update shared buffer for mirroring\n");
			return err;
		}
	}

	return 0;
}

static struct mlxsw_sp_span_inspected_port *
mlxsw_sp_span_entry_bound_port_find(struct mlxsw_sp_span_entry *span_entry,
				    enum mlxsw_sp_span_type type,
				    struct mlxsw_sp_port *port,
				    bool bind)
{
	struct mlxsw_sp_span_inspected_port *p;

	list_for_each_entry(p, &span_entry->bound_ports_list, list)
		if (type == p->type &&
		    port->local_port == p->local_port &&
		    bind == p->bound)
			return p;
	return NULL;
}

static int
mlxsw_sp_span_inspected_port_bind(struct mlxsw_sp_port *port,
				  struct mlxsw_sp_span_entry *span_entry,
				  enum mlxsw_sp_span_type type,
				  bool bind)
{
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	char mpar_pl[MLXSW_REG_MPAR_LEN];
	int pa_id = span_entry->id;

	/* bind the port to the SPAN entry */
	mlxsw_reg_mpar_pack(mpar_pl, port->local_port,
			    (enum mlxsw_reg_mpar_i_e)type, bind, pa_id);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(mpar), mpar_pl);
}

static int
mlxsw_sp_span_inspected_port_add(struct mlxsw_sp_port *port,
				 struct mlxsw_sp_span_entry *span_entry,
				 enum mlxsw_sp_span_type type,
				 bool bind)
{
	struct mlxsw_sp_span_inspected_port *inspected_port;
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	char sbib_pl[MLXSW_REG_SBIB_LEN];
	int i;
	int err;

	/* A given (source port, direction) can only be bound to one analyzer,
	 * so if a binding is requested, check for conflicts.
	 */
	if (bind)
		for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
			struct mlxsw_sp_span_entry *curr =
				&mlxsw_sp->span.entries[i];

			if (mlxsw_sp_span_entry_bound_port_find(curr, type,
								port, bind))
				return -EEXIST;
		}

	/* if it is an egress SPAN, bind a shared buffer to it */
	if (type == MLXSW_SP_SPAN_EGRESS) {
		u32 buffsize = mlxsw_sp_span_mtu_to_buffsize(mlxsw_sp,
							     port->dev->mtu);

		mlxsw_reg_sbib_pack(sbib_pl, port->local_port, buffsize);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbib), sbib_pl);
		if (err) {
			netdev_err(port->dev, "Could not create shared buffer for mirroring\n");
			return err;
		}
	}

	if (bind) {
		err = mlxsw_sp_span_inspected_port_bind(port, span_entry, type,
							true);
		if (err)
			goto err_port_bind;
	}

	inspected_port = kzalloc(sizeof(*inspected_port), GFP_KERNEL);
	if (!inspected_port) {
		err = -ENOMEM;
		goto err_inspected_port_alloc;
	}
	inspected_port->local_port = port->local_port;
	inspected_port->type = type;
	inspected_port->bound = bind;
	list_add_tail(&inspected_port->list, &span_entry->bound_ports_list);

	return 0;

err_inspected_port_alloc:
	if (bind)
		mlxsw_sp_span_inspected_port_bind(port, span_entry, type,
						  false);
err_port_bind:
	if (type == MLXSW_SP_SPAN_EGRESS) {
		mlxsw_reg_sbib_pack(sbib_pl, port->local_port, 0);
		mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbib), sbib_pl);
	}
	return err;
}

static void
mlxsw_sp_span_inspected_port_del(struct mlxsw_sp_port *port,
				 struct mlxsw_sp_span_entry *span_entry,
				 enum mlxsw_sp_span_type type,
				 bool bind)
{
	struct mlxsw_sp_span_inspected_port *inspected_port;
	struct mlxsw_sp *mlxsw_sp = port->mlxsw_sp;
	char sbib_pl[MLXSW_REG_SBIB_LEN];

	inspected_port = mlxsw_sp_span_entry_bound_port_find(span_entry, type,
							     port, bind);
	if (!inspected_port)
		return;

	if (bind)
		mlxsw_sp_span_inspected_port_bind(port, span_entry, type,
						  false);
	/* remove the SBIB buffer if it was egress SPAN */
	if (type == MLXSW_SP_SPAN_EGRESS) {
		mlxsw_reg_sbib_pack(sbib_pl, port->local_port, 0);
		mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sbib), sbib_pl);
	}

	mlxsw_sp_span_entry_put(mlxsw_sp, span_entry);

	list_del(&inspected_port->list);
	kfree(inspected_port);
}

static const struct mlxsw_sp_span_entry_ops *
mlxsw_sp_span_entry_ops(struct mlxsw_sp *mlxsw_sp,
			const struct net_device *to_dev)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(mlxsw_sp_span_entry_types); ++i)
		if (mlxsw_sp_span_entry_types[i]->can_handle(to_dev))
			return mlxsw_sp_span_entry_types[i];

	return NULL;
}

int mlxsw_sp_span_mirror_add(struct mlxsw_sp_port *from,
			     const struct net_device *to_dev,
			     enum mlxsw_sp_span_type type, bool bind,
			     int *p_span_id)
{
	struct mlxsw_sp *mlxsw_sp = from->mlxsw_sp;
	const struct mlxsw_sp_span_entry_ops *ops;
	struct mlxsw_sp_span_parms sparms = {NULL};
	struct mlxsw_sp_span_entry *span_entry;
	int err;

	ops = mlxsw_sp_span_entry_ops(mlxsw_sp, to_dev);
	if (!ops) {
		netdev_err(to_dev, "Cannot mirror to %s", to_dev->name);
		return -EOPNOTSUPP;
	}

	err = ops->parms(to_dev, &sparms);
	if (err)
		return err;

	span_entry = mlxsw_sp_span_entry_get(mlxsw_sp, to_dev, ops, sparms);
	if (!span_entry)
		return -ENOBUFS;

	netdev_dbg(from->dev, "Adding inspected port to SPAN entry %d\n",
		   span_entry->id);

	err = mlxsw_sp_span_inspected_port_add(from, span_entry, type, bind);
	if (err)
		goto err_port_bind;

	*p_span_id = span_entry->id;
	return 0;

err_port_bind:
	mlxsw_sp_span_entry_put(mlxsw_sp, span_entry);
	return err;
}

void mlxsw_sp_span_mirror_del(struct mlxsw_sp_port *from, int span_id,
			      enum mlxsw_sp_span_type type, bool bind)
{
	struct mlxsw_sp_span_entry *span_entry;

	span_entry = mlxsw_sp_span_entry_find_by_id(from->mlxsw_sp, span_id);
	if (!span_entry) {
		netdev_err(from->dev, "no span entry found\n");
		return;
	}

	netdev_dbg(from->dev, "removing inspected port from SPAN entry %d\n",
		   span_entry->id);
	mlxsw_sp_span_inspected_port_del(from, span_entry, type, bind);
}

void mlxsw_sp_span_respin(struct mlxsw_sp *mlxsw_sp)
{
	int i;
	int err;

	ASSERT_RTNL();
	for (i = 0; i < mlxsw_sp->span.entries_count; i++) {
		struct mlxsw_sp_span_entry *curr = &mlxsw_sp->span.entries[i];
		struct mlxsw_sp_span_parms sparms = {NULL};

		if (!curr->ref_count)
			continue;

		err = curr->ops->parms(curr->to_dev, &sparms);
		if (err)
			continue;

		if (memcmp(&sparms, &curr->parms, sizeof(sparms))) {
			mlxsw_sp_span_entry_deconfigure(curr);
			mlxsw_sp_span_entry_configure(mlxsw_sp, curr, sparms);
		}
	}
}
