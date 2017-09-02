/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_ipip.c
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Petr Machata <petrm@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <net/ip_tunnels.h>

#include "spectrum_ipip.h"

static bool
mlxsw_sp_ipip_netdev_has_ikey(const struct net_device *ol_dev)
{
	struct ip_tunnel *tun = netdev_priv(ol_dev);

	return !!(tun->parms.i_flags & TUNNEL_KEY);
}

static bool
mlxsw_sp_ipip_netdev_has_okey(const struct net_device *ol_dev)
{
	struct ip_tunnel *tun = netdev_priv(ol_dev);

	return !!(tun->parms.o_flags & TUNNEL_KEY);
}

static u32 mlxsw_sp_ipip_netdev_ikey(const struct net_device *ol_dev)
{
	struct ip_tunnel *tun = netdev_priv(ol_dev);

	return mlxsw_sp_ipip_netdev_has_ikey(ol_dev) ?
		be32_to_cpu(tun->parms.i_key) : 0;
}

static u32 mlxsw_sp_ipip_netdev_okey(const struct net_device *ol_dev)
{
	struct ip_tunnel *tun = netdev_priv(ol_dev);

	return mlxsw_sp_ipip_netdev_has_okey(ol_dev) ?
		be32_to_cpu(tun->parms.o_key) : 0;
}

static int
mlxsw_sp_ipip_nexthop_update_gre4(struct mlxsw_sp *mlxsw_sp, u32 adj_index,
				  struct mlxsw_sp_ipip_entry *ipip_entry)
{
	u16 rif_index = mlxsw_sp_ipip_lb_rif_index(ipip_entry->ol_lb);
	__be32 daddr4 = mlxsw_sp_ipip_netdev_daddr4(ipip_entry->ol_dev);
	char ratr_pl[MLXSW_REG_RATR_LEN];

	mlxsw_reg_ratr_pack(ratr_pl, MLXSW_REG_RATR_OP_WRITE_WRITE_ENTRY,
			    true, MLXSW_REG_RATR_TYPE_IPIP,
			    adj_index, rif_index);
	mlxsw_reg_ratr_ipip4_entry_pack(ratr_pl, be32_to_cpu(daddr4));

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ratr), ratr_pl);
}

static int
mlxsw_sp_ipip_fib_entry_op_gre4_rtdp(struct mlxsw_sp *mlxsw_sp,
				     u32 tunnel_index,
				     struct mlxsw_sp_ipip_entry *ipip_entry)
{
	bool has_ikey = mlxsw_sp_ipip_netdev_has_ikey(ipip_entry->ol_dev);
	u16 rif_index = mlxsw_sp_ipip_lb_rif_index(ipip_entry->ol_lb);
	u32 ikey = mlxsw_sp_ipip_netdev_ikey(ipip_entry->ol_dev);
	char rtdp_pl[MLXSW_REG_RTDP_LEN];
	unsigned int type_check;
	u32 daddr4;

	mlxsw_reg_rtdp_pack(rtdp_pl, MLXSW_REG_RTDP_TYPE_IPIP, tunnel_index);

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

static int
mlxsw_sp_ipip_fib_entry_op_gre4_ralue(struct mlxsw_sp *mlxsw_sp,
				      u32 dip, u8 prefix_len, u16 ul_vr_id,
				      enum mlxsw_reg_ralue_op op,
				      u32 tunnel_index)
{
	char ralue_pl[MLXSW_REG_RALUE_LEN];

	mlxsw_reg_ralue_pack4(ralue_pl, MLXSW_REG_RALXX_PROTOCOL_IPV4, op,
			      ul_vr_id, prefix_len, dip);
	mlxsw_reg_ralue_act_ip2me_tun_pack(ralue_pl, tunnel_index);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ralue), ralue_pl);
}

static int mlxsw_sp_ipip_fib_entry_op_gre4(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_ipip_entry *ipip_entry,
					enum mlxsw_reg_ralue_op op,
					u32 tunnel_index)
{
	u16 ul_vr_id = mlxsw_sp_ipip_lb_ul_vr_id(ipip_entry->ol_lb);
	__be32 dip;
	int err;

	err = mlxsw_sp_ipip_fib_entry_op_gre4_rtdp(mlxsw_sp, tunnel_index,
						   ipip_entry);
	if (err)
		return err;

	dip = mlxsw_sp_ipip_netdev_saddr(MLXSW_SP_L3_PROTO_IPV4,
					 ipip_entry->ol_dev).addr4;
	return mlxsw_sp_ipip_fib_entry_op_gre4_ralue(mlxsw_sp, be32_to_cpu(dip),
						     32, ul_vr_id, op,
						     tunnel_index);
}

static bool mlxsw_sp_ipip_tunnel_complete(enum mlxsw_sp_l3proto proto,
					  const struct net_device *ol_dev)
{
	union mlxsw_sp_l3addr saddr = mlxsw_sp_ipip_netdev_saddr(proto, ol_dev);
	union mlxsw_sp_l3addr daddr = mlxsw_sp_ipip_netdev_daddr(proto, ol_dev);
	union mlxsw_sp_l3addr naddr = {0};

	/* Tunnels with unset local or remote address are valid in Linux and
	 * used for lightweight tunnels (LWT) and Non-Broadcast Multi-Access
	 * (NBMA) tunnels. In principle these can be offloaded, but the driver
	 * currently doesn't support this. So punt.
	 */
	return memcmp(&saddr, &naddr, sizeof(naddr)) &&
	       memcmp(&daddr, &naddr, sizeof(naddr));
}

static bool mlxsw_sp_ipip_can_offload_gre4(const struct mlxsw_sp *mlxsw_sp,
					   const struct net_device *ol_dev,
					   enum mlxsw_sp_l3proto ol_proto)
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
	enum mlxsw_reg_ritr_loopback_ipip_type lb_ipipt;

	lb_ipipt = mlxsw_sp_ipip_netdev_has_okey(ol_dev) ?
		MLXSW_REG_RITR_LOOPBACK_IPIP_TYPE_IP_IN_GRE_KEY_IN_IP :
		MLXSW_REG_RITR_LOOPBACK_IPIP_TYPE_IP_IN_GRE_IN_IP;
	return (struct mlxsw_sp_rif_ipip_lb_config){
		.lb_ipipt = lb_ipipt,
		.okey = mlxsw_sp_ipip_netdev_okey(ol_dev),
		.ul_protocol = MLXSW_SP_L3_PROTO_IPV4,
		.saddr = mlxsw_sp_ipip_netdev_saddr(MLXSW_SP_L3_PROTO_IPV4,
						    ol_dev),
	};
}

static const struct mlxsw_sp_ipip_ops mlxsw_sp_ipip_gre4_ops = {
	.dev_type = ARPHRD_IPGRE,
	.ul_proto = MLXSW_SP_L3_PROTO_IPV4,
	.nexthop_update = mlxsw_sp_ipip_nexthop_update_gre4,
	.fib_entry_op = mlxsw_sp_ipip_fib_entry_op_gre4,
	.can_offload = mlxsw_sp_ipip_can_offload_gre4,
	.ol_loopback_config = mlxsw_sp_ipip_ol_loopback_config_gre4,
};

const struct mlxsw_sp_ipip_ops *mlxsw_sp_ipip_ops_arr[] = {
	[MLXSW_SP_IPIP_TYPE_GRE4] = &mlxsw_sp_ipip_gre4_ops,
};
