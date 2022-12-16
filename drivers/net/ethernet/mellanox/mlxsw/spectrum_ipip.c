// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <net/ip_tunnels.h>
#include <net/ip6_tunnel.h>
#include <net/inet_ecn.h>

#include "spectrum_ipip.h"
#include "reg.h"

struct ip_tunnel_parm
mlxsw_sp_ipip_netdev_parms4(const struct net_device *ol_dev)
{
	struct ip_tunnel *tun = netdev_priv(ol_dev);

	return tun->parms;
}

struct __ip6_tnl_parm
mlxsw_sp_ipip_netdev_parms6(const struct net_device *ol_dev)
{
	struct ip6_tnl *tun = netdev_priv(ol_dev);

	return tun->parms;
}

static bool mlxsw_sp_ipip_parms4_has_ikey(const struct ip_tunnel_parm *parms)
{
	return !!(parms->i_flags & TUNNEL_KEY);
}

static bool mlxsw_sp_ipip_parms6_has_ikey(const struct __ip6_tnl_parm *parms)
{
	return !!(parms->i_flags & TUNNEL_KEY);
}

static bool mlxsw_sp_ipip_parms4_has_okey(const struct ip_tunnel_parm *parms)
{
	return !!(parms->o_flags & TUNNEL_KEY);
}

static bool mlxsw_sp_ipip_parms6_has_okey(const struct __ip6_tnl_parm *parms)
{
	return !!(parms->o_flags & TUNNEL_KEY);
}

static u32 mlxsw_sp_ipip_parms4_ikey(const struct ip_tunnel_parm *parms)
{
	return mlxsw_sp_ipip_parms4_has_ikey(parms) ?
		be32_to_cpu(parms->i_key) : 0;
}

static u32 mlxsw_sp_ipip_parms6_ikey(const struct __ip6_tnl_parm *parms)
{
	return mlxsw_sp_ipip_parms6_has_ikey(parms) ?
		be32_to_cpu(parms->i_key) : 0;
}

static u32 mlxsw_sp_ipip_parms4_okey(const struct ip_tunnel_parm *parms)
{
	return mlxsw_sp_ipip_parms4_has_okey(parms) ?
		be32_to_cpu(parms->o_key) : 0;
}

static u32 mlxsw_sp_ipip_parms6_okey(const struct __ip6_tnl_parm *parms)
{
	return mlxsw_sp_ipip_parms6_has_okey(parms) ?
		be32_to_cpu(parms->o_key) : 0;
}

static union mlxsw_sp_l3addr
mlxsw_sp_ipip_parms4_saddr(const struct ip_tunnel_parm *parms)
{
	return (union mlxsw_sp_l3addr) { .addr4 = parms->iph.saddr };
}

static union mlxsw_sp_l3addr
mlxsw_sp_ipip_parms6_saddr(const struct __ip6_tnl_parm *parms)
{
	return (union mlxsw_sp_l3addr) { .addr6 = parms->laddr };
}

static union mlxsw_sp_l3addr
mlxsw_sp_ipip_parms4_daddr(const struct ip_tunnel_parm *parms)
{
	return (union mlxsw_sp_l3addr) { .addr4 = parms->iph.daddr };
}

static union mlxsw_sp_l3addr
mlxsw_sp_ipip_parms6_daddr(const struct __ip6_tnl_parm *parms)
{
	return (union mlxsw_sp_l3addr) { .addr6 = parms->raddr };
}

union mlxsw_sp_l3addr
mlxsw_sp_ipip_netdev_saddr(enum mlxsw_sp_l3proto proto,
			   const struct net_device *ol_dev)
{
	struct ip_tunnel_parm parms4;
	struct __ip6_tnl_parm parms6;

	switch (proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		parms4 = mlxsw_sp_ipip_netdev_parms4(ol_dev);
		return mlxsw_sp_ipip_parms4_saddr(&parms4);
	case MLXSW_SP_L3_PROTO_IPV6:
		parms6 = mlxsw_sp_ipip_netdev_parms6(ol_dev);
		return mlxsw_sp_ipip_parms6_saddr(&parms6);
	}

	WARN_ON(1);
	return (union mlxsw_sp_l3addr) {0};
}

static __be32 mlxsw_sp_ipip_netdev_daddr4(const struct net_device *ol_dev)
{

	struct ip_tunnel_parm parms4 = mlxsw_sp_ipip_netdev_parms4(ol_dev);

	return mlxsw_sp_ipip_parms4_daddr(&parms4).addr4;
}

static union mlxsw_sp_l3addr
mlxsw_sp_ipip_netdev_daddr(enum mlxsw_sp_l3proto proto,
			   const struct net_device *ol_dev)
{
	struct ip_tunnel_parm parms4;
	struct __ip6_tnl_parm parms6;

	switch (proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		parms4 = mlxsw_sp_ipip_netdev_parms4(ol_dev);
		return mlxsw_sp_ipip_parms4_daddr(&parms4);
	case MLXSW_SP_L3_PROTO_IPV6:
		parms6 = mlxsw_sp_ipip_netdev_parms6(ol_dev);
		return mlxsw_sp_ipip_parms6_daddr(&parms6);
	}

	WARN_ON(1);
	return (union mlxsw_sp_l3addr) {0};
}

bool mlxsw_sp_l3addr_is_zero(union mlxsw_sp_l3addr addr)
{
	union mlxsw_sp_l3addr naddr = {0};

	return !memcmp(&addr, &naddr, sizeof(naddr));
}

static struct mlxsw_sp_ipip_parms
mlxsw_sp_ipip_netdev_parms_init_gre4(const struct net_device *ol_dev)
{
	struct ip_tunnel_parm parms = mlxsw_sp_ipip_netdev_parms4(ol_dev);

	return (struct mlxsw_sp_ipip_parms) {
		.proto = MLXSW_SP_L3_PROTO_IPV4,
		.saddr = mlxsw_sp_ipip_parms4_saddr(&parms),
		.daddr = mlxsw_sp_ipip_parms4_daddr(&parms),
		.link = parms.link,
		.ikey = mlxsw_sp_ipip_parms4_ikey(&parms),
		.okey = mlxsw_sp_ipip_parms4_okey(&parms),
	};
}

static int
mlxsw_sp_ipip_nexthop_update_gre4(struct mlxsw_sp *mlxsw_sp, u32 adj_index,
				  struct mlxsw_sp_ipip_entry *ipip_entry,
				  bool force, char *ratr_pl)
{
	u16 rif_index = mlxsw_sp_ipip_lb_rif_index(ipip_entry->ol_lb);
	__be32 daddr4 = mlxsw_sp_ipip_netdev_daddr4(ipip_entry->ol_dev);
	enum mlxsw_reg_ratr_op op;

	op = force ? MLXSW_REG_RATR_OP_WRITE_WRITE_ENTRY :
		     MLXSW_REG_RATR_OP_WRITE_WRITE_ENTRY_ON_ACTIVITY;
	mlxsw_reg_ratr_pack(ratr_pl, op, true, MLXSW_REG_RATR_TYPE_IPIP,
			    adj_index, rif_index);
	mlxsw_reg_ratr_ipip4_entry_pack(ratr_pl, be32_to_cpu(daddr4));

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ratr), ratr_pl);
}

static int
mlxsw_sp_ipip_decap_config_gre4(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_ipip_entry *ipip_entry,
				u32 tunnel_index)
{
	u16 rif_index = mlxsw_sp_ipip_lb_rif_index(ipip_entry->ol_lb);
	u16 ul_rif_id = mlxsw_sp_ipip_lb_ul_rif_id(ipip_entry->ol_lb);
	char rtdp_pl[MLXSW_REG_RTDP_LEN];
	struct ip_tunnel_parm parms;
	unsigned int type_check;
	bool has_ikey;
	u32 daddr4;
	u32 ikey;

	parms = mlxsw_sp_ipip_netdev_parms4(ipip_entry->ol_dev);
	has_ikey = mlxsw_sp_ipip_parms4_has_ikey(&parms);
	ikey = mlxsw_sp_ipip_parms4_ikey(&parms);

	mlxsw_reg_rtdp_pack(rtdp_pl, MLXSW_REG_RTDP_TYPE_IPIP, tunnel_index);
	mlxsw_reg_rtdp_egress_router_interface_set(rtdp_pl, ul_rif_id);

	type_check = has_ikey ?
		MLXSW_REG_RTDP_IPIP_TYPE_CHECK_ALLOW_GRE_KEY :
		MLXSW_REG_RTDP_IPIP_TYPE_CHECK_ALLOW_GRE;

	/* Linux demuxes tunnels based on packet SIP (which must match tunnel
	 * remote IP). Thus configure decap so that it filters out packets that
	 * are not IPv4 or have the wrong SIP. IPIP_DECAP_ERROR trap is
	 * generated for packets that fail this criterion. Linux then handles
	 * such packets in slow path and generates ICMP destination unreachable.
	 */
	daddr4 = be32_to_cpu(mlxsw_sp_ipip_netdev_daddr4(ipip_entry->ol_dev));
	mlxsw_reg_rtdp_ipip4_pack(rtdp_pl, rif_index,
				  MLXSW_REG_RTDP_IPIP_SIP_CHECK_FILTER_IPV4,
				  type_check, has_ikey, daddr4, ikey);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rtdp), rtdp_pl);
}

static bool mlxsw_sp_ipip_tunnel_complete(enum mlxsw_sp_l3proto proto,
					  const struct net_device *ol_dev)
{
	union mlxsw_sp_l3addr saddr = mlxsw_sp_ipip_netdev_saddr(proto, ol_dev);
	union mlxsw_sp_l3addr daddr = mlxsw_sp_ipip_netdev_daddr(proto, ol_dev);

	/* Tunnels with unset local or remote address are valid in Linux and
	 * used for lightweight tunnels (LWT) and Non-Broadcast Multi-Access
	 * (NBMA) tunnels. In principle these can be offloaded, but the driver
	 * currently doesn't support this. So punt.
	 */
	return !mlxsw_sp_l3addr_is_zero(saddr) &&
	       !mlxsw_sp_l3addr_is_zero(daddr);
}

static bool mlxsw_sp_ipip_can_offload_gre4(const struct mlxsw_sp *mlxsw_sp,
					   const struct net_device *ol_dev)
{
	struct ip_tunnel *tunnel = netdev_priv(ol_dev);
	__be16 okflags = TUNNEL_KEY; /* We can't offload any other features. */
	bool inherit_ttl = tunnel->parms.iph.ttl == 0;
	bool inherit_tos = tunnel->parms.iph.tos & 0x1;

	return (tunnel->parms.i_flags & ~okflags) == 0 &&
	       (tunnel->parms.o_flags & ~okflags) == 0 &&
	       inherit_ttl && inherit_tos &&
	       mlxsw_sp_ipip_tunnel_complete(MLXSW_SP_L3_PROTO_IPV4, ol_dev);
}

static struct mlxsw_sp_rif_ipip_lb_config
mlxsw_sp_ipip_ol_loopback_config_gre4(struct mlxsw_sp *mlxsw_sp,
				      const struct net_device *ol_dev)
{
	struct ip_tunnel_parm parms = mlxsw_sp_ipip_netdev_parms4(ol_dev);
	enum mlxsw_reg_ritr_loopback_ipip_type lb_ipipt;

	lb_ipipt = mlxsw_sp_ipip_parms4_has_okey(&parms) ?
		MLXSW_REG_RITR_LOOPBACK_IPIP_TYPE_IP_IN_GRE_KEY_IN_IP :
		MLXSW_REG_RITR_LOOPBACK_IPIP_TYPE_IP_IN_GRE_IN_IP;
	return (struct mlxsw_sp_rif_ipip_lb_config){
		.lb_ipipt = lb_ipipt,
		.okey = mlxsw_sp_ipip_parms4_okey(&parms),
		.ul_protocol = MLXSW_SP_L3_PROTO_IPV4,
		.saddr = mlxsw_sp_ipip_netdev_saddr(MLXSW_SP_L3_PROTO_IPV4,
						    ol_dev),
	};
}

static int
mlxsw_sp_ipip_ol_netdev_change_gre(struct mlxsw_sp *mlxsw_sp,
				   struct mlxsw_sp_ipip_entry *ipip_entry,
				   const struct mlxsw_sp_ipip_parms *new_parms,
				   struct netlink_ext_ack *extack)
{
	const struct mlxsw_sp_ipip_parms *old_parms = &ipip_entry->parms;
	bool update_tunnel = false;
	bool update_decap = false;
	bool update_nhs = false;
	int err = 0;

	if (!mlxsw_sp_l3addr_eq(&new_parms->saddr, &old_parms->saddr)) {
		u16 ul_tb_id = mlxsw_sp_ipip_dev_ul_tb_id(ipip_entry->ol_dev);

		/* Since the local address has changed, if there is another
		 * tunnel with a matching saddr, both need to be demoted.
		 */
		if (mlxsw_sp_ipip_demote_tunnel_by_saddr(mlxsw_sp,
							 new_parms->proto,
							 new_parms->saddr,
							 ul_tb_id,
							 ipip_entry)) {
			mlxsw_sp_ipip_entry_demote_tunnel(mlxsw_sp, ipip_entry);
			return 0;
		}

		update_tunnel = true;
	} else if (old_parms->okey != new_parms->okey ||
		   old_parms->link != new_parms->link) {
		update_tunnel = true;
	} else if (!mlxsw_sp_l3addr_eq(&new_parms->daddr, &old_parms->daddr)) {
		update_nhs = true;
	} else if (old_parms->ikey != new_parms->ikey) {
		update_decap = true;
	}

	if (update_tunnel)
		err = __mlxsw_sp_ipip_entry_update_tunnel(mlxsw_sp, ipip_entry,
							  true, true, true,
							  extack);
	else if (update_nhs)
		err = __mlxsw_sp_ipip_entry_update_tunnel(mlxsw_sp, ipip_entry,
							  false, false, true,
							  extack);
	else if (update_decap)
		err = __mlxsw_sp_ipip_entry_update_tunnel(mlxsw_sp, ipip_entry,
							  false, false, false,
							  extack);
	if (err)
		return err;

	ipip_entry->parms = *new_parms;
	return 0;
}

static int
mlxsw_sp_ipip_ol_netdev_change_gre4(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_ipip_entry *ipip_entry,
				    struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_ipip_parms new_parms;

	new_parms = mlxsw_sp_ipip_netdev_parms_init_gre4(ipip_entry->ol_dev);
	return mlxsw_sp_ipip_ol_netdev_change_gre(mlxsw_sp, ipip_entry,
						  &new_parms, extack);
}

static int
mlxsw_sp_ipip_rem_addr_set_gre4(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_ipip_entry *ipip_entry)
{
	return 0;
}

static void
mlxsw_sp_ipip_rem_addr_unset_gre4(struct mlxsw_sp *mlxsw_sp,
				  const struct mlxsw_sp_ipip_entry *ipip_entry)
{
}

static const struct mlxsw_sp_ipip_ops mlxsw_sp_ipip_gre4_ops = {
	.dev_type = ARPHRD_IPGRE,
	.ul_proto = MLXSW_SP_L3_PROTO_IPV4,
	.inc_parsing_depth = false,
	.parms_init = mlxsw_sp_ipip_netdev_parms_init_gre4,
	.nexthop_update = mlxsw_sp_ipip_nexthop_update_gre4,
	.decap_config = mlxsw_sp_ipip_decap_config_gre4,
	.can_offload = mlxsw_sp_ipip_can_offload_gre4,
	.ol_loopback_config = mlxsw_sp_ipip_ol_loopback_config_gre4,
	.ol_netdev_change = mlxsw_sp_ipip_ol_netdev_change_gre4,
	.rem_ip_addr_set = mlxsw_sp_ipip_rem_addr_set_gre4,
	.rem_ip_addr_unset = mlxsw_sp_ipip_rem_addr_unset_gre4,
};

static struct mlxsw_sp_ipip_parms
mlxsw_sp_ipip_netdev_parms_init_gre6(const struct net_device *ol_dev)
{
	struct __ip6_tnl_parm parms = mlxsw_sp_ipip_netdev_parms6(ol_dev);

	return (struct mlxsw_sp_ipip_parms) {
		.proto = MLXSW_SP_L3_PROTO_IPV6,
		.saddr = mlxsw_sp_ipip_parms6_saddr(&parms),
		.daddr = mlxsw_sp_ipip_parms6_daddr(&parms),
		.link = parms.link,
		.ikey = mlxsw_sp_ipip_parms6_ikey(&parms),
		.okey = mlxsw_sp_ipip_parms6_okey(&parms),
	};
}

static int
mlxsw_sp_ipip_nexthop_update_gre6(struct mlxsw_sp *mlxsw_sp, u32 adj_index,
				  struct mlxsw_sp_ipip_entry *ipip_entry,
				  bool force, char *ratr_pl)
{
	u16 rif_index = mlxsw_sp_ipip_lb_rif_index(ipip_entry->ol_lb);
	enum mlxsw_reg_ratr_op op;

	op = force ? MLXSW_REG_RATR_OP_WRITE_WRITE_ENTRY :
		     MLXSW_REG_RATR_OP_WRITE_WRITE_ENTRY_ON_ACTIVITY;
	mlxsw_reg_ratr_pack(ratr_pl, op, true, MLXSW_REG_RATR_TYPE_IPIP,
			    adj_index, rif_index);
	mlxsw_reg_ratr_ipip6_entry_pack(ratr_pl,
					ipip_entry->dip_kvdl_index);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ratr), ratr_pl);
}

static int
mlxsw_sp_ipip_decap_config_gre6(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_ipip_entry *ipip_entry,
				u32 tunnel_index)
{
	u16 rif_index = mlxsw_sp_ipip_lb_rif_index(ipip_entry->ol_lb);
	u16 ul_rif_id = mlxsw_sp_ipip_lb_ul_rif_id(ipip_entry->ol_lb);
	char rtdp_pl[MLXSW_REG_RTDP_LEN];
	struct __ip6_tnl_parm parms;
	unsigned int type_check;
	bool has_ikey;
	u32 ikey;

	parms = mlxsw_sp_ipip_netdev_parms6(ipip_entry->ol_dev);
	has_ikey = mlxsw_sp_ipip_parms6_has_ikey(&parms);
	ikey = mlxsw_sp_ipip_parms6_ikey(&parms);

	mlxsw_reg_rtdp_pack(rtdp_pl, MLXSW_REG_RTDP_TYPE_IPIP, tunnel_index);
	mlxsw_reg_rtdp_egress_router_interface_set(rtdp_pl, ul_rif_id);

	type_check = has_ikey ?
		MLXSW_REG_RTDP_IPIP_TYPE_CHECK_ALLOW_GRE_KEY :
		MLXSW_REG_RTDP_IPIP_TYPE_CHECK_ALLOW_GRE;

	/* Linux demuxes tunnels based on packet SIP (which must match tunnel
	 * remote IP). Thus configure decap so that it filters out packets that
	 * are not IPv6 or have the wrong SIP. IPIP_DECAP_ERROR trap is
	 * generated for packets that fail this criterion. Linux then handles
	 * such packets in slow path and generates ICMP destination unreachable.
	 */
	mlxsw_reg_rtdp_ipip6_pack(rtdp_pl, rif_index,
				  MLXSW_REG_RTDP_IPIP_SIP_CHECK_FILTER_IPV6,
				  type_check, has_ikey,
				  ipip_entry->dip_kvdl_index, ikey);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(rtdp), rtdp_pl);
}

static bool mlxsw_sp_ipip_can_offload_gre6(const struct mlxsw_sp *mlxsw_sp,
					   const struct net_device *ol_dev)
{
	struct __ip6_tnl_parm tparm = mlxsw_sp_ipip_netdev_parms6(ol_dev);
	bool inherit_tos = tparm.flags & IP6_TNL_F_USE_ORIG_TCLASS;
	bool inherit_ttl = tparm.hop_limit == 0;
	__be16 okflags = TUNNEL_KEY; /* We can't offload any other features. */

	return (tparm.i_flags & ~okflags) == 0 &&
	       (tparm.o_flags & ~okflags) == 0 &&
	       inherit_ttl && inherit_tos &&
	       mlxsw_sp_ipip_tunnel_complete(MLXSW_SP_L3_PROTO_IPV6, ol_dev);
}

static struct mlxsw_sp_rif_ipip_lb_config
mlxsw_sp_ipip_ol_loopback_config_gre6(struct mlxsw_sp *mlxsw_sp,
				      const struct net_device *ol_dev)
{
	struct __ip6_tnl_parm parms = mlxsw_sp_ipip_netdev_parms6(ol_dev);
	enum mlxsw_reg_ritr_loopback_ipip_type lb_ipipt;

	lb_ipipt = mlxsw_sp_ipip_parms6_has_okey(&parms) ?
		MLXSW_REG_RITR_LOOPBACK_IPIP_TYPE_IP_IN_GRE_KEY_IN_IP :
		MLXSW_REG_RITR_LOOPBACK_IPIP_TYPE_IP_IN_GRE_IN_IP;
	return (struct mlxsw_sp_rif_ipip_lb_config){
		.lb_ipipt = lb_ipipt,
		.okey = mlxsw_sp_ipip_parms6_okey(&parms),
		.ul_protocol = MLXSW_SP_L3_PROTO_IPV6,
		.saddr = mlxsw_sp_ipip_netdev_saddr(MLXSW_SP_L3_PROTO_IPV6,
						    ol_dev),
	};
}

static int
mlxsw_sp_ipip_ol_netdev_change_gre6(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_ipip_entry *ipip_entry,
				    struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_ipip_parms new_parms;

	new_parms = mlxsw_sp_ipip_netdev_parms_init_gre6(ipip_entry->ol_dev);
	return mlxsw_sp_ipip_ol_netdev_change_gre(mlxsw_sp, ipip_entry,
						  &new_parms, extack);
}

static int
mlxsw_sp_ipip_rem_addr_set_gre6(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_ipip_entry *ipip_entry)
{
	return mlxsw_sp_ipv6_addr_kvdl_index_get(mlxsw_sp,
						 &ipip_entry->parms.daddr.addr6,
						 &ipip_entry->dip_kvdl_index);
}

static void
mlxsw_sp_ipip_rem_addr_unset_gre6(struct mlxsw_sp *mlxsw_sp,
				  const struct mlxsw_sp_ipip_entry *ipip_entry)
{
	mlxsw_sp_ipv6_addr_put(mlxsw_sp, &ipip_entry->parms.daddr.addr6);
}

static const struct mlxsw_sp_ipip_ops mlxsw_sp1_ipip_gre6_ops = {
	.dev_type = ARPHRD_IP6GRE,
	.ul_proto = MLXSW_SP_L3_PROTO_IPV6,
	.inc_parsing_depth = true,
	.double_rif_entry = true,
	.parms_init = mlxsw_sp_ipip_netdev_parms_init_gre6,
	.nexthop_update = mlxsw_sp_ipip_nexthop_update_gre6,
	.decap_config = mlxsw_sp_ipip_decap_config_gre6,
	.can_offload = mlxsw_sp_ipip_can_offload_gre6,
	.ol_loopback_config = mlxsw_sp_ipip_ol_loopback_config_gre6,
	.ol_netdev_change = mlxsw_sp_ipip_ol_netdev_change_gre6,
	.rem_ip_addr_set = mlxsw_sp_ipip_rem_addr_set_gre6,
	.rem_ip_addr_unset = mlxsw_sp_ipip_rem_addr_unset_gre6,
};

const struct mlxsw_sp_ipip_ops *mlxsw_sp1_ipip_ops_arr[] = {
	[MLXSW_SP_IPIP_TYPE_GRE4] = &mlxsw_sp_ipip_gre4_ops,
	[MLXSW_SP_IPIP_TYPE_GRE6] = &mlxsw_sp1_ipip_gre6_ops,
};

static const struct mlxsw_sp_ipip_ops mlxsw_sp2_ipip_gre6_ops = {
	.dev_type = ARPHRD_IP6GRE,
	.ul_proto = MLXSW_SP_L3_PROTO_IPV6,
	.inc_parsing_depth = true,
	.parms_init = mlxsw_sp_ipip_netdev_parms_init_gre6,
	.nexthop_update = mlxsw_sp_ipip_nexthop_update_gre6,
	.decap_config = mlxsw_sp_ipip_decap_config_gre6,
	.can_offload = mlxsw_sp_ipip_can_offload_gre6,
	.ol_loopback_config = mlxsw_sp_ipip_ol_loopback_config_gre6,
	.ol_netdev_change = mlxsw_sp_ipip_ol_netdev_change_gre6,
	.rem_ip_addr_set = mlxsw_sp_ipip_rem_addr_set_gre6,
	.rem_ip_addr_unset = mlxsw_sp_ipip_rem_addr_unset_gre6,
};

const struct mlxsw_sp_ipip_ops *mlxsw_sp2_ipip_ops_arr[] = {
	[MLXSW_SP_IPIP_TYPE_GRE4] = &mlxsw_sp_ipip_gre4_ops,
	[MLXSW_SP_IPIP_TYPE_GRE6] = &mlxsw_sp2_ipip_gre6_ops,
};

static int mlxsw_sp_ipip_ecn_encap_init_one(struct mlxsw_sp *mlxsw_sp,
					    u8 inner_ecn, u8 outer_ecn)
{
	char tieem_pl[MLXSW_REG_TIEEM_LEN];

	mlxsw_reg_tieem_pack(tieem_pl, inner_ecn, outer_ecn);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tieem), tieem_pl);
}

int mlxsw_sp_ipip_ecn_encap_init(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	/* Iterate over inner ECN values */
	for (i = INET_ECN_NOT_ECT; i <= INET_ECN_CE; i++) {
		u8 outer_ecn = INET_ECN_encapsulate(0, i);
		int err;

		err = mlxsw_sp_ipip_ecn_encap_init_one(mlxsw_sp, i, outer_ecn);
		if (err)
			return err;
	}

	return 0;
}

static int mlxsw_sp_ipip_ecn_decap_init_one(struct mlxsw_sp *mlxsw_sp,
					    u8 inner_ecn, u8 outer_ecn)
{
	char tidem_pl[MLXSW_REG_TIDEM_LEN];
	u8 new_inner_ecn;
	bool trap_en;

	new_inner_ecn = mlxsw_sp_tunnel_ecn_decap(outer_ecn, inner_ecn,
						  &trap_en);
	mlxsw_reg_tidem_pack(tidem_pl, outer_ecn, inner_ecn, new_inner_ecn,
			     trap_en, trap_en ? MLXSW_TRAP_ID_DECAP_ECN0 : 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tidem), tidem_pl);
}

int mlxsw_sp_ipip_ecn_decap_init(struct mlxsw_sp *mlxsw_sp)
{
	int i, j, err;

	/* Iterate over inner ECN values */
	for (i = INET_ECN_NOT_ECT; i <= INET_ECN_CE; i++) {
		/* Iterate over outer ECN values */
		for (j = INET_ECN_NOT_ECT; j <= INET_ECN_CE; j++) {
			err = mlxsw_sp_ipip_ecn_decap_init_one(mlxsw_sp, i, j);
			if (err)
				return err;
		}
	}

	return 0;
}

struct net_device *
mlxsw_sp_ipip_netdev_ul_dev_get(const struct net_device *ol_dev)
{
	struct net *net = dev_net(ol_dev);
	struct ip_tunnel *tun4;
	struct ip6_tnl *tun6;

	switch (ol_dev->type) {
	case ARPHRD_IPGRE:
		tun4 = netdev_priv(ol_dev);
		return dev_get_by_index_rcu(net, tun4->parms.link);
	case ARPHRD_IP6GRE:
		tun6 = netdev_priv(ol_dev);
		return dev_get_by_index_rcu(net, tun6->parms.link);
	default:
		return NULL;
	}
}
